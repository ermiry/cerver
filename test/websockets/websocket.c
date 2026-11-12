#include <stdlib.h>
#include <stdio.h>

#include <signal.h>

#include "websocket.h"

#include "../test.h"

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	// a peer that leaves is a failed send, never a signal
	(void) signal (SIGPIPE, SIG_IGN);

	(void) printf ("Testing WebSocket...\n");

	web_socket_tests_text ();

	web_socket_tests_frame ();

	web_socket_tests_message ();

	web_socket_tests_handshake ();

	web_socket_tests_deflate ();

	web_socket_tests_server ();

	web_socket_tests_client ();

	(void) printf ("\nDone with WebSocket tests!\n\n");

	return 0;

}
