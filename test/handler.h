#ifndef _CERVER_TESTS_HANDLER_H_
#define _CERVER_TESTS_HANDLER_H_

#include <stdlib.h>
#include <string.h>

#include <cerver/core/types.h>

// the web servers of the tests run once with each way of handling their connections:
// a thread for each one (the default) or event loops
#define TEST_HANDLER_VARIABLE				"CERVER_TEST_HANDLER"
#define TEST_HANDLER_EVENTS					"events"

// the I/O threads of a web server of the tests with event loops, 1 by default
#define TEST_IO_THREADS_VARIABLE			"CERVER_TEST_IO_THREADS"
#define TEST_IO_THREADS_BASE				10

static inline CerverHandlerType test_handler_type (void) {

	const char *handler = getenv (TEST_HANDLER_VARIABLE);

	return (handler && !strcmp (handler, TEST_HANDLER_EVENTS)) ?
		CERVER_HANDLER_TYPE_EVENTS : CERVER_HANDLER_TYPE_THREADS;

}

// returns 0 when the variable is not set: cerver_set_io_threads () keeps its default
static inline unsigned int test_io_threads (void) {

	const char *threads = getenv (TEST_IO_THREADS_VARIABLE);

	return (threads && threads[0]) ? (unsigned int) strtoul (threads, NULL, TEST_IO_THREADS_BASE) : 0;

}

#endif
