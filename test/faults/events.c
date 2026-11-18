#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerver/types/types.h>

#include <cerver/events/events.h>

#include "cerver/events/backend.h"

#include "../handler.h"
#include "../test.h"
#include "events.h"
#include "faults.h"

// the system calls of io_uring that fail: cerver makes them with syscall (), that this binary defines
// (faults.c). A cerver that did not ask for a backend uses epoll when io_uring can not be set up, whatever
// the reason, and one that asked for io_uring does not start

// a wait that must not find anything returns after these milliseconds
#define EVENTS_FAULTS_SHORT_WAIT			32

// the waits that a test gives an operation to complete
#define EVENTS_FAULTS_ROUNDS				256

// the calls to io_uring_register () of a backend that is created: the operations of the kernel, and its buffers
#define EVENTS_FAULTS_REGISTERS				2

static const char events_faults_message[] = { "hello" };

// why io_uring is not available: a process that is not allowed to use it (the seccomp profile of a
// container), a kernel without it, and a kernel without memory for its queues
static const int events_faults_setup_errors[] = { EPERM, ENOSYS, ENOMEM };

typedef struct EventsFaultsTest {

	unsigned int receives;
	int result;

} EventsFaultsTest;

static void events_faults_handler (const EventCompletion *completion, void *test_ptr) {

	EventsFaultsTest *test = (EventsFaultsTest *) test_ptr;

	if (completion->operation == EVENT_OPERATION_RECEIVE) {
		test->receives += 1;
		test->result = completion->result;
	}

}

// whether this process can use io_uring at all: without it the backend of epoll is the only one
static bool events_faults_ring_available (void) {

	EventBackend *backend = event_backend_uring_create (NULL);

	const bool available = backend != NULL;

	event_backend_delete (backend);

	return available;

}

// io_uring can not be set up: the backend that a cerver takes by default is the one of epoll, and it tells why
static void test_faults_events_setup (void) {

	for (size_t i = 0; i < (sizeof (events_faults_setup_errors) / sizeof (events_faults_setup_errors[0])); i++) {
		const int error = events_faults_setup_errors[i];
		const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_SYSCALL);

		faults_arm_syscall (__NR_io_uring_setup, error, 0);

		int unavailable = 0;
		EventBackend *backend = event_backend_create (CERVER_EVENTS_BACKEND_AUTO, NULL, &unavailable);

		test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_SYSCALL), triggered + 1, NULL);

		test_check_ptr (backend);
		test_check_int_eq ((int) backend->type, (int) CERVER_EVENTS_BACKEND_EPOLL, NULL);
		test_check_int_eq (unavailable, error, NULL);

		event_backend_delete (backend);
	}

}

// a cerver that asked for io_uring gets no backend where it can not be set up, and the reason
static void test_faults_events_setup_asked (void) {

	for (size_t i = 0; i < (sizeof (events_faults_setup_errors) / sizeof (events_faults_setup_errors[0])); i++) {
		const int error = events_faults_setup_errors[i];

		faults_arm_syscall (__NR_io_uring_setup, error, 0);

		int unavailable = 0;
		errno = 0;
		EventBackend *backend = event_backend_create (CERVER_EVENTS_BACKEND_URING, NULL, &unavailable);

		test_check_null_ptr (backend);
		test_check_int_eq (errno, error, NULL);
		test_check_int_eq (unavailable, 0, NULL);
	}

}

// a backend of epoll makes no system call of io_uring: an arm that nobody takes is left as it was
static void test_faults_events_epoll (void) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_SYSCALL);

	faults_arm_syscall (__NR_io_uring_setup, EPERM, 0);

	int unavailable = 0;
	EventBackend *backend = event_backend_create (CERVER_EVENTS_BACKEND_EPOLL, NULL, &unavailable);

	test_check_ptr (backend);
	test_check_int_eq ((int) backend->type, (int) CERVER_EVENTS_BACKEND_EPOLL, NULL);
	test_check_int_eq (unavailable, 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_SYSCALL), triggered, NULL);

	faults_disarm (FAULTS_FUNCTION_SYSCALL);

	event_backend_delete (backend);

}

// the kernel sets the ring up but one of its registrations fails (the operations that it supports, the
// buffers of the reads): the ring is released and the backend is the one of epoll
static void test_faults_events_register (void) {

	for (unsigned int passed = 0; passed < EVENTS_FAULTS_REGISTERS; passed++) {
		const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_SYSCALL);

		faults_arm_syscall (__NR_io_uring_register, EINVAL, passed);

		int unavailable = 0;
		EventBackend *backend = event_backend_create (CERVER_EVENTS_BACKEND_AUTO, NULL, &unavailable);

		test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_SYSCALL), triggered + 1, NULL);

		test_check_ptr (backend);
		test_check_int_eq ((int) backend->type, (int) CERVER_EVENTS_BACKEND_EPOLL, NULL);
		test_check_int_eq (unavailable, EINVAL, NULL);

		event_backend_delete (backend);
	}

}

// a signal interrupts the wait of the ring: nothing is lost, the read completes in the next wait
static void test_faults_events_enter_interrupted (void) {

	EventsFaultsTest test = { 0, 0 };
	EventHandle handle;
	int fds[2];

	EventBackend *backend = event_backend_uring_create (NULL);
	test_check_ptr (backend);

	// the backend is of this thread from its first wait
	test_check_int_eq (event_backend_wait (backend, 0, events_faults_handler, &test), 0, NULL);

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0, NULL);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);
	test_check_unsigned_eq (event_backend_receive (backend, &handle), 0, NULL);

	test_check_long_int_eq (
		(long) write (fds[1], events_faults_message, sizeof (events_faults_message)),
		(long) sizeof (events_faults_message), NULL
	);

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_SYSCALL);

	faults_arm_syscall (__NR_io_uring_enter, EINTR, 0);

	// the wait that was interrupted is not an error, and it gave nothing
	test_check_int_eq (event_backend_wait (backend, EVENTS_FAULTS_SHORT_WAIT, events_faults_handler, &test), 0, NULL);
	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_SYSCALL), triggered + 1, NULL);
	test_check_unsigned_eq (test.receives, 0, NULL);
	test_check_true ((handle.operations & EVENT_OPERATION_RECEIVE) != 0);

	for (unsigned int round = 0; (round < EVENTS_FAULTS_ROUNDS) && !test.receives; round++) {
		test_check_int_gt (event_backend_wait (backend, EVENTS_FAULTS_SHORT_WAIT, events_faults_handler, &test), -1);
	}

	test_check_unsigned_eq (test.receives, 1, NULL);
	test_check_int_eq (test.result, (int) sizeof (events_faults_message), NULL);

	(void) close (fds[0]);
	(void) close (fds[1]);

	event_backend_delete (backend);

}

// the kernel fails the wait of the ring with an error that is not a signal nor a timeout: the wait tells
static void test_faults_events_enter_failed (void) {

	EventsFaultsTest test = { 0, 0 };

	EventBackend *backend = event_backend_uring_create (NULL);
	test_check_ptr (backend);

	test_check_int_eq (event_backend_wait (backend, 0, events_faults_handler, &test), 0, NULL);

	faults_arm_syscall (__NR_io_uring_enter, EBADF, 0);

	test_check_int_eq (event_backend_wait (backend, 0, events_faults_handler, &test), -1, NULL);

	// the next one works
	test_check_int_eq (event_backend_wait (backend, 0, events_faults_handler, &test), 0, NULL);

	event_backend_delete (backend);

}

void faults_tests_events (void) {

	(void) printf ("\nTesting EVENTS faults...\n");

	test_run (test_faults_events_setup);
	test_run (test_faults_events_setup_asked);
	test_run (test_faults_events_epoll);

	// the ones that need a ring that works
	if (events_faults_ring_available ()) {
		test_run (test_faults_events_register);
		test_run (test_faults_events_enter_interrupted);
		test_run (test_faults_events_enter_failed);
	}

	else {
		(void) printf ("\tio_uring is not available, the faults of a ring that works are skipped\n");
	}

	(void) printf ("Done!\n");

}
