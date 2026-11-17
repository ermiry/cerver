#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/tls.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../handler.h"

// a web cerver with TLS for the steps that run outside of the process: nginx with proxy_pass https://,
// testssl.sh and the load; THREADS only, EVENTS has no TLS until F8b
// - TLS_CERTIFICATE and TLS_KEY: the chain and the key of the cerver (required)
// - TLS_CLIENT_AUTHORITY: the authority of the certificates of the clients, the handshake requires one (mTLS)
// - TLS_KTLS: "1" asks for kTLS
// routes: GET /tls/hello, POST /tls/echo, GET /tls/large, GET /tls/certificate (the subject of the client),
// GET /tls/stats (the counters of the TLS of the cerver) and the admin routes (/tls/cerver/metrics)

#define TLS_PORT						8443
#define TLS_THREADS						8
#define TLS_RECEIVE_BUFFER_SIZE			(1 << 12)

#define TLS_CERTIFICATE_VARIABLE		"TLS_CERTIFICATE"
#define TLS_KEY_VARIABLE				"TLS_KEY"
#define TLS_CLIENT_AUTHORITY_VARIABLE	"TLS_CLIENT_AUTHORITY"
#define TLS_KTLS_VARIABLE				"TLS_KTLS"
#define TLS_ENABLED						"1"

// more than the records of 2^14 and the mark of 2^16 of a send
#define TLS_LARGE_SIZE					((1 << 20) + 333)
#define TLS_LARGE_FIRST					'a'
#define TLS_LARGE_LETTERS				26

#define TLS_STATS_SIZE					(1 << 9)
#define TLS_STATS_FORMAT				\
	"{\"handshakes\": %llu, \"resumed\": %llu, \"ktls_send\": %llu, \"certificate_failures\": %llu}"

static Cerver *tls_cerver = NULL;

static char *tls_large = NULL;

static void end (int dummy) {

	(void) dummy;

	if (tls_cerver) (void) cerver_shutdown (tls_cerver);

}

#pragma region routes

static void tls_hello_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, "hello");

}

static void tls_echo_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, request->body ? request->body->str : "");

}

static void tls_large_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;
	(void) http_response_render_text (receive, HTTP_STATUS_OK, tls_large, TLS_LARGE_SIZE);

}

// behind nginx it is the certificate of nginx (proxy_ssl_certificate)
static void tls_certificate_handler (const HttpReceive *receive, const HttpRequest *request) {

	const TlsCertificate *certificate = http_request_get_client_certificate (request);

	(void) http_response_json_msg_send (receive, HTTP_STATUS_OK, certificate ? certificate->subject : "none");

}

static void tls_stats_handler (const HttpReceive *receive, const HttpRequest *request) {

	(void) request;

	TlsStats stats;
	(void) memset (&stats, 0, sizeof (TlsStats));
	(void) cerver_tls_get_stats (tls_cerver, &stats);

	char json[TLS_STATS_SIZE];
	const int length = snprintf (
		json, TLS_STATS_SIZE, TLS_STATS_FORMAT,
		(unsigned long long) stats.handshakes, (unsigned long long) stats.resumed,
		(unsigned long long) stats.ktls_send,
		(unsigned long long) stats.failures[TLS_FAILURE_CERTIFICATE]
	);

	(void) http_response_render_json (receive, HTTP_STATUS_OK, json, (size_t) length);

}

static void tls_routes (HttpCerver *http_cerver) {

	HttpGroup *group = http_cerver_group_create (http_cerver, "tls");
	(void) http_group_get (group, "hello", tls_hello_handler);
	(void) http_group_post (group, "echo", tls_echo_handler);
	(void) http_group_get (group, "large", tls_large_handler);
	(void) http_group_get (group, "certificate", tls_certificate_handler);
	(void) http_group_get (group, "stats", tls_stats_handler);

}

#pragma endregion

#pragma region settings

static bool tls_variable_enabled (const char *name) {

	const char *value = getenv (name);

	return value && !strcmp (value, TLS_ENABLED);

}

static void tls_settings_client_authority (TlsSettings *settings) {

	const char *authority = getenv (TLS_CLIENT_AUTHORITY_VARIABLE);
	if (authority && authority[0]) {
		(void) tls_settings_set_ca (settings, authority, NULL);
		tls_settings_set_client_verify (settings, TLS_CLIENT_VERIFY_REQUIRED);
	}

}

// returns NULL without the certificate and its key
static TlsSettings *tls_settings_from_environment (void) {

	const char *certificate = getenv (TLS_CERTIFICATE_VARIABLE);
	const char *key = getenv (TLS_KEY_VARIABLE);
	if (!certificate || !key) return NULL;

	TlsSettings *settings = tls_settings_create ();
	if (settings && tls_settings_add_certificate (settings, certificate, key)) {
		tls_settings_delete (settings);
		return NULL;
	}

	tls_settings_client_authority (settings);
	tls_settings_set_ktls (settings, tls_variable_enabled (TLS_KTLS_VARIABLE));

	return settings;

}

static u8 tls_large_create (void) {

	tls_large = (char *) malloc (TLS_LARGE_SIZE);
	if (!tls_large) return 1;

	for (size_t index = 0; index < TLS_LARGE_SIZE; index++) {
		tls_large[index] = (char) (TLS_LARGE_FIRST + (index % TLS_LARGE_LETTERS));
	}

	return 0;

}

#pragma endregion

#pragma region start

static void tls_configure (HttpCerver *http_cerver) {

	if (test_keep_alive_is_set ()) http_cerver_set_keep_alive (http_cerver, test_keep_alive ());

	http_cerver_set_not_found_handler (http_cerver);
	http_cerver_enable_admin_routes (http_cerver, true);

	tls_routes (http_cerver);

}

static int tls_run (TlsSettings *settings) {

	tls_cerver = cerver_create_web ("tls-cerver", TLS_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (!tls_cerver) {
		tls_settings_delete (settings);
		return 1;
	}

	(void) cerver_set_bind_address (tls_cerver, test_address ());
	cerver_set_receive_buffer_size (tls_cerver, TLS_RECEIVE_BUFFER_SIZE);
	cerver_set_thpool_n_threads (tls_cerver, TLS_THREADS);
	cerver_set_handler_type (tls_cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (tls_cerver, true);

	tls_configure ((HttpCerver *) tls_cerver->cerver_data);

	int retval = 1;
	if (!cerver_set_tls (tls_cerver, settings) && !cerver_start (tls_cerver)) retval = 0;
	else cerver_log_error ("Failed to start %s!", tls_cerver->info->name);

	(void) cerver_teardown (tls_cerver);
	tls_cerver = NULL;

	return retval;

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	int retval = 1;

	TlsSettings *settings = tls_settings_from_environment ();
	if (!settings) cerver_log_error ("%s and %s are required", TLS_CERTIFICATE_VARIABLE, TLS_KEY_VARIABLE);
	else if (tls_large_create ()) tls_settings_delete (settings);
	else retval = tls_run (settings);

	free (tls_large);

	cerver_end ();

	return retval;

}

#pragma endregion
