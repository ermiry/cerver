#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/io_uring.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/types.h>

#include "cerver/events/ring.h"

#include "../handler.h"
#include "../test.h"
#include "events.h"

// a ring that is filled by a few submissions
#define RING_TEST_SMALL_SUBMISSIONS			4
#define RING_TEST_SMALL_COMPLETIONS			8

// sizes that are not a power of 2 and the ones that the kernel gives for them
#define RING_TEST_ODD_SUBMISSIONS			3
#define RING_TEST_ODD_COMPLETIONS			5
#define RING_TEST_ROUNDED_SUBMISSIONS		4
#define RING_TEST_ROUNDED_COMPLETIONS		8

// more operations than a small ring has submissions and completions
#define RING_TEST_MANY						64

// the operations that fill the completions of a small ring and leave the kernel some of them
#define RING_TEST_OVERFLOW					(RING_TEST_SMALL_COMPLETIONS + RING_TEST_SMALL_SUBMISSIONS)

// the completions that a test reads at most
#define RING_TEST_COMPLETIONS				128

// a wait that must not find anything returns after these milliseconds, and it took at least half of them
#define RING_TEST_SHORT_WAIT				32
#define RING_TEST_SHORT_WAIT_MINIMUM		16

// the rounds of waits that a test gives the kernel to hand its completions over
#define RING_TEST_ROUNDS					1024

// the group of the buffers of the tests, its buffers and their bytes
#define RING_TEST_GROUP						7
#define RING_TEST_BUFFERS					2
#define RING_TEST_BUFFER_SIZE				64

// an opcode that no kernel has
#define RING_TEST_OPERATION_NONE			255

// the data of the operations of a test
#define RING_TEST_DATA						0xC0FFEE00u

#define RING_TEST_MILLISECONDS_PER_SECOND	1000.0

static const char ring_test_message[] = { "hello" };

// what a test keeps of a completion
typedef struct RingTestCompletion {

	u64 data;
	int result;
	unsigned int flags;

} RingTestCompletion;

// the tests of the ring need io_uring: a kernel without it, or a container that does not allow its
// system calls, skips them, unless the backend of the tests was asked to be the ring
static bool ring_test_available (void) {

	bool available = true;

	EventRing ring;
	if (event_ring_create (&ring, 0, 0)) {
		const int error = errno;

		if (test_backend_is (TEST_BACKEND_URING)) {
			(void) fprintf (stderr, "io_uring is not available (errno %d: %s)\n", error, strerror (error));
			exit (1);
		}

		(void) printf (
			"\tio_uring is not available (errno %d: %s), the tests of the ring are skipped\n",
			error, strerror (error)
		);

		available = false;
	}

	else {
		event_ring_delete (&ring);
	}

	return available;

}

// a ring of the thread of the test: created and enabled by it
static void ring_test_create (EventRing *ring, const unsigned int submissions, const unsigned int completions) {

	test_check_unsigned_eq (event_ring_create (ring, submissions, completions), 0, NULL);
	test_check_false (ring->enabled);

	test_check_unsigned_eq (event_ring_enable (ring), 0, NULL);
	test_check_true (ring->enabled);

}

static void ring_test_nop (EventRing *ring, const u64 data) {

	struct io_uring_sqe *submission = event_ring_submission (ring);
	test_check_ptr (submission);

	submission->opcode = IORING_OP_NOP;
	submission->user_data = data;

}

static void ring_test_receive (EventRing *ring, const int fd, const u64 data) {

	struct io_uring_sqe *submission = event_ring_submission (ring);
	test_check_ptr (submission);

	submission->opcode = IORING_OP_RECV;
	submission->fd = fd;
	submission->flags = IOSQE_BUFFER_SELECT;
	submission->buf_group = RING_TEST_GROUP;
	submission->user_data = data;

}

// reads the completions that are ready
static unsigned int ring_test_reap (
	EventRing *ring, RingTestCompletion *completions, const unsigned int max
) {

	unsigned int count = 0;

	unsigned int position = event_ring_completions_begin (ring);
	const unsigned int end = event_ring_completions_end (ring);

	while ((position != end) && (count < max)) {
		const struct io_uring_cqe *completion = event_ring_completion (ring, position);

		completions[count].data = completion->user_data;
		completions[count].result = completion->res;
		completions[count].flags = completion->flags;

		count += 1;
		position += 1;
	}

	event_ring_completions_seen (ring, position);

	return count;

}

// waits for the completions of a test, however many waits the kernel takes to give them
static unsigned int ring_test_collect (
	EventRing *ring, RingTestCompletion *completions, const unsigned int expected
) {

	unsigned int count = 0;

	for (unsigned int round = 0; (round < RING_TEST_ROUNDS) && (count < expected); round++) {
		test_check_unsigned_eq (event_ring_wait (ring, RING_TEST_SHORT_WAIT), 0, NULL);

		count += ring_test_reap (ring, completions + count, expected - count);
	}

	return count;

}

static void ring_test_pair (int *fds) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds), 0, NULL);

}

static void ring_test_pair_close (const int *fds) {

	(void) close (fds[0]);
	(void) close (fds[1]);

}

static void ring_test_write (const int fd) {

	test_check_long_int_eq (
		(long) write (fd, ring_test_message, sizeof (ring_test_message)),
		(long) sizeof (ring_test_message), NULL
	);

}

static void test_ring_create (void) {

	EventRing ring;
	ring_test_create (&ring, 0, 0);

	test_check_int_gt (ring.fd, -1);
	test_check_unsigned_eq (ring.submissions_entries, EVENT_RING_SUBMISSIONS, NULL);
	test_check_unsigned_eq (ring.completions_entries, EVENT_RING_COMPLETIONS, NULL);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 0, NULL);
	test_check_false (event_ring_completions_overflowed (&ring));

	// the operations of an event loop
	test_check_true (event_ring_supports (&ring, IORING_OP_NOP));
	test_check_true (event_ring_supports (&ring, IORING_OP_ACCEPT));
	test_check_true (event_ring_supports (&ring, IORING_OP_RECV));
	test_check_true (event_ring_supports (&ring, IORING_OP_SEND));
	test_check_true (event_ring_supports (&ring, IORING_OP_CLOSE));
	test_check_true (event_ring_supports (&ring, IORING_OP_POLL_ADD));
	test_check_true (event_ring_supports (&ring, IORING_OP_ASYNC_CANCEL));
	test_check_false (event_ring_supports (&ring, RING_TEST_OPERATION_NONE));
	test_check_false (event_ring_supports (&ring, EVENT_RING_OPERATIONS));

	event_ring_delete (&ring);
	test_check_int_eq (ring.fd, -1, NULL);
	test_check_null_ptr (ring.queues);

	// a ring that was deleted is deleted again without harm
	event_ring_delete (&ring);

}

static void test_ring_create_sizes (void) {

	EventRing ring;
	ring_test_create (&ring, RING_TEST_SMALL_SUBMISSIONS, RING_TEST_SMALL_COMPLETIONS);
	test_check_unsigned_eq (ring.submissions_entries, RING_TEST_SMALL_SUBMISSIONS, NULL);
	test_check_unsigned_eq (ring.completions_entries, RING_TEST_SMALL_COMPLETIONS, NULL);
	event_ring_delete (&ring);

	// the kernel rounds the sizes up to a power of 2
	ring_test_create (&ring, RING_TEST_ODD_SUBMISSIONS, RING_TEST_ODD_COMPLETIONS);
	test_check_unsigned_eq (ring.submissions_entries, RING_TEST_ROUNDED_SUBMISSIONS, NULL);
	test_check_unsigned_eq (ring.completions_entries, RING_TEST_ROUNDED_COMPLETIONS, NULL);
	event_ring_delete (&ring);

}

static void test_ring_null (void) {

	errno = 0;
	test_check_unsigned_eq (event_ring_create (NULL, 0, 0), 1, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	event_ring_delete (NULL);

	event_ring_buffers_delete (NULL, NULL);

}

// many rings are created and deleted by the same thread: each one gives back its place in the thread
static void test_ring_create_many (void) {

	for (unsigned int i = 0; i < RING_TEST_MANY; i++) {
		EventRing ring;
		ring_test_create (&ring, RING_TEST_SMALL_SUBMISSIONS, RING_TEST_SMALL_COMPLETIONS);

		test_check_true ((ring.enter_flags & IORING_ENTER_REGISTERED_RING) != 0);

		ring_test_nop (&ring, RING_TEST_DATA + i);

		RingTestCompletion completion;
		test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
		test_check_true (completion.data == (RING_TEST_DATA + i));

		event_ring_delete (&ring);
	}

}

static void test_ring_nop (void) {

	EventRing ring;
	ring_test_create (&ring, 0, 0);

	ring_test_nop (&ring, RING_TEST_DATA);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 1, NULL);

	test_check_unsigned_eq (event_ring_wait (&ring, EVENT_RING_WAIT_FOREVER), 0, NULL);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 0, NULL);
	test_check_true (ring.n_enters == 1);

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_reap (&ring, &completion, 1), 1, NULL);
	test_check_true (completion.data == RING_TEST_DATA);
	test_check_int_eq (completion.result, 0, NULL);

	// nothing is left
	test_check_unsigned_eq (ring_test_reap (&ring, &completion, 1), 0, NULL);

	event_ring_delete (&ring);

}

// a submit gives the operations to the kernel without waiting for them
static void test_ring_submit (void) {

	EventRing ring;
	ring_test_create (&ring, 0, 0);

	// nothing was written: no system call
	test_check_unsigned_eq (event_ring_submit (&ring), 0, NULL);
	test_check_true (ring.n_enters == 0);

	ring_test_nop (&ring, RING_TEST_DATA);
	test_check_unsigned_eq (event_ring_submit (&ring), 0, NULL);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 0, NULL);
	test_check_true (ring.n_enters == 1);

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_true (completion.data == RING_TEST_DATA);

	event_ring_delete (&ring);

}

// more operations than the ring has submissions and completions: a full ring gives its submissions to the
// kernel by itself, and the kernel keeps the completions that do not fit until they are read
static void test_ring_full (void) {

	EventRing ring;
	ring_test_create (&ring, RING_TEST_SMALL_SUBMISSIONS, RING_TEST_SMALL_COMPLETIONS);

	bool overflowed = false;
	for (unsigned int i = 0; i < RING_TEST_MANY; i++) {
		ring_test_nop (&ring, RING_TEST_DATA + i);

		test_check_true (event_ring_submissions_pending (&ring) <= RING_TEST_SMALL_SUBMISSIONS);

		if (event_ring_completions_overflowed (&ring)) overflowed = true;
	}

	RingTestCompletion completions[RING_TEST_COMPLETIONS];
	test_check_unsigned_eq (ring_test_collect (&ring, completions, RING_TEST_MANY), RING_TEST_MANY, NULL);

	// every operation completed once, in the order they were written
	for (unsigned int i = 0; i < RING_TEST_MANY; i++) {
		test_check_true (completions[i].data == (RING_TEST_DATA + i));
		test_check_int_eq (completions[i].result, 0, NULL);
	}

	test_check_true (overflowed);
	test_check_unsigned_eq (*ring.completions_overflow, 0, NULL);
	test_check_false (event_ring_completions_overflowed (&ring));

	event_ring_delete (&ring);

}

// the completions that the kernel kept are given to the ring by the next submit once it has room for them,
// without a wait
static void test_ring_overflow_submit (void) {

	EventRing ring;
	ring_test_create (&ring, RING_TEST_SMALL_SUBMISSIONS, RING_TEST_SMALL_COMPLETIONS);

	for (unsigned int i = 0; i < RING_TEST_OVERFLOW; i++) {
		ring_test_nop (&ring, RING_TEST_DATA + i);
		test_check_unsigned_eq (event_ring_submit (&ring), 0, NULL);
	}

	test_check_true (event_ring_completions_overflowed (&ring));

	RingTestCompletion completions[RING_TEST_COMPLETIONS];
	unsigned int count = ring_test_reap (&ring, completions, RING_TEST_COMPLETIONS);
	test_check_unsigned_eq (count, RING_TEST_SMALL_COMPLETIONS, NULL);

	// the ring has room: the next submit brings the ones that the kernel kept
	ring_test_nop (&ring, RING_TEST_DATA + RING_TEST_OVERFLOW);
	test_check_unsigned_eq (event_ring_submit (&ring), 0, NULL);

	const unsigned int given = ring_test_reap (&ring, completions + count, RING_TEST_COMPLETIONS - count);
	test_check_unsigned_gt (given, 0);
	count += given;

	count += ring_test_collect (&ring, completions + count, (RING_TEST_OVERFLOW + 1) - count);
	test_check_unsigned_eq (count, RING_TEST_OVERFLOW + 1, NULL);

	// every operation completed once, in the order they were written
	for (unsigned int i = 0; i < count; i++) {
		test_check_true (completions[i].data == (RING_TEST_DATA + i));
	}

	test_check_false (event_ring_completions_overflowed (&ring));

	event_ring_delete (&ring);

}

static double ring_test_now (void) {

	struct timespec now;
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((double) now.tv_sec * RING_TEST_MILLISECONDS_PER_SECOND)
		+ ((double) now.tv_nsec / (RING_TEST_MILLISECONDS_PER_SECOND * RING_TEST_MILLISECONDS_PER_SECOND));

}

static void test_ring_wait_timeout (void) {

	EventRing ring;
	ring_test_create (&ring, 0, 0);

	const double start = ring_test_now ();
	test_check_unsigned_eq (event_ring_wait (&ring, RING_TEST_SHORT_WAIT), 0, NULL);
	test_check_true ((ring_test_now () - start) >= RING_TEST_SHORT_WAIT_MINIMUM);

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_reap (&ring, &completion, 1), 0, NULL);

	// a wait that only looks returns at once
	test_check_unsigned_eq (event_ring_wait (&ring, 0), 0, NULL);
	test_check_unsigned_eq (ring_test_reap (&ring, &completion, 1), 0, NULL);

	event_ring_delete (&ring);

}

// the kernel chooses the buffer of a read and its completion tells which
static void test_ring_receive (void) {

	EventRing ring;
	EventRingBuffers buffers;
	int fds[2];

	ring_test_create (&ring, 0, 0);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		0, NULL
	);
	test_check_unsigned_eq (buffers.entries, RING_TEST_BUFFERS, NULL);
	test_check_unsigned_eq (buffers.tail, RING_TEST_BUFFERS, NULL);

	ring_test_pair (fds);

	// the read waits in the kernel until its bytes arrive
	ring_test_receive (&ring, fds[0], RING_TEST_DATA);
	test_check_unsigned_eq (event_ring_wait (&ring, 0), 0, NULL);

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_reap (&ring, &completion, 1), 0, NULL);

	ring_test_write (fds[1]);

	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_true (completion.data == RING_TEST_DATA);
	test_check_int_eq (completion.result, (int) sizeof (ring_test_message), NULL);
	test_check_true ((completion.flags & IORING_CQE_F_BUFFER) != 0);

	const unsigned int id = completion.flags >> IORING_CQE_BUFFER_SHIFT;
	test_check_true (id < RING_TEST_BUFFERS);
	test_check_int_eq (
		memcmp (event_ring_buffer (&buffers, id), ring_test_message, sizeof (ring_test_message)), 0, NULL
	);

	event_ring_buffer_give (&buffers, id);
	test_check_unsigned_eq (buffers.tail, RING_TEST_BUFFERS + 1, NULL);

	// the end of the peer is a read of 0 bytes, without a buffer
	(void) shutdown (fds[1], SHUT_WR);
	ring_test_receive (&ring, fds[0], RING_TEST_DATA + 1);
	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_int_eq (completion.result, 0, NULL);

	ring_test_pair_close (fds);

	event_ring_buffers_delete (&ring, &buffers);
	test_check_null_ptr (buffers.memory);

	event_ring_delete (&ring);

}

// a read without a free buffer fails with ENOBUFS, and it works again when one is given back
static void test_ring_buffers_exhausted (void) {

	EventRing ring;
	EventRingBuffers buffers;
	int fds[2];

	ring_test_create (&ring, 0, 0);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		0, NULL
	);

	ring_test_pair (fds);

	RingTestCompletion completion;
	unsigned int ids[RING_TEST_BUFFERS];

	for (unsigned int i = 0; i < RING_TEST_BUFFERS; i++) {
		ring_test_write (fds[1]);
		ring_test_receive (&ring, fds[0], RING_TEST_DATA + i);

		test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
		test_check_int_eq (completion.result, (int) sizeof (ring_test_message), NULL);

		ids[i] = completion.flags >> IORING_CQE_BUFFER_SHIFT;
	}

	test_check_unsigned_ne (ids[0], ids[1]);

	ring_test_write (fds[1]);
	ring_test_receive (&ring, fds[0], RING_TEST_DATA);

	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_int_eq (completion.result, -ENOBUFS, NULL);
	test_check_false ((completion.flags & IORING_CQE_F_BUFFER) != 0);

	// the bytes are still in the socket: the read is asked again with a buffer
	event_ring_buffer_give (&buffers, ids[0]);
	ring_test_receive (&ring, fds[0], RING_TEST_DATA);

	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_int_eq (completion.result, (int) sizeof (ring_test_message), NULL);
	test_check_unsigned_eq (completion.flags >> IORING_CQE_BUFFER_SHIFT, ids[0], NULL);

	ring_test_pair_close (fds);

	event_ring_buffers_delete (&ring, &buffers);
	event_ring_delete (&ring);

}

static void test_ring_buffers_invalid (void) {

	EventRing ring;
	EventRingBuffers buffers;

	ring_test_create (&ring, 0, 0);

	// not a power of 2
	errno = 0;
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_ODD_SUBMISSIONS, RING_TEST_BUFFER_SIZE),
		1, NULL
	);
	test_check_int_eq (errno, EINVAL, NULL);
	test_check_null_ptr (buffers.memory);

	// no buffers, no bytes, too many
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, 0, RING_TEST_BUFFER_SIZE), 1, NULL
	);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, 0), 1, NULL
	);
	test_check_unsigned_eq (
		event_ring_buffers_create (
			&ring, &buffers, RING_TEST_GROUP, EVENT_RING_BUFFERS_MAX << 1, RING_TEST_BUFFER_SIZE
		), 1, NULL
	);

	test_check_unsigned_eq (
		event_ring_buffers_create (NULL, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		1, NULL
	);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, NULL, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		1, NULL
	);

	// a group whose id is taken
	EventRingBuffers first;
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &first, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		0, NULL
	);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		1, NULL
	);
	test_check_null_ptr (buffers.memory);

	// after the group left, its id is free
	event_ring_buffers_delete (&ring, &first);
	test_check_unsigned_eq (
		event_ring_buffers_create (&ring, &buffers, RING_TEST_GROUP, RING_TEST_BUFFERS, RING_TEST_BUFFER_SIZE),
		0, NULL
	);

	event_ring_buffers_delete (&ring, &buffers);
	event_ring_delete (&ring);

}

static void test_ring_send (void) {

	EventRing ring;
	int fds[2];

	ring_test_create (&ring, 0, 0);

	ring_test_pair (fds);

	struct io_uring_sqe *submission = event_ring_submission (&ring);
	test_check_ptr (submission);

	submission->opcode = IORING_OP_SEND;
	submission->fd = fds[0];
	submission->addr = (u64) (uintptr_t) ring_test_message;
	submission->len = sizeof (ring_test_message);
	submission->msg_flags = MSG_NOSIGNAL;
	submission->user_data = RING_TEST_DATA;

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_int_eq (completion.result, (int) sizeof (ring_test_message), NULL);

	char received[RING_TEST_BUFFER_SIZE] = { 0 };
	test_check_long_int_eq (
		(long) read (fds[1], received, sizeof (received)), (long) sizeof (ring_test_message), NULL
	);
	test_check_int_eq (memcmp (received, ring_test_message, sizeof (ring_test_message)), 0, NULL);

	// a send to a peer that left is an error of its completion, never a signal
	(void) close (fds[1]);
	fds[1] = -1;

	submission = event_ring_submission (&ring);
	test_check_ptr (submission);

	submission->opcode = IORING_OP_SEND;
	submission->fd = fds[0];
	submission->addr = (u64) (uintptr_t) ring_test_message;
	submission->len = sizeof (ring_test_message);
	submission->msg_flags = MSG_NOSIGNAL;
	submission->user_data = RING_TEST_DATA + 1;

	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_int_eq (completion.result, -EPIPE, NULL);

	(void) close (fds[0]);

	event_ring_delete (&ring);

}

static void *test_ring_other_thread_submit (void *ring_ptr) {

	EventRing *ring = (EventRing *) ring_ptr;

	ring_test_nop (ring, RING_TEST_DATA);

	return (void *) (uintptr_t) event_ring_submit (ring);

}

// a ring is of its thread: the kernel refuses the submissions of another one
static void test_ring_other_thread (void) {

	EventRing ring;
	ring_test_create (&ring, 0, 0);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, test_ring_other_thread_submit, &ring), 0, NULL);

	void *result = NULL;
	test_check_int_eq (pthread_join (thread, &result), 0, NULL);
	test_check_true ((uintptr_t) result == 1);

	// the thread of the ring gives it, with its own enter
	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_true (completion.data == RING_TEST_DATA);

	event_ring_delete (&ring);

}

// a ring that is not enabled keeps the submissions that are written, and takes them when it is enabled
static void test_ring_enable (void) {

	EventRing ring;
	test_check_unsigned_eq (event_ring_create (&ring, 0, 0), 0, NULL);

	ring_test_nop (&ring, RING_TEST_DATA);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 1, NULL);

	// the kernel refuses a ring that is disabled
	test_check_unsigned_eq (event_ring_submit (&ring), 1, NULL);
	test_check_unsigned_eq (event_ring_submissions_pending (&ring), 1, NULL);

	test_check_unsigned_eq (event_ring_enable (&ring), 0, NULL);

	RingTestCompletion completion;
	test_check_unsigned_eq (ring_test_collect (&ring, &completion, 1), 1, NULL);
	test_check_true (completion.data == RING_TEST_DATA);

	// it is enabled once
	errno = 0;
	test_check_unsigned_eq (event_ring_enable (&ring), 1, NULL);
	test_check_int_eq (errno, EINVAL, NULL);

	event_ring_delete (&ring);

	// neither a ring that was deleted, nor no ring
	test_check_unsigned_eq (event_ring_enable (&ring), 1, NULL);
	test_check_unsigned_eq (event_ring_enable (NULL), 1, NULL);

}

static void *test_ring_enabled_by_thread_run (void *ring_ptr) {

	EventRing *ring = (EventRing *) ring_ptr;

	uintptr_t errors = event_ring_enable (ring);

	ring_test_nop (ring, RING_TEST_DATA + 1);

	RingTestCompletion completions[RING_TEST_SMALL_COMPLETIONS];
	if (ring_test_collect (ring, completions, 2) != 2) errors += 1;
	if (completions[0].data != RING_TEST_DATA) errors += 1;
	if (completions[1].data != (RING_TEST_DATA + 1)) errors += 1;

	return (void *) errors;

}

// a ring is of the thread that enables it, whoever created it: the loops of a cerver are created by the
// thread that starts it, and each one runs in its own
static void test_ring_enabled_by_thread (void) {

	EventRing ring;
	test_check_unsigned_eq (event_ring_create (&ring, 0, 0), 0, NULL);

	// written by the thread that created it, taken by the one that enables it
	ring_test_nop (&ring, RING_TEST_DATA);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, test_ring_enabled_by_thread_run, &ring), 0, NULL);

	void *result = NULL;
	test_check_int_eq (pthread_join (thread, &result), 0, NULL);
	test_check_true ((uintptr_t) result == 0);

	// the thread that created it is not its thread
	ring_test_nop (&ring, RING_TEST_DATA);
	test_check_unsigned_eq (event_ring_submit (&ring), 1, NULL);

	event_ring_delete (&ring);

}

void events_tests_ring (void) {

	(void) printf ("\nTesting RING...\n");

	if (ring_test_available ()) {
		test_run (test_ring_create);
		test_run (test_ring_create_sizes);
		test_run (test_ring_null);
		test_run (test_ring_create_many);
		test_run (test_ring_nop);
		test_run (test_ring_submit);
		test_run (test_ring_full);
		test_run (test_ring_overflow_submit);
		test_run (test_ring_wait_timeout);
		test_run (test_ring_receive);
		test_run (test_ring_buffers_exhausted);
		test_run (test_ring_buffers_invalid);
		test_run (test_ring_send);
		test_run (test_ring_other_thread);
		test_run (test_ring_enable);
		test_run (test_ring_enabled_by_thread);
	}

	(void) printf ("Done!\n");

}
