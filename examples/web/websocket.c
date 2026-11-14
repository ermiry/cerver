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
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

// a chat with rooms over WebSocket, and its page in http://localhost:8080/
//	ws://localhost:8080/ws/echo		every message comes back, compressed with permessage-deflate
//	ws://localhost:8080/ws/chat		"/join room" joins a room, any other text goes to the room
//	ws://localhost:8080/ws/private	only with ?token=secret

#define WEBSOCKET_PORT				8080
#define WEBSOCKET_THREADS			4

#define WEBSOCKET_PAGE				"./examples/web/public/websocket.html"

#define WEBSOCKET_ROOM_SIZE			(1 << 6)
#define WEBSOCKET_TEXT_SIZE			(1 << 10)

#define WEBSOCKET_JOIN_COMMAND		"/join "
#define WEBSOCKET_JOIN_LENGTH		(sizeof (WEBSOCKET_JOIN_COMMAND) - 1)

#define WEBSOCKET_DEFAULT_ROOM		"lobby"

static const char *websocket_token = { "secret" };
static const char *websocket_token_parameter = { "token" };

static Cerver *websocket_cerver = NULL;

// the room of each connection, its data
typedef struct ChatMember {

	char room[WEBSOCKET_ROOM_SIZE];

} ChatMember;

static void end (int dummy) {

	(void) dummy;

	if (websocket_cerver) (void) cerver_shutdown (websocket_cerver);

}

#pragma region echo

static void echo_on_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) http_web_socket_send (web_socket, type, data, size);

}

static const HttpWebSocketHandlers echo_handlers = {
	NULL, NULL, echo_on_message, NULL, NULL, NULL, NULL, NULL
};

#pragma endregion

#pragma region chat

// every connection starts in the lobby
static void chat_on_open (HttpWebSocket *web_socket) {

	ChatMember *member = (ChatMember *) calloc (1, sizeof (ChatMember));
	if (member) {
		(void) strcpy (member->room, WEBSOCKET_DEFAULT_ROOM);
		http_web_socket_set_data (web_socket, member, free);
		(void) http_web_socket_join (web_socket, member->room);
	}

	cerver_log_msg ("Connection %lu from %s joined the lobby", http_web_socket_get_id (web_socket), http_web_socket_get_ip (web_socket));

}

static void chat_join (HttpWebSocket *web_socket, ChatMember *member, const char *room) {

	if (strlen (room) && (strlen (room) < WEBSOCKET_ROOM_SIZE)) {
		(void) http_web_socket_leave (web_socket, member->room);
		(void) strcpy (member->room, room);
		(void) http_web_socket_join (web_socket, member->room);

		char text[WEBSOCKET_TEXT_SIZE];
		const int length = snprintf (text, WEBSOCKET_TEXT_SIZE, "you are in %s", member->room);
		(void) http_web_socket_send_text (web_socket, text, (size_t) length);
	}

}

// the bytes of a text that was written with snprintf (), that returns the length of the whole text
// also when it was cut: only the ones that fit in the buffer are sent
static size_t chat_text_length (const int written) {

	size_t length = 0;

	if (written > 0) {
		length = ((size_t) written < WEBSOCKET_TEXT_SIZE) ? (size_t) written : (WEBSOCKET_TEXT_SIZE - 1);
	}

	return length;

}

// a text goes to the others of the room, with the id of its sender
static void chat_say (HttpWebSocket *web_socket, const ChatMember *member, const char *message) {

	char text[WEBSOCKET_TEXT_SIZE];
	const int written = snprintf (text, WEBSOCKET_TEXT_SIZE, "%lu: %s", http_web_socket_get_id (web_socket), message);

	(void) http_cerver_web_socket_broadcast (
		http_web_socket_get_http_cerver (web_socket), member->room, web_socket,
		HTTP_WEB_SOCKET_OPCODE_TEXT, text, chat_text_length (written)
	);

}

static void chat_on_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	ChatMember *member = (ChatMember *) http_web_socket_get_data (web_socket);

	char message[WEBSOCKET_TEXT_SIZE] = { 0 };
	if (member && (type == HTTP_WEB_SOCKET_OPCODE_TEXT) && (size < WEBSOCKET_TEXT_SIZE)) {
		(void) memcpy (message, data, size);

		if (!strncmp (message, WEBSOCKET_JOIN_COMMAND, WEBSOCKET_JOIN_LENGTH)) {
			chat_join (web_socket, member, message + WEBSOCKET_JOIN_LENGTH);
		}

		else {
			chat_say (web_socket, member, message);
		}
	}

}

static void chat_on_close (HttpWebSocket *web_socket, u16 code, const char *reason, size_t length) {

	(void) reason;
	(void) length;

	cerver_log_msg ("Connection %lu left with %u", http_web_socket_get_id (web_socket), code);

}

static const HttpWebSocketHandlers chat_handlers = {
	NULL, chat_on_open, chat_on_message, NULL, NULL, NULL, chat_on_close, NULL
};

#pragma endregion

#pragma region private

// the browsers can not send an Authorization in an upgrade, the token goes in the query
static bool private_on_upgrade (
	const HttpReceive *http_receive, const HttpRequest *request,
	HttpWebSocketUpgrade *upgrade
) {

	(void) http_receive;

	const String *token = http_request_query_params_get_value (request, websocket_token_parameter);
	const bool accepted = token && !strcmp (token->str, websocket_token);

	if (!accepted) upgrade->status = HTTP_STATUS_UNAUTHORIZED;

	return accepted;

}

static void private_on_open (HttpWebSocket *web_socket) {

	(void) http_web_socket_send_text (web_socket, "welcome", 7);

}

static const HttpWebSocketHandlers private_handlers = {
	private_on_upgrade, private_on_open, echo_on_message, NULL, NULL, NULL, NULL, NULL
};

#pragma endregion

// GET /
static void page_handler (const HttpReceive *http_receive, const HttpRequest *request) {

	(void) request;

	if (http_response_send_file (http_receive, HTTP_STATUS_OK, WEBSOCKET_PAGE)) {
		cerver_log_error ("Failed to send " WEBSOCKET_PAGE);
	}

}

static void websocket_routes (HttpCerver *http_cerver) {

	HttpGroup *root = http_cerver_group_create (http_cerver, "");
	(void) http_group_root_add (root, REQUEST_METHOD_GET, page_handler);

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");

	HttpEndpoint *echo = http_group_web_socket_add (ws, "echo", &echo_handlers);
	http_endpoint_set_web_socket_compression (echo, true);

	(void) http_group_web_socket_add (ws, "chat", &chat_handlers);

	(void) http_group_web_socket_add (ws, "private", &private_handlers);

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
		cerver_set_thpool_n_threads (websocket_cerver, WEBSOCKET_THREADS);
		cerver_set_handler_type (websocket_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_reusable_address_flags (websocket_cerver, true);

		websocket_routes ((HttpCerver *) websocket_cerver->cerver_data);

		if (!cerver_start (websocket_cerver)) {
			(void) cerver_teardown (websocket_cerver);
		}

		else {
			cerver_log_error ("Failed to start %s!", websocket_cerver->info->name);
			(void) cerver_teardown (websocket_cerver);
		}
	}

	cerver_end ();

	return 0;

}
