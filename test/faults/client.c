#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>

#include "../test.h"
#include "client.h"
#include "faults.h"

// the failure paths of the client, with a fault injected in one of its calls

#define FAULTS_CLIENT_ADDRESS			"127.0.0.1"

// a socket that listens in a free port of the address: the connect completes in its backlog
static int faults_client_listen (u16 *port) {

	const int sock_fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (sock_fd >= 0, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	(void) inet_pton (AF_INET, FAULTS_CLIENT_ADDRESS, &address.sin_addr);

	test_check_int_eq (bind (sock_fd, (struct sockaddr *) &address, sizeof (struct sockaddr_in)), 0, NULL);
	test_check_int_eq (listen (sock_fd, SOMAXCONN), 0, NULL);

	socklen_t length = sizeof (struct sockaddr_in);
	test_check_int_eq (getsockname (sock_fd, (struct sockaddr *) &address, &length), 0, NULL);
	*port = ntohs (address.sin_port);

	return sock_fd;

}

// a connection whose update thread can not be created releases the ClientConnection of the thread
// (it was allocated for the thread and leaked when its creation failed)
static void faults_client_connection_start (void) {

	u16 port = 0;
	const int listener = faults_client_listen (&port);

	Client *client = client_create ();
	test_check_ptr (client);

	Connection *connection = client_connection_create (
		client, FAULTS_CLIENT_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);
	test_check_unsigned_eq (client_connect (client, connection), 0, NULL);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_THREAD);

	faults_watch_start (sizeof (ClientConnection));
	faults_arm (FAULTS_FUNCTION_THREAD, true, FAULTS_ANY_SIZE);
	const int started = client_connection_start (client, connection);
	faults_disarm (FAULTS_FUNCTION_THREAD);
	faults_watch_stop ();

	test_check_int_eq (started, 1, NULL);
	test_check_unsigned_eq (
		faults_triggered (FAULTS_FUNCTION_THREAD), triggered + 1, "the update thread did not fail"
	);

	test_check (faults_watch_allocated () > 0, "the ClientConnection of the update thread was not allocated");
	test_check_unsigned_eq (faults_watch_alive (), 0, "the ClientConnection of the update thread leaked");

	test_check_int_eq (client_connection_end (client, connection), 0, NULL);
	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	(void) close (listener);

}

void faults_tests_client (void) {

	(void) printf ("Testing FAULTS client...\n");

	faults_client_connection_start ();

	(void) printf ("Done!\n");

}
