#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/http.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/client.h>
#include <cerver/http/websockets/settings.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/tls.h>

#include "../test.h"
#include "tls.h"

#define WEBSOCKET_THREADS				4

#define WEBSOCKET_SMALL_MESSAGES		(1 << 9)
#define WEBSOCKET_SMALL_SIZE			(1 << 7)
#define WEBSOCKET_LARGE_MESSAGES		(1 << 3)
#define WEBSOCKET_LARGE_SIZE			200000

#define WEBSOCKET_URL_SIZE				(1 << 7)

// the port after the ones of the server tests
#define WEBSOCKET_PORT					(TLS_TEST_PORT_BASE + (1 << 7))

typedef struct WebSocketCounters {

	unsigned int messages;
	unsigned int wrong;
	unsigned int errors;

} WebSocketCounters;

static WebSocketCounters websocket_counters;

static char *websocket_expected = NULL;
static size_t websocket_expected_size = 0;

static void websocket_echo (HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size) {

	// the test waits for every message: the queue keeps them
	while (http_web_socket_send (web_socket, type, data, size) == HTTP_WEB_SOCKET_SEND_DROPPED) (void) usleep (1 << 10);

}


static void websocket_message (HttpWebSocket *web_socket, HttpWebSocketOpcode type, const void *data, size_t size) {

	(void) web_socket;
	(void) type;

	if ((size != websocket_expected_size) || memcmp (data, websocket_expected, size)) {
		(void) __atomic_add_fetch (&websocket_counters.wrong, 1, __ATOMIC_SEQ_CST);
	}

	(void) __atomic_add_fetch (&websocket_counters.messages, 1, __ATOMIC_SEQ_CST);

}

static void websocket_error (HttpWebSocket *web_socket, HttpWebSocketError error) {

	(void) web_socket;
	(void) error;

	(void) __atomic_add_fetch (&websocket_counters.errors, 1, __ATOMIC_SEQ_CST);

}

static void *websocket_thread (void *cerver_ptr) {

	(void) cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static void websocket_wait_listen (const u16 port) {

	bool listening = false;
	for (unsigned int attempt = 0; !listening && (attempt < (1 << 9)); attempt++) {
		const int fd = socket (AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in address;
		(void) memset (&address, 0, sizeof (address));
		address.sin_family = AF_INET;
		address.sin_port = htons (port);
		address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

		listening = !connect (fd, (struct sockaddr *) &address, sizeof (address));
		(void) close (fd);

		if (!listening) (void) usleep (1 << 12);
	}

	test_check_true (listening);

}

static void websocket_expect (const size_t size) {

	free (websocket_expected);
	websocket_expected = (char *) malloc (size);
	test_check_ptr (websocket_expected);
	for (size_t index = 0; index < size; index++) websocket_expected[index] = (char) ('a' + (index % 26));
	websocket_expected_size = size;

	(void) memset (&websocket_counters, 0, sizeof (WebSocketCounters));

}

static HttpWebSocketClient *websocket_client (const TestCertificates *certificates, const u16 port, const bool verified) {

	HttpWebSocketClient *client = http_web_socket_client_create ();
	test_check_ptr (client);

	// without the authority of the test the certificate of the cerver never verifies
	if (verified) {
		TlsSettings *settings = tls_settings_create ();
		test_check_unsigned_eq (tls_settings_set_ca (settings, certificates->authority, NULL), 0, NULL);
		test_check_unsigned_eq (http_web_socket_client_set_tls (client, settings), 0, NULL);
	}

	HttpWebSocketHandlers handlers;
	(void) memset (&handlers, 0, sizeof (HttpWebSocketHandlers));
	handlers.on_message = websocket_message;
	handlers.on_error = websocket_error;
	http_web_socket_client_set_handlers (client, &handlers);

	char url[WEBSOCKET_URL_SIZE];
	(void) snprintf (url, sizeof (url), "wss://localhost:%u/ws/echo", (unsigned int) port);
	test_check_unsigned_eq (http_web_socket_client_set_url (client, url), 0, NULL);

	return client;

}

static void websocket_exchange (HttpWebSocketClient *client, const unsigned int count, const size_t size) {

	websocket_expect (size);

	for (unsigned int index = 0; index < count; index++) {
		while (http_web_socket_client_send_binary (client, websocket_expected, size) == HTTP_WEB_SOCKET_SEND_DROPPED) {
			(void) usleep (1 << 10);
		}
	}

	test_check_unsigned_eq (test_wait_counter (&websocket_counters.messages, count, TEST_WAIT_SECONDS), count, NULL);
	test_check_unsigned_eq (websocket_counters.wrong, 0, NULL);

}

void tls_tests_websocket (const TestCertificates *certificates) {

	(void) printf ("Testing TLS WebSocket...\n");

	const u16 port = WEBSOCKET_PORT;

	Cerver *cerver = cerver_create_web ("tls-websocket", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, WEBSOCKET_THREADS);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL);
	test_check_unsigned_eq (cerver_set_tls (cerver, settings), 0, NULL);

	HttpWebSocketHandlers echo_handlers;
	(void) memset (&echo_handlers, 0, sizeof (HttpWebSocketHandlers));
	echo_handlers.on_message = websocket_echo;

	HttpGroup *ws = http_cerver_group_create ((HttpCerver *) cerver->cerver_data, "ws");
	test_check_ptr (http_group_web_socket_add (ws, "echo", &echo_handlers));

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, websocket_thread, cerver), 0, NULL);
	websocket_wait_listen (port);

	// wss:// with the authority of the test: small and large messages, both ways encrypted
	HttpWebSocketClient *client = websocket_client (certificates, port, true);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 0, NULL);
	websocket_exchange (client, WEBSOCKET_SMALL_MESSAGES, WEBSOCKET_SMALL_SIZE);
	websocket_exchange (client, WEBSOCKET_LARGE_MESSAGES, WEBSOCKET_LARGE_SIZE);
	http_web_socket_client_delete (client);

	// the store of the system does not have the authority of the test: the verification fails
	websocket_expect (WEBSOCKET_SMALL_SIZE);
	client = websocket_client (certificates, port, false);
	test_check_unsigned_eq (http_web_socket_client_connect (client), 1, NULL);
	test_check_unsigned_eq (websocket_counters.errors, 1, NULL);
	http_web_socket_client_delete (client);

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	free (websocket_expected);
	websocket_expected = NULL;

	(void) printf ("Done!\n");

}
