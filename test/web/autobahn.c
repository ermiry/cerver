#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>

#include <cerver/http/http.h>
#include <cerver/http/tree.h>

#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

#include "../address.h"

// the echo server of autobahn-testsuite (fuzzingclient), ws://cerver:9001/ws/echo
#define AUTOBAHN_PORT					9001
#define AUTOBAHN_THREADS				4

// the largest messages of autobahn are 16 MiB (cases 9.1.6 and 9.2.6)
#define AUTOBAHN_MAX_MESSAGE			(1 << 25)

static Cerver *autobahn_cerver = NULL;

static void end (int dummy) {

	(void) dummy;

	if (autobahn_cerver) (void) cerver_shutdown (autobahn_cerver);

}

static void autobahn_on_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) http_web_socket_send (web_socket, type, data, size);

}

static const HttpWebSocketHandlers autobahn_handlers = {
	NULL, NULL, autobahn_on_message, NULL, NULL, NULL, NULL, NULL
};

static void autobahn_configure (HttpCerver *http_cerver) {

	HttpGroup *group = http_cerver_group_create (http_cerver, "ws");

	HttpEndpoint *echo = http_group_web_socket_add (group, "echo", &autobahn_handlers);
	http_endpoint_set_web_socket_max_message (echo, AUTOBAHN_MAX_MESSAGE);
	http_endpoint_set_web_socket_compression (echo, true);

	// autobahn never answers pings of the server
	http_endpoint_set_web_socket_ping (echo, 0, 0);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	autobahn_cerver = cerver_create_web ("autobahn-cerver", AUTOBAHN_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (autobahn_cerver) {
		(void) cerver_set_bind_address (autobahn_cerver, test_address ());
		cerver_set_thpool_n_threads (autobahn_cerver, AUTOBAHN_THREADS);
		cerver_set_handler_type (autobahn_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_reusable_address_flags (autobahn_cerver, true);

		autobahn_configure ((HttpCerver *) autobahn_cerver->cerver_data);

		if (!cerver_start (autobahn_cerver)) {
			(void) cerver_teardown (autobahn_cerver);
		}

		else {
			cerver_log_error ("Failed to start %s!", autobahn_cerver->info->name);
			(void) cerver_teardown (autobahn_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
