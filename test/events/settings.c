#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/events/events.h>

#include "cerver/events/backend.h"

#include "../test.h"
#include "events.h"

// what an application sets of the event loops of its cerver (cerver/events/events.h)

// the port of the cervers of these tests, that never start
#define SETTINGS_TEST_PORT					7999

// a backend that is not in the table
#define SETTINGS_TEST_BACKEND_NONE			64

// the I/O threads that a test sets
#define SETTINGS_TEST_IO_THREADS			4

static const char *settings_test_name = { "events-settings" };

static Cerver *settings_test_cerver (void) {

	Cerver *cerver = cerver_create_web (settings_test_name, SETTINGS_TEST_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	return cerver;

}

// each backend has the string and the description of its row, and a value out of the table the ones of the first
static void test_settings_backend_strings (void) {

	test_check_str_eq (cerver_events_backend_to_string (CERVER_EVENTS_BACKEND_AUTO), "Auto", NULL);
	test_check_str_eq (cerver_events_backend_to_string (CERVER_EVENTS_BACKEND_URING), "io_uring", NULL);
	test_check_str_eq (cerver_events_backend_to_string (CERVER_EVENTS_BACKEND_EPOLL), "epoll", NULL);
	test_check_str_eq (
		cerver_events_backend_to_string ((CerverEventsBackend) SETTINGS_TEST_BACKEND_NONE), "Auto", NULL
	);

	#define XX(num, name, string, description) \
		test_check_str_eq (cerver_events_backend_description (CERVER_EVENTS_BACKEND_##name), #description, NULL);
	CERVER_EVENTS_BACKEND_MAP (XX)
	#undef XX

	test_check_ptr_eq (
		cerver_events_backend_description ((CerverEventsBackend) SETTINGS_TEST_BACKEND_NONE),
		cerver_events_backend_description (CERVER_EVENTS_BACKEND_AUTO)
	);

}

// a cerver chooses its backend by itself until one is set; a value out of the table is ignored
static void test_settings_backend (void) {

	Cerver *cerver = settings_test_cerver ();

	test_check_int_eq ((int) cerver_get_events_backend (cerver), (int) CERVER_EVENTS_BACKEND_AUTO, NULL);

	cerver_set_events_backend (cerver, CERVER_EVENTS_BACKEND_EPOLL);
	test_check_int_eq ((int) cerver_get_events_backend (cerver), (int) CERVER_EVENTS_BACKEND_EPOLL, NULL);

	cerver_set_events_backend (cerver, CERVER_EVENTS_BACKEND_URING);
	test_check_int_eq ((int) cerver_get_events_backend (cerver), (int) CERVER_EVENTS_BACKEND_URING, NULL);

	cerver_set_events_backend (cerver, (CerverEventsBackend) SETTINGS_TEST_BACKEND_NONE);
	test_check_int_eq ((int) cerver_get_events_backend (cerver), (int) CERVER_EVENTS_BACKEND_URING, NULL);

	cerver_set_events_backend (cerver, CERVER_EVENTS_BACKEND_AUTO);
	test_check_int_eq ((int) cerver_get_events_backend (cerver), (int) CERVER_EVENTS_BACKEND_AUTO, NULL);

	// without a cerver
	cerver_set_events_backend (NULL, CERVER_EVENTS_BACKEND_EPOLL);
	test_check_int_eq ((int) cerver_get_events_backend (NULL), (int) CERVER_EVENTS_BACKEND_AUTO, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the I/O threads of a cerver: a count, or one for each processor; and whether each one stays in its processor
static void test_settings_io_threads (void) {

	Cerver *cerver = settings_test_cerver ();

	test_check_unsigned_eq (cerver->n_io_threads, CERVER_DEFAULT_IO_THREADS, NULL);
	test_check_false (cerver->io_threads_affinity);

	cerver_set_io_threads (cerver, SETTINGS_TEST_IO_THREADS);
	test_check_unsigned_eq (cerver->n_io_threads, SETTINGS_TEST_IO_THREADS, NULL);

	// 0 keeps what it has
	cerver_set_io_threads (cerver, 0);
	test_check_unsigned_eq (cerver->n_io_threads, SETTINGS_TEST_IO_THREADS, NULL);

	cerver_set_io_threads (cerver, CERVER_IO_THREADS_PER_CORE);
	test_check_unsigned_eq (cerver->n_io_threads, CERVER_IO_THREADS_PER_CORE, NULL);

	cerver_set_io_threads_affinity (cerver, true);
	test_check_true (cerver->io_threads_affinity);

	cerver_set_io_threads_affinity (cerver, false);
	test_check_false (cerver->io_threads_affinity);

	cerver_set_io_threads_affinity (NULL, true);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the backend that a cerver asks for is the one that is created, and the one of io_uring or of epoll when it
// did not ask
static void test_settings_backend_create (void) {

	int unavailable = 0;

	EventBackend *backend = event_backend_create (CERVER_EVENTS_BACKEND_EPOLL, NULL, &unavailable);
	test_check_ptr (backend);
	test_check_int_eq ((int) backend->type, (int) CERVER_EVENTS_BACKEND_EPOLL, NULL);
	test_check_str_eq (backend->name, "epoll", NULL);
	test_check_int_eq (unavailable, 0, NULL);
	event_backend_delete (backend);

	// io_uring where this process can use it, epoll and the reason where it can not
	backend = event_backend_create (CERVER_EVENTS_BACKEND_AUTO, NULL, &unavailable);
	test_check_ptr (backend);
	test_check_true (
		((backend->type == CERVER_EVENTS_BACKEND_URING) && !unavailable)
		|| ((backend->type == CERVER_EVENTS_BACKEND_EPOLL) && unavailable)
	);
	test_check_str_eq (backend->name, cerver_events_backend_to_string (backend->type), NULL);
	event_backend_delete (backend);

	// the reason is not asked for
	backend = event_backend_create (CERVER_EVENTS_BACKEND_AUTO, NULL, NULL);
	test_check_ptr (backend);
	event_backend_delete (backend);

}

void events_tests_settings (void) {

	(void) printf ("\nTesting SETTINGS...\n");

	test_run (test_settings_backend_strings);
	test_run (test_settings_backend);
	test_run (test_settings_io_threads);
	test_run (test_settings_backend_create);

	(void) printf ("Done!\n");

}
