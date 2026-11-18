#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>

#include "../test.h"
#include "tls.h"

// a guard for a test that hangs
#define TLS_TEST_ALARM				(1 << 9)

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) signal (SIGPIPE, SIG_IGN);
	(void) alarm (TLS_TEST_ALARM * test_time_scale ());

	cerver_init ();

	(void) printf ("Testing TLS...\n");

	TestCertificates certificates;
	test_certificates_create (&certificates);

	tls_tests_settings ();
	tls_tests_server (&certificates);
	tls_tests_websocket (&certificates);
	tls_tests_client (&certificates);
	tls_tests_providers (&certificates);
	tls_tests_cervers (&certificates);
	tls_tests_disconnect (&certificates);

	test_certificates_delete (&certificates);

	cerver_end ();

	(void) printf ("\nDone with TLS tests!\n\n");

	return 0;

}
