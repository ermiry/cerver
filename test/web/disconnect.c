#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>

#include <cerver/utils/log.h>

#include "../address.h"

// a cerver behind nginx (test/nginx) whose clients leave in every part of the process,
// it tells what it saw: the states that its handler found, the files left in its uploads path
// and its metrics, so test/client/web/disconnect.c can check them through nginx and directly

#define DISCONNECT_PORT						8080
#define DISCONNECT_THREADS					8

// milliseconds
#define DISCONNECT_IDLE_TIMEOUT				1000
#define DISCONNECT_GONE_WAIT				4000
#define DISCONNECT_SLOW_WAIT				500

#define DISCONNECT_SEND_TIMEOUT				2
#define DISCONNECT_DRAIN_TIMEOUT			8

#define DISCONNECT_POLL_NANOSECONDS			1000000L
#define DISCONNECT_NANOSECONDS				1000000000L
#define DISCONNECT_NANOSECONDS_PER_MILLISECOND	1000000L
#define DISCONNECT_MILLISECONDS_PER_SECOND	1000

#define DISCONNECT_LARGE_SIZE				(1 << 22)
#define DISCONNECT_SMALL_BUFFER				(1 << 12)

#define DISCONNECT_JSON_SIZE				256

#define DISCONNECT_UPLOADS_PATH				"/tmp/cerver-web-disconnect"

// the instance with this variable exits in the middle of a request to /crash
#define DISCONNECT_CRASH_VARIABLE			"DISCONNECT_CRASH"
#define DISCONNECT_CRASH_EXIT				1

static Cerver *disconnect_cerver = NULL;

static char *disconnect_large_body = NULL;

static unsigned int gone_states[HTTP_CLIENT_STATE_RESET + 1] = { 0 };

static void end (int dummy) {

	(void) dummy;

	if (disconnect_cerver) (void) cerver_shutdown (disconnect_cerver);

}

static double disconnect_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / (double) DISCONNECT_NANOSECONDS);

}

static void disconnect_sleep (void) {

	const struct timespec interval = { 0, DISCONNECT_POLL_NANOSECONDS };
	(void) nanosleep (&interval, NULL);

}

// GET /ok, POST /echo
static void disconnect_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// POST /upload, the files are discarded, so the uploads path ends empty
static void disconnect_upload_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	http_request_multi_part_discard_files (request);

	(void) http_response_send (oki_doki, http_receive);

}

// GET /gone waits until its client (nginx) leaves, and answers anyway
static void disconnect_gone_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double deadline = disconnect_now () + ((double) DISCONNECT_GONE_WAIT / DISCONNECT_MILLISECONDS_PER_SECOND);

	HttpClientState state = http_receive_get_client_state (http_receive);
	while ((state == HTTP_CLIENT_STATE_CONNECTED) && (disconnect_now () < deadline)) {
		disconnect_sleep ();
		state = http_receive_get_client_state (http_receive);
	}

	(void) __atomic_add_fetch (&gone_states[state], 1, __ATOMIC_SEQ_CST);

	(void) http_response_send (oki_doki, http_receive);

}

// GET /slow takes a while, so it is still running when the cerver starts to drain
static void disconnect_slow_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const struct timespec wait = {
		DISCONNECT_SLOW_WAIT / DISCONNECT_MILLISECONDS_PER_SECOND,
		(DISCONNECT_SLOW_WAIT % DISCONNECT_MILLISECONDS_PER_SECOND) * DISCONNECT_NANOSECONDS_PER_MILLISECOND
	};

	(void) nanosleep (&wait, NULL);

	(void) http_response_send (oki_doki, http_receive);

}

// GET /large, its socket keeps a small buffer, so the response never fits in it
static void disconnect_large_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const int size = DISCONNECT_SMALL_BUFFER;
	(void) setsockopt (http_receive_get_sock_fd (http_receive), SOL_SOCKET, SO_SNDBUF, &size, sizeof (int));

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, disconnect_large_body, DISCONNECT_LARGE_SIZE);

}

// GET and POST /crash, the instance with DISCONNECT_CRASH exits without answering,
// like a process that dies in the middle of a request
static void disconnect_crash_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	if (getenv (DISCONNECT_CRASH_VARIABLE)) _exit (DISCONNECT_CRASH_EXIT);

	(void) http_response_send (oki_doki, http_receive);

}

static void *disconnect_drain_thread (void *cerver_ptr) {

	(void) cerver_shutdown ((Cerver *) cerver_ptr);

	return NULL;

}

// GET /drain starts to stop the cerver after its answer, its connections are drained
static void disconnect_drain_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

	pthread_t thread_id;
	if (!pthread_create (&thread_id, NULL, disconnect_drain_thread, disconnect_cerver)) {
		(void) pthread_detach (thread_id);
	}

}

// GET /states, the states of the clients that the gone handler found
static void disconnect_states_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char json[DISCONNECT_JSON_SIZE] = { 0 };
	(void) snprintf (
		json, DISCONNECT_JSON_SIZE, "{\"connected\": %u, \"closed\": %u, \"reset\": %u}",
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_CONNECTED], __ATOMIC_SEQ_CST),
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_CLOSED], __ATOMIC_SEQ_CST),
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_RESET], __ATOMIC_SEQ_CST)
	);

	(void) http_response_json_custom_send (http_receive, HTTP_STATUS_OK, json);

}

// the entries of the uploads path, without "." and ".."
static unsigned int disconnect_uploads_count (void) {

	unsigned int count = 0;

	DIR *directory = opendir (DISCONNECT_UPLOADS_PATH);
	if (directory) {
		const struct dirent *entry = NULL;
		while ((entry = readdir (directory)) != NULL) {
			if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, "..")) count += 1;
		}

		(void) closedir (directory);
	}

	return count;

}

// GET /uploads, the files and the directories left in the uploads path
static void disconnect_uploads_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char json[DISCONNECT_JSON_SIZE] = { 0 };
	(void) snprintf (json, DISCONNECT_JSON_SIZE, "{\"uploads\": %u}", disconnect_uploads_count ());

	(void) http_response_json_custom_send (http_receive, HTTP_STATUS_OK, json);

}

static void disconnect_route (
	HttpCerver *http_cerver, const RequestMethod method, const char *path,
	void (*handler) (const HttpReceive *, const HttpRequest *)
) {

	HttpRoute *route = http_route_create (method, path, handler);
	http_cerver_route_register (http_cerver, route);

}

static void disconnect_routes (HttpCerver *http_cerver) {

	// the admin routes are its children: GET /instance/cerver/metrics
	HttpRoute *instance_route = http_route_create (REQUEST_METHOD_GET, "instance", disconnect_ok_handler);
	http_cerver_route_register (http_cerver, instance_route);
	http_cerver_set_main_route (http_cerver, instance_route);

	disconnect_route (http_cerver, REQUEST_METHOD_GET, "ok", disconnect_ok_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_POST, "echo", disconnect_ok_handler);

	HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", disconnect_upload_handler);
	http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
	http_cerver_route_register (http_cerver, upload_route);

	disconnect_route (http_cerver, REQUEST_METHOD_GET, "gone", disconnect_gone_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "slow", disconnect_slow_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "large", disconnect_large_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "crash", disconnect_crash_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_POST, "crash", disconnect_crash_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "drain", disconnect_drain_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "states", disconnect_states_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "uploads", disconnect_uploads_handler);

}

static void disconnect_configure (Cerver *cerver) {

	(void) cerver_set_bind_address (cerver, test_address ());
	cerver_set_thpool_n_threads (cerver, DISCONNECT_THREADS);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_send_timeout (cerver, DISCONNECT_SEND_TIMEOUT);
	cerver_set_drain_policy (cerver, CERVER_DRAIN_POLICY_RUN);
	cerver_set_drain_timeout (cerver, DISCONNECT_DRAIN_TIMEOUT);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	http_cerver_set_idle_timeout_milliseconds (http_cerver, DISCONNECT_IDLE_TIMEOUT);

	http_cerver_set_uploads_path (http_cerver, DISCONNECT_UPLOADS_PATH);
	http_cerver_set_default_uploads_filename_generator (http_cerver);
	http_cerver_set_default_uploads_dirname_generator (http_cerver);

	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	http_cerver_enable_admin_routes (http_cerver, true);

	disconnect_routes (http_cerver);

}

static bool disconnect_resources_create (void) {

	bool created = !mkdir (DISCONNECT_UPLOADS_PATH, HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE) || (errno == EEXIST);

	disconnect_large_body = (char *) malloc (DISCONNECT_LARGE_SIZE);
	if (disconnect_large_body) {
		for (size_t i = 0; i < DISCONNECT_LARGE_SIZE; i++) {
			disconnect_large_body[i] = (char) ('a' + (i % ('z' - 'a' + 1)));
		}
	}

	return created && disconnect_large_body;

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	if (disconnect_resources_create ()) {
		disconnect_cerver = cerver_create_web ("disconnect-cerver", DISCONNECT_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
		if (disconnect_cerver) {
			disconnect_configure (disconnect_cerver);

			if (!cerver_start (disconnect_cerver)) {
				http_cerver_all_stats_print ((HttpCerver *) disconnect_cerver->cerver_data);
				(void) cerver_teardown (disconnect_cerver);
			}

			else {
				cerver_log_error ("Failed to start %s!", disconnect_cerver->info->name);
				(void) cerver_teardown (disconnect_cerver);
			}
		}

		else {
			cerver_log_error ("Failed to create cerver!");
		}
	}

	else {
		cerver_log_error ("Failed to create the resources of the disconnect cerver!");
	}

	free (disconnect_large_body);

	cerver_end ();

	return 0;

}
