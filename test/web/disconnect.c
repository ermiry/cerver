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
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../handler.h"

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
static void disconnect_ok_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// POST /upload, the files are discarded, so the uploads path ends empty
static void disconnect_upload_handler (CerverHttpContext *context) {

	cerver_http_request_discard_files (context);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /gone waits until its client (nginx) leaves, and answers anyway
static void disconnect_gone_handler (CerverHttpContext *context) {

	const double deadline = disconnect_now () + ((double) DISCONNECT_GONE_WAIT / DISCONNECT_MILLISECONDS_PER_SECOND);

	CerverHttpClientState state = cerver_http_context_client_state (context);
	while ((state == HTTP_CLIENT_STATE_CONNECTED) && (disconnect_now () < deadline)) {
		disconnect_sleep ();
		state = cerver_http_context_client_state (context);
	}

	(void) __atomic_add_fetch (&gone_states[state], 1, __ATOMIC_SEQ_CST);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /slow takes a while, so it is still running when the cerver starts to drain
static void disconnect_slow_handler (CerverHttpContext *context) {

	const struct timespec wait = {
		DISCONNECT_SLOW_WAIT / DISCONNECT_MILLISECONDS_PER_SECOND,
		(DISCONNECT_SLOW_WAIT % DISCONNECT_MILLISECONDS_PER_SECOND) * DISCONNECT_NANOSECONDS_PER_MILLISECOND
	};

	(void) nanosleep (&wait, NULL);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /large, its socket keeps a small buffer, so the response never fits in it
static void disconnect_large_handler (CerverHttpContext *context) {

	const int size = DISCONNECT_SMALL_BUFFER;
	(void) setsockopt (cerver_http_context_socket (context), SOL_SOCKET, SO_SNDBUF, &size, sizeof (int));

	(void) cerver_http_response_send_content (
		context, HTTP_CONTENT_TYPE_HTML, disconnect_large_body, DISCONNECT_LARGE_SIZE
	);

}

// GET and POST /crash, the instance with DISCONNECT_CRASH exits without answering,
// like a process that dies in the middle of a request
static void disconnect_crash_handler (CerverHttpContext *context) {

	if (getenv (DISCONNECT_CRASH_VARIABLE)) _exit (DISCONNECT_CRASH_EXIT);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

static void *disconnect_drain_thread (void *cerver_ptr) {

	(void) cerver_shutdown ((Cerver *) cerver_ptr);

	return NULL;

}

// GET /drain starts to stop the cerver after its answer, its connections are drained
static void disconnect_drain_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

	pthread_t thread_id;
	if (!pthread_create (&thread_id, NULL, disconnect_drain_thread, disconnect_cerver)) {
		(void) pthread_detach (thread_id);
	}

}

// GET /states, the states of the clients that the gone handler found
static void disconnect_states_handler (CerverHttpContext *context) {

	char json[DISCONNECT_JSON_SIZE] = { 0 };
	const int length = snprintf (
		json, DISCONNECT_JSON_SIZE, "{\"connected\": %u, \"closed\": %u, \"reset\": %u}",
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_CONNECTED], __ATOMIC_SEQ_CST),
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_CLOSED], __ATOMIC_SEQ_CST),
		__atomic_load_n (&gone_states[HTTP_CLIENT_STATE_RESET], __ATOMIC_SEQ_CST)
	);

	(void) cerver_http_response_send_json (context, json, (size_t) length);

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
static void disconnect_uploads_handler (CerverHttpContext *context) {

	char json[DISCONNECT_JSON_SIZE] = { 0 };
	const int length = snprintf (json, DISCONNECT_JSON_SIZE, "{\"uploads\": %u}", disconnect_uploads_count ());

	(void) cerver_http_response_send_json (context, json, (size_t) length);

}

static void disconnect_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *instance = cerver_http_group_create (http_cerver, "/instance");
	(void) cerver_http_group_root_add (instance, REQUEST_METHOD_GET, disconnect_ok_handler);

	CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

	(void) cerver_http_group_get (main_group, "ok", disconnect_ok_handler);
	(void) cerver_http_group_post (main_group, "echo", disconnect_ok_handler);

	CerverHttpEndpoint *upload = cerver_http_group_post (main_group, "upload", disconnect_upload_handler);
	cerver_http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	(void) cerver_http_group_get (main_group, "gone", disconnect_gone_handler);
	(void) cerver_http_group_get (main_group, "slow", disconnect_slow_handler);
	(void) cerver_http_group_get (main_group, "large", disconnect_large_handler);
	(void) cerver_http_group_get (main_group, "crash", disconnect_crash_handler);
	(void) cerver_http_group_post (main_group, "crash", disconnect_crash_handler);
	(void) cerver_http_group_get (main_group, "drain", disconnect_drain_handler);
	(void) cerver_http_group_get (main_group, "states", disconnect_states_handler);
	(void) cerver_http_group_get (main_group, "uploads", disconnect_uploads_handler);

}

static void disconnect_configure (Cerver *cerver) {

	(void) cerver_set_bind_address (cerver, test_address ());
	cerver_set_thpool_n_threads (cerver, DISCONNECT_THREADS);
	cerver_set_events_backend (cerver, test_events_backend ());
	cerver_set_io_threads (cerver, test_io_threads ());
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_send_timeout (cerver, DISCONNECT_SEND_TIMEOUT);
	cerver_set_drain_policy (cerver, CERVER_DRAIN_POLICY_RUN);
	cerver_set_drain_timeout (cerver, DISCONNECT_DRAIN_TIMEOUT);

	CerverHttp *http_cerver = cerver_http_get (cerver);

	// the gone handler waits for its client and the slow one sleeps: the handlers run in threads
	(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

	if (test_keep_alive_is_set ()) cerver_http_set_keep_alive (http_cerver, test_keep_alive ());

	cerver_http_set_idle_timeout_milliseconds (http_cerver, DISCONNECT_IDLE_TIMEOUT);

	cerver_http_set_uploads_path (http_cerver, DISCONNECT_UPLOADS_PATH);
	cerver_http_set_default_uploads_filename_generator (http_cerver);
	cerver_http_set_default_uploads_dirname_generator (http_cerver);

	cerver_http_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	// GET /instance/cerver/metrics
	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_admin_set_path (http_cerver, "/instance");

	disconnect_routes (http_cerver);

}

static bool disconnect_resources_create (void) {

	bool created = !mkdir (DISCONNECT_UPLOADS_PATH, CERVER_HTTP_DEFAULT_UPLOADS_DIR_MODE) || (errno == EEXIST);

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
				cerver_http_all_stats_print (cerver_http_get (disconnect_cerver));
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
