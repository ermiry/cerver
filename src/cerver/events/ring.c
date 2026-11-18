#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/io_uring.h>
#include <linux/time_types.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/ring.h"

// the three system calls of io_uring are made here, with syscall (): cerver does not use liburing,
// and a test can define syscall () in its binary to make one of them fail

// the bytes of the signal mask that a wait with a timeout tells, the kernel ignores it without a mask
#define RING_SIGNAL_MASK_SIZE				(_NSIG / 8)

// a wait that has to find one completion, and one that only looks
#define RING_WAIT_ONE						1u
#define RING_WAIT_NONE						0u

// the registration of a ring in its thread asks the kernel for any free index
#define RING_REGISTER_ANY_INDEX				UINT32_MAX

// one ring is registered, and one group of buffers at a time
#define RING_REGISTER_ONE					1u

// the words of 64 bits that hold the answer of the kernel about its operations
#define RING_PROBE_WORDS					(															\
	(sizeof (struct io_uring_probe) + (EVENT_RING_OPERATIONS * sizeof (struct io_uring_probe_op)))		\
	/ sizeof (u64)																						\
)

// the answer of the kernel about its operations: its head and, behind it in the same memory, an entry for each
// operation
typedef union RingProbe {

	struct io_uring_probe probe;
	u64 words[RING_PROBE_WORDS];

} RingProbe;

static const long ring_milliseconds_per_second = 1000L;
static const long ring_nanoseconds_per_millisecond = 1000000L;

#pragma region system

static int ring_system_setup (const unsigned int entries, struct io_uring_params *params) {

	return (int) syscall (__NR_io_uring_setup, entries, params);

}

static int ring_system_enter (
	const int fd, const unsigned int to_submit, const unsigned int min_complete,
	const unsigned int flags, const void *argument, const size_t argument_size
) {

	return (int) syscall (__NR_io_uring_enter, fd, to_submit, min_complete, flags, argument, argument_size);

}

static int ring_system_register (
	const int fd, const unsigned int opcode, void *argument, const unsigned int count
) {

	return (int) syscall (__NR_io_uring_register, fd, opcode, argument, count);

}

#pragma endregion

#pragma region lifecycle

static void ring_clear (EventRing *ring) {

	(void) memset (ring, 0, sizeof (EventRing));

	ring->fd = -1;
	ring->enter_fd = -1;

}

static void *ring_offset (void *base, const size_t offset) {

	return (char *) base + offset;

}

// the queues are read and written through the offsets that the kernel gave
static void ring_pointers (EventRing *ring, const struct io_uring_params *params) {

	ring->submissions_head = (unsigned int *) ring_offset (ring->queues, params->sq_off.head);
	ring->submissions_tail = (unsigned int *) ring_offset (ring->queues, params->sq_off.tail);
	ring->submissions_flags = (unsigned int *) ring_offset (ring->queues, params->sq_off.flags);
	ring->submissions_mask = *(unsigned int *) ring_offset (ring->queues, params->sq_off.ring_mask);
	ring->submissions_entries = *(unsigned int *) ring_offset (ring->queues, params->sq_off.ring_entries);

	ring->completions_head = (unsigned int *) ring_offset (ring->queues, params->cq_off.head);
	ring->completions_tail = (unsigned int *) ring_offset (ring->queues, params->cq_off.tail);
	ring->completions_overflow = (unsigned int *) ring_offset (ring->queues, params->cq_off.overflow);
	ring->completions_mask = *(unsigned int *) ring_offset (ring->queues, params->cq_off.ring_mask);
	ring->completions_entries = *(unsigned int *) ring_offset (ring->queues, params->cq_off.ring_entries);
	ring->completions = (struct io_uring_cqe *) ring_offset (ring->queues, params->cq_off.cqes);

	// the submissions are always written in order, so each position of the queue names its own submission
	unsigned int *array = (unsigned int *) ring_offset (ring->queues, params->sq_off.array);
	for (unsigned int i = 0; i < ring->submissions_entries; i++) {
		array[i] = i;
	}

	ring->submissions_written = *ring->submissions_tail;

}

static unsigned int ring_map (EventRing *ring, const struct io_uring_params *params) {

	unsigned int retval = 1;

	const size_t submissions_end = params->sq_off.array + (params->sq_entries * sizeof (unsigned int));
	const size_t completions_end = params->cq_off.cqes + (params->cq_entries * sizeof (struct io_uring_cqe));
	const size_t queues_size = (submissions_end > completions_end) ? submissions_end : completions_end;

	void *queues = mmap (
		NULL, queues_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING
	);

	if (queues != MAP_FAILED) {
		ring->queues = queues;
		ring->queues_size = queues_size;

		const size_t submissions_size = params->sq_entries * sizeof (struct io_uring_sqe);
		void *submissions = mmap (
			NULL, submissions_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQES
		);

		if (submissions != MAP_FAILED) {
			ring->submissions = (struct io_uring_sqe *) submissions;
			ring->submissions_size = submissions_size;

			ring_pointers (ring, params);

			retval = 0;
		}
	}

	return retval;

}

// asks the kernel for the operations that it has
static unsigned int ring_probe (EventRing *ring) {

	unsigned int retval = 1;

	RingProbe memory;
	(void) memset (&memory, 0, sizeof (RingProbe));

	struct io_uring_probe *probe = &memory.probe;

	if (!ring_system_register (ring->fd, IORING_REGISTER_PROBE, probe, EVENT_RING_OPERATIONS)) {
		for (unsigned int i = 0; i < probe->ops_len; i++) {
			if (probe->ops[i].flags & IO_URING_OP_SUPPORTED) {
				ring->operations[probe->ops[i].op / EVENT_RING_OPERATIONS_WORD_BITS] |=
					((u64) 1 << (probe->ops[i].op % EVENT_RING_OPERATIONS_WORD_BITS));
			}
		}

		retval = 0;
	}

	return retval;

}

// the system calls of a ring that was registered in its thread do not look its file up each time;
// a thread that has no place for another ring uses its fd
static void ring_register (EventRing *ring) {

	struct io_uring_rsrc_update update;
	(void) memset (&update, 0, sizeof (struct io_uring_rsrc_update));

	update.offset = RING_REGISTER_ANY_INDEX;
	update.data = (u64) ring->fd;

	ring->thread = pthread_self ();
	ring->enter_fd = ring->fd;

	if (
		ring_system_register (ring->fd, IORING_REGISTER_RING_FDS, &update, RING_REGISTER_ONE)
		== (int) RING_REGISTER_ONE
	) {
		ring->enter_fd = (int) update.offset;
		ring->enter_flags = IORING_ENTER_REGISTERED_RING;
	}

}

// the registration keeps the ring alive after its fd is closed, and it is of the thread that made it:
// another thread has other rings in the same indexes
static void ring_unregister (EventRing *ring) {

	if ((ring->enter_flags & IORING_ENTER_REGISTERED_RING) && pthread_equal (ring->thread, pthread_self ())) {
		struct io_uring_rsrc_update update;
		(void) memset (&update, 0, sizeof (struct io_uring_rsrc_update));

		update.offset = (u32) ring->enter_fd;

		(void) ring_system_register (ring->fd, IORING_UNREGISTER_RING_FDS, &update, RING_REGISTER_ONE);
	}

}

static unsigned int ring_start (EventRing *ring, const struct io_uring_params *params) {

	unsigned int retval = 1;

	ring->features = params->features;

	// both queues are one mapping since Linux 5.4
	if (!(params->features & IORING_FEAT_SINGLE_MMAP)) {
		errno = ENOSYS;
	}

	else if (!ring_map (ring, params) && !ring_probe (ring)) {
		ring->enter_fd = ring->fd;

		retval = 0;
	}

	return retval;

}

// the thread that enables the ring is the one that the kernel takes its submissions from
unsigned int event_ring_enable (EventRing *ring) {

	unsigned int retval = 1;

	if (ring && (ring->fd >= 0) && !ring->enabled) {
		if (!ring_system_register (ring->fd, IORING_REGISTER_ENABLE_RINGS, NULL, 0)) {
			ring->enabled = true;

			ring_register (ring);

			retval = 0;
		}
	}

	else {
		errno = EINVAL;
	}

	return retval;

}

unsigned int event_ring_create (
	EventRing *ring, const unsigned int submissions, const unsigned int completions
) {

	unsigned int retval = 1;

	if (ring) {
		ring_clear (ring);

		struct io_uring_params params;
		(void) memset (&params, 0, sizeof (struct io_uring_params));

		params.flags = EVENT_RING_SETUP_FLAGS;
		params.cq_entries = completions ? completions : EVENT_RING_COMPLETIONS;

		ring->fd = ring_system_setup (submissions ? submissions : EVENT_RING_SUBMISSIONS, &params);
		if (ring->fd >= 0) {
			retval = ring_start (ring, &params);
			if (retval) {
				const int error = errno;
				event_ring_delete (ring);
				errno = error;
			}
		}
	}

	else {
		errno = EINVAL;
	}

	return retval;

}

void event_ring_delete (EventRing *ring) {

	if (ring) {
		if (ring->fd >= 0) {
			ring_unregister (ring);
		}

		if (ring->submissions) (void) munmap (ring->submissions, ring->submissions_size);
		if (ring->queues) (void) munmap (ring->queues, ring->queues_size);

		if (ring->fd >= 0) (void) close (ring->fd);

		ring_clear (ring);
	}

}

#pragma endregion

#pragma region enter

// the submissions that were written are the kernel's from their tail on
static unsigned int ring_publish (EventRing *ring) {

	__atomic_store_n (ring->submissions_tail, ring->submissions_written, __ATOMIC_RELEASE);

	return event_ring_submissions_pending (ring);

}

// the errors of an enter that only ask to come back: a signal, the end of the timeout,
// and a kernel that wants its completions to be read before it takes more submissions
static bool ring_enter_is_retried (const int error) {

	return (error == EINTR) || (error == ETIME) || (error == EAGAIN) || (error == EBUSY);

}

static unsigned int ring_enter (
	EventRing *ring, const unsigned int to_submit, const unsigned int min_complete,
	const unsigned int flags, const void *argument, const size_t argument_size
) {

	ring->n_enters += 1;

	return (
		(ring_system_enter (
			ring->enter_fd, to_submit, min_complete, ring->enter_flags | flags, argument, argument_size
		) < 0) && !ring_enter_is_retried (errno)
	) ? 1 : 0;

}

unsigned int event_ring_submit (EventRing *ring) {

	unsigned int retval = 0;

	const unsigned int pending = ring_publish (ring);
	if (pending) {
		// completions that did not fit are given before the kernel takes more submissions
		retval = ring_enter (
			ring, pending, RING_WAIT_NONE,
			event_ring_completions_overflowed (ring) ? IORING_ENTER_GETEVENTS : 0, NULL, 0
		);
	}

	return retval;

}

static void ring_wait_argument (
	struct io_uring_getevents_arg *argument, struct __kernel_timespec *time, const int timeout
) {

	time->tv_sec = timeout / ring_milliseconds_per_second;
	time->tv_nsec = (timeout % ring_milliseconds_per_second) * ring_nanoseconds_per_millisecond;

	(void) memset (argument, 0, sizeof (struct io_uring_getevents_arg));
	argument->sigmask_sz = RING_SIGNAL_MASK_SIZE;
	argument->ts = (u64) (uintptr_t) time;

}

unsigned int event_ring_wait (EventRing *ring, const int timeout) {

	unsigned int retval = 0;

	const unsigned int pending = ring_publish (ring);

	if (timeout > 0) {
		struct __kernel_timespec time;
		struct io_uring_getevents_arg argument;
		ring_wait_argument (&argument, &time, timeout);

		retval = ring_enter (
			ring, pending, RING_WAIT_ONE, IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG,
			&argument, sizeof (struct io_uring_getevents_arg)
		);
	}

	else {
		retval = ring_enter (
			ring, pending, timeout ? RING_WAIT_ONE : RING_WAIT_NONE, IORING_ENTER_GETEVENTS, NULL, 0
		);
	}

	return retval;

}

#pragma endregion

#pragma region buffers

static bool ring_buffers_are_valid (const unsigned int entries, const unsigned int size) {

	return entries && (entries <= EVENT_RING_BUFFERS_MAX) && !(entries & (entries - 1)) && size;

}

static void *ring_buffers_memory (const size_t size) {

	void *memory = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return (memory != MAP_FAILED) ? memory : NULL;

}

static unsigned int ring_buffers_map (
	EventRingBuffers *buffers, const unsigned int entries, const unsigned int size
) {

	buffers->shared_size = (size_t) entries * sizeof (struct io_uring_buf);
	buffers->shared = (struct io_uring_buf_ring *) ring_buffers_memory (buffers->shared_size);

	buffers->memory_size = (size_t) entries * size;
	buffers->memory = (char *) ring_buffers_memory (buffers->memory_size);

	buffers->entries = entries;
	buffers->mask = entries - 1;
	buffers->size = size;

	return (buffers->shared && buffers->memory) ? 0 : 1;

}

static unsigned int ring_buffers_register (
	EventRing *ring, EventRingBuffers *buffers, const u16 group
) {

	unsigned int retval = 1;

	struct io_uring_buf_reg registration;
	(void) memset (&registration, 0, sizeof (struct io_uring_buf_reg));

	registration.ring_addr = (u64) (uintptr_t) buffers->shared;
	registration.ring_entries = buffers->entries;
	registration.bgid = group;

	if (!ring_system_register (ring->fd, IORING_REGISTER_PBUF_RING, &registration, RING_REGISTER_ONE)) {
		buffers->group = group;
		buffers->registered = true;

		retval = 0;
	}

	return retval;

}

unsigned int event_ring_buffers_create (
	EventRing *ring, EventRingBuffers *buffers,
	const u16 group, const unsigned int entries, const unsigned int size
) {

	unsigned int retval = 1;

	if (buffers) {
		(void) memset (buffers, 0, sizeof (EventRingBuffers));
	}

	if (ring && buffers && ring_buffers_are_valid (entries, size)) {
		if (!ring_buffers_map (buffers, entries, size) && !ring_buffers_register (ring, buffers, group)) {
			for (unsigned int id = 0; id < entries; id++) {
				event_ring_buffer_give (buffers, id);
			}

			retval = 0;
		}

		else {
			const int error = errno;
			event_ring_buffers_delete (ring, buffers);
			errno = error;
		}
	}

	else {
		errno = EINVAL;
	}

	return retval;

}

void event_ring_buffers_delete (EventRing *ring, EventRingBuffers *buffers) {

	if (buffers) {
		if (ring && buffers->registered) {
			struct io_uring_buf_reg registration;
			(void) memset (&registration, 0, sizeof (struct io_uring_buf_reg));

			registration.bgid = buffers->group;

			(void) ring_system_register (
				ring->fd, IORING_UNREGISTER_PBUF_RING, &registration, RING_REGISTER_ONE
			);
		}

		if (buffers->memory) (void) munmap (buffers->memory, buffers->memory_size);
		if (buffers->shared) (void) munmap (buffers->shared, buffers->shared_size);

		(void) memset (buffers, 0, sizeof (EventRingBuffers));
	}

}

#pragma endregion
