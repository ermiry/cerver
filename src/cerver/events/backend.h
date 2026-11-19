/**
 * @file events/backend.h
 * @internal
 * @brief The I/O of an event loop as operations that complete: the loop asks for an accept, a read or a send and
 * gets its result later, whatever system call gives it; never installed.
 *
 * A backend belongs to the thread of its loop. The one of io_uring (events/uring.c) gives the operations to the
 * kernel and reads their completions; the one of epoll (events/epoll.c) waits until a socket is ready and makes the
 * system call of the operation itself. The methods that the tests use are exported (#CERVER_PRIVATE).
 *
 * @since 3.0b-11
 */

#ifndef _CERVER_EVENTS_BACKEND_H_
#define _CERVER_EVENTS_BACKEND_H_

#include <stdbool.h>
#include <stddef.h>

#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/events.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @defgroup events_backend Backend
 * @brief The operations of the sockets of a loop and their completions.
 * @{
 */

/** @internal @brief No operation. */
#define EVENT_OPERATION_NONE				0u
/** @internal @brief The connections of a socket that listens: each one is a completion with its fd, until it is stopped or it fails. */
#define EVENT_OPERATION_ACCEPT				(1u << 0)
/** @internal @brief The next bytes of a socket, once: a completion with them, with 0 when its peer ended, or with an error. */
#define EVENT_OPERATION_RECEIVE				(1u << 1)
/** @internal @brief Bytes to a socket, once: a completion with the ones that it took, that can be less, or with an error. */
#define EVENT_OPERATION_SEND				(1u << 2)
/** @internal @brief A completion when a socket can be written, once. */
#define EVENT_OPERATION_WRITABLE			(1u << 3)
/** @internal @brief The end of the operations of a handle that were stopped: its owner can release it. */
#define EVENT_OPERATION_STOP				(1u << 4)

/** @internal @brief A wait without a timeout. */
#define EVENT_BACKEND_WAIT_FOREVER			(-1)

/** @internal @brief The connections that the backend of epoll accepts at most each time that its socket is ready. */
#define EVENT_BACKEND_ACCEPT_BATCH			(1 << 6)

/** @internal @brief The sockets that a wait of the backend of epoll reports at most. */
#define EVENT_BACKEND_EVENTS				(1u << 8)

/** @internal @brief The bytes of the buffer of the reads of the backend of epoll, one for all its sockets. */
#define EVENT_BACKEND_BUFFER_SIZE			(1u << 16)

/** @internal @brief What a handle of a backend is, its owner reads it to know what completed. */
typedef enum EventHandleType {

	/** @internal @brief Not a handle. */
	EVENT_HANDLE_TYPE_NONE					= 0,
	/** @internal @brief A socket that accepts connections. */
	EVENT_HANDLE_TYPE_LISTEN				= 1,
	/** @internal @brief A connection. */
	EVENT_HANDLE_TYPE_CONNECTION			= 2,
	/** @internal @brief The socket of a source of a loop (struct EventSource): its completions go to its methods. */
	EVENT_HANDLE_TYPE_SOURCE				= 3

} EventHandleType;

/**
 * @internal
 * @brief A socket of a backend, embedded in its owner.
 *
 * Its memory must stay valid while it has operations: a completion carries its pointer. Only the thread of its
 * backend uses it.
 */
typedef struct EventHandle {

	/** @internal @brief What the handle is. */
	EventHandleType type;
	/** @internal @brief The file descriptor, -1 without one. */
	int fd;
	/** @internal @brief The owner of the handle. */
	void *owner;

	/**
	 * @internal
	 * @brief The operations that were asked and have not completed, EVENT_OPERATION_* flags. An operation is asked
	 * again only after its completion; a handle is released only without operations.
	 */
	unsigned int operations;
	/** @internal @brief Its operations were stopped: their completions are not given, only the one of the stop. */
	bool stopped;

	/** @internal @brief The bytes of the send that was asked, valid until its completion. */
	const char *send_data;
	/** @internal @brief The length of #send_data. */
	size_t send_length;

	/** @internal @brief Of the backend of epoll: the events of the socket that are registered. */
	unsigned int interest;
	/** @internal @brief Of the backend of epoll: the socket is registered. */
	bool registered;
	/** @internal @brief Of the backend of epoll: the socket has bytes that no read has asked for. */
	bool readable;
	/** @internal @brief Of the backend of epoll: the last read took all the bytes that the socket had. */
	bool drained;
	/** @internal @brief Of the backend of epoll: the handle is in the list of the ones whose operations are tried. */
	bool queued;
	/** @internal @brief Of the backend of epoll: the next handle of that list. */
	struct EventHandle *next;

} EventHandle;

/** @internal @brief The result of an operation. */
typedef struct EventCompletion {

	/** @internal @brief The handle of the operation. */
	EventHandle *handle;
	/** @internal @brief The operation, one EVENT_OPERATION_* flag. */
	unsigned int operation;
	/**
	 * @internal
	 * @brief The result: the fd of a connection that was accepted, the bytes that were read (0 when the peer ended)
	 * or sent, 0 for a socket that can be written and for a stop, or the negative errno of an error.
	 */
	int result;
	/** @internal @brief The bytes of a read, valid only during the call that gets the completion. */
	const char *data;
	/** @internal @brief The address of the peer of a connection that was accepted, NULL when the backend does not have it. */
	const struct sockaddr_storage *address;

} EventCompletion;

/** @internal @brief Called with each completion of a wait, by the thread of the backend. */
typedef void (*EventCompletionHandler) (const EventCompletion *completion, void *args);

/** @internal @brief The sizes of a backend, 0 for the default of each one; the tests make them small. */
typedef struct EventBackendSettings {

	/** @internal @brief The submissions of the ring (io_uring). */
	unsigned int submissions;
	/** @internal @brief The completions of the ring (io_uring). */
	unsigned int completions;
	/** @internal @brief The buffers of the reads (io_uring). */
	unsigned int buffers;
	/** @internal @brief The bytes of each buffer of the reads (io_uring), or of the buffer of all the reads (epoll). */
	unsigned int buffer_size;
	/** @internal @brief The sockets that a wait reports at most (epoll). */
	unsigned int events;

} EventBackendSettings;

/** @internal @brief The counters of a backend, for the tests and the stats. */
typedef struct EventBackendStats {

	/** @internal @brief The times that it entered the kernel to wait. */
	u64 n_waits;
	/** @internal @brief The completions that it gave. */
	u64 n_completions;
	/** @internal @brief The reads that found no buffer and were asked again (io_uring). */
	u64 n_no_buffers;
	/** @internal @brief The wake ups that it took. */
	u64 n_wakes;
	/** @internal @brief The sends that it was asked for: the responses of a pipeline that leave together are one. */
	u64 n_sends;

} EventBackendStats;

/** @internal @brief A backend. */
typedef struct EventBackend EventBackend;

/**
 * @internal
 * @brief A backend: its methods and what every backend has. Each backend has its own struct that starts with
 * this one.
 */
struct EventBackend {

	/** @internal @brief The name of the backend, a static string. */
	const char *name;
	/** @internal @brief Which backend it is: CERVER_EVENTS_BACKEND_URING or CERVER_EVENTS_BACKEND_EPOLL. */
	CerverEventsBackend type;

	/** @internal @brief Releases the backend, event_backend_delete (). */
	void (*end) (EventBackend *backend);
	/** @internal @brief event_backend_accept (). */
	unsigned int (*accept) (EventBackend *backend, EventHandle *handle);
	/** @internal @brief event_backend_receive (). */
	unsigned int (*receive) (EventBackend *backend, EventHandle *handle);
	/** @internal @brief event_backend_send (). */
	unsigned int (*send) (EventBackend *backend, EventHandle *handle, const char *data, const size_t length);
	/** @internal @brief event_backend_writable (). */
	unsigned int (*writable) (EventBackend *backend, EventHandle *handle);
	/** @internal @brief event_backend_stop (). */
	unsigned int (*stop) (EventBackend *backend, EventHandle *handle);
	/** @internal @brief event_backend_wait (). */
	int (*wait) (EventBackend *backend, const int timeout, const EventCompletionHandler handler, void *args);

	/** @internal @brief The eventfd of its wake ups. */
	int wake_fd;
	/** @internal @brief A wake up was written and the backend has not taken it; atomic. */
	bool wake_pending;

	/** @internal @brief Its counters. */
	EventBackendStats stats;

};

/**
 * @internal
 * @brief Sets a handle without operations.
 *
 * @param[out] handle the handle
 * @param[in] type what the handle is
 * @param[in] fd its file descriptor
 * @param[in] owner the owner of the handle
 */
CERVER_PRIVATE void event_handle_init (
	EventHandle *handle, const EventHandleType type, const int fd, void *owner
);

/**
 * @internal
 * @brief Creates the backend of io_uring.
 *
 * @param[in] settings its sizes, NULL for the defaults
 * @return the backend, NULL on error: errno tells why (EPERM when io_uring is not allowed, ENOSYS or EINVAL when
 * the kernel does not have what the backend needs)
 * @threadsafety Any thread creates it and asks for its first operations; the backend belongs to the thread that
 * waits on it first, and only that thread uses it from then on.
 * @warning Before its first wait the kernel takes nothing: the operations that are asked then must fit in its
 * submissions, one more fails.
 */
CERVER_PRIVATE EventBackend *event_backend_uring_create (const EventBackendSettings *settings);

/**
 * @internal
 * @brief Creates the backend of epoll.
 *
 * @param[in] settings its sizes, NULL for the defaults
 * @return the backend, NULL on error (errno tells)
 */
CERVER_PRIVATE EventBackend *event_backend_epoll_create (const EventBackendSettings *settings);

/**
 * @internal
 * @brief Creates the backend that a cerver asks for: CERVER_EVENTS_BACKEND_AUTO is the one of io_uring, and the
 * one of epoll where io_uring is not available.
 *
 * @param[in] type the backend
 * @param[in] settings its sizes, NULL for the defaults
 * @param[out] unavailable the errno that io_uring gave when CERVER_EVENTS_BACKEND_AUTO took epoll instead, 0
 * otherwise; it can be NULL
 * @return the backend, NULL on error (errno tells): also when CERVER_EVENTS_BACKEND_URING was asked for and
 * io_uring is not available
 */
CERVER_PRIVATE EventBackend *event_backend_create (
	const CerverEventsBackend type, const EventBackendSettings *settings, int *unavailable
);

/**
 * @internal
 * @brief Releases a backend; its handles are not closed and must have no operations.
 *
 * @param[in] backend the backend, it can be NULL
 */
CERVER_PRIVATE void event_backend_delete (EventBackend *backend);

/**
 * @internal
 * @brief Makes the wait of a backend return; the wake ups that are written before the backend takes one are one.
 *
 * @param[in,out] backend the backend
 * @threadsafety Safe from any thread and from a signal handler: an atomic exchange and one write ().
 */
CERVER_PRIVATE void event_backend_wake (EventBackend *backend);

/**
 * @internal
 * @brief Accepts the connections of a socket that listens: each one is a completion with its fd, non blocking. An
 * error is a completion too and ends the operation, that is asked again to go on.
 *
 * @param[in,out] backend the backend
 * @param[in,out] handle the handle of the socket, without #EVENT_OPERATION_ACCEPT
 * @return 0 on success, 1 on error
 */
static inline unsigned int event_backend_accept (EventBackend *backend, EventHandle *handle) {

	return backend->accept (backend, handle);

}

/**
 * @internal
 * @brief Asks for the next bytes of a socket, once.
 *
 * @param[in,out] backend the backend
 * @param[in,out] handle the handle of the socket, without #EVENT_OPERATION_RECEIVE
 * @return 0 on success, 1 on error
 */
static inline unsigned int event_backend_receive (EventBackend *backend, EventHandle *handle) {

	return backend->receive (backend, handle);

}

/**
 * @internal
 * @brief Sends bytes to a socket, once; its completion tells how many were taken.
 *
 * @param[in,out] backend the backend
 * @param[in,out] handle the handle of the socket, without #EVENT_OPERATION_SEND
 * @param[in] data the bytes, that stay valid and are not changed until the completion
 * @param[in] length how many, more than 0
 * @return 0 on success, 1 on error
 */
static inline unsigned int event_backend_send (
	EventBackend *backend, EventHandle *handle, const char *data, const size_t length
) {

	backend->stats.n_sends += 1;

	return backend->send (backend, handle, data, length);

}

/**
 * @internal
 * @brief Asks for a completion when a socket can be written, once.
 *
 * @param[in,out] backend the backend
 * @param[in,out] handle the handle of the socket, without #EVENT_OPERATION_WRITABLE
 * @return 0 on success, 1 on error
 */
static inline unsigned int event_backend_writable (EventBackend *backend, EventHandle *handle) {

	return backend->writable (backend, handle);

}

/**
 * @internal
 * @brief Stops the operations of a handle: none of them is given anymore, except the connections that a socket
 * that listens had already accepted.
 *
 * @param[in,out] backend the backend
 * @param[in,out] handle the handle
 * @return 0 when the handle has no operations left: its owner can release it and close its socket; 1 when the
 * kernel still has some: a completion of #EVENT_OPERATION_STOP tells when the last one ended, and the handle
 * must stay valid until then
 */
static inline unsigned int event_backend_stop (EventBackend *backend, EventHandle *handle) {

	return backend->stop (backend, handle);

}

/**
 * @internal
 * @brief Gives the kernel the operations that were asked and waits until one completes, the backend is woken up
 * or the timeout ends; every completion that is ready is given to the handler.
 *
 * @param[in,out] backend the backend
 * @param[in] timeout the milliseconds to wait, 0 to return at once, #EVENT_BACKEND_WAIT_FOREVER
 * @param[in] handler called with each completion; it can ask for more operations
 * @param[in] args passed to handler
 * @return the completions that were given (0 after a wake up, a timeout or a signal), -1 on error
 */
static inline int event_backend_wait (
	EventBackend *backend, const int timeout, const EventCompletionHandler handler, void *args
) {

	return backend->wait (backend, timeout, handler, args);

}

/**
 * @internal
 * @brief Sets what every backend has: its name and its wake up.
 *
 * @param[out] backend the backend
 * @param[in] type which backend it is
 * @return 0 on success, 1 when the eventfd of its wake ups was not created
 */
CERVER_HIDDEN unsigned int event_backend_init (EventBackend *backend, const CerverEventsBackend type);

/**
 * @internal
 * @brief Releases what every backend has.
 *
 * @param[in,out] backend the backend
 */
CERVER_HIDDEN void event_backend_clear (EventBackend *backend);

/**
 * @internal
 * @brief Takes the wake up of a backend that its eventfd reported: the next one is written again.
 *
 * @param[in,out] backend the backend
 */
CERVER_HIDDEN void event_backend_wake_taken (EventBackend *backend);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
