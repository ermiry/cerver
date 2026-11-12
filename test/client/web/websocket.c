#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>

#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/utils/log.h>

#include "../../test.h"

// the client of the web/websocket pair, with the public client of cerver:
// the echo of every size, with permessage-deflate, a chat of many clients and the authentication
// WEBSOCKET_HOST, WEBSOCKET_PORT and WEBSOCKET_PREFIX point it to a proxy

#define WEBSOCKET_DEFAULT_PORT			8080

#define WEBSOCKET_URL_SIZE				(1 << 9)
#define WEBSOCKET_MESSAGE_SIZE			(1 << 19)

#define WEBSOCKET_CHAT_CLIENTS			8

#define WEBSOCKET_DECIMAL_BASE			10

static const char *websocket_token = { "pair-token" };

// what a client received
typedef struct WebSocketPairClient {

	HttpWebSocketClient *client;

	unsigned int messages;
	unsigned int closed;
	unsigned int last_code;

	pthread_mutex_t mutex;
	u8 message[WEBSOCKET_MESSAGE_SIZE];
	size_t size;

} WebSocketPairClient;

static void websocket_on_message (
	HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) type;

	WebSocketPairClient *pair = (WebSocketPairClient *) http_web_socket_get_data (web_socket);

	(void) pthread_mutex_lock (&pair->mutex);
	if (size <= WEBSOCKET_MESSAGE_SIZE) {
		(void) memcpy (pair->message, data, size);
		pair->size = size;
	}
	(void) pthread_mutex_unlock (&pair->mutex);

	test_counter_add (&pair->messages);

}

static void websocket_on_close (HttpWebSocket *web_socket, u16 code, const char *reason, size_t length) {

	(void) reason;
	(void) length;

	WebSocketPairClient *pair = (WebSocketPairClient *) http_web_socket_get_data (web_socket);
	__atomic_store_n (&pair->last_code, (unsigned int) code, __ATOMIC_SEQ_CST);
	test_counter_add (&pair->closed);

}

static const HttpWebSocketHandlers websocket_handlers = {
	NULL, NULL, websocket_on_message, NULL, NULL, NULL, websocket_on_close, NULL
};

static void websocket_url (char *url, const char *path) {

	const char *host = getenv ("WEBSOCKET_HOST");
	if (!host) host = test_address ();

	const char *port_value = getenv ("WEBSOCKET_PORT");
	const unsigned int port = port_value ? (unsigned int) strtoul (port_value, NULL, WEBSOCKET_DECIMAL_BASE) : WEBSOCKET_DEFAULT_PORT;

	const char *prefix = getenv ("WEBSOCKET_PREFIX");
	if (!prefix) prefix = "";

	const bool ipv6 = (strchr (host, ':') != NULL);
	(void) snprintf (
		url, WEBSOCKET_URL_SIZE, ipv6 ? "ws://[%s]:%u%s%s" : "ws://%s:%u%s%s",
		host, port, prefix, path
	);

}

// connects a client, its connection keeps the pair as its data
static bool websocket_connect (WebSocketPairClient *pair, const char *path, const bool compression, const char *token) {

	(void) memset (pair, 0, sizeof (WebSocketPairClient));
	(void) pthread_mutex_init (&pair->mutex, NULL);

	char url[WEBSOCKET_URL_SIZE];
	websocket_url (url, path);

	pair->client = http_web_socket_client_create ();
	test_check_ptr (pair->client);
	test_check_unsigned_eq (http_web_socket_client_set_url (pair->client, url), 0, NULL);
	http_web_socket_client_set_handlers (pair->client, &websocket_handlers);
	http_web_socket_client_set_data (pair->client, pair);
	http_web_socket_client_set_compression (pair->client, compression);
	if (token) (void) http_web_socket_client_add_header (pair->client, "Authorization", token);

	return !http_web_socket_client_connect (pair->client);

}

static void websocket_disconnect (WebSocketPairClient *pair) {

	(void) http_web_socket_client_close (pair->client, 1000, NULL);
	http_web_socket_client_wait (pair->client);

	test_check_unsigned_eq (__atomic_load_n (&pair->last_code, __ATOMIC_SEQ_CST), 1000, NULL);

	http_web_socket_client_delete (pair->client);
	(void) pthread_mutex_destroy (&pair->mutex);

}

static void websocket_expect (WebSocketPairClient *pair, const unsigned int count, const void *data, const size_t size) {

	test_check_unsigned_eq (test_wait_counter (&pair->messages, count, TEST_WAIT_SECONDS), count, NULL);

	(void) pthread_mutex_lock (&pair->mutex);
	test_check_unsigned_eq (pair->size, size, NULL);
	test_check (!memcmp (pair->message, data, size), "the message is wrong");
	(void) pthread_mutex_unlock (&pair->mutex);

}

static void websocket_echo (const bool compression) {

	static const size_t sizes[] = { 0, 1, 125, 126, 65535, 65536, 400000 };

	WebSocketPairClient *pair = (WebSocketPairClient *) calloc (1, sizeof (WebSocketPairClient));
	test_check (websocket_connect (pair, "/ws/echo", compression, NULL), "the echo did not connect");

	u8 *data = (u8 *) malloc (WEBSOCKET_MESSAGE_SIZE);
	for (size_t i = 0; i < WEBSOCKET_MESSAGE_SIZE; i++) data[i] = (u8) ('a' + (i % 23));

	for (size_t i = 0; i < (sizeof (sizes) / sizeof (sizes[0])); i++) {
		test_check_unsigned_eq (http_web_socket_client_send_binary (pair->client, data, sizes[i]), 0, NULL);
		websocket_expect (pair, (unsigned int) i + 1, data, sizes[i]);
	}

	free (data);
	websocket_disconnect (pair);
	free (pair);

}

// a message of a client reaches every other client of its room
static void websocket_chat (void) {

	WebSocketPairClient *pairs = (WebSocketPairClient *) calloc (WEBSOCKET_CHAT_CLIENTS, sizeof (WebSocketPairClient));

	for (unsigned int i = 0; i < WEBSOCKET_CHAT_CLIENTS; i++) {
		test_check (websocket_connect (&pairs[i], "/ws/chat", false, NULL), "a chat did not connect");
		test_check_unsigned_eq (http_web_socket_client_send_text (pairs[i].client, "join:pair", 9), 0, NULL);
		websocket_expect (&pairs[i], 1, "joined", 6);
	}

	test_check_unsigned_eq (http_web_socket_client_send_text (pairs[0].client, "count:pair", 10), 0, NULL);
	websocket_expect (&pairs[0], 2, "8", 1);

	test_check_unsigned_eq (http_web_socket_client_send_text (pairs[0].client, "say:pair:hello", 14), 0, NULL);
	for (unsigned int i = 1; i < WEBSOCKET_CHAT_CLIENTS; i++) websocket_expect (&pairs[i], 2, "hello", 5);

	for (unsigned int i = 0; i < WEBSOCKET_CHAT_CLIENTS; i++) websocket_disconnect (&pairs[i]);

	free (pairs);

}

static void websocket_auth (void) {

	WebSocketPairClient *pair = (WebSocketPairClient *) calloc (1, sizeof (WebSocketPairClient));

	test_check (!websocket_connect (pair, "/ws/auth", false, NULL), "a connection without its token was accepted");
	test_check_unsigned_eq (__atomic_load_n (&pair->last_code, __ATOMIC_SEQ_CST), 1006, NULL);
	http_web_socket_client_delete (pair->client);
	(void) pthread_mutex_destroy (&pair->mutex);

	test_check (websocket_connect (pair, "/ws/auth", false, websocket_token), "the token was not accepted");
	test_check_unsigned_eq (http_web_socket_client_send_text (pair->client, "secret", 6), 0, NULL);
	websocket_expect (pair, 1, "secret", 6);
	websocket_disconnect (pair);

	free (pair);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	(void) printf ("Testing the web/websocket pair...\n");

	(void) printf ("\techo\n");
	websocket_echo (false);

	(void) printf ("\techo with permessage-deflate\n");
	websocket_echo (true);

	test_run (websocket_chat);
	test_run (websocket_auth);

	(void) printf ("Done!\n");

	cerver_end ();

	return 0;

}
