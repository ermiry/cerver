#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

#include "../address.h"

// the server of the web/websocket pair: an echo, a chat with rooms and an endpoint with authentication

#define WEBSOCKET_PORT					8080
#define WEBSOCKET_THREADS				4

#define WEBSOCKET_TEXT_SIZE				(1 << 8)
#define WEBSOCKET_ROOM_SIZE				(1 << 6)

static const char *websocket_token = { "pair-token" };

static Cerver *websocket_cerver = NULL;

static void end (int dummy) {

	(void) dummy;

	if (websocket_cerver) (void) cerver_shutdown (websocket_cerver);

}

static void websocket_echo (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) http_web_socket_send (web_socket, type, data, size);

}

static const HttpWebSocketHandlers websocket_echo_handlers = {
	NULL, NULL, websocket_echo, NULL, NULL, NULL, NULL, NULL
};

// "join:room" joins a room, "say:room:message" sends the message to the others of the room,
// "count:room" answers with the connections of the room
static void websocket_chat (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	char text[WEBSOCKET_TEXT_SIZE] = { 0 };
	if ((type == HTTP_WEB_SOCKET_OPCODE_TEXT) && (size < WEBSOCKET_TEXT_SIZE)) {
		(void) memcpy (text, data, size);

		char room[WEBSOCKET_ROOM_SIZE] = { 0 };
		int offset = 0;
		if (sscanf (text, "join:%63s", room) == 1) {
			(void) http_web_socket_join (web_socket, room);
			(void) http_web_socket_send_text (web_socket, "joined", 6);
		}

		else if ((sscanf (text, "say:%63[^:]:%n", room, &offset) == 1) && offset) {
			(void) http_cerver_web_socket_broadcast (
				http_web_socket_get_http_cerver (web_socket), room, web_socket,
				HTTP_WEB_SOCKET_OPCODE_TEXT, text + offset, strlen (text + offset)
			);
		}

		else if (sscanf (text, "count:%63s", room) == 1) {
			char count[WEBSOCKET_ROOM_SIZE];
			const int length = snprintf (
				count, WEBSOCKET_ROOM_SIZE, "%u",
				http_cerver_web_socket_count (http_web_socket_get_http_cerver (web_socket), room)
			);

			(void) http_web_socket_send_text (web_socket, count, (size_t) length);
		}
	}

}

static const HttpWebSocketHandlers websocket_chat_handlers = {
	NULL, NULL, websocket_chat, NULL, NULL, NULL, NULL, NULL
};

static unsigned int websocket_authenticate (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];

	return (authorization && !strcmp (authorization->str, websocket_token)) ? 0 : 1;

}

static void websocket_configure (HttpCerver *http_cerver) {

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");

	HttpEndpoint *echo = http_group_web_socket_add (ws, "echo", &websocket_echo_handlers);
	http_endpoint_set_web_socket_compression (echo, true);

	(void) http_group_web_socket_add (ws, "chat", &websocket_chat_handlers);

	HttpEndpoint *auth = http_group_web_socket_add (ws, "auth", &websocket_echo_handlers);
	http_endpoint_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (auth, websocket_authenticate);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	websocket_cerver = cerver_create_web ("websocket-cerver", WEBSOCKET_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (websocket_cerver) {
		(void) cerver_set_bind_address (websocket_cerver, test_address ());
		cerver_set_thpool_n_threads (websocket_cerver, WEBSOCKET_THREADS);
		cerver_set_handler_type (websocket_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_reusable_address_flags (websocket_cerver, true);

		websocket_configure ((HttpCerver *) websocket_cerver->cerver_data);

		if (!cerver_start (websocket_cerver)) {
			(void) cerver_teardown (websocket_cerver);
		}

		else {
			cerver_log_error ("Failed to start %s!", websocket_cerver->info->name);
			(void) cerver_teardown (websocket_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
