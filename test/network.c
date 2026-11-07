#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

#include <cerver/network.h>

#include "test.h"

#define NETWORK_BAD_FD			-1

// sock_set_blocking () had its flags inverted,
// the poll cerver called it with true to get a non blocking socket
static void test_network_blocking (void) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > NETWORK_BAD_FD, NULL);

	test_check_true (sock_set_blocking (fd, false));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) != 0, "the socket is not non blocking");

	test_check_true (sock_set_blocking (fd, true));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) == 0, "the socket is not blocking");

	test_check_false (sock_set_blocking (NETWORK_BAD_FD, true));

	(void) close (fd);

}

int main (int argc, char **argv) {

	(void) printf ("Testing NETWORK...\n");

	test_network_blocking ();

	(void) printf ("\nDone with NETWORK tests!\n\n");

	return 0;

}
