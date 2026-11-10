#ifndef _CERVER_TESTS_ADDRESS_H_
#define _CERVER_TESTS_ADDRESS_H_

#include <stdlib.h>

// the integration pairs run once in each family,
// the servers bind this address and the clients connect to it
#define TEST_ADDRESS_VARIABLE				"CERVER_TEST_ADDRESS"
#define TEST_DEFAULT_ADDRESS				"127.0.0.1"

static inline const char *test_address (void) {

	const char *address = getenv (TEST_ADDRESS_VARIABLE);

	return (address && address[0]) ? address : TEST_DEFAULT_ADDRESS;

}

#endif
