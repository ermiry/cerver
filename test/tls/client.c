#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
#include <cerver/network/network.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/client.h>
#include <cerver/tls/tls.h>

#include "../test.h"
#include "tls.h"

// the ports after the ones of the WebSocket test
#define CLIENT_PORT_BASE				(TLS_TEST_PORT_BASE + (1 << 8))

#define CLIENT_ADDRESS					"127.0.0.1"

#define CLIENT_THREADS					4

// the deadline of the handshakes of the cerver: a plain client never sends its hello
#define CLIENT_HANDSHAKE_TIMEOUT		(1 << 9)

// how long a plain socket waits for the close of the cerver, after its deadline
#define CLIENT_MILLISECONDS_PER_SECOND	1000
#define CLIENT_PLAIN_MARGIN_SECONDS		4

static u16 client_port = CLIENT_PORT_BASE;

static void *client_cerver_thread (void *cerver_ptr) {

	(void) cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static void client_wait_listen (const u16 port) {

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

// a plain TCP socket that never sends its hello: the cerver sends nothing and closes it at the deadline of its
// handshake (a client of cerver can not be used here: client_connect_to_cerver () returns 0 when its first read
// fails, after the receive already released the connection)
static void client_check_plain (const u16 port) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check_int_gt (fd, -1);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	test_check_int_eq (connect (fd, (struct sockaddr *) &address, sizeof (address)), 0, NULL);

	const time_t seconds = (time_t) (
		(test_milliseconds (CLIENT_HANDSHAKE_TIMEOUT) / CLIENT_MILLISECONDS_PER_SECOND) + CLIENT_PLAIN_MARGIN_SECONDS
	);

	test_check_int_eq (sock_set_timeout (fd, seconds), 0, NULL);

	char byte = 0;
	test_check_int_eq ((int) recv (fd, &byte, sizeof (byte), 0), 0, "the cerver sent bytes to a client without TLS");

	(void) close (fd);

}

// a CUSTOM cerver with TLS: its info packet is the first thing that goes encrypted, after the handshake that runs
// in the pool of its TLS
static Cerver *client_cerver_start (
	const TestCertificates *certificates, const u16 port, const CerverHandlerType type, pthread_t *thread
) {

	Cerver *cerver = cerver_create (CERVER_TYPE_CUSTOM, "tls-custom", port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, type);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, CLIENT_THREADS);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL);
	tls_settings_set_handshake_timeout_milliseconds (settings, test_milliseconds (CLIENT_HANDSHAKE_TIMEOUT));
	test_check_unsigned_eq (cerver_set_tls (cerver, settings), 0, NULL);

	test_check_int_eq (pthread_create (thread, NULL, client_cerver_thread, cerver), 0, NULL);
	client_wait_listen (port);

	return cerver;

}

static void client_cerver_stop (Cerver *cerver, pthread_t thread) {

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

static Client *client_with_tls (const TestCertificates *certificates, const char *server_name) {

	Client *client = client_create ();
	test_check_ptr (client);

	TlsSettings *settings = tls_settings_create ();
	test_check_unsigned_eq (tls_settings_set_ca (settings, certificates->authority, NULL), 0, NULL);
	if (server_name) test_check_unsigned_eq (tls_settings_set_server_name (settings, server_name), 0, NULL);
	test_check_unsigned_eq (client_set_tls (client, settings), 0, NULL);

	return client;

}

// connects, reads the info packet of the cerver, and closes
// returns true when the info of the cerver arrived (a plain connect to a cerver with TLS connects, but its
// cerver never sends it)
static bool client_check_connect (Client *client, const u16 port) {

	Connection *connection = client_connection_create (client, CLIENT_ADDRESS, port, PROTOCOL_TCP, false);
	test_check_ptr (connection);
	connection_set_max_sleep (connection, 1);

	bool reported = false;

	// a connect that fails keeps the connection
	if (client_connect (client, connection)) {
		(void) client_connection_drop (NULL, client, connection);
	}

	// a first read that fails has ended the connection, so it is not used after it
	else if (!client_connection_get_next_packet (client, connection)) {
		reported = (connection->cerver_report != NULL);
		(void) client_connection_drop (NULL, client, connection);
	}

	return reported;

}

static void client_check_model (const TestCertificates *certificates, const CerverHandlerType type) {

	const u16 port = client_port++;

	pthread_t thread;
	Cerver *cerver = client_cerver_start (certificates, port, type, &thread);

	// the address of the connection is verified in the IP of the certificate
	Client *client = client_with_tls (certificates, NULL);
	test_check_true (client_check_connect (client, port));

	// a second connection resumes the session of the first one
	test_check_true (client_check_connect (client, port));

	TlsStats stats;
	test_check_unsigned_eq (client_tls_get_stats (client, &stats), 0, NULL);
	test_check_unsigned_eq ((unsigned int) stats.handshakes, 2, NULL);
	test_check_true (stats.resumed >= 1);

	client_delete (client);

	// a name that is not in the certificate never verifies
	client = client_with_tls (certificates, "other.test");
	test_check_false (client_check_connect (client, port));
	client_delete (client);

	// a client without TLS gets nothing from a cerver with it: the handshake of the cerver ends with its deadline
	client_check_plain (port);

	TlsStats cerver_stats;
	test_check_unsigned_eq (cerver_tls_get_stats (cerver, &cerver_stats), 0, NULL);
	test_check_true (cerver_stats.handshakes >= 2);

	client_cerver_stop (cerver, thread);

}

void tls_tests_client (const TestCertificates *certificates) {

	(void) printf ("Testing TLS client...\n");

	client_check_model (certificates, CERVER_HANDLER_TYPE_THREADS);
	client_check_model (certificates, CERVER_HANDLER_TYPE_POLL);

	(void) printf ("Done!\n");

}
