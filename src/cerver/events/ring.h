/**
 * @file events/ring.h
 * @internal
 * @brief The io_uring of an event loop: its system calls, the memory of its queues and its provided buffers,
 * written over the interface of the kernel without liburing; never installed.
 *
 * A ring is created disabled by any thread, and it belongs to the thread that enables it
 * (event_ring_enable ()): only that thread submits to it and waits on it (IORING_SETUP_SINGLE_ISSUER), and the
 * kernel runs the work of its completions only when that thread waits (IORING_SETUP_DEFER_TASKRUN). The methods
 * that the tests use are exported (#CERVER_PRIVATE).
 *
 * @since 3.0b-11
 */

#ifndef _CERVER_EVENTS_RING_H_
#define _CERVER_EVENTS_RING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <linux/io_uring.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifndef IORING_SETUP_DEFER_TASKRUN
#error "cerver needs the io_uring header of Linux 6.1 or newer (linux/io_uring.h)"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @defgroup events_ring Ring
 * @brief An io_uring: the submissions that its thread writes, the completions that the kernel writes, and the
 * buffers that the kernel chooses for what it reads.
 * @{
 */

/** @internal @brief The submissions of the ring of a loop: the operations that can wait for one system call. */
#define EVENT_RING_SUBMISSIONS				(1u << 12)

/** @internal @brief The completions of the ring of a loop; the kernel keeps the ones that do not fit and gives them later. */
#define EVENT_RING_COMPLETIONS				(1u << 14)

/**
 * @internal
 * @brief How the ring of a loop is set up: one thread submits, the work of its completions runs when that thread
 * waits, a submission that fails does not stop the ones after it, the completions have their own size, and the
 * ring starts disabled: the thread that enables it is the one that submits.
 */
#define EVENT_RING_SETUP_FLAGS				(														\
	IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN											\
	| IORING_SETUP_SUBMIT_ALL | IORING_SETUP_CQSIZE | IORING_SETUP_CLAMP							\
	| IORING_SETUP_R_DISABLED																		\
)

/** @internal @brief The buffers that the kernel chooses from for the reads of a loop. */
#define EVENT_RING_BUFFERS					(1u << 8)

/** @internal @brief The bytes of each buffer of the reads of a loop. */
#define EVENT_RING_BUFFER_SIZE				(1u << 14)

/** @internal @brief The most buffers that a group can have, the limit of the kernel. */
#define EVENT_RING_BUFFERS_MAX				(1u << 15)

/** @internal @brief The group of the buffers of the reads of a loop. */
#define EVENT_RING_BUFFERS_GROUP			0

/** @internal @brief A wait without a timeout. */
#define EVENT_RING_WAIT_FOREVER				(-1)

/** @internal @brief The operations that a ring can be asked about, the size of the opcode of a submission. */
#define EVENT_RING_OPERATIONS				(1u << 8)

/** @internal @brief The bits of each word of the operations that a ring supports. */
#define EVENT_RING_OPERATIONS_WORD_BITS		(1u << 6)

/** @internal @brief The words of the operations that a ring supports. */
#define EVENT_RING_OPERATIONS_WORDS			(EVENT_RING_OPERATIONS / EVENT_RING_OPERATIONS_WORD_BITS)

/**
 * @internal
 * @brief An io_uring with the memory of its two queues.
 *
 * The submissions are written in order in their own memory and published with the tail of their queue; the
 * completions are read between the head and the tail of theirs.
 */
typedef struct EventRing {

	/** @internal @brief The file descriptor of the ring, -1 when it was not created. */
	int fd;
	/** @internal @brief What the system call of the ring takes as its ring: its registered index, or its fd. */
	int enter_fd;
	/** @internal @brief The flags of every enter: IORING_ENTER_REGISTERED_RING when the ring was registered. */
	unsigned int enter_flags;
	/** @internal @brief The IORING_FEAT_* flags of the kernel. */
	unsigned int features;
	/** @internal @brief The ring was enabled: the kernel takes its submissions. */
	bool enabled;
	/** @internal @brief The thread that enabled the ring: its registration for the system calls is of that thread. */
	pthread_t thread;

	/** @internal @brief The head of the submissions, written by the kernel when it takes them. */
	unsigned int *submissions_head;
	/** @internal @brief The tail of the submissions, written by the thread of the ring to publish them. */
	unsigned int *submissions_tail;
	/** @internal @brief The IORING_SQ_* flags, written by the kernel. */
	unsigned int *submissions_flags;
	/** @internal @brief The mask of the index of a submission. */
	unsigned int submissions_mask;
	/** @internal @brief The submissions that fit. */
	unsigned int submissions_entries;
	/** @internal @brief The memory of the submissions. */
	struct io_uring_sqe *submissions;
	/** @internal @brief The submissions that were written, the ones after the tail were not published yet. */
	unsigned int submissions_written;

	/** @internal @brief The head of the completions, written by the thread of the ring when it has read them. */
	unsigned int *completions_head;
	/** @internal @brief The tail of the completions, written by the kernel. */
	unsigned int *completions_tail;
	/** @internal @brief The completions that the kernel could not keep, only without IORING_FEAT_NODROP. */
	unsigned int *completions_overflow;
	/** @internal @brief The mask of the index of a completion. */
	unsigned int completions_mask;
	/** @internal @brief The completions that fit. */
	unsigned int completions_entries;
	/** @internal @brief The memory of the completions. */
	struct io_uring_cqe *completions;

	/** @internal @brief The mapping of the two queues. */
	void *queues;
	/** @internal @brief The bytes of the mapping of the two queues. */
	size_t queues_size;
	/** @internal @brief The bytes of the mapping of the submissions. */
	size_t submissions_size;

	/** @internal @brief The operations that the kernel supports, a bit for each opcode. */
	u64 operations[EVENT_RING_OPERATIONS_WORDS];

	/** @internal @brief The times that the ring entered the kernel, for the tests and the stats. */
	u64 n_enters;

} EventRing;

/**
 * @internal
 * @brief A group of buffers that the kernel chooses from for the reads of a ring (a provided buffer ring).
 *
 * A read that selects a buffer takes the first one of the group and its completion tells which; the thread of the
 * ring gives it back when it has used its bytes.
 */
typedef struct EventRingBuffers {

	/** @internal @brief The buffers that the group has for the kernel, shared with it. */
	struct io_uring_buf_ring *shared;
	/** @internal @brief The memory of the buffers, one after the other. */
	char *memory;
	/** @internal @brief The buffers of the group, a power of 2. */
	unsigned int entries;
	/** @internal @brief The mask of the index of a buffer in the shared memory. */
	unsigned int mask;
	/** @internal @brief The bytes of each buffer. */
	unsigned int size;
	/** @internal @brief The group, the id that a read selects. */
	u16 group;
	/** @internal @brief The buffers that were given, the tail that the kernel sees. */
	u16 tail;
	/** @internal @brief The bytes of the mapping that is shared with the kernel. */
	size_t shared_size;
	/** @internal @brief The bytes of the mapping of the buffers. */
	size_t memory_size;
	/** @internal @brief The group was registered in its ring. */
	bool registered;

} EventRingBuffers;

/**
 * @internal
 * @brief Creates an io_uring with the flags of #EVENT_RING_SETUP_FLAGS and asks the kernel for the operations
 * that it supports. The ring is disabled: its submissions can be written, and they are taken once a thread
 * enables it (event_ring_enable ()).
 *
 * @param[out] ring the ring
 * @param[in] submissions the submissions of the ring, a power of 2, 0 for #EVENT_RING_SUBMISSIONS
 * @param[in] completions the completions of the ring, a power of 2, 0 for #EVENT_RING_COMPLETIONS
 * @return 0 on success, 1 on error: errno tells why (EPERM when the system call is not allowed, ENOSYS without
 * io_uring, EINVAL in a kernel older than the 6.1) and the ring is left without resources
 */
CERVER_PRIVATE unsigned int event_ring_create (
	EventRing *ring, const unsigned int submissions, const unsigned int completions
);

/**
 * @internal
 * @brief Enables a ring for the thread that calls and registers it for its system calls: from now on the kernel
 * takes the submissions of that thread, and refuses the ones of any other.
 *
 * @param[in,out] ring a ring that was created and not enabled
 * @return 0 on success, 1 on error (errno tells; EINVAL for a NULL ring, one that was not created or one that
 * was already enabled)
 * @threadsafety The ring belongs to the thread that calls: only it submits to the ring and waits on it.
 */
CERVER_PRIVATE unsigned int event_ring_enable (EventRing *ring);

/**
 * @internal
 * @brief Closes a ring and releases its memory; nothing is done with one that was not created.
 *
 * @param[in,out] ring the ring, it can be NULL
 */
CERVER_PRIVATE void event_ring_delete (EventRing *ring);

/**
 * @internal
 * @brief Publishes the submissions that were written and gives them to the kernel without waiting.
 *
 * @param[in,out] ring the ring
 * @return 0 on success, also when the kernel asks to read the completions first; 1 on error (errno tells)
 */
CERVER_PRIVATE unsigned int event_ring_submit (EventRing *ring);

/**
 * @internal
 * @brief Gives the submissions that were written to the kernel and waits until a completion is ready or the
 * timeout ends, in one system call; the work of the completions runs here.
 *
 * @param[in,out] ring the ring
 * @param[in] timeout the milliseconds to wait, 0 to return at once, #EVENT_RING_WAIT_FOREVER
 * @return 0 on success, also after a timeout or a signal; 1 on error (errno tells)
 */
CERVER_PRIVATE unsigned int event_ring_wait (EventRing *ring, const int timeout);

/**
 * @internal
 * @brief Creates a group of buffers and registers it in a ring, with all its buffers given to the kernel.
 *
 * @param[in,out] ring the ring
 * @param[out] buffers the group
 * @param[in] group the id of the group
 * @param[in] entries the buffers, a power of 2 up to #EVENT_RING_BUFFERS_MAX
 * @param[in] size the bytes of each buffer, more than 0
 * @return 0 on success, 1 on error (errno tells), with the group left without resources
 */
CERVER_PRIVATE unsigned int event_ring_buffers_create (
	EventRing *ring, EventRingBuffers *buffers,
	const u16 group, const unsigned int entries, const unsigned int size
);

/**
 * @internal
 * @brief Takes a group of buffers out of its ring and releases its memory.
 *
 * @param[in,out] ring the ring, the one where the group was registered
 * @param[in,out] buffers the group, it can be NULL
 */
CERVER_PRIVATE void event_ring_buffers_delete (EventRing *ring, EventRingBuffers *buffers);

/**
 * @internal
 * @brief The submissions that the kernel has not taken: the ones that were written and wait for the next system
 * call.
 *
 * @param[in] ring the ring
 * @return the submissions
 */
static inline unsigned int event_ring_submissions_pending (const EventRing *ring) {

	return ring->submissions_written - __atomic_load_n (ring->submissions_head, __ATOMIC_ACQUIRE);

}

/**
 * @internal
 * @brief The next submission of a ring, zeroed, for its caller to fill; when no one is free the ones that were
 * written are given to the kernel first, so a ring that is full never loses an operation.
 *
 * @param[in,out] ring the ring
 * @return the submission, NULL when the kernel did not take the ones that were written (errno tells)
 */
static inline struct io_uring_sqe *event_ring_submission (EventRing *ring) {

	if (
		(event_ring_submissions_pending (ring) == ring->submissions_entries)
		&& (event_ring_submit (ring) || (event_ring_submissions_pending (ring) == ring->submissions_entries))
	) {
		return NULL;
	}

	struct io_uring_sqe *submission = &ring->submissions[ring->submissions_written & ring->submissions_mask];
	ring->submissions_written += 1;

	(void) memset (submission, 0, sizeof (struct io_uring_sqe));

	return submission;

}

/**
 * @internal
 * @brief The first completion that was not read.
 *
 * @param[in] ring the ring
 * @return the position of the completion, for event_ring_completion ()
 */
static inline unsigned int event_ring_completions_begin (const EventRing *ring) {

	return *ring->completions_head;

}

/**
 * @internal
 * @brief The end of the completions that the kernel has written.
 *
 * @param[in] ring the ring
 * @return the position after the last completion
 */
static inline unsigned int event_ring_completions_end (const EventRing *ring) {

	return __atomic_load_n (ring->completions_tail, __ATOMIC_ACQUIRE);

}

/**
 * @internal
 * @brief The completion of a position.
 *
 * @param[in] ring the ring
 * @param[in] position a position from event_ring_completions_begin () up to, not including,
 * event_ring_completions_end ()
 * @return the completion, valid until its position is given back with event_ring_completions_seen ()
 */
static inline const struct io_uring_cqe *event_ring_completion (
	const EventRing *ring, const unsigned int position
) {

	return &ring->completions[position & ring->completions_mask];

}

/**
 * @internal
 * @brief Gives back to the kernel the completions that were read.
 *
 * @param[in,out] ring the ring
 * @param[in] position the position after the last completion that was read
 */
static inline void event_ring_completions_seen (EventRing *ring, const unsigned int position) {

	__atomic_store_n (ring->completions_head, position, __ATOMIC_RELEASE);

}

/**
 * @internal
 * @brief Whether the kernel has completions that did not fit in the ring: a wait gives them.
 *
 * @param[in] ring the ring
 * @return true when completions wait in the kernel
 */
static inline bool event_ring_completions_overflowed (const EventRing *ring) {

	return (__atomic_load_n (ring->submissions_flags, __ATOMIC_RELAXED) & IORING_SQ_CQ_OVERFLOW) != 0;

}

/**
 * @internal
 * @brief Whether the kernel of a ring supports an operation.
 *
 * @param[in] ring the ring
 * @param[in] operation the opcode, an IORING_OP_* value
 * @return true when a submission can ask for it
 */
static inline bool event_ring_supports (const EventRing *ring, const unsigned int operation) {

	return (operation < EVENT_RING_OPERATIONS) && (
		(ring->operations[operation / EVENT_RING_OPERATIONS_WORD_BITS]
			>> (operation % EVENT_RING_OPERATIONS_WORD_BITS)) & 1u
	);

}

/**
 * @internal
 * @brief The memory of a buffer of a group.
 *
 * @param[in] buffers the group
 * @param[in] id the buffer, the one that a completion told
 * @return its first byte
 */
static inline char *event_ring_buffer (const EventRingBuffers *buffers, const unsigned int id) {

	return buffers->memory + ((size_t) id * buffers->size);

}

/**
 * @internal
 * @brief The buffer that a completion used.
 *
 * @param[in] completion a completion with IORING_CQE_F_BUFFER
 * @return the id of the buffer
 */
static inline unsigned int event_ring_completion_buffer (const struct io_uring_cqe *completion) {

	return completion->flags >> IORING_CQE_BUFFER_SHIFT;

}

/**
 * @internal
 * @brief Gives a buffer back to its group: the kernel can choose it again.
 *
 * @param[in,out] buffers the group
 * @param[in] id the buffer, one that a completion gave
 */
static inline void event_ring_buffer_give (EventRingBuffers *buffers, const unsigned int id) {

	// the tail of the group is where a buffer has its reserved field: only the other fields are written
	struct io_uring_buf *buffer = &buffers->shared->bufs[buffers->tail & buffers->mask];
	buffer->addr = (u64) (uintptr_t) event_ring_buffer (buffers, id);
	buffer->len = buffers->size;
	buffer->bid = (u16) id;

	buffers->tail = (u16) (buffers->tail + 1);
	__atomic_store_n (&buffers->shared->tail, buffers->tail, __ATOMIC_RELEASE);

}

/** @} */

#ifdef __cplusplus
}
#endif

#endif
