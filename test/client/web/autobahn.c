#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>

#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

// the client of autobahn-testsuite (fuzzingserver): it asks for the number of cases,
// it runs each one echoing every message, and it asks the server to update the reports

#define AUTOBAHN_URL_VARIABLE			"AUTOBAHN_URL"
#define AUTOBAHN_DEFAULT_URL			"ws://autobahn:9001"

#define AUTOBAHN_AGENT					"cerver"

#define AUTOBAHN_URL_SIZE				(1 << 9)
#define AUTOBAHN_COUNT_SIZE				(1 << 5)

#define AUTOBAHN_MAX_MESSAGE			(1 << 25)

#define AUTOBAHN_DECIMAL_BASE			10

#define AUTOBAHN_CASE_COUNT_FORMAT		"%s/getCaseCount"
#define AUTOBAHN_RUN_CASE_FORMAT		"%s/runCase?case=%u&agent=" AUTOBAHN_AGENT
#define AUTOBAHN_REPORTS_FORMAT			"%s/updateReports?agent=" AUTOBAHN_AGENT

static unsigned int autobahn_cases = 0;

static void autobahn_echo (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) http_web_socket_send (web_socket, type, data, size);

}

static void autobahn_count (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) web_socket;
	(void) type;

	char count[AUTOBAHN_COUNT_SIZE] = { 0 };
	(void) memcpy (count, data, (size < (AUTOBAHN_COUNT_SIZE - 1)) ? size : (AUTOBAHN_COUNT_SIZE - 1));
	autobahn_cases = (unsigned int) strtoul (count, NULL, AUTOBAHN_DECIMAL_BASE);

}

static const HttpWebSocketHandlers autobahn_echo_handlers = {
	NULL, NULL, autobahn_echo, NULL, NULL, NULL, NULL, NULL
};

static const HttpWebSocketHandlers autobahn_count_handlers = {
	NULL, NULL, autobahn_count, NULL, NULL, NULL, NULL, NULL
};

// runs a connection until the server closes it
// returns 0 on success, 1 if it could not connect
static unsigned int autobahn_run (const char *url, const HttpWebSocketHandlers *handlers) {

	unsigned int retval = 1;

	HttpWebSocketClient *client = http_web_socket_client_create ();
	if (client && !http_web_socket_client_set_url (client, url)) {
		http_web_socket_client_set_handlers (client, handlers);
		http_web_socket_client_set_max_message (client, AUTOBAHN_MAX_MESSAGE);
		http_web_socket_client_set_compression (client, true);
		http_web_socket_client_set_ping_milliseconds (client, 0, 0);

		if (!http_web_socket_client_connect (client)) {
			http_web_socket_client_wait (client);
			retval = 0;
		}
	}

	http_web_socket_client_delete (client);

	return retval;

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	const char *base = getenv (AUTOBAHN_URL_VARIABLE);
	if (!base) base = AUTOBAHN_DEFAULT_URL;

	char url[AUTOBAHN_URL_SIZE];
	unsigned int errors = 0;

	(void) snprintf (url, AUTOBAHN_URL_SIZE, AUTOBAHN_CASE_COUNT_FORMAT, base);
	errors |= autobahn_run (url, &autobahn_count_handlers);

	cerver_log_msg ("Running %u autobahn cases...", autobahn_cases);

	for (unsigned int i = 1; i <= autobahn_cases; i++) {
		(void) snprintf (url, AUTOBAHN_URL_SIZE, AUTOBAHN_RUN_CASE_FORMAT, base, i);
		errors |= autobahn_run (url, &autobahn_echo_handlers);
	}

	(void) snprintf (url, AUTOBAHN_URL_SIZE, AUTOBAHN_REPORTS_FORMAT, base);
	errors |= autobahn_run (url, &autobahn_echo_handlers);

	if (!autobahn_cases) errors = 1;

	cerver_end ();

	return (int) errors;

}
