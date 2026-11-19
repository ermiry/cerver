#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/types.h>

#include <cerver/events/events.h>

#include "cerver/events/internal.h"

#include "../handler.h"
#include "../test.h"
#include "events.h"

// the secs that a test waits for a method of its source
#define SOURCE_TEST_WAIT					16

// the milliseconds of the timer that expires, and the ones of the timer that is armed again before it does
#define SOURCE_TEST_SHORT_TIMER				16
#define SOURCE_TEST_LONG_TIMER				60000

// the bytes that a source keeps of what its socket received
#define SOURCE_TEST_DATA_SIZE				64

// the sources of the loop that ends with many of them
#define SOURCE_TEST_MANY					8

// the milliseconds of a step that only looks
#define SOURCE_TEST_NO_WAIT					0

static const char *source_test_message = { "source" };

// a source of a loop without a cerver, with one end of a pair of sockets
typedef struct SourceTest {

	EventSource source;

	// the socket of the source and the one of the test, -1 without them
	int fds[2];

	// the source that this one removes when the loop tells it that it ends, NULL without one
	struct SourceTest *other;

	unsigned int given;
	unsigned int expired;
	unsigned int received;
	unsigned int stopped;
	unsigned int removed;

	char data[SOURCE_TEST_DATA_SIZE];
	size_t length;

} SourceTest;

static void source_test_remove (SourceTest *test) {

	event_loop_source_remove (&test->source);

	test_counter_add (&test->removed);

}

// the source takes its socket and asks for its bytes; its timer is armed twice, and it expires once
static void source_test_given (EventSource *source) {

	SourceTest *test = (SourceTest *) source;

	event_loop_source_attach (source);

	if (test->fds[0] > -1) {
		event_handle_init (&source->handle, EVENT_HANDLE_TYPE_SOURCE, test->fds[0], source);

		test_check_unsigned_eq (event_backend_receive (source->loop->backend, &source->handle), 0, NULL);

		event_loop_source_timer_set (source, SOURCE_TEST_LONG_TIMER);
		event_loop_source_timer_set (source, SOURCE_TEST_SHORT_TIMER);
	}

	test_counter_add (&test->given);

}

// a timer that is cleared does not expire again
static void source_test_expired (EventSource *source) {

	SourceTest *test = (SourceTest *) source;

	event_loop_source_timer_set (source, SOURCE_TEST_SHORT_TIMER);
	event_loop_source_timer_clear (source);

	test_counter_add (&test->expired);

}

static void source_test_received (SourceTest *test, const EventCompletion *completion) {

	test_check_true (completion->result > 0);
	test_check_true ((size_t) completion->result < SOURCE_TEST_DATA_SIZE);

	(void) memcpy (test->data, completion->data, (size_t) completion->result);
	test->length = (size_t) completion->result;

	test_counter_add (&test->received);

}

static void source_test_completed (EventSource *source, const EventCompletion *completion) {

	SourceTest *test = (SourceTest *) source;

	if (completion->operation == EVENT_OPERATION_RECEIVE) source_test_received (test, completion);
	else if (completion->operation == EVENT_OPERATION_STOP) source_test_remove (test);

}

// the source leaves its loop when its socket has no operations; one with another source takes it with it
static void source_test_stopped (EventSource *source) {

	SourceTest *test = (SourceTest *) source;

	test_counter_add (&test->stopped);

	if (test->other) source_test_remove (test->other);

	if (!event_backend_stop (source->loop->backend, &source->handle)) source_test_remove (test);

}

static const EventSourceMethods source_test_methods = {
	.completed = source_test_completed,
	.expired = source_test_expired,
	.given = source_test_given,
	.stopped = source_test_stopped
};

static void source_test_init (SourceTest *test, const bool with_sockets) {

	(void) memset (test, 0, sizeof (SourceTest));

	event_source_init (&test->source, &source_test_methods);

	test->fds[0] = -1;
	test->fds[1] = -1;

	if (with_sockets) test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, test->fds), 0, NULL);

}

static void source_test_end (SourceTest *test) {

	if (test->fds[0] > -1) (void) close (test->fds[0]);
	if (test->fds[1] > -1) (void) close (test->fds[1]);

}

static unsigned int source_test_wait (const unsigned int *counter, const unsigned int expected) {

	return test_wait_counter (counter, expected, SOURCE_TEST_WAIT * test_time_scale ());

}

static void *source_test_thread (void *loop_ptr) {

	event_loop_run ((EventLoop *) loop_ptr);

	return NULL;

}

// io_uring is not available where its system calls are not allowed: its tests are skipped there, unless the
// backend of the tests was asked to be that one
static EventLoop *source_test_loop (const CerverEventsBackend backend) {

	EventLoop *loop = event_loop_create_alone (backend);

	if (!loop && (backend == CERVER_EVENTS_BACKEND_URING) && !test_backend_is (TEST_BACKEND_URING)) {
		(void) printf ("\nio_uring is not available, the tests of the sources with it are skipped\n");
	}

	else {
		test_check_ptr (loop);
	}

	return loop;

}

// a source of a loop without a cerver: another thread gives it, its timer expires once, its socket receives,
// and the loop that ends tells it and waits until it left; a loop that ends takes no more sources
static void test_source_lifecycle (const CerverEventsBackend backend) {

	EventLoop *loop = source_test_loop (backend);
	if (loop) {
		pthread_t thread = 0;
		test_check_int_eq (pthread_create (&thread, NULL, source_test_thread, loop), 0, NULL);

		SourceTest test;
		source_test_init (&test, true);

		test_check_unsigned_eq (event_loop_source_add (loop, &test.source), 0, NULL);
		test_check_unsigned_eq (event_loop_sources (loop), 1, NULL);

		event_loop_source_give (&test.source);

		test_check_unsigned_eq (source_test_wait (&test.given, 1), 1, NULL);
		test_check_unsigned_eq (source_test_wait (&test.expired, 1), 1, NULL);

		const size_t length = strlen (source_test_message);
		test_check_true (write (test.fds[1], source_test_message, length) == (ssize_t) length);

		test_check_unsigned_eq (source_test_wait (&test.received, 1), 1, NULL);
		test_check_unsigned_eq (test.length, length, NULL);
		test_check_int_eq (memcmp (test.data, source_test_message, length), 0, NULL);

		event_loop_finish (loop);

		test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

		test_check_unsigned_eq (__atomic_load_n (&test.given, __ATOMIC_SEQ_CST), 1, NULL);
		test_check_unsigned_eq (__atomic_load_n (&test.expired, __ATOMIC_SEQ_CST), 1, NULL);
		test_check_unsigned_eq (__atomic_load_n (&test.stopped, __ATOMIC_SEQ_CST), 1, NULL);
		test_check_unsigned_eq (__atomic_load_n (&test.removed, __ATOMIC_SEQ_CST), 1, NULL);
		test_check_unsigned_eq (event_loop_sources (loop), 0, NULL);

		SourceTest late;
		source_test_init (&late, false);
		test_check_unsigned_eq (event_loop_source_add (loop, &late.source), 1, NULL);
		test_check_unsigned_eq (event_loop_sources (loop), 0, NULL);

		source_test_end (&test);

		event_loop_delete (loop);
	}

}

// a loop that ends with many sources tells each one once; a source that removes another one when it is told
// leaves the loop with it, and the one that was removed is not told
static void test_source_many (const CerverEventsBackend backend) {

	EventLoop *loop = source_test_loop (backend);
	if (loop) {
		pthread_t thread = 0;
		test_check_int_eq (pthread_create (&thread, NULL, source_test_thread, loop), 0, NULL);

		SourceTest tests[SOURCE_TEST_MANY];
		for (unsigned int idx = 0; idx < SOURCE_TEST_MANY; idx++) {
			source_test_init (&tests[idx], false);

			test_check_unsigned_eq (event_loop_source_add (loop, &tests[idx].source), 0, NULL);
		}

		// the sources are attached in the order that they are given, and told from the last one: the last one
		// takes the first one with it
		tests[SOURCE_TEST_MANY - 1].other = &tests[0];

		for (unsigned int idx = 0; idx < SOURCE_TEST_MANY; idx++) {
			event_loop_source_give (&tests[idx].source);

			test_check_unsigned_eq (source_test_wait (&tests[idx].given, 1), 1, NULL);
		}

		test_check_unsigned_eq (event_loop_sources (loop), SOURCE_TEST_MANY, NULL);

		event_loop_finish (loop);

		test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

		test_check_unsigned_eq (__atomic_load_n (&tests[0].stopped, __ATOMIC_SEQ_CST), 0, NULL);
		test_check_unsigned_eq (__atomic_load_n (&tests[0].removed, __ATOMIC_SEQ_CST), 1, NULL);

		for (unsigned int idx = 1; idx < SOURCE_TEST_MANY; idx++) {
			test_check_unsigned_eq (__atomic_load_n (&tests[idx].stopped, __ATOMIC_SEQ_CST), 1, NULL);
			test_check_unsigned_eq (__atomic_load_n (&tests[idx].removed, __ATOMIC_SEQ_CST), 1, NULL);
		}

		test_check_unsigned_eq (event_loop_sources (loop), 0, NULL);

		event_loop_delete (loop);
	}

}

// the loop of a thread is the one whose step it took last, until that loop is deleted: the thread then runs
// none, and a loop that is created in the same memory is not taken for the one that it ran
static void test_source_current (const CerverEventsBackend backend) {

	EventLoop *loop = source_test_loop (backend);
	if (loop) {
		test_check_true (!event_loop_is_current (loop));

		test_check_true (event_loop_step (loop, SOURCE_TEST_NO_WAIT) >= 0);
		test_check_true (event_loop_is_current (loop));

		const EventLoop *deleted = loop;

		event_loop_delete (loop);

		test_check_true (!event_loop_is_current (deleted));
	}

}

static void source_tests_backend (const CerverEventsBackend backend) {

	test_source_lifecycle (backend);
	test_source_many (backend);
	test_source_current (backend);

}

void events_tests_source (void) {

	(void) printf ("\nTesting the sources of the event loops...\n");

	if (!test_backend_is (TEST_BACKEND_URING)) source_tests_backend (CERVER_EVENTS_BACKEND_EPOLL);

	if (!test_backend_is (TEST_BACKEND_EPOLL)) source_tests_backend (CERVER_EVENTS_BACKEND_URING);

	(void) printf ("Done!\n");

}
