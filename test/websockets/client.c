#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
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

// the short timeouts of the clients of the fake servers
#define CLIENT_TEST_TIMEOUT					(1 << 10)

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
	http_web_socket_client_set_timeouts (client, CLIENT_TEST_TIMEOUT, CLIENT_TEST_TIMEOUT);

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

	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Bad", "a\r\nInjected: yes"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_client_add_header (client, "Authorization", "secret"), 0, NULL);

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

static void *client_test_sender (void *sender_ptr) {

	const ClientTestSender *sender = (const ClientTestSender *) sender_ptr;

	char text[1 << 6];
	for (unsigned int i = 0; i < CLIENT_TEST_SENDER_MESSAGES; i++) {
		const int length = snprintf (text, sizeof (text), "%u:%u", sender->id, i);
		test_check_unsigned_eq (http_web_socket_client_send_text (sender->client, text, (size_t) length), 0, NULL);
	}

	return NULL;

}

// many threads send with the same client, the masks of their frames never mix
static void test_client_concurrent (void) {

	client_test_state_reset ();
	ClientTestServer *server = client_test_server_start (client_test_port++);

	HttpWebSocketClient *client = client_test_create (server->port, "/ws/echo");
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
	client_test_server_stop (server);

}

// the client reconnects when the cerver goes away and comes back in the same port
static void test_client_reconnect (void) {

	client_test_state_reset ();
	const u16 port = client_test_port++;
	ClientTestServer *server = client_test_server_start (port);

	HttpWebSocketClient *client = client_test_create (port, "/ws/echo");
	http_web_socket_client_set_reconnect (client, true, CLIENT_TEST_RECONNECT_MIN, CLIENT_TEST_RECONNECT_MAX);
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
	CLIENT_TEST_FAKE_SILENT = 9

} ClientTestFake;

typedef struct ClientTestFakeServer {

	int listener;
	u16 port;
	ClientTestFake behavior;
	pthread_t thread;

	// the code of the close frame that the client sent
	u16 client_close;

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

// the response of each behavior, and what follows it
static void client_test_fake_respond (ClientTestFakeServer *fake, const int fd, const char *accept) {

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

	test_run (test_client_urls);
	test_run (test_client_echo);
	test_run (test_client_handshake);
	test_run (test_client_concurrent);
	test_run (test_client_reconnect);
	test_run (test_client_refused);
	test_run (test_client_fake_servers);

	(void) alarm (0);

	(void) pthread_mutex_destroy (&state.mutex);

	(void) printf ("Done!\n");

}
