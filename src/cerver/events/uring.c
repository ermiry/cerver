#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/io_uring.h>
#include <poll.h>
#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/backend.h"
#include "cerver/events/ring.h"

// the backend of io_uring: each operation is a submission of the ring of the loop, and the kernel makes it;
// the submissions that the handlers of a wait write go to the kernel with the next wait, in one system call

// the data of a submission is its handle with the operation in the bits that its alignment leaves free
#define URING_TAG_ACCEPT					1u
#define URING_TAG_RECEIVE					2u
#define URING_TAG_SEND						3u
#define URING_TAG_WRITABLE					4u
#define URING_TAG_STOP						5u
#define URING_TAG_WAKE						6u
#define URING_TAG_MASK						7u

typedef struct EventBackendUring {

	EventBackend backend;

	EventRing ring;

	// the buffers of the reads: the kernel chooses one for each read
	EventRingBuffers buffers;

	// the read of the eventfd of the wake ups puts its counter here
	u64 wake_value;
	bool wake_armed;

} EventBackendUring;

// the operations that the kernel must have
static const unsigned int uring_operations[] = {
	IORING_OP_ACCEPT, IORING_OP_RECV, IORING_OP_SEND, IORING_OP_POLL_ADD, IORING_OP_ASYNC_CANCEL, IORING_OP_READ
};

#pragma region submissions

static struct io_uring_sqe *uring_submission (
	EventBackendUring *uring, EventHandle *handle, const unsigned int tag, const unsigned int opcode
) {

	struct io_uring_sqe *submission = event_ring_submission (&uring->ring);
	if (submission) {
		submission->opcode = (u8) opcode;
		submission->fd = handle ? handle->fd : uring->backend.wake_fd;
		submission->user_data = (u64) (uintptr_t) handle | tag;
	}

	return submission;

}

static unsigned int uring_accept (EventBackend *backend, EventHandle *handle) {

	unsigned int retval = 1;

	struct io_uring_sqe *submission = uring_submission (
		(EventBackendUring *) backend, handle, URING_TAG_ACCEPT, IORING_OP_ACCEPT
	);

	if (submission) {
		// one submission gives every connection of the socket
		submission->ioprio = IORING_ACCEPT_MULTISHOT;
		submission->accept_flags = SOCK_NONBLOCK | SOCK_CLOEXEC;

		handle->operations |= EVENT_OPERATION_ACCEPT;

		retval = 0;
	}

	return retval;

}

static unsigned int uring_receive (EventBackend *backend, EventHandle *handle) {

	unsigned int retval = 1;

	EventBackendUring *uring = (EventBackendUring *) backend;

	struct io_uring_sqe *submission = uring_submission (uring, handle, URING_TAG_RECEIVE, IORING_OP_RECV);
	if (submission) {
		// the kernel chooses the buffer when the bytes are there: a socket that waits has no memory
		submission->flags = IOSQE_BUFFER_SELECT;
		submission->buf_group = uring->buffers.group;

		handle->operations |= EVENT_OPERATION_RECEIVE;

		retval = 0;
	}

	return retval;

}

static unsigned int uring_send (
	EventBackend *backend, EventHandle *handle, const char *data, const size_t length
) {

	unsigned int retval = 1;

	struct io_uring_sqe *submission = uring_submission (
		(EventBackendUring *) backend, handle, URING_TAG_SEND, IORING_OP_SEND
	);

	if (submission) {
		submission->addr = (u64) (uintptr_t) data;
		submission->len = (u32) length;
		submission->msg_flags = MSG_NOSIGNAL;

		handle->send_data = data;
		handle->send_length = length;
		handle->operations |= EVENT_OPERATION_SEND;

		retval = 0;
	}

	return retval;

}

static unsigned int uring_writable (EventBackend *backend, EventHandle *handle) {

	unsigned int retval = 1;

	struct io_uring_sqe *submission = uring_submission (
		(EventBackendUring *) backend, handle, URING_TAG_WRITABLE, IORING_OP_POLL_ADD
	);

	if (submission) {
		submission->poll32_events = POLLOUT;

		handle->operations |= EVENT_OPERATION_WRITABLE;

		retval = 0;
	}

	return retval;

}

// the kernel is asked to end every operation of the socket; each one still completes, and the handle
// has to be there for them
static unsigned int uring_stop (EventBackend *backend, EventHandle *handle) {

	unsigned int retval = 0;

	if (handle->operations) {
		if (!(handle->operations & EVENT_OPERATION_STOP)) {
			struct io_uring_sqe *submission = uring_submission (
				(EventBackendUring *) backend, handle, URING_TAG_STOP, IORING_OP_ASYNC_CANCEL
			);

			if (submission) {
				submission->cancel_flags = IORING_ASYNC_CANCEL_FD | IORING_ASYNC_CANCEL_ALL;

				handle->operations |= EVENT_OPERATION_STOP;
			}
		}

		handle->stopped = true;

		retval = 1;
	}

	return retval;

}

// the wake ups are a read of their eventfd, asked again after each one
static void uring_wake_arm (EventBackendUring *uring) {

	if (!uring->wake_armed) {
		struct io_uring_sqe *submission = uring_submission (uring, NULL, URING_TAG_WAKE, IORING_OP_READ);
		if (submission) {
			submission->addr = (u64) (uintptr_t) &uring->wake_value;
			submission->len = sizeof (uring->wake_value);

			uring->wake_armed = true;
		}
	}

}

#pragma endregion

#pragma region completions

static int uring_give (
	EventBackendUring *uring, EventHandle *handle, const unsigned int operation,
	const int result, const char *data,
	const EventCompletionHandler handler, void *args
) {

	EventCompletion completion;
	completion.handle = handle;
	completion.operation = operation;
	completion.result = result;
	completion.data = data;
	completion.address = NULL;

	uring->backend.stats.n_completions += 1;

	handler (&completion, args);

	return 1;

}

// the last operation of a handle that was stopped ended: its owner is told, it can release it
static int uring_stopped (
	EventBackendUring *uring, EventHandle *handle,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	if (!handle->operations) {
		handle->stopped = false;

		count = uring_give (uring, handle, EVENT_OPERATION_STOP, 0, NULL, handler, args);
	}

	return count;

}

// a connection that was accepted is given also after a stop: its client is already connected.
// The operation goes on while the kernel says that more completions come
static int uring_completed_accept (
	EventBackendUring *uring, EventHandle *handle, const int result, const unsigned int flags,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	const bool stopped = handle->stopped;

	if (!(flags & IORING_CQE_F_MORE)) {
		handle->operations &= ~EVENT_OPERATION_ACCEPT;
	}

	if ((result >= 0) || !stopped) {
		count = uring_give (uring, handle, EVENT_OPERATION_ACCEPT, result, NULL, handler, args);
	}

	else {
		count = uring_stopped (uring, handle, handler, args);
	}

	return count;

}

// a read without a free buffer is asked again: the buffers of the completions before it were given back
static int uring_completed_receive (
	EventBackendUring *uring, EventHandle *handle, const int result, const unsigned int flags,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	const bool has_buffer = (flags & IORING_CQE_F_BUFFER) != 0;
	const unsigned int buffer = flags >> IORING_CQE_BUFFER_SHIFT;

	handle->operations &= ~EVENT_OPERATION_RECEIVE;

	if (handle->stopped) {
		count = uring_stopped (uring, handle, handler, args);
	}

	else if (result == -ENOBUFS) {
		uring->backend.stats.n_no_buffers += 1;

		(void) uring_receive (&uring->backend, handle);
	}

	else {
		count = uring_give (
			uring, handle, EVENT_OPERATION_RECEIVE, result,
			has_buffer ? event_ring_buffer (&uring->buffers, buffer) : NULL,
			handler, args
		);
	}

	// the handler used the bytes: the kernel can choose the buffer again
	if (has_buffer) {
		event_ring_buffer_give (&uring->buffers, buffer);
	}

	return count;

}

// the result of a send is its bytes, the one of a socket that can be written is only that it can
static int uring_completed_other (
	EventBackendUring *uring, EventHandle *handle, const unsigned int operation, const int result,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	handle->operations &= ~operation;

	if (handle->stopped) {
		count = uring_stopped (uring, handle, handler, args);
	}

	else if (operation != EVENT_OPERATION_STOP) {
		count = uring_give (
			uring, handle, operation,
			((operation == EVENT_OPERATION_WRITABLE) && (result > 0)) ? 0 : result,
			NULL, handler, args
		);
	}

	return count;

}

static int uring_completed_wake (EventBackendUring *uring) {

	uring->wake_armed = false;

	event_backend_wake_taken (&uring->backend);

	return 0;

}

static int uring_completed (
	EventBackendUring *uring, const u64 data, const int result, const unsigned int flags,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	EventHandle *handle = (EventHandle *) (uintptr_t) (data & ~((u64) URING_TAG_MASK));

	switch (data & URING_TAG_MASK) {
		case URING_TAG_ACCEPT:
			count = uring_completed_accept (uring, handle, result, flags, handler, args);
			break;

		case URING_TAG_RECEIVE:
			count = uring_completed_receive (uring, handle, result, flags, handler, args);
			break;

		case URING_TAG_SEND:
			count = uring_completed_other (uring, handle, EVENT_OPERATION_SEND, result, handler, args);
			break;

		case URING_TAG_WRITABLE:
			count = uring_completed_other (uring, handle, EVENT_OPERATION_WRITABLE, result, handler, args);
			break;

		case URING_TAG_STOP:
			count = uring_completed_other (uring, handle, EVENT_OPERATION_STOP, result, handler, args);
			break;

		case URING_TAG_WAKE:
			count = uring_completed_wake (uring);
			break;

		default: break;
	}

	return count;

}

static int uring_wait (
	EventBackend *backend, const int timeout, const EventCompletionHandler handler, void *args
) {

	int count = -1;

	EventBackendUring *uring = (EventBackendUring *) backend;

	uring_wake_arm (uring);

	// the ring is of the thread that waits on it first: the one of its loop, that can be another one
	// than the thread that created the backend
	if ((uring->ring.enabled || !event_ring_enable (&uring->ring)) && !event_ring_wait (&uring->ring, timeout)) {
		count = 0;

		backend->stats.n_waits += 1;

		// each completion is given back to the kernel before its handler runs: a handler can fill the ring,
		// and the kernel gives the completions that it kept when the ring has room for them
		unsigned int position = event_ring_completions_begin (&uring->ring);
		while (position != event_ring_completions_end (&uring->ring)) {
			const struct io_uring_cqe *completion = event_ring_completion (&uring->ring, position);

			const u64 data = completion->user_data;
			const int result = completion->res;
			const unsigned int flags = completion->flags;

			position += 1;
			event_ring_completions_seen (&uring->ring, position);

			count += uring_completed (uring, data, result, flags, handler, args);
		}
	}

	return count;

}

#pragma endregion

#pragma region lifecycle

static void uring_end (EventBackend *backend) {

	EventBackendUring *uring = (EventBackendUring *) backend;

	event_ring_buffers_delete (&uring->ring, &uring->buffers);
	event_ring_delete (&uring->ring);

	event_backend_clear (backend);

	free (uring);

}

static bool uring_is_supported (const EventRing *ring) {

	bool supported = true;

	for (size_t i = 0; i < (sizeof (uring_operations) / sizeof (uring_operations[0])); i++) {
		if (!event_ring_supports (ring, uring_operations[i])) supported = false;
	}

	return supported;

}

static unsigned int uring_start (EventBackendUring *uring, const EventBackendSettings *settings) {

	unsigned int retval = 1;

	if (!event_ring_create (&uring->ring, settings->submissions, settings->completions)) {
		if (!uring_is_supported (&uring->ring)) {
			errno = ENOSYS;
		}

		else {
			retval = event_ring_buffers_create (
				&uring->ring, &uring->buffers, EVENT_RING_BUFFERS_GROUP,
				settings->buffers ? settings->buffers : EVENT_RING_BUFFERS,
				settings->buffer_size ? settings->buffer_size : EVENT_RING_BUFFER_SIZE
			);
		}
	}

	return retval;

}

EventBackend *event_backend_uring_create (const EventBackendSettings *settings) {

	static const EventBackendSettings defaults = { 0, 0, 0, 0, 0 };

	EventBackendUring *uring = (EventBackendUring *) calloc (1, sizeof (EventBackendUring));
	if (uring) {
		uring->ring.fd = -1;

		uring->backend.end = uring_end;
		uring->backend.accept = uring_accept;
		uring->backend.receive = uring_receive;
		uring->backend.send = uring_send;
		uring->backend.writable = uring_writable;
		uring->backend.stop = uring_stop;
		uring->backend.wait = uring_wait;

		if (
			event_backend_init (&uring->backend, CERVER_EVENTS_BACKEND_URING)
			|| uring_start (uring, settings ? settings : &defaults)
		) {
			const int error = errno;
			uring_end (&uring->backend);
			errno = error;

			uring = NULL;
		}
	}

	return (EventBackend *) uring;

}

#pragma endregion
