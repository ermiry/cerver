#ifndef _CERVER_TESTS_HANDLER_H_
#define _CERVER_TESTS_HANDLER_H_

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/types.h>
#include <cerver/events/events.h>

// a web cerver always handles its connections with event loops:
// the web servers of the tests run once with each backend of the loops (CERVER_TEST_BACKEND)

// the I/O threads of a web server of the tests, the default of the cerver (one for each processor) without it
#define TEST_IO_THREADS_VARIABLE			"CERVER_TEST_IO_THREADS"
#define TEST_IO_THREADS_BASE				10

// returns 0 when the variable is not set: cerver_set_io_threads () keeps its default
static inline unsigned int test_io_threads (void) {

	const char *threads = getenv (TEST_IO_THREADS_VARIABLE);

	return (threads && threads[0]) ? (unsigned int) strtoul (threads, NULL, TEST_IO_THREADS_BASE) : 0;

}

// the backend of the event loops of the tests: "uring" asks for io_uring, and a test that needs it fails
// where it is not available; "epoll" asks for epoll; without the variable the cerver chooses
#define TEST_BACKEND_VARIABLE				"CERVER_TEST_BACKEND"
#define TEST_BACKEND_URING					"uring"
#define TEST_BACKEND_EPOLL					"epoll"

static inline bool test_backend_is (const char *name) {

	const char *backend = getenv (TEST_BACKEND_VARIABLE);

	return backend && !strcmp (backend, name);

}

// what a web server of the tests passes to cerver_set_events_backend ()
static inline CerverEventsBackend test_events_backend (void) {

	CerverEventsBackend backend = CERVER_EVENTS_BACKEND_AUTO;

	if (test_backend_is (TEST_BACKEND_URING)) backend = CERVER_EVENTS_BACKEND_URING;
	else if (test_backend_is (TEST_BACKEND_EPOLL)) backend = CERVER_EVENTS_BACKEND_EPOLL;

	return backend;

}

// whether a web server of the tests keeps its connections between requests: "1" asks for it and "0"
// closes each one after its response; without the variable the cerver decides, and it keeps them
#define TEST_KEEP_ALIVE_VARIABLE			"CERVER_TEST_KEEP_ALIVE"
#define TEST_KEEP_ALIVE_ENABLED				"1"

// returns true when the variable is set: the server calls cerver_http_set_keep_alive () with test_keep_alive ()
static inline bool test_keep_alive_is_set (void) {

	const char *keep_alive = getenv (TEST_KEEP_ALIVE_VARIABLE);

	return keep_alive && keep_alive[0];

}

// what the server does with its connections, that its client expects in its responses
static inline bool test_keep_alive (void) {

	const char *keep_alive = getenv (TEST_KEEP_ALIVE_VARIABLE);

	return (keep_alive && keep_alive[0]) ? !strcmp (keep_alive, TEST_KEEP_ALIVE_ENABLED) : true;

}

#endif
