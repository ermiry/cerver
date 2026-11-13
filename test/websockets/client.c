#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>
#include <cerver/http/tree.h>

#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/handshake.h>
#include <cerver/http/websockets/internal.h>
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include "../test.h"

#include "raw.h"
#include "websocket.h"

// the ports of these servers, after the ones of the server suite
#define CLIENT_TEST_PORT_BASE				(11264 + (1 << 9))

#define CLIENT_TEST_THREADS					4

#define CLIENT_TEST_ALARM					(1 << 9)

#define CLIENT_TEST_URL_SIZE				(1 << 8)
#define CLIENT_TEST_MESSAGE_SIZE			(1 << 19)
#define CLIENT_TEST_LARGE_MESSAGE			300000

// milliseconds of the reconnections of the test
#define CLIENT_TEST_RECONNECT_MIN			(1 << 5)
#define CLIENT_TEST_RECONNECT_MAX			(1 << 7)

// the short timeouts of the clients of the fake servers, with the scale of the run
#define CLIENT_TEST_TIMEOUT					(test_milliseconds (1 << 10))

#define CLIENT_TEST_SENDERS					8
#define CLIENT_TEST_SENDER_MESSAGES			(1 << 6)

static const char *client_test_secret = { "secret" };

static u16 client_test_port = CLIENT_TEST_PORT_BASE;

// what the methods of the client saw
typedef struct ClientTestState {

	unsigned int opened;
	unsigned int closed;
	unsigned int errors;
	unsigned int messages;

	// the closes with 1001, a reconnection can report other ones after it
	unsigned int going_away;

	unsigned int last_code;
	unsigned int last_error;

	// the client that its own on_close waits for and deletes, and the times that it did
	HttpWebSocketClient *own_client;
	unsigned int own_calls;

	pthread_mutex_t mutex;
	u8 message[CLIENT_TEST_MESSAGE_SIZE];
	size_t message_size;
	HttpWebSocketOpcode message_type;

} ClientTestState;

static ClientTestState state;

static void client_test_state_reset (void) {

	(void) pthread_mutex_lock (&state.mutex);
	state.message_size = 0;
	(void) pthread_mutex_unlock (&state.mutex);

	__atomic_store_n (&state.opened, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.closed, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.errors, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.messages, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.going_away, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.last_code, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.last_error, 0, __ATOMIC_SEQ_CST);

	__atomic_store_n (&state.own_client, NULL, __ATOMIC_SEQ_CST);
	__atomic_store_n (&state.own_calls, 0, __ATOMIC_SEQ_CST);

}

#pragma region handlers

static void client_test_on_open (HttpWebSocket *web_socket) {

	test_check_int_eq (http_web_socket_get_role (web_socket), HTTP_WEB_SOCKET_ROLE_CLIENT, NULL);
	test_counter_add (&state.opened);

}

static void client_test_on_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) web_socket;

	(void) pthread_mutex_lock (&state.mutex);
	if (size <= CLIENT_TEST_MESSAGE_SIZE) {
		(void) memcpy (state.message, data, size);
		state.message_size = size;
		state.message_type = type;
	}
	(void) pthread_mutex_unlock (&state.mutex);

	test_counter_add (&state.messages);

}

static void client_test_on_close (
	HttpWebSocket *web_socket, u16 code, const char *reason, size_t length
) {

	(void) web_socket;
	(void) reason;
	(void) length;

	__atomic_store_n (&state.last_code, (unsigned int) code, __ATOMIC_SEQ_CST);
	if (code == HTTP_WEB_SOCKET_CLOSE_GOING_AWAY) test_counter_add (&state.going_away);
	test_counter_add (&state.closed);

	// a method of the client that waits for it and deletes it, in the thread of the client
	HttpWebSocketClient *own_client = __atomic_load_n (&state.own_client, __ATOMIC_SEQ_CST);
	if (own_client) {
		http_web_socket_client_wait (own_client);
		http_web_socket_client_delete (own_client);

		test_counter_add (&state.own_calls);
	}

}

static void client_test_on_error (HttpWebSocket *web_socket, HttpWebSocketError error) {

	(void) web_socket;

	__atomic_store_n (&state.last_error, (unsigned int) error, __ATOMIC_SEQ_CST);
	test_counter_add (&state.errors);

}

static const HttpWebSocketHandlers client_test_handlers = {
	NULL, client_test_on_open, client_test_on_message, NULL,
	NULL, NULL, client_test_on_close, client_test_on_error
};

// the server echoes, and closes with 4002 when it receives "close"
static void client_test_server_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	if ((size == 5) && !memcmp (data, "close", 5)) (void) http_web_socket_close (web_socket, 4002, NULL);
	else (void) http_web_socket_send (web_socket, type, data, size);

}

static const HttpWebSocketHandlers client_test_server_handlers = {
	NULL, NULL, client_test_server_message, NULL, NULL, NULL, NULL, NULL
};

// the server closes the connection that it just accepted
static void client_test_server_leave (HttpWebSocket *web_socket) {

	(void) http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);

}

static const HttpWebSocketHandlers client_test_leaving_handlers = {
	NULL, client_test_server_leave, NULL, NULL, NULL, NULL, NULL, NULL
};

static unsigned int client_test_authenticate (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];

	return (authorization && !strcmp (authorization->str, client_test_secret)) ? 0 : 1;

}

#pragma endregion

#pragma region server

typedef struct ClientTestServer {

	Cerver *cerver;
	pthread_t thread;
	u16 port;

} ClientTestServer;

static void *client_test_server_thread (void *server_ptr) {

	(void) cerver_start (((ClientTestServer *) server_ptr)->cerver);

	return NULL;

}

static ClientTestServer *client_test_server_start (const u16 port) {

	ClientTestServer *server = (ClientTestServer *) calloc (1, sizeof (ClientTestServer));
	test_check_ptr (server);
	server->port = port;

	Cerver *cerver = cerver_create_web ("websocket-client-test", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);
	cerver_set_thpool_n_threads (cerver, CLIENT_TEST_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	http_group_set_web_socket (ws, &client_test_server_handlers);

	test_check_ptr (http_group_web_socket_add (ws, "echo", NULL));

	const char *protocols[] = { "chat.v2", "chat" };
	HttpEndpoint *with_protocols = http_group_web_socket_add (ws, "protocols", NULL);
	test_check_unsigned_eq (http_endpoint_set_web_socket_protocols (with_protocols, protocols, 2), 0, NULL);

	HttpEndpoint *auth = http_group_web_socket_add (ws, "auth", NULL);
	http_endpoint_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (auth, client_test_authenticate);

	HttpEndpoint *compressed = http_group_web_socket_add (ws, "compressed", NULL);
	http_endpoint_set_web_socket_compression (compressed, true);

	test_check_ptr (http_group_web_socket_add (ws, "leaving", &client_test_leaving_handlers));

	server->cerver = cerver;
	test_check_int_eq (pthread_create (&server->thread, NULL, client_test_server_thread, server), 0, NULL);

	(void) close (raw_connect (port));

	return server;

}

static void client_test_server_stop (ClientTestServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

// the URL of a path in the cerver, an IPv6 goes in brackets
static void client_test_url (char *url, const u16 port, const char *path) {

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	(void) snprintf (
		url, CLIENT_TEST_URL_SIZE, ipv6 ? "ws://[%s]:%u%s" : "ws://%s:%u%s",
		address, (unsigned int) port, path
	);

}

static HttpWebSocketClient *client_test_create (const u16 port, const char *path) {

	HttpWebSocketClient *client = http_web_socket_client_create ();
	test_check_ptr (client);

	char url[CLIENT_TEST_URL_SIZE];
	client_test_url (url, port, path);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, url), 0, NULL);

	http_web_socket_client_set_handlers (client, &client_test_handlers);
	http_web_socket_client_set_timeouts_milliseconds (client, CLIENT_TEST_TIMEOUT, CLIENT_TEST_TIMEOUT);

	return client;

}

// waits for the next message of the client and checks it
static void client_test_expect_message (
	const unsigned int count, const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	test_check_unsigned_eq (test_wait_counter (&state.messages, count, TEST_WAIT_SECONDS), count, NULL);

	(void) pthread_mutex_lock (&state.mutex);
	test_check_int_eq (state.message_type, type, NULL);
	test_check_unsigned_eq (state.message_size, size, NULL);
	test_check (!memcmp (state.message, data, size), "the message is wrong");
	(void) pthread_mutex_unlock (&state.mutex);

}

#pragma endregion

#pragma region tests

// the results of the parser of the response of a handshake: a response without its end
// in the most bytes that are read was not valid, but its invalid said that it was
static void test_client_responses (void) {

	static const char *response = {
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
		"\r\n"
	};

	const size_t length = strlen (response);

	HttpWebSocketClientResponse parsed;
	for (size_t size = 0; size < length; size++) {
		test_check_unsigned_eq (
			http_web_socket_client_response_parse (response, size, &parsed),
			HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL, NULL
		);

		test_check_false (parsed.invalid);
		test_check_false (parsed.complete);
	}

	test_check_unsigned_eq (
		http_web_socket_client_response_parse (response, length, &parsed),
		HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE, NULL
	);

	test_check_true (parsed.complete);
	test_check_false (parsed.invalid);
	test_check_int_eq (parsed.status, 101, NULL);
	test_check_unsigned_eq (parsed.size, length, NULL);
	test_check_str_eq (parsed.accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", NULL);

	static const char *wrong = { "HTTP/1.1 101 Switching Protocols\r\nUpgrade websocket\r\n\r\n" };
	test_check_unsigned_eq (
		http_web_socket_client_response_parse (wrong, strlen (wrong), &parsed),
		HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID, NULL
	);

	test_check_true (parsed.invalid);

	char *endless = (char *) malloc (HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE);
	test_check_ptr (endless);
	(void) memset (endless, 'a', HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE);
	(void) memcpy (endless, response, length - 2);

	test_check_unsigned_eq (
		http_web_socket_client_response_parse (endless, HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE - 1, &parsed),
		HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL, NULL
	);

	test_check_false (parsed.invalid);

	test_check_unsigned_eq (
		http_web_socket_client_response_parse (endless, HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE, &parsed),
		HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID, NULL
	);

	test_check_true (parsed.invalid);

	free (endless);

	test_check_unsigned_eq (
		http_web_socket_client_response_parse (NULL, 0, &parsed),
		HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID, NULL
	);

}

#define CLIENT_TEST_BACKOFF_MIN				256
#define CLIENT_TEST_BACKOFF_MAX				4096
#define CLIENT_TEST_BACKOFF_FAILURES		64

// the wait before a reconnection is doubled after each failure and it never passes its maximum:
// its jitter was added above the maximum, the doubling wrapped to 0 after many failures
// and a maximum above INT_MAX became a negative timeout, a wait without an end
static void test_client_backoff (void) {

	// without failures and without jitter it is the first wait
	test_check_unsigned_eq (
		http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, CLIENT_TEST_BACKOFF_MAX, 0, 0),
		CLIENT_TEST_BACKOFF_MIN, NULL
	);

	// the jitter is less than a quarter of the wait
	for (unsigned int random = 0; random < CLIENT_TEST_BACKOFF_MAX; random++) {
		const unsigned int delay = http_web_socket_client_backoff_delay (
			CLIENT_TEST_BACKOFF_MIN, CLIENT_TEST_BACKOFF_MAX, 0, random
		);

		test_check ((delay >= CLIENT_TEST_BACKOFF_MIN) && (delay < (CLIENT_TEST_BACKOFF_MIN + (CLIENT_TEST_BACKOFF_MIN / 4))), NULL);
	}

	// doubled after each failure, up to the maximum, with any jitter
	test_check_unsigned_eq (http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, CLIENT_TEST_BACKOFF_MAX, 1, 0), 512, NULL);
	test_check_unsigned_eq (http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, CLIENT_TEST_BACKOFF_MAX, 3, 0), 2048, NULL);

	for (unsigned int failures = 4; failures < CLIENT_TEST_BACKOFF_FAILURES; failures++) {
		test_check_unsigned_eq (
			http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, CLIENT_TEST_BACKOFF_MAX, failures, UINT_MAX),
			CLIENT_TEST_BACKOFF_MAX, NULL
		);
	}

	// a maximum that a poll () does not take is the longest timeout of one
	for (unsigned int failures = 0; failures < CLIENT_TEST_BACKOFF_FAILURES; failures++) {
		const unsigned int delay = http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, UINT_MAX, failures, UINT_MAX);
		test_check ((delay >= CLIENT_TEST_BACKOFF_MIN) && (delay <= (unsigned int) INT_MAX), NULL);
	}

	test_check_unsigned_eq (
		http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MIN, UINT_MAX, CLIENT_TEST_BACKOFF_FAILURES, 0),
		(unsigned int) INT_MAX, NULL
	);

	test_check_unsigned_eq (http_web_socket_client_backoff_delay (UINT_MAX, UINT_MAX, 0, UINT_MAX), (unsigned int) INT_MAX, NULL);

	// a first wait above the maximum is the maximum
	test_check_unsigned_eq (
		http_web_socket_client_backoff_delay (CLIENT_TEST_BACKOFF_MAX, CLIENT_TEST_BACKOFF_MIN, 0, UINT_MAX),
		CLIENT_TEST_BACKOFF_MIN, NULL
	);

}

static void test_client_urls (void) {

	HttpWebSocketClient *client = http_web_socket_client_create ();
	test_check_ptr (client);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://example.com"), 0, NULL);
	test_check_str_eq (client->host, "example.com", NULL);
	test_check_unsigned_eq (client->port, 80, NULL);
	test_check_str_eq (client->path, "/", NULL);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, "WS://example.com:8080/chat?room=1"), 0, NULL);
	test_check_unsigned_eq (client->port, 8080, NULL);
	test_check_str_eq (client->path, "/chat?room=1", NULL);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://[::1]:9000"), 0, NULL);
	test_check_str_eq (client->host, "::1", NULL);
	test_check_unsigned_eq (client->port, 9000, NULL);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host?query"), 0, NULL);
	test_check_str_eq (client->path, "/?query", NULL);

	test_check_unsigned_eq (http_web_socket_client_set_url (client, "wss://example.com"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "http://example.com"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host:0"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host:65536"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host:"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host/path#fragment"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://[::1"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host:80x"), 1, NULL);

	// a url with a space or a control character would write other lines in the request
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host/chat HTTP/1.1\r\nInjected: yes\r\n\r\nGET /other"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host/chat?room=a b"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host/chat\t"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://ho\nst/chat"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host\r\n:80/chat"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://host/chat\x7F"), 1, NULL);

	// a url that is rejected does not change the client: its host and its port were already the new ones
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://kept.example:8081/kept?a=1"), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://other.example:9090/path#fragment"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, "ws://another.example:0"), 1, NULL);
	test_check_str_eq (client->host, "kept.example", NULL);
	test_check_unsigned_eq (client->port, 8081, NULL);
	test_check_str_eq (client->path, "/kept?a=1", NULL);

	// a close that can not be sent does not stop the client, it stopped its reconnections
	test_check_unsigned_eq (http_web_socket_client_close (client, HTTP_WEB_SOCKET_CLOSE_NO_STATUS, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_close (client, HTTP_WEB_SOCKET_CLOSE_NORMAL, "\xC0\xAF"), 1, NULL);
	test_check_false (__atomic_load_n (&client->stopping, __ATOMIC_SEQ_CST));

	// the subprotocols are tokens, and a list that is rejected leaves none
	const char *injected[] = { "chat", "chat\r\nInjected: yes" };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, injected, 2), 1, NULL);
	test_check_unsigned_eq (client->settings.n_protocols, 0, NULL);

	const char *missing[] = { "chat", NULL };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, missing, 2), 1, NULL);
	test_check_unsigned_eq (client->settings.n_protocols, 0, NULL);

	const char *list[] = { "chat, other" };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, list, 1), 1, NULL);

	const char *empty[] = { "" };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, empty, 1), 1, NULL);
	test_check_unsigned_eq (client->settings.n_protocols, 0, NULL);

	const char *tokens[] = { "chat.v2", "chat" };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, tokens, 2), 0, NULL);
	test_check_unsigned_eq (client->settings.n_protocols, 2, NULL);

	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Bad", "a\r\nInjected: yes"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Authorization", "secret"), 0, NULL);

	// the name of a header is a token, and the headers of the handshake are the ones of the client
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "", "value"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "a b", "value"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "name:", "value"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Host", "other.example"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "upgrade", "h2c"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Connection", "close"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Sec-WebSocket-Version", "8"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Sec-WebSocket-Protocol", "chat"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Sec-WebSocket-Extensions", "x"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "X-Trace", "abc"), 0, NULL);

	http_web_socket_client_delete (client);

}

// messages of every kind, and the close of the client
static void test_client_echo (void) {

	client_test_state_reset ();
	ClientTestServer *server = client_test_server_start (client_test_port++);

	HttpWebSocketClient *client = client_test_create (server->port, "/ws/echo");
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);
	test_check_unsigned_eq (test_wait_counter (&state.opened, 1, TEST_WAIT_SECONDS), 1, NULL);

	test_check_unsigned_eq (http_web_socket_client_send_text (client, "hello", 5), 0, NULL);
	client_test_expect_message (1, HTTP_WEB_SOCKET_OPCODE_TEXT, "hello", 5);

	u8 *large = (u8 *) malloc (CLIENT_TEST_LARGE_MESSAGE);
	test_check_ptr (large);
	for (size_t i = 0; i < CLIENT_TEST_LARGE_MESSAGE; i++) large[i] = (u8) (i * 13);

	test_check_unsigned_eq (http_web_socket_client_send_binary (client, large, CLIENT_TEST_LARGE_MESSAGE), 0, NULL);
	client_test_expect_message (2, HTTP_WEB_SOCKET_OPCODE_BINARY, large, CLIENT_TEST_LARGE_MESSAGE);
	free (large);

	// a message in pieces through the connection of the client
	HttpWebSocket *web_socket = http_web_socket_client_get_web_socket (client);
	test_check_ptr (web_socket);
	test_check_unsigned_eq (http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT), 0, NULL);
	test_check_unsigned_eq (http_web_socket_send_write (web_socket, "in ", 3), 0, NULL);
	test_check_unsigned_eq (http_web_socket_send_write (web_socket, "pieces", 6), 0, NULL);
	test_check_unsigned_eq (http_web_socket_send_end (web_socket), 0, NULL);
	client_test_expect_message (3, HTTP_WEB_SOCKET_OPCODE_TEXT, "in pieces", 9);
	http_web_socket_release (web_socket);

	test_check_unsigned_eq (http_web_socket_client_close (client, 1000, "bye"), 0, NULL);
	http_web_socket_client_wait (client);
	test_check_unsigned_eq (__atomic_load_n (&state.closed, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_code, __ATOMIC_SEQ_CST), 1000, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.errors, __ATOMIC_SEQ_CST), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_send_text (client, "late", 4), 1, NULL);

	http_web_socket_client_delete (client);

	// the server closes, the client answers and waits for the end of the TCP connection
	client_test_state_reset ();
	client = client_test_create (server->port, "/ws/echo");
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_send_text (client, "close", 5), 0, NULL);
	http_web_socket_client_wait (client);
	test_check_unsigned_eq (__atomic_load_n (&state.last_code, __ATOMIC_SEQ_CST), 4002, NULL);
	http_web_socket_client_delete (client);

	client_test_server_stop (server);

}

// the protocols, the headers and the compression of the handshake
static void test_client_handshake (void) {

	client_test_state_reset ();
	ClientTestServer *server = client_test_server_start (client_test_port++);

	HttpWebSocketClient *client = client_test_create (server->port, "/ws/protocols");
	const char *protocols[] = { "x-unknown", "chat" };
	test_check_unsigned_eq (http_web_socket_client_set_protocols (client, protocols, 2), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

	HttpWebSocket *web_socket = http_web_socket_client_get_web_socket (client);
	test_check_str_eq (http_web_socket_get_protocol (web_socket), "chat", NULL);
	http_web_socket_release (web_socket);
	http_web_socket_client_delete (client);

	// the authentication of the endpoint with a header of the client
	client = client_test_create (server->port, "/ws/auth");
	test_check_unsigned_eq (http_web_socket_client_connect (client), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_HANDSHAKE, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL);

	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Authorization", client_test_secret), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);
	http_web_socket_client_delete (client);

	if (http_web_socket_deflate_available ()) {
		client_test_state_reset ();
		client = client_test_create (server->port, "/ws/compressed");
		http_web_socket_client_set_compression (client, true);
		test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

		web_socket = http_web_socket_client_get_web_socket (client);
		test_check_true (http_web_socket_is_compressed (web_socket));
		http_web_socket_release (web_socket);

		char text[1 << 12];
		for (size_t i = 0; i < sizeof (text); i++) text[i] = (char) ('a' + (i % 5));
		for (unsigned int n = 1; n <= 3; n++) {
			test_check_unsigned_eq (http_web_socket_client_send_text (client, text, sizeof (text)), 0, NULL);
			client_test_expect_message (n, HTTP_WEB_SOCKET_OPCODE_TEXT, text, sizeof (text));
		}

		http_web_socket_client_delete (client);

		// an endpoint without the extension ignores the offer
		client = client_test_create (server->port, "/ws/echo");
		http_web_socket_client_set_compression (client, true);
		test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);
		web_socket = http_web_socket_client_get_web_socket (client);
		test_check_false (http_web_socket_is_compressed (web_socket));
		http_web_socket_release (web_socket);
		http_web_socket_client_delete (client);
	}

	client_test_server_stop (server);

}

typedef struct ClientTestSender {

	HttpWebSocketClient *client;
	unsigned int id;

} ClientTestSender;

// the bytes of the messages of the senders, with their end
#define CLIENT_TEST_SENDER_TEXT_SIZE		(1 << 6)

// the format of a message of a sender: its id, the number of the message and a filler that changes its size
#define CLIENT_TEST_SENDER_FORMAT			"%u:%u:%.*s"
#define CLIENT_TEST_SENDER_FILLER			"abcdefghijklmnopqrstuvwxyz"
#define CLIENT_TEST_SENDER_FILLER_SIZE		(sizeof (CLIENT_TEST_SENDER_FILLER) - 1)

// the message that each sender expects next, and the echoes that were not it
static unsigned int client_test_sender_next[CLIENT_TEST_SENDERS];
static unsigned int client_test_sender_mixed = 0;

// writes the message of a sender, its size changes so a frame mixed with another one is noticed
static int client_test_sender_text (char *text, const unsigned int id, const unsigned int message) {

	return snprintf (
		text, CLIENT_TEST_SENDER_TEXT_SIZE, CLIENT_TEST_SENDER_FORMAT,
		id, message, (int) ((id + message) % CLIENT_TEST_SENDER_FILLER_SIZE), CLIENT_TEST_SENDER_FILLER
	);

}

static void *client_test_sender (void *sender_ptr) {

	const ClientTestSender *sender = (const ClientTestSender *) sender_ptr;

	char text[CLIENT_TEST_SENDER_TEXT_SIZE];
	for (unsigned int i = 0; i < CLIENT_TEST_SENDER_MESSAGES; i++) {
		const int length = client_test_sender_text (text, sender->id, i);
		test_check_unsigned_eq (http_web_socket_client_send_text (sender->client, text, (size_t) length), 0, NULL);
	}

	return NULL;

}

// an echo must be the next message of its sender, byte by byte: the masks of two frames that mix
// or the bytes of two messages that cross give a text that is not it
static void client_test_on_echo (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) web_socket;

	char received[CLIENT_TEST_SENDER_TEXT_SIZE] = { 0 };
	if (size < CLIENT_TEST_SENDER_TEXT_SIZE) (void) memcpy (received, data, size);

	unsigned int id = CLIENT_TEST_SENDERS;
	unsigned int message = 0;
	bool expected = (type == HTTP_WEB_SOCKET_OPCODE_TEXT)
		&& (sscanf (received, "%u:%u:", &id, &message) == 2) && (id < CLIENT_TEST_SENDERS);

	if (expected) {
		char text[CLIENT_TEST_SENDER_TEXT_SIZE];
		const int length = client_test_sender_text (text, id, client_test_sender_next[id]);
		expected = ((size_t) length == size) && !memcmp (text, received, size);
	}

	// the methods of a connection never run at the same time
	if (expected) client_test_sender_next[id] += 1;
	else test_counter_add (&client_test_sender_mixed);

	test_counter_add (&state.messages);

}

static const HttpWebSocketHandlers client_test_echo_handlers = {
	NULL, client_test_on_open, client_test_on_echo, NULL,
	NULL, NULL, client_test_on_close, client_test_on_error
};

// many threads send with the same client, the masks of their frames never mix:
// every echo is the next message of its sender (only their number was checked)
static void test_client_concurrent (void) {

	client_test_state_reset ();
	ClientTestServer *server = client_test_server_start (client_test_port++);

	(void) memset (client_test_sender_next, 0, sizeof (client_test_sender_next));
	__atomic_store_n (&client_test_sender_mixed, 0, __ATOMIC_SEQ_CST);

	HttpWebSocketClient *client = client_test_create (server->port, "/ws/echo");
	http_web_socket_client_set_handlers (client, &client_test_echo_handlers);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

	pthread_t threads[CLIENT_TEST_SENDERS];
	ClientTestSender senders[CLIENT_TEST_SENDERS];
	for (unsigned int i = 0; i < CLIENT_TEST_SENDERS; i++) {
		senders[i].client = client;
		senders[i].id = i;
		test_check_int_eq (pthread_create (&threads[i], NULL, client_test_sender, &senders[i]), 0, NULL);
	}

	for (unsigned int i = 0; i < CLIENT_TEST_SENDERS; i++) test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);

	const unsigned int expected = CLIENT_TEST_SENDERS * CLIENT_TEST_SENDER_MESSAGES;
	test_check_unsigned_eq (test_wait_counter (&state.messages, expected, TEST_WAIT_SECONDS), expected, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.errors, __ATOMIC_SEQ_CST), 0, NULL);

	http_web_socket_client_delete (client);

	// every message of every sender came back whole and in the order of its thread
	test_check_unsigned_eq (__atomic_load_n (&client_test_sender_mixed, __ATOMIC_SEQ_CST), 0, NULL);
	for (unsigned int i = 0; i < CLIENT_TEST_SENDERS; i++) {
		test_check_unsigned_eq (client_test_sender_next[i], CLIENT_TEST_SENDER_MESSAGES, NULL);
	}

	client_test_server_stop (server);

}

// the client reconnects when the cerver goes away and comes back in the same port
static void test_client_reconnect (void) {

	client_test_state_reset ();
	const u16 port = client_test_port++;
	ClientTestServer *server = client_test_server_start (port);

	HttpWebSocketClient *client = client_test_create (port, "/ws/echo");
	http_web_socket_client_set_reconnect_milliseconds (client, true, CLIENT_TEST_RECONNECT_MIN, CLIENT_TEST_RECONNECT_MAX);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

	client_test_server_stop (server);
	test_check_unsigned_eq (test_wait_counter (&state.going_away, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the attempts while it is down fail
	test_check (test_wait_counter (&state.errors, 2, TEST_WAIT_SECONDS) >= 2, "the client did not try again");
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_CONNECT, NULL);

	server = client_test_server_start (port);
	test_check_unsigned_eq (test_wait_counter (&state.opened, 2, TEST_WAIT_SECONDS), 2, NULL);
	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 2, NULL);

	test_check_unsigned_eq (http_web_socket_client_send_text (client, "again", 5), 0, NULL);
	client_test_expect_message (1, HTTP_WEB_SOCKET_OPCODE_TEXT, "again", 5);

	http_web_socket_client_delete (client);
	client_test_server_stop (server);

}

// milliseconds of a wait between reconnections that the test never lets end,
// and the most secs that a delete during it can take
#define CLIENT_TEST_LONG_BACKOFF			(1 << 15)
#define CLIENT_TEST_STOP_SECONDS			4.0

// the attempts of a client that a server rejects before the test stops it
#define CLIENT_TEST_REJECTED_ATTEMPTS		3

// how a client with reconnection ends, only the reconnection that works had a test:
// its close stops the reconnections, a server that rejects its handshake is tried again
// like one that is down, and its delete does not wait for the next attempt
static void test_client_reconnect_ends (void) {

	client_test_state_reset ();
	const u16 port = client_test_port++;
	ClientTestServer *server = client_test_server_start (port);

	HttpWebSocketClient *client = client_test_create (port, "/ws/echo");
	http_web_socket_client_set_reconnect_milliseconds (client, true, CLIENT_TEST_RECONNECT_MIN, CLIENT_TEST_RECONNECT_MAX);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

	// the thread of the client ends with its connection, it does not open another one
	test_check_unsigned_eq (http_web_socket_client_close (client, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL), 0, NULL);
	http_web_socket_client_wait (client);

	test_check_unsigned_eq (__atomic_load_n (&state.closed, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.errors, __ATOMIC_SEQ_CST), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 1, NULL);

	http_web_socket_client_delete (client);

	// a 401 of the server: every attempt is a handshake error, and the delete ends them
	client_test_state_reset ();
	client = client_test_create (port, "/ws/auth");
	http_web_socket_client_set_reconnect_milliseconds (client, true, CLIENT_TEST_RECONNECT_MIN, CLIENT_TEST_RECONNECT_MAX);
	test_check_unsigned_eq (http_web_socket_client_connect_async (client), 0, NULL);

	test_check (
		test_wait_counter (&state.errors, CLIENT_TEST_REJECTED_ATTEMPTS, TEST_WAIT_SECONDS) >= CLIENT_TEST_REJECTED_ATTEMPTS,
		"the client did not try again after its handshake was rejected"
	);

	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_HANDSHAKE, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);
	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 0, NULL);

	http_web_socket_client_delete (client);
	client_test_server_stop (server);

	// nobody listens and the next attempt is far away: the delete wakes the wait up
	client_test_state_reset ();
	client = client_test_create (client_test_port++, "/");
	http_web_socket_client_set_reconnect_milliseconds (client, true, CLIENT_TEST_LONG_BACKOFF, CLIENT_TEST_LONG_BACKOFF);
	test_check_unsigned_eq (http_web_socket_client_connect_async (client), 0, NULL);
	test_check_unsigned_eq (test_wait_counter (&state.errors, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_CONNECT, NULL);

	const double start = test_wait_now ();
	http_web_socket_client_delete (client);
	test_check ((test_wait_now () - start) < CLIENT_TEST_STOP_SECONDS, "the delete waited for the next attempt");

	test_check_unsigned_eq (__atomic_load_n (&state.errors, __ATOMIC_SEQ_CST), 1, NULL);

}

// the connections that a server accepts and closes at once before the test stops the client,
// and the milliseconds of the first wait between them
#define CLIENT_TEST_UNSTABLE_CONNECTIONS	3
#define CLIENT_TEST_UNSTABLE_RECONNECT_MIN	(1 << 3)

// a server that accepts and closes at once: a connection that did not last the longest wait
// between reconnections is another failure for the wait of the next attempt
// (the failures were forgotten at every 101, so the client came back after the first wait forever)
static void test_client_reconnect_unstable (void) {

	client_test_state_reset ();
	const u16 port = client_test_port++;
	ClientTestServer *server = client_test_server_start (port);

	HttpWebSocketClient *client = client_test_create (port, "/ws/leaving");
	http_web_socket_client_set_reconnect_milliseconds (
		client, true, CLIENT_TEST_UNSTABLE_RECONNECT_MIN, CLIENT_TEST_LONG_BACKOFF
	);

	test_check_unsigned_eq (http_web_socket_client_connect_async (client), 0, NULL);

	test_check (
		test_wait_counter (&state.closed, CLIENT_TEST_UNSTABLE_CONNECTIONS, TEST_WAIT_SECONDS)
			>= CLIENT_TEST_UNSTABLE_CONNECTIONS,
		"the client did not come back to the server that closes its connections"
	);

	// the thread of the client ended, its failures are read without it
	(void) http_web_socket_client_close (client, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);
	http_web_socket_client_wait (client);

	const unsigned int connections = http_web_socket_client_get_connections (client);
	test_check (connections >= CLIENT_TEST_UNSTABLE_CONNECTIONS, "the connections of the client were not counted");
	test_check (client->n_failures >= connections, "a connection that ended at once was not a failure");

	http_web_socket_client_delete (client);
	client_test_server_stop (server);

}

// a method of the client that waits for its client or deletes it runs in the thread of the client:
// both are refused there, the client is still whole and it is deleted from another thread
// (the wait joined its own thread and the delete released the client that its thread still used)
static void test_client_own_thread (void) {

	client_test_state_reset ();
	const u16 port = client_test_port++;
	ClientTestServer *server = client_test_server_start (port);

	HttpWebSocketClient *client = client_test_create (port, "/ws/echo");
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);

	__atomic_store_n (&state.own_client, client, __ATOMIC_SEQ_CST);

	client_test_server_stop (server);
	test_check_unsigned_eq (test_wait_counter (&state.own_calls, 1, TEST_WAIT_SECONDS), 1, NULL);

	__atomic_store_n (&state.own_client, NULL, __ATOMIC_SEQ_CST);

	// the client was not released: it is used and deleted here
	test_check_unsigned_eq (http_web_socket_client_get_connections (client), 1, NULL);
	http_web_socket_client_wait (client);
	http_web_socket_client_delete (client);

}

// a connect to a port where nothing listens
static void test_client_refused (void) {

	client_test_state_reset ();

	HttpWebSocketClient *client = client_test_create (client_test_port++, "/");
	test_check_unsigned_eq (http_web_socket_client_connect_async (client), 0, NULL);
	http_web_socket_client_wait (client);

	test_check_unsigned_eq (__atomic_load_n (&state.errors, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_CONNECT, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.closed, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);

	http_web_socket_client_delete (client);

}

#pragma endregion

#pragma region fake

typedef enum ClientTestFake {

	CLIENT_TEST_FAKE_WRONG_ACCEPT = 0,
	CLIENT_TEST_FAKE_STATUS = 1,
	CLIENT_TEST_FAKE_NO_UPGRADE = 2,
	CLIENT_TEST_FAKE_EXTENSION = 3,
	CLIENT_TEST_FAKE_PROTOCOL = 4,
	CLIENT_TEST_FAKE_MASKED = 5,
	CLIENT_TEST_FAKE_PENDING = 6,
	CLIENT_TEST_FAKE_CLOSE = 7,
	CLIENT_TEST_FAKE_SPLIT = 8,
	CLIENT_TEST_FAKE_SILENT = 9,

	// a response whose headers never end
	CLIENT_TEST_FAKE_ENDLESS = 10

} ClientTestFake;

// the bytes of the header without an end of the endless response, more than the client reads of a response
#define CLIENT_TEST_ENDLESS_SIZE			(HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE << 1)

static const char *client_test_endless_start = { "HTTP/1.1 101 Switching Protocols\r\nX-Endless: " };

typedef struct ClientTestFakeServer {

	int listener;
	u16 port;
	ClientTestFake behavior;
	pthread_t thread;

	// the code of the close frame that the client sent
	u16 client_close;

	// the connections of the client that the server took
	unsigned int accepted;

} ClientTestFakeServer;

static int client_test_listen (const u16 port) {

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	struct sockaddr_storage storage;
	(void) memset (&storage, 0, sizeof (struct sockaddr_storage));
	socklen_t length = 0;

	if (ipv6) {
		struct sockaddr_in6 *address6 = (struct sockaddr_in6 *) &storage;
		address6->sin6_family = AF_INET6;
		address6->sin6_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET6, address, &address6->sin6_addr), 1, NULL);
		length = sizeof (struct sockaddr_in6);
	}

	else {
		struct sockaddr_in *address4 = (struct sockaddr_in *) &storage;
		address4->sin_family = AF_INET;
		address4->sin_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET, address, &address4->sin_addr), 1, NULL);
		length = sizeof (struct sockaddr_in);
	}

	const int listener = socket (ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	test_check (listener > -1, "socket () failed");

	const int enabled = 1;
	(void) setsockopt (listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof (int));
	test_check_int_eq (bind (listener, (const struct sockaddr *) &storage, length), 0, NULL);
	test_check_int_eq (listen (listener, 1), 0, NULL);

	return listener;

}

// reads the request of the client and returns its accept
static void client_test_fake_request (const int fd, char *accept) {

	char request[RAW_RESPONSE_SIZE] = { 0 };
	size_t size = 0;
	while (!strstr (request, "\r\n\r\n") && (size < (RAW_RESPONSE_SIZE - 1))) {
		const ssize_t received = recv (fd, request + size, RAW_RESPONSE_SIZE - 1 - size, 0);
		test_check (received > 0, "the request did not arrive");
		size += (size_t) received;
	}

	const char *key = strstr (request, "Sec-WebSocket-Key: ");
	test_check_ptr (key);
	key += strlen ("Sec-WebSocket-Key: ");
	test_check_true (http_web_socket_accept_compute (key, HTTP_WEB_SOCKET_KEY_LENGTH, accept));

}

// the start of a response and a header that has more bytes than a client reads, without its end:
// the client leaves when it has read them, so the sends can fail
static void client_test_fake_endless (const int fd) {

	char *value = (char *) malloc (CLIENT_TEST_ENDLESS_SIZE);
	test_check_ptr (value);
	(void) memset (value, 'e', CLIENT_TEST_ENDLESS_SIZE);

	(void) send (fd, client_test_endless_start, strlen (client_test_endless_start), MSG_NOSIGNAL);
	(void) send (fd, value, CLIENT_TEST_ENDLESS_SIZE, MSG_NOSIGNAL);

	free (value);

}

// the response of each behavior, and what follows it
static void client_test_fake_respond (ClientTestFakeServer *fake, const int fd, const char *accept) {

	if (fake->behavior == CLIENT_TEST_FAKE_ENDLESS) {
		client_test_fake_endless (fd);
		return;
	}

	char response[RAW_RESPONSE_SIZE];
	const char *upgrade = (fake->behavior == CLIENT_TEST_FAKE_NO_UPGRADE) ? "" : "Upgrade: websocket\r\n";
	const char *status = (fake->behavior == CLIENT_TEST_FAKE_STATUS) ? "200 OK" : "101 Switching Protocols";
	const char *extra = (fake->behavior == CLIENT_TEST_FAKE_EXTENSION) ? "Sec-WebSocket-Extensions: permessage-deflate\r\n" :
		(fake->behavior == CLIENT_TEST_FAKE_PROTOCOL) ? "Sec-WebSocket-Protocol: unknown\r\n" : "";

	int length = snprintf (
		response, RAW_RESPONSE_SIZE,
		"HTTP/1.1 %s\r\n%sConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n%s\r\n",
		status, upgrade, (fake->behavior == CLIENT_TEST_FAKE_WRONG_ACCEPT) ? "dGhlIHNhbXBsZSBub25jZQ==" : accept, extra
	);

	// a text frame of the server in the same packet as the response
	if (fake->behavior == CLIENT_TEST_FAKE_PENDING) {
		length += (int) raw_frame ((u8 *) response + length, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "early", 5, false);
	}

	if (fake->behavior == CLIENT_TEST_FAKE_SPLIT) {
		for (int i = 0; i < length; i++) raw_send (fd, response + i, 1);
	}

	else {
		raw_send (fd, response, (size_t) length);
	}

}

// what the fake server does after its response
static void client_test_fake_after (ClientTestFakeServer *fake, const int fd) {

	RawReader reader;
	raw_reader_init (&reader, fd);

	// the reader of a client takes masked frames
	reader.parser.role = HTTP_WEB_SOCKET_ROLE_SERVER;

	u8 frame[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE + HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	if (fake->behavior == CLIENT_TEST_FAKE_MASKED) {
		raw_send (fd, frame, raw_frame (frame, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "masked", 6, true));
	}

	else if (fake->behavior == CLIENT_TEST_FAKE_CLOSE) {
		raw_send (fd, frame, raw_frame (frame, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xE8", 2, false));
	}

	// the close of the client, then the server ends the TCP connection
	RawFrame close_frame;
	if (raw_read_frame (&reader, &close_frame, CLIENT_TEST_TIMEOUT * 4)) {
		if (close_frame.opcode == HTTP_WEB_SOCKET_OPCODE_CLOSE) fake->client_close = raw_close_code (&close_frame);
		raw_frame_delete (&close_frame);
	}

	raw_reader_end (&reader);

}

static void *client_test_fake_thread (void *fake_ptr) {

	ClientTestFakeServer *fake = (ClientTestFakeServer *) fake_ptr;

	const int fd = accept (fake->listener, NULL, NULL);
	if (fd >= 0) {
		test_counter_add (&fake->accepted);

		if (fake->behavior != CLIENT_TEST_FAKE_SILENT) {
			char accept_value[HTTP_WEB_SOCKET_ACCEPT_SIZE];
			client_test_fake_request (fd, accept_value);
			client_test_fake_respond (fake, fd, accept_value);
			client_test_fake_after (fake, fd);
		}

		else {
			// it never answers, the handshake timeout of the client ends it
			struct pollfd pfd = { fd, POLLRDHUP, 0 };
			(void) poll (&pfd, 1, CLIENT_TEST_TIMEOUT * 4);
		}

		(void) close (fd);
	}

	return NULL;

}

// the timeouts of an attempt that is cancelled, and the most that its cancel can take
#define CLIENT_TEST_CANCEL_TIMEOUT			(1 << 14)
#define CLIENT_TEST_CANCEL_SECONDS			1.0

// an attempt that waits for the answer of its server ends when the client is deleted:
// the delete waited for the timeout of the handshake
static void test_client_cancel (void) {

	client_test_state_reset ();

	ClientTestFakeServer fake;
	(void) memset (&fake, 0, sizeof (ClientTestFakeServer));
	fake.port = client_test_port++;
	fake.behavior = CLIENT_TEST_FAKE_SILENT;
	fake.listener = client_test_listen (fake.port);
	test_check_int_eq (pthread_create (&fake.thread, NULL, client_test_fake_thread, &fake), 0, NULL);

	HttpWebSocketClient *client = client_test_create (fake.port, "/fake");
	http_web_socket_client_set_timeouts_milliseconds (client, CLIENT_TEST_CANCEL_TIMEOUT, CLIENT_TEST_CANCEL_TIMEOUT);
	test_check_unsigned_eq (http_web_socket_client_connect_async (client), 0, NULL);

	// the server has the connection and it never answers the handshake
	test_check_unsigned_eq (test_wait_counter (&fake.accepted, 1, TEST_WAIT_SECONDS), 1, NULL);

	const double start = test_wait_now ();
	http_web_socket_client_delete (client);
	test_check ((test_wait_now () - start) < CLIENT_TEST_CANCEL_SECONDS, "the delete waited for the timeout of the handshake");

	test_check_int_eq (pthread_join (fake.thread, NULL), 0, NULL);
	(void) close (fake.listener);

}

// a client against a fake server, returns the result of its connect,
// the client closes after its first message when the fake server never does
static u8 client_test_fake (const ClientTestFake behavior, ClientTestFakeServer *fake) {

	client_test_state_reset ();

	(void) memset (fake, 0, sizeof (ClientTestFakeServer));
	fake->port = client_test_port++;
	fake->behavior = behavior;
	fake->listener = client_test_listen (fake->port);
	test_check_int_eq (pthread_create (&fake->thread, NULL, client_test_fake_thread, fake), 0, NULL);

	HttpWebSocketClient *client = client_test_create (fake->port, "/fake");

	// the endless response must be rejected long before the timeout of its handshake
	if (behavior == CLIENT_TEST_FAKE_ENDLESS) {
		http_web_socket_client_set_timeouts_milliseconds (client, CLIENT_TEST_CANCEL_TIMEOUT, CLIENT_TEST_CANCEL_TIMEOUT);
	}

	const u8 result = http_web_socket_client_connect (client);

	if (!result && (behavior == CLIENT_TEST_FAKE_PENDING)) {
		test_check_unsigned_eq (test_wait_counter (&state.messages, 1, TEST_WAIT_SECONDS), 1, NULL);
	}

	if (!result && ((behavior == CLIENT_TEST_FAKE_PENDING) || (behavior == CLIENT_TEST_FAKE_SPLIT))) {
		test_check_unsigned_eq (http_web_socket_client_close (client, 1000, NULL), 0, NULL);
	}

	http_web_socket_client_wait (client);
	http_web_socket_client_delete (client);

	test_check_int_eq (pthread_join (fake->thread, NULL), 0, NULL);
	(void) close (fake->listener);

	return result;

}

// the responses that the client rejects, and the frames that break the protocol
static void test_client_fake_servers (void) {

	ClientTestFakeServer fake;

	static const ClientTestFake rejected[] = {
		CLIENT_TEST_FAKE_WRONG_ACCEPT, CLIENT_TEST_FAKE_STATUS, CLIENT_TEST_FAKE_NO_UPGRADE,
		CLIENT_TEST_FAKE_EXTENSION, CLIENT_TEST_FAKE_PROTOCOL, CLIENT_TEST_FAKE_SILENT
	};

	for (size_t i = 0; i < (sizeof (rejected) / sizeof (rejected[0])); i++) {
		test_check_unsigned_eq (client_test_fake (rejected[i], &fake), 1, NULL);
		test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_HANDSHAKE, NULL);
		test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);
		test_check_unsigned_eq (__atomic_load_n (&state.closed, __ATOMIC_SEQ_CST), 1, NULL);
	}

	// a response without an end is rejected when the client has read the most bytes of one,
	// it does not wait for the timeout of its handshake
	const double start = test_wait_now ();
	test_check_unsigned_eq (client_test_fake (CLIENT_TEST_FAKE_ENDLESS, &fake), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_HANDSHAKE, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 0, NULL);
	test_check ((test_wait_now () - start) < CLIENT_TEST_STOP_SECONDS, "the client waited for the end of an endless response");

	// a masked frame of the server fails the connection with 1002
	test_check_unsigned_eq (client_test_fake (CLIENT_TEST_FAKE_MASKED, &fake), 0, NULL);
	test_check_unsigned_eq (fake.client_close, 1002, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_PROTOCOL, NULL);

	// the frame that came with the response
	test_check_unsigned_eq (client_test_fake (CLIENT_TEST_FAKE_PENDING, &fake), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.messages, __ATOMIC_SEQ_CST), 1, NULL);

	// the close of the server is answered with its code
	test_check_unsigned_eq (client_test_fake (CLIENT_TEST_FAKE_CLOSE, &fake), 0, NULL);
	test_check_unsigned_eq (fake.client_close, 1000, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.last_code, __ATOMIC_SEQ_CST), 1000, NULL);

	// a response that arrives one byte at a time
	test_check_unsigned_eq (client_test_fake (CLIENT_TEST_FAKE_SPLIT, &fake), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&state.opened, __ATOMIC_SEQ_CST), 1, NULL);

}

#pragma endregion

void web_socket_tests_client (void) {

	(void) printf ("Testing WebSocket client...\n");

	(void) pthread_mutex_init (&state.mutex, NULL);

	(void) alarm (CLIENT_TEST_ALARM);

	test_run (test_client_responses);
	test_run (test_client_backoff);
	test_run (test_client_urls);
	test_run (test_client_echo);
	test_run (test_client_handshake);
	test_run (test_client_concurrent);
	test_run (test_client_reconnect);
	test_run (test_client_reconnect_ends);
	test_run (test_client_reconnect_unstable);
	test_run (test_client_own_thread);
	test_run (test_client_refused);
	test_run (test_client_cancel);
	test_run (test_client_fake_servers);

	(void) alarm (0);

	(void) pthread_mutex_destroy (&state.mutex);

	(void) printf ("Done!\n");

}
