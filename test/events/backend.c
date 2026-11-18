#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/types.h>

#include "cerver/events/backend.h"

#include "../handler.h"
#include "../test.h"
#include "events.h"

// the completions that a test keeps at most, and the bytes that it keeps of each read
#define BACKEND_TEST_COMPLETIONS			256
#define BACKEND_TEST_DATA_SIZE				64

// a wait that must not find anything returns after these milliseconds, and one that sleeps took at least
// half of them
#define BACKEND_TEST_SHORT_WAIT				32
#define BACKEND_TEST_SHORT_WAIT_MINIMUM		16.0

// a wait that must be woken up fails its test when it ends by this timeout
#define BACKEND_TEST_LONG_WAIT				8000
#define BACKEND_TEST_LONG_WAIT_MINIMUM		4000.0

// the rounds of waits that a test gives its operations to complete
#define BACKEND_TEST_ROUNDS					1024

// the sockets of the tests with many of them
#define BACKEND_TEST_MANY					32

// the handles of the test where one stops the other
#define BACKEND_TEST_OTHERS					2

// the clients of a socket that listens
#define BACKEND_TEST_CLIENTS				3
#define BACKEND_TEST_BACKLOG				16

// the wake ups that are asked before one wait, and the counter of the eventfd that they leave: one write
#define BACKEND_TEST_WAKES					64
#define BACKEND_TEST_WAKE_COUNTER			1

// a send that does not fit in its socket: the bytes, and the ones that its peer reads each time
#define BACKEND_TEST_LARGE_SIZE				(1 << 20)
#define BACKEND_TEST_READ_SIZE				(1 << 14)
#define BACKEND_TEST_SOCKET_BUFFER			(1 << 12)

// the bytes of a large send follow this pattern
#define BACKEND_TEST_PATTERN				251

// the sizes of a backend that is filled by a few operations
#define BACKEND_TEST_SMALL_SUBMISSIONS		2
#define BACKEND_TEST_SMALL_COMPLETIONS		4
#define BACKEND_TEST_SMALL_BUFFERS			2
#define BACKEND_TEST_SMALL_BUFFER_SIZE		BACKEND_TEST_DATA_SIZE
#define BACKEND_TEST_SMALL_EVENTS			2

#define BACKEND_TEST_MILLISECONDS			1000.0

static const char backend_test_message[] = { "hello" };

static const char *backend_test_loopback = { "127.0.0.1" };

typedef EventBackend *(*BackendTestCreate) (const EventBackendSettings *settings);

// the backend that the tests of the current pass use, and whether it is the one of io_uring
static BackendTestCreate backend_test_create = NULL;
static bool backend_test_ring = false;

// what a test keeps of each completion
typedef struct BackendTestCompletion {

	EventHandle *handle;
	unsigned int operation;
	int result;
	char data[BACKEND_TEST_DATA_SIZE];
	bool has_address;

} BackendTestCompletion;

typedef struct BackendTest {

	EventBackend *backend;

	BackendTestCompletion completions[BACKEND_TEST_COMPLETIONS];
	unsigned int count;

	// the waits of backend_test_quiet () that were interrupted, and the milliseconds of its last wait
	unsigned int interrupted;
	double elapsed;

} BackendTest;

static void backend_test_handler (const EventCompletion *completion, void *test_ptr) {

	BackendTest *test = (BackendTest *) test_ptr;

	test_check_true (test->count < BACKEND_TEST_COMPLETIONS);

	BackendTestCompletion *kept = &test->completions[test->count];
	(void) memset (kept, 0, sizeof (BackendTestCompletion));

	kept->handle = completion->handle;
	kept->operation = completion->operation;
	kept->result = completion->result;
	kept->has_address = completion->address != NULL;

	if ((completion->operation == EVENT_OPERATION_RECEIVE) && (completion->result > 0)) {
		test_check_ptr (completion->data);

		const size_t size = ((size_t) completion->result < BACKEND_TEST_DATA_SIZE) ?
			(size_t) completion->result : BACKEND_TEST_DATA_SIZE;

		(void) memcpy (kept->data, completion->data, size);
	}

	test->count += 1;

}

static void backend_test_start (BackendTest *test, const EventBackendSettings *settings) {

	(void) memset (test, 0, sizeof (BackendTest));

	test->backend = backend_test_create (settings);
	test_check_ptr (test->backend);

	// the backend is of the thread that waits on it first: before that wait it only keeps the operations
	// that fit in it, and a test asks for more than a small one has
	test_check_int_eq (event_backend_wait (test->backend, 0, backend_test_handler, test), 0, NULL);

}

static void backend_test_end (BackendTest *test) {

	event_backend_delete (test->backend);

}

// waits until the test has the completions that it expects, however many waits they take
static void backend_test_collect (BackendTest *test, const unsigned int expected) {

	for (unsigned int round = 0; (round < BACKEND_TEST_ROUNDS) && (test->count < expected); round++) {
		test_check_int_gt (
			event_backend_wait (test->backend, BACKEND_TEST_SHORT_WAIT, backend_test_handler, test), -1
		);
	}

	test_check_unsigned_eq (test->count, expected, NULL);

}

static double backend_test_now (void) {

	struct timespec now;
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((double) now.tv_sec * BACKEND_TEST_MILLISECONDS)
		+ ((double) now.tv_nsec / (BACKEND_TEST_MILLISECONDS * BACKEND_TEST_MILLISECONDS));

}

// a wait that is interrupted ends before its timeout with nothing: the operations that the tests of the ring
// left in the kernel end after their rings with the task work of this thread, that interrupts its next wait
static bool backend_test_wait_interrupted (BackendTest *test) {

	errno = 0;

	const double start = backend_test_now ();

	const int waited = event_backend_wait (test->backend, BACKEND_TEST_SHORT_WAIT, backend_test_handler, test);

	test->elapsed = backend_test_now () - start;

	test_check_int_eq (waited, 0, NULL);

	return errno == EINTR;

}

// nothing more completes: a wait that was interrupted is repeated
static void backend_test_quiet (BackendTest *test) {

	const unsigned int count = test->count;

	for (unsigned int round = 0; (round < BACKEND_TEST_ROUNDS) && backend_test_wait_interrupted (test); round++) {
		test->interrupted += 1;
	}

	test_check_unsigned_eq (test->count, count, NULL);

}

// nothing more completes and nothing is reported: the wait sleeps until its timeout, it does not spin
static void backend_test_idle (BackendTest *test) {

	backend_test_quiet (test);

	test_check_true (test->elapsed >= BACKEND_TEST_SHORT_WAIT_MINIMUM);

}

static void backend_test_pair (int *fds) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0, NULL);

}

static void backend_test_pair_close (int *fds) {

	if (fds[0] > -1) (void) close (fds[0]);
	if (fds[1] > -1) (void) close (fds[1]);

	fds[0] = -1;
	fds[1] = -1;

}

static void backend_test_write (const int fd) {

	test_check_long_int_eq (
		(long) write (fd, backend_test_message, sizeof (backend_test_message)),
		(long) sizeof (backend_test_message), NULL
	);

}

// the operations of a handle end before it is released: a backend that still has some tells with a completion
static void backend_test_stop (BackendTest *test, EventHandle *handle) {

	if (event_backend_stop (test->backend, handle)) {
		const unsigned int count = test->count;

		for (unsigned int round = 0; (round < BACKEND_TEST_ROUNDS) && handle->operations; round++) {
			test_check_int_gt (
				event_backend_wait (test->backend, BACKEND_TEST_SHORT_WAIT, backend_test_handler, test), -1
			);
		}

		// only the stop was given
		test_check_unsigned_eq (test->count, count + 1, NULL);
		test_check_ptr_eq (test->completions[count].handle, handle);
		test_check_unsigned_eq (test->completions[count].operation, EVENT_OPERATION_STOP, NULL);

		test->count = count;
	}

	test_check_unsigned_eq (handle->operations, EVENT_OPERATION_NONE, NULL);

}

static void test_backend_create (void) {

	EventBackend *backend = backend_test_create (NULL);
	test_check_ptr (backend);
	test_check_ptr (backend->name);
	test_check_int_gt (backend->wake_fd, -1);

	// nothing was asked
	BackendTest test;
	(void) memset (&test, 0, sizeof (BackendTest));
	test_check_int_eq (event_backend_wait (backend, 0, backend_test_handler, &test), 0, NULL);
	test_check_unsigned_eq (test.count, 0, NULL);

	event_backend_delete (backend);

	event_backend_delete (NULL);

	event_handle_init (NULL, EVENT_HANDLE_TYPE_CONNECTION, -1, NULL);

	EventHandle handle;
	(void) memset (&handle, 1, sizeof (EventHandle));
	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, 1, &test);
	test_check_int_eq ((int) handle.type, (int) EVENT_HANDLE_TYPE_CONNECTION, NULL);
	test_check_int_eq (handle.fd, 1, NULL);
	test_check_ptr_eq (handle.owner, &test);
	test_check_unsigned_eq (handle.operations, EVENT_OPERATION_NONE, NULL);
	test_check_false (handle.stopped);

}

static void test_backend_receive (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	// a read that is asked before its bytes
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	test_check_true ((handle.operations & EVENT_OPERATION_RECEIVE) != 0);
	backend_test_quiet (&test);

	backend_test_write (fds[1]);
	backend_test_collect (&test, 1);

	test_check_ptr_eq (test.completions[0].handle, &handle);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_RECEIVE, NULL);
	test_check_int_eq (test.completions[0].result, (int) sizeof (backend_test_message), NULL);
	test_check_int_eq (
		memcmp (test.completions[0].data, backend_test_message, sizeof (backend_test_message)), 0, NULL
	);
	test_check_unsigned_eq (handle.operations, EVENT_OPERATION_NONE, NULL);

	// the operation is once: the bytes that follow wait until they are asked
	backend_test_write (fds[1]);
	backend_test_quiet (&test);

	// a read that is asked after its bytes
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 2);
	test_check_int_eq (test.completions[1].result, (int) sizeof (backend_test_message), NULL);

	// and again, with the socket empty
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);
	backend_test_write (fds[1]);
	backend_test_collect (&test, 3);
	test_check_int_eq (test.completions[2].result, (int) sizeof (backend_test_message), NULL);

	// the end of the peer is a read of 0 bytes
	(void) shutdown (fds[1], SHUT_WR);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 4);
	test_check_unsigned_eq (test.completions[3].operation, EVENT_OPERATION_RECEIVE, NULL);
	test_check_int_eq (test.completions[3].result, 0, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

// the peer ends while nobody reads the socket: the read that is asked later finds it, and nothing spins
static void test_backend_receive_unasked (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	// the socket was read once: a backend that registers it keeps it registered
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);
	backend_test_write (fds[1]);
	backend_test_collect (&test, 1);

	// bytes and the end of the peer, with no read asked
	backend_test_write (fds[1]);
	(void) close (fds[1]);
	fds[1] = -1;

	// the first wait can find the socket, the second one sleeps
	const u64 waits = test.backend->stats.n_waits;
	const unsigned int interrupted = test.interrupted;
	backend_test_quiet (&test);
	backend_test_idle (&test);
	test_check_true ((test.backend->stats.n_waits - waits - (test.interrupted - interrupted)) == 2);

	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 2);
	test_check_int_eq (test.completions[1].result, (int) sizeof (backend_test_message), NULL);

	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 3);
	test_check_int_eq (test.completions[2].result, 0, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

static void test_backend_send (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	test_check_unsigned_eq (
		event_backend_send (test.backend, &handle, backend_test_message, sizeof (backend_test_message)), 0, NULL
	);
	test_check_true ((handle.operations & EVENT_OPERATION_SEND) != 0);

	backend_test_collect (&test, 1);
	test_check_ptr_eq (test.completions[0].handle, &handle);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_SEND, NULL);
	test_check_int_eq (test.completions[0].result, (int) sizeof (backend_test_message), NULL);
	test_check_unsigned_eq (handle.operations, EVENT_OPERATION_NONE, NULL);

	char received[BACKEND_TEST_DATA_SIZE] = { 0 };
	test_check_long_int_eq (
		(long) read (fds[1], received, sizeof (received)), (long) sizeof (backend_test_message), NULL
	);
	test_check_int_eq (memcmp (received, backend_test_message, sizeof (backend_test_message)), 0, NULL);

	// a send to a peer that left is an error of its completion, never a signal
	(void) close (fds[1]);
	fds[1] = -1;

	test_check_unsigned_eq (
		event_backend_send (test.backend, &handle, backend_test_message, sizeof (backend_test_message)), 0, NULL
	);
	backend_test_collect (&test, 2);
	test_check_unsigned_eq (test.completions[1].operation, EVENT_OPERATION_SEND, NULL);
	test_check_int_eq (test.completions[1].result, -EPIPE, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

static void backend_test_small_buffers (const int *fds) {

	const int size = BACKEND_TEST_SOCKET_BUFFER;

	test_check_int_eq (setsockopt (fds[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof (size)), 0, NULL);
	test_check_int_eq (setsockopt (fds[1], SOL_SOCKET, SO_RCVBUF, &size, sizeof (size)), 0, NULL);

}

// reads what the peer has and checks that its bytes follow the pattern from an offset
static size_t backend_test_drain (const int fd, const size_t offset) {

	size_t total = 0;

	unsigned char bytes[BACKEND_TEST_READ_SIZE];
	ssize_t received = 0;
	while ((received = read (fd, bytes, sizeof (bytes))) > 0) {
		for (ssize_t i = 0; i < received; i++) {
			test_check_unsigned_eq (
				bytes[i], (unsigned char) ((offset + total + (size_t) i) % BACKEND_TEST_PATTERN), NULL
			);
		}

		total += (size_t) received;
	}

	return total;

}

// a send that does not fit in its socket completes with the bytes that fit, and the rest is sent
// as its peer reads: every byte arrives once and in order
static void test_backend_send_partial (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);
	backend_test_small_buffers (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	unsigned char *bytes = (unsigned char *) malloc (BACKEND_TEST_LARGE_SIZE);
	test_check_ptr (bytes);
	for (size_t i = 0; i < BACKEND_TEST_LARGE_SIZE; i++) {
		bytes[i] = (unsigned char) (i % BACKEND_TEST_PATTERN);
	}

	size_t sent = 0;
	size_t received = 0;
	unsigned int sends = 0;

	for (unsigned int round = 0; (round < BACKEND_TEST_ROUNDS) && (received < BACKEND_TEST_LARGE_SIZE); round++) {
		if (!(handle.operations & EVENT_OPERATION_SEND) && (sent < BACKEND_TEST_LARGE_SIZE)) {
			test_check_unsigned_eq (
				event_backend_send (
					test.backend, &handle, (const char *) bytes + sent, BACKEND_TEST_LARGE_SIZE - sent
				), 0, NULL
			);
		}

		test.count = 0;
		test_check_int_gt (event_backend_wait (test.backend, 0, backend_test_handler, &test), -1);

		for (unsigned int i = 0; i < test.count; i++) {
			test_check_unsigned_eq (test.completions[i].operation, EVENT_OPERATION_SEND, NULL);
			test_check_int_gt (test.completions[i].result, 0);

			sent += (size_t) test.completions[i].result;
			sends += 1;
		}

		received += backend_test_drain (fds[1], received);
	}

	test_check_true (sent == BACKEND_TEST_LARGE_SIZE);
	test_check_true (received == BACKEND_TEST_LARGE_SIZE);

	// the socket did not take it at once
	test_check_unsigned_gt (sends, 1);

	free (bytes);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

// fills a socket until it takes no more
static void backend_test_fill (const int fd) {

	char bytes[BACKEND_TEST_READ_SIZE];
	(void) memset (bytes, 0, sizeof (bytes));

	while (send (fd, bytes, sizeof (bytes), MSG_DONTWAIT | MSG_NOSIGNAL) > 0);

	test_check_true ((errno == EAGAIN) || (errno == EWOULDBLOCK));

}

static void test_backend_writable (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);
	backend_test_small_buffers (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	// a socket with room can be written at once
	test_check_unsigned_eq (event_backend_writable (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 1);
	test_check_ptr_eq (test.completions[0].handle, &handle);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_WRITABLE, NULL);
	test_check_int_eq (test.completions[0].result, 0, NULL);
	test_check_unsigned_eq (handle.operations, EVENT_OPERATION_NONE, NULL);

	// once: nothing more is told, and the socket that can still be written is not reported again
	backend_test_idle (&test);

	// a socket that is full waits until its peer reads
	backend_test_fill (fds[0]);

	test_check_unsigned_eq (event_backend_writable (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);
	test_check_true ((handle.operations & EVENT_OPERATION_WRITABLE) != 0);

	char bytes[BACKEND_TEST_READ_SIZE];
	while (read (fds[1], bytes, sizeof (bytes)) > 0);

	backend_test_collect (&test, 2);
	test_check_unsigned_eq (test.completions[1].operation, EVENT_OPERATION_WRITABLE, NULL);
	test_check_int_eq (test.completions[1].result, 0, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

// a send to a socket that is full waits until its peer reads, and then it is made
static void test_backend_send_full (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);
	backend_test_small_buffers (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	backend_test_fill (fds[0]);

	test_check_unsigned_eq (
		event_backend_send (test.backend, &handle, backend_test_message, sizeof (backend_test_message)), 0, NULL
	);
	backend_test_quiet (&test);
	test_check_true ((handle.operations & EVENT_OPERATION_SEND) != 0);

	char bytes[BACKEND_TEST_READ_SIZE];
	while (read (fds[1], bytes, sizeof (bytes)) > 0);

	backend_test_collect (&test, 1);
	test_check_ptr_eq (test.completions[0].handle, &handle);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_SEND, NULL);
	test_check_int_eq (test.completions[0].result, (int) sizeof (backend_test_message), NULL);
	test_check_unsigned_eq (handle.operations, EVENT_OPERATION_NONE, NULL);

	test_check_long_int_eq (
		(long) read (fds[1], bytes, sizeof (bytes)), (long) sizeof (backend_test_message), NULL
	);
	test_check_int_eq (memcmp (bytes, backend_test_message, sizeof (backend_test_message)), 0, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

// a handle that is stopped gets none of its operations, and it can be used again
static void test_backend_stop (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);
	backend_test_small_buffers (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	// without operations there is nothing to wait for
	test_check_unsigned_eq (event_backend_stop (test.backend, &handle), 0, NULL);

	// a read that waits
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);

	backend_test_stop (&test, &handle);
	test_check_unsigned_eq (test.count, 0, NULL);

	// the bytes that arrive later are of nobody
	backend_test_write (fds[1]);
	backend_test_quiet (&test);
	test_check_unsigned_eq (test.count, 0, NULL);

	// the handle works again
	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, 1);
	test_check_int_eq (test.completions[0].result, (int) sizeof (backend_test_message), NULL);

	// a read and a socket that waits to be written, stopped together
	backend_test_fill (fds[0]);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	test_check_unsigned_eq (event_backend_writable (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);

	backend_test_stop (&test, &handle);
	test_check_unsigned_eq (test.count, 1, NULL);

	// a stop that is asked twice ends once
	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);
	(void) event_backend_stop (test.backend, &handle);
	backend_test_stop (&test, &handle);
	backend_test_quiet (&test);
	test_check_unsigned_eq (test.count, 1, NULL);

	backend_test_pair_close (fds);
	backend_test_end (&test);

}

// a handle that is stopped before its operations were given to the kernel is released, and its memory is
// used again: the operations of the other handles still complete
static void test_backend_stop_released (void) {

	BackendTest test;
	EventHandle released;
	EventHandle handle;
	int released_fds[2];
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (released_fds);
	backend_test_pair (fds);

	event_handle_init (&released, EVENT_HANDLE_TYPE_CONNECTION, released_fds[0], &test);
	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	// both are asked before a wait, the first one is stopped
	test_check_unsigned_eq (event_backend_receive (test.backend, &released), 0, NULL);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);

	backend_test_stop (&test, &released);
	test_check_unsigned_eq (test.count, 0, NULL);

	// its owner releases it and the memory is a new handle
	event_handle_init (&released, EVENT_HANDLE_TYPE_CONNECTION, released_fds[0], &test);

	backend_test_write (fds[1]);
	backend_test_collect (&test, 1);
	test_check_ptr_eq (test.completions[0].handle, &handle);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_RECEIVE, NULL);
	test_check_int_eq (test.completions[0].result, (int) sizeof (backend_test_message), NULL);

	backend_test_quiet (&test);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_pair_close (released_fds);
	backend_test_end (&test);

}

// two handles whose reads complete in the same wait: the handler of the first one stops the other one
// and releases its memory as soon as the backend lets it
typedef struct BackendTestOthers {

	EventBackend *backend;

	EventHandle *handles[BACKEND_TEST_OTHERS];

	unsigned int receives;
	unsigned int stops;

} BackendTestOthers;

static void backend_test_others_release (BackendTestOthers *others, const unsigned int position) {

	free (others->handles[position]);
	others->handles[position] = NULL;

}

static void backend_test_others_handler (const EventCompletion *completion, void *others_ptr) {

	BackendTestOthers *others = (BackendTestOthers *) others_ptr;

	const unsigned int position = (completion->handle == others->handles[0]) ? 0 : 1;
	test_check_ptr_eq (completion->handle, others->handles[position]);

	if (completion->operation == EVENT_OPERATION_RECEIVE) {
		others->receives += 1;

		// a backend without operations of the other handle lets it go at once
		const unsigned int other = position ? 0 : 1;
		if (!event_backend_stop (others->backend, others->handles[other])) {
			backend_test_others_release (others, other);
		}
	}

	// the backend ended the operations of the handle that was stopped
	else {
		test_check_unsigned_eq (completion->operation, EVENT_OPERATION_STOP, NULL);

		others->stops += 1;

		backend_test_others_release (others, position);
	}

}

// a handler stops a handle that completed in the same wait and releases it: its completion is of nobody,
// and the backend does not use its memory
static void test_backend_stop_other (void) {

	BackendTestOthers others;
	int fds[BACKEND_TEST_OTHERS][2];

	(void) memset (&others, 0, sizeof (BackendTestOthers));

	others.backend = backend_test_create (NULL);
	test_check_ptr (others.backend);

	for (unsigned int i = 0; i < BACKEND_TEST_OTHERS; i++) {
		backend_test_pair (fds[i]);

		others.handles[i] = (EventHandle *) malloc (sizeof (EventHandle));
		test_check_ptr (others.handles[i]);

		event_handle_init (others.handles[i], EVENT_HANDLE_TYPE_CONNECTION, fds[i][0], &others);

		test_check_unsigned_eq (event_backend_receive (others.backend, others.handles[i]), 0, NULL);
	}

	// both reads wait in the backend
	test_check_int_eq (
		event_backend_wait (others.backend, BACKEND_TEST_SHORT_WAIT, backend_test_others_handler, &others), 0, NULL
	);

	for (unsigned int i = 0; i < BACKEND_TEST_OTHERS; i++) {
		backend_test_write (fds[i][1]);
	}

	// one read is given, the other one was stopped by its handler
	for (unsigned int round = 0; (round < BACKEND_TEST_ROUNDS) && !others.receives; round++) {
		test_check_int_gt (
			event_backend_wait (others.backend, BACKEND_TEST_SHORT_WAIT, backend_test_others_handler, &others), -1
		);
	}

	// the handle that was stopped ends: at once, or when the backend tells
	for (
		unsigned int round = 0;
		(round < BACKEND_TEST_ROUNDS) && others.handles[0] && others.handles[1];
		round++
	) {
		test_check_int_gt (
			event_backend_wait (others.backend, BACKEND_TEST_SHORT_WAIT, backend_test_others_handler, &others), -1
		);
	}

	test_check_unsigned_eq (others.receives, 1, NULL);
	test_check_true (!others.handles[0] || !others.handles[1]);

	// nothing more is given
	test_check_int_eq (
		event_backend_wait (others.backend, BACKEND_TEST_SHORT_WAIT, backend_test_others_handler, &others), 0, NULL
	);
	test_check_unsigned_eq (others.receives, 1, NULL);

	for (unsigned int i = 0; i < BACKEND_TEST_OTHERS; i++) {
		if (others.handles[i]) {
			test_check_unsigned_eq (event_backend_stop (others.backend, others.handles[i]), 0, NULL);
			backend_test_others_release (&others, i);
		}

		backend_test_pair_close (fds[i]);
	}

	event_backend_delete (others.backend);

}

// a wait whose operations completed before it sleeps returns at once, whatever its timeout
static void test_backend_wait_completed (void) {

	BackendTest test;
	EventHandle handle;
	int fds[2];

	backend_test_start (&test, NULL);
	backend_test_pair (fds);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fds[0], &test);

	backend_test_write (fds[1]);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);

	double start = backend_test_now ();
	test_check_int_eq (
		event_backend_wait (test.backend, BACKEND_TEST_LONG_WAIT, backend_test_handler, &test), 1, NULL
	);
	test_check_true ((backend_test_now () - start) < BACKEND_TEST_LONG_WAIT_MINIMUM);
	test_check_unsigned_eq (test.completions[0].operation, EVENT_OPERATION_RECEIVE, NULL);

	test_check_unsigned_eq (
		event_backend_send (test.backend, &handle, backend_test_message, sizeof (backend_test_message)), 0, NULL
	);

	start = backend_test_now ();
	test_check_int_eq (
		event_backend_wait (test.backend, BACKEND_TEST_LONG_WAIT, backend_test_handler, &test), 1, NULL
	);
	test_check_true ((backend_test_now () - start) < BACKEND_TEST_LONG_WAIT_MINIMUM);
	test_check_unsigned_eq (test.completions[1].operation, EVENT_OPERATION_SEND, NULL);

	backend_test_stop (&test, &handle);
	backend_test_pair_close (fds);
	backend_test_end (&test);

}

static int backend_test_listen (struct sockaddr_in *address) {

	const int fd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	test_check_int_gt (fd, -1);

	(void) memset (address, 0, sizeof (struct sockaddr_in));
	address->sin_family = AF_INET;
	address->sin_port = 0;
	test_check_int_eq (inet_pton (AF_INET, backend_test_loopback, &address->sin_addr), 1, NULL);

	test_check_int_eq (bind (fd, (struct sockaddr *) address, sizeof (struct sockaddr_in)), 0, NULL);
	test_check_int_eq (listen (fd, BACKEND_TEST_BACKLOG), 0, NULL);

	socklen_t size = sizeof (struct sockaddr_in);
	test_check_int_eq (getsockname (fd, (struct sockaddr *) address, &size), 0, NULL);

	return fd;

}

static int backend_test_connect (const struct sockaddr_in *address) {

	const int fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	test_check_int_gt (fd, -1);

	test_check_int_eq (
		connect (fd, (const struct sockaddr *) address, sizeof (struct sockaddr_in)), 0, NULL
	);

	return fd;

}

static void test_backend_accept (void) {

	BackendTest test;
	EventHandle handle;
	struct sockaddr_in address;
	int clients[BACKEND_TEST_CLIENTS + 1];

	backend_test_start (&test, NULL);

	const int listen_fd = backend_test_listen (&address);
	event_handle_init (&handle, EVENT_HANDLE_TYPE_LISTEN, listen_fd, &test);

	test_check_unsigned_eq (event_backend_accept (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);

	for (unsigned int i = 0; i < BACKEND_TEST_CLIENTS; i++) {
		clients[i] = backend_test_connect (&address);
	}

	backend_test_collect (&test, BACKEND_TEST_CLIENTS);

	for (unsigned int i = 0; i < BACKEND_TEST_CLIENTS; i++) {
		test_check_ptr_eq (test.completions[i].handle, &handle);
		test_check_unsigned_eq (test.completions[i].operation, EVENT_OPERATION_ACCEPT, NULL);
		test_check_int_gt (test.completions[i].result, -1);

		// the socket of the connection is the one of a peer
		struct sockaddr_storage peer;
		socklen_t peer_size = sizeof (struct sockaddr_storage);
		test_check_int_eq (
			getpeername (test.completions[i].result, (struct sockaddr *) &peer, &peer_size), 0, NULL
		);
		test_check_int_eq (peer.ss_family, AF_INET, NULL);

		(void) close (test.completions[i].result);
	}

	// the operation goes on until it is stopped
	test_check_true ((handle.operations & EVENT_OPERATION_ACCEPT) != 0);

	backend_test_stop (&test, &handle);

	// a client that connects now waits in the socket: it is accepted when the operation is asked again
	clients[BACKEND_TEST_CLIENTS] = backend_test_connect (&address);
	backend_test_quiet (&test);
	test_check_unsigned_eq (test.count, BACKEND_TEST_CLIENTS, NULL);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_LISTEN, listen_fd, &test);
	test_check_unsigned_eq (event_backend_accept (test.backend, &handle), 0, NULL);
	backend_test_collect (&test, BACKEND_TEST_CLIENTS + 1);
	test_check_int_gt (test.completions[BACKEND_TEST_CLIENTS].result, -1);
	(void) close (test.completions[BACKEND_TEST_CLIENTS].result);

	backend_test_stop (&test, &handle);

	for (unsigned int i = 0; i <= BACKEND_TEST_CLIENTS; i++) {
		(void) close (clients[i]);
	}

	(void) close (listen_fd);

	backend_test_end (&test);

}

// a peer that resets its connection is the error of the read
static void test_backend_receive_reset (void) {

	BackendTest test;
	EventHandle listen_handle;
	EventHandle handle;
	struct sockaddr_in address;

	backend_test_start (&test, NULL);

	const int listen_fd = backend_test_listen (&address);
	event_handle_init (&listen_handle, EVENT_HANDLE_TYPE_LISTEN, listen_fd, &test);
	test_check_unsigned_eq (event_backend_accept (test.backend, &listen_handle), 0, NULL);

	const int client = backend_test_connect (&address);
	backend_test_collect (&test, 1);

	const int fd = test.completions[0].result;
	test_check_int_gt (fd, -1);

	event_handle_init (&handle, EVENT_HANDLE_TYPE_CONNECTION, fd, &test);
	test_check_unsigned_eq (event_backend_receive (test.backend, &handle), 0, NULL);
	backend_test_quiet (&test);

	// a close with a linger of 0 sends a reset
	const struct linger reset = { 1, 0 };
	test_check_int_eq (setsockopt (client, SOL_SOCKET, SO_LINGER, &reset, sizeof (reset)), 0, NULL);
	(void) close (client);

	backend_test_collect (&test, 2);
	test_check_ptr_eq (test.completions[1].handle, &handle);
	test_check_unsigned_eq (test.completions[1].operation, EVENT_OPERATION_RECEIVE, NULL);
	test_check_int_eq (test.completions[1].result, -ECONNRESET, NULL);

	backend_test_stop (&test, &handle);
	backend_test_stop (&test, &listen_handle);

	(void) close (fd);
	(void) close (listen_fd);

	backend_test_end (&test);

}

// many sockets with their bytes at the same time: each one gets its own, also with a backend that has
// fewer buffers, submissions and events than sockets
static void backend_test_many (const EventBackendSettings *settings) {

	BackendTest test;
	EventHandle handles[BACKEND_TEST_MANY];
	int fds[BACKEND_TEST_MANY][2];
	bool received[BACKEND_TEST_MANY];

	backend_test_start (&test, settings);

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		backend_test_pair (fds[i]);

		event_handle_init (&handles[i], EVENT_HANDLE_TYPE_CONNECTION, fds[i][0], &test);

		// each socket has the bytes of its position
		const unsigned char byte = (unsigned char) i;
		test_check_long_int_eq ((long) write (fds[i][1], &byte, sizeof (byte)), 1, NULL);

		received[i] = false;
	}

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		test_check_unsigned_eq (event_backend_receive (test.backend, &handles[i]), 0, NULL);
	}

	backend_test_collect (&test, BACKEND_TEST_MANY);

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		const BackendTestCompletion *completion = &test.completions[i];

		test_check_unsigned_eq (completion->operation, EVENT_OPERATION_RECEIVE, NULL);
		test_check_int_eq (completion->result, 1, NULL);

		const size_t position = (size_t) (completion->handle - handles);
		test_check_true (position < BACKEND_TEST_MANY);
		test_check_false (received[position]);
		test_check_unsigned_eq ((unsigned char) completion->data[0], (unsigned char) position, NULL);

		received[position] = true;
	}

	backend_test_quiet (&test);

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		backend_test_stop (&test, &handles[i]);
		backend_test_pair_close (fds[i]);
	}

	backend_test_end (&test);

}

static void test_backend_many (void) {

	backend_test_many (NULL);

}

static void test_backend_many_small (void) {

	const EventBackendSettings settings = {
		BACKEND_TEST_SMALL_SUBMISSIONS, BACKEND_TEST_SMALL_COMPLETIONS,
		BACKEND_TEST_SMALL_BUFFERS, BACKEND_TEST_SMALL_BUFFER_SIZE,
		BACKEND_TEST_SMALL_EVENTS
	};

	backend_test_many (&settings);

}

// with fewer buffers than reads that are ready, the ring leaves some reads without one: they are asked
// again and every socket still gets its bytes
static void test_backend_no_buffers (void) {

	BackendTest test;
	EventHandle handles[BACKEND_TEST_MANY];
	int fds[BACKEND_TEST_MANY][2];

	const EventBackendSettings settings = {
		0, 0, BACKEND_TEST_SMALL_BUFFERS, BACKEND_TEST_SMALL_BUFFER_SIZE, 0
	};

	backend_test_start (&test, &settings);

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		backend_test_pair (fds[i]);
		event_handle_init (&handles[i], EVENT_HANDLE_TYPE_CONNECTION, fds[i][0], &test);
		backend_test_write (fds[i][1]);

		test_check_unsigned_eq (event_backend_receive (test.backend, &handles[i]), 0, NULL);
	}

	backend_test_collect (&test, BACKEND_TEST_MANY);

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		test_check_int_eq (test.completions[i].result, (int) sizeof (backend_test_message), NULL);
		test_check_int_eq (
			memcmp (test.completions[i].data, backend_test_message, sizeof (backend_test_message)), 0, NULL
		);
	}

	test_check_true (!backend_test_ring || (test.backend->stats.n_no_buffers > 0));

	for (unsigned int i = 0; i < BACKEND_TEST_MANY; i++) {
		backend_test_stop (&test, &handles[i]);
		backend_test_pair_close (fds[i]);
	}

	backend_test_end (&test);

}

static void test_backend_wake (void) {

	BackendTest test;
	backend_test_start (&test, NULL);

	// many wake ups before a wait are one
	for (unsigned int i = 0; i < BACKEND_TEST_WAKES; i++) {
		event_backend_wake (test.backend);
	}

	// they wrote once: the counter of the eventfd is read, and written back for the wait
	u64 counter = 0;
	test_check_long_int_eq (
		(long) read (test.backend->wake_fd, &counter, sizeof (counter)), (long) sizeof (counter), NULL
	);
	test_check_true (counter == BACKEND_TEST_WAKE_COUNTER);
	test_check_long_int_eq (
		(long) write (test.backend->wake_fd, &counter, sizeof (counter)), (long) sizeof (counter), NULL
	);

	double start = backend_test_now ();
	test_check_int_eq (
		event_backend_wait (test.backend, BACKEND_TEST_LONG_WAIT, backend_test_handler, &test), 0, NULL
	);
	test_check_true ((backend_test_now () - start) < BACKEND_TEST_LONG_WAIT_MINIMUM);
	test_check_unsigned_eq (test.count, 0, NULL);
	test_check_true (test.backend->stats.n_wakes == 1);

	// it was taken: the next wait ends by its timeout
	backend_test_quiet (&test);
	test_check_true (test.backend->stats.n_wakes == 1);

	// and the next wake up is a new one
	event_backend_wake (test.backend);
	start = backend_test_now ();
	test_check_int_eq (
		event_backend_wait (test.backend, BACKEND_TEST_LONG_WAIT, backend_test_handler, &test), 0, NULL
	);
	test_check_true ((backend_test_now () - start) < BACKEND_TEST_LONG_WAIT_MINIMUM);
	test_check_true (test.backend->stats.n_wakes == 2);

	// the errno of the caller is kept: a signal handler wakes a backend up
	errno = ENOENT;
	event_backend_wake (test.backend);
	test_check_int_eq (errno, ENOENT, NULL);

	event_backend_wake (NULL);

	backend_test_end (&test);

}

static void *test_backend_wake_thread (void *backend_ptr) {

	const struct timespec delay = { 0, BACKEND_TEST_SHORT_WAIT * 1000000L };
	(void) nanosleep (&delay, NULL);

	event_backend_wake ((EventBackend *) backend_ptr);

	return NULL;

}

// another thread wakes up a backend that waits
static void test_backend_wake_from_thread (void) {

	BackendTest test;
	backend_test_start (&test, NULL);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, test_backend_wake_thread, test.backend), 0, NULL);

	const double start = backend_test_now ();

	// a signal can end the wait before
	int waited = 0;
	while (!waited && !test.backend->stats.n_wakes) {
		test_check_int_eq (
			event_backend_wait (test.backend, BACKEND_TEST_LONG_WAIT, backend_test_handler, &test), 0, NULL
		);

		waited = (backend_test_now () - start) >= BACKEND_TEST_LONG_WAIT_MINIMUM;
	}

	test_check_true ((backend_test_now () - start) < BACKEND_TEST_LONG_WAIT_MINIMUM);
	test_check_true (test.backend->stats.n_wakes == 1);

	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	backend_test_end (&test);

}

static void backend_tests_run (const char *name, const BackendTestCreate create, const bool ring) {

	(void) printf ("\nTesting BACKEND %s...\n", name);

	backend_test_create = create;
	backend_test_ring = ring;

	test_run (test_backend_create);
	test_run (test_backend_receive);
	test_run (test_backend_receive_unasked);
	test_run (test_backend_send);
	test_run (test_backend_send_partial);
	test_run (test_backend_writable);
	test_run (test_backend_send_full);
	test_run (test_backend_stop);
	test_run (test_backend_stop_released);
	test_run (test_backend_stop_other);
	test_run (test_backend_accept);
	test_run (test_backend_receive_reset);
	test_run (test_backend_many);
	test_run (test_backend_many_small);
	test_run (test_backend_no_buffers);
	test_run (test_backend_wait_completed);
	test_run (test_backend_wake);
	test_run (test_backend_wake_from_thread);

	(void) printf ("Done!\n");

}

// the backend of io_uring needs io_uring: a kernel without it, or a container that does not allow its
// system calls, skips its tests, unless the backend of the tests was asked to be that one
static bool backend_test_uring_available (void) {

	bool available = true;

	EventBackend *backend = event_backend_uring_create (NULL);
	if (backend) {
		event_backend_delete (backend);
	}

	else {
		const int error = errno;

		if (test_backend_is (TEST_BACKEND_URING)) {
			(void) fprintf (stderr, "io_uring is not available (errno %d: %s)\n", error, strerror (error));
			exit (1);
		}

		(void) printf (
			"\nio_uring is not available (errno %d: %s), the tests of its backend are skipped\n",
			error, strerror (error)
		);

		available = false;
	}

	return available;

}

void events_tests_backend (void) {

	if (!test_backend_is (TEST_BACKEND_EPOLL) && backend_test_uring_available ()) {
		backend_tests_run ("io_uring", event_backend_uring_create, true);
	}

	if (!test_backend_is (TEST_BACKEND_URING)) {
		backend_tests_run ("epoll", event_backend_epoll_create, false);
	}

}
