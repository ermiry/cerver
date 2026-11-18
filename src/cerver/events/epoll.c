#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/backend.h"

// the backend of epoll, for a kernel without io_uring or a process that can not use it: it waits until a socket
// is ready and makes the system call of its operation itself, so its loop gets the same completions.
// An operation is tried when it is asked (a new connection has its request, a response fits in its socket),
// before the next wait, and a socket is only registered when it has to wait; it stays registered for its reads,
// so a connection that is kept costs no epoll_ctl () between its requests

typedef struct EventBackendEpoll {

	EventBackend backend;

	int epoll_fd;

	// what epoll reports
	struct epoll_event *events;
	int max_events;

	// the events of the wait that are being handled: the one of now, and how many there are
	int batch_position;
	int batch_count;

	// the buffer of the reads, one for all the sockets: its bytes are used before the next read
	char *buffer;
	size_t buffer_size;

	// the handles whose operations are tried before the next wait
	EventHandle *tries_head;
	EventHandle *tries_tail;

	// the handle whose operations are being handled: a handler that stops it clears it
	EventHandle *current;

	// the address of the connection that is being accepted
	struct sockaddr_storage address;

} EventBackendEpoll;

#pragma region registration

// the events of a socket are registered when it has to wait for them; a socket without events leaves epoll,
// that would report its errors forever
static unsigned int epoll_register (EventBackendEpoll *epoll, EventHandle *handle, const unsigned int interest) {

	unsigned int retval = 0;

	if (interest != handle->interest) {
		struct epoll_event event;
		(void) memset (&event, 0, sizeof (struct epoll_event));
		event.events = interest;
		event.data.ptr = handle;

		const int operation = !interest ? EPOLL_CTL_DEL : (handle->registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD);

		if (!epoll_ctl (epoll->epoll_fd, operation, handle->fd, &event)) {
			handle->interest = interest;
			handle->registered = interest != 0;
		}

		else {
			retval = 1;
		}
	}

	return retval;

}

static unsigned int epoll_watch (EventBackendEpoll *epoll, EventHandle *handle, const unsigned int events) {

	return epoll_register (epoll, handle, handle->interest | events);

}

static unsigned int epoll_unwatch (EventBackendEpoll *epoll, EventHandle *handle, const unsigned int events) {

	return epoll_register (epoll, handle, handle->interest & ~events);

}

static void epoll_tries_push (EventBackendEpoll *epoll, EventHandle *handle) {

	if (!handle->queued) {
		handle->queued = true;
		handle->next = NULL;

		if (epoll->tries_tail) epoll->tries_tail->next = handle;
		else epoll->tries_head = handle;

		epoll->tries_tail = handle;
	}

}

static EventHandle *epoll_tries_pop (EventBackendEpoll *epoll) {

	EventHandle *handle = epoll->tries_head;
	if (handle) {
		epoll->tries_head = handle->next;
		if (!epoll->tries_head) epoll->tries_tail = NULL;

		handle->queued = false;
		handle->next = NULL;
	}

	return handle;

}

static void epoll_tries_remove (EventBackendEpoll *epoll, EventHandle *handle) {

	if (handle->queued) {
		EventHandle *previous = NULL;
		EventHandle *current = epoll->tries_head;
		while (current && (current != handle)) {
			previous = current;
			current = current->next;
		}

		if (current) {
			if (previous) previous->next = handle->next;
			else epoll->tries_head = handle->next;

			if (epoll->tries_tail == handle) epoll->tries_tail = previous;
		}

		handle->queued = false;
		handle->next = NULL;
	}

}

#pragma endregion

#pragma region operations

// an operation that waits in epoll and could not be registered was not asked
static unsigned int epoll_ask (
	EventBackendEpoll *epoll, EventHandle *handle, const unsigned int operation, const unsigned int events
) {

	handle->operations |= operation;

	const unsigned int retval = epoll_watch (epoll, handle, events);
	if (retval) handle->operations &= ~operation;

	return retval;

}

static unsigned int epoll_accept (EventBackend *backend, EventHandle *handle) {

	return epoll_ask ((EventBackendEpoll *) backend, handle, EVENT_OPERATION_ACCEPT, EPOLLIN);

}

// a socket that may have bytes is read before the next wait; one that had none waits for them in epoll
static unsigned int epoll_receive (EventBackend *backend, EventHandle *handle) {

	unsigned int retval = 0;

	EventBackendEpoll *epoll = (EventBackendEpoll *) backend;

	if (handle->readable || !handle->drained) {
		handle->operations |= EVENT_OPERATION_RECEIVE;

		epoll_tries_push (epoll, handle);
	}

	else {
		retval = epoll_ask (epoll, handle, EVENT_OPERATION_RECEIVE, EPOLLIN);
	}

	return retval;

}

static unsigned int epoll_send (
	EventBackend *backend, EventHandle *handle, const char *data, const size_t length
) {

	handle->send_data = data;
	handle->send_length = length;
	handle->operations |= EVENT_OPERATION_SEND;

	epoll_tries_push ((EventBackendEpoll *) backend, handle);

	return 0;

}

static unsigned int epoll_writable (EventBackend *backend, EventHandle *handle) {

	return epoll_ask ((EventBackendEpoll *) backend, handle, EVENT_OPERATION_WRITABLE, EPOLLOUT);

}

// a handle that is stopped by a handler can have an event that waits in the same wait: its owner can
// release its memory at once, so that event is marked as nobody's (the backend itself is never a handle)
static void epoll_batch_forget (EventBackendEpoll *epoll, const EventHandle *handle) {

	for (int i = epoll->batch_position + 1; i < epoll->batch_count; i++) {
		if (epoll->events[i].data.ptr == handle) epoll->events[i].data.ptr = epoll;
	}

}

// nothing of the handle is in the kernel but its registration: it is free at once
static unsigned int epoll_stop (EventBackend *backend, EventHandle *handle) {

	EventBackendEpoll *epoll = (EventBackendEpoll *) backend;

	epoll_tries_remove (epoll, handle);

	epoll_batch_forget (epoll, handle);

	(void) epoll_register (epoll, handle, 0);

	handle->operations = EVENT_OPERATION_NONE;
	handle->registered = false;
	handle->interest = 0;
	handle->readable = false;
	handle->drained = false;

	if (epoll->current == handle) epoll->current = NULL;

	return 0;

}

#pragma endregion

#pragma region completions

static int epoll_give (
	EventBackendEpoll *epoll, EventHandle *handle, const unsigned int operation,
	const int result, const char *data, const struct sockaddr_storage *address,
	const EventCompletionHandler handler, void *args
) {

	EventCompletion completion;
	completion.handle = handle;
	completion.operation = operation;
	completion.result = result;
	completion.data = data;
	completion.address = address;

	epoll->backend.stats.n_completions += 1;

	handler (&completion, args);

	return 1;

}

// a read that takes less than the buffer left the socket without bytes: the next one waits in epoll
static int epoll_try_receive (
	EventBackendEpoll *epoll, EventHandle *handle,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	const ssize_t received = recv (handle->fd, epoll->buffer, epoll->buffer_size, MSG_DONTWAIT);

	handle->readable = false;

	if ((received < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))) {
		handle->drained = (errno != EINTR);

		if (handle->drained) (void) epoll_watch (epoll, handle, EPOLLIN);
		else epoll_tries_push (epoll, handle);
	}

	else {
		handle->drained = (received >= 0) && ((size_t) received < epoll->buffer_size);
		handle->operations &= ~EVENT_OPERATION_RECEIVE;

		count = epoll_give (
			epoll, handle, EVENT_OPERATION_RECEIVE,
			(received < 0) ? -errno : (int) received, epoll->buffer, NULL, handler, args
		);
	}

	return count;

}

// a socket that is full waits in epoll until it can be written
static int epoll_try_send (
	EventBackendEpoll *epoll, EventHandle *handle,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	const ssize_t sent = send (
		handle->fd, handle->send_data, handle->send_length, MSG_NOSIGNAL | MSG_DONTWAIT
	);

	if ((sent < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
		(void) epoll_watch (epoll, handle, EPOLLOUT);
	}

	else if ((sent < 0) && (errno == EINTR)) {
		epoll_tries_push (epoll, handle);
	}

	else {
		const int result = (sent < 0) ? -errno : (int) sent;

		handle->operations &= ~EVENT_OPERATION_SEND;

		if (!(handle->operations & EVENT_OPERATION_WRITABLE)) (void) epoll_unwatch (epoll, handle, EPOLLOUT);

		count = epoll_give (epoll, handle, EVENT_OPERATION_SEND, result, NULL, NULL, handler, args);
	}

	return count;

}

static int epoll_give_writable (
	EventBackendEpoll *epoll, EventHandle *handle,
	const EventCompletionHandler handler, void *args
) {

	handle->operations &= ~EVENT_OPERATION_WRITABLE;

	if (!(handle->operations & EVENT_OPERATION_SEND)) (void) epoll_unwatch (epoll, handle, EPOLLOUT);

	return epoll_give (epoll, handle, EVENT_OPERATION_WRITABLE, 0, NULL, NULL, handler, args);

}

// the errors of an accept that only tell to try the next connection
static bool epoll_accept_is_retried (const int error) {

	return (error == EINTR) || (error == ECONNABORTED) || (error == EPROTO);

}

// any other error ends the operation, as the kernel ends the one of the ring: its loop asks again
static int epoll_accept_failed (
	EventBackendEpoll *epoll, EventHandle *handle, const int error,
	const EventCompletionHandler handler, void *args
) {

	handle->operations &= ~EVENT_OPERATION_ACCEPT;

	(void) epoll_unwatch (epoll, handle, EPOLLIN);

	return epoll_give (epoll, handle, EVENT_OPERATION_ACCEPT, -error, NULL, NULL, handler, args);

}

// the connections that wait are accepted in batches, the other sockets are handled between them;
// a handler that stops the accept ends the batch
static int epoll_accept_batch (
	EventBackendEpoll *epoll, EventHandle *handle,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	bool more = true;
	for (
		int i = 0;
		more && (i < EVENT_BACKEND_ACCEPT_BATCH)
		&& (epoll->current == handle) && (handle->operations & EVENT_OPERATION_ACCEPT);
		i++
	) {
		socklen_t address_size = sizeof (struct sockaddr_storage);
		(void) memset (&epoll->address, 0, sizeof (struct sockaddr_storage));

		const int fd = accept4 (
			handle->fd, (struct sockaddr *) &epoll->address, &address_size, SOCK_NONBLOCK | SOCK_CLOEXEC
		);

		if (fd >= 0) {
			count += epoll_give (epoll, handle, EVENT_OPERATION_ACCEPT, fd, NULL, &epoll->address, handler, args);
		}

		else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			more = false;
		}

		else if (!epoll_accept_is_retried (errno)) {
			count += epoll_accept_failed (epoll, handle, errno, handler, args);
			more = false;
		}
	}

	return count;

}

// a socket that has bytes, or whose peer left, with no read asked: it is remembered and it leaves epoll,
// that would report it in every wait; the read that is asked later finds it
static void epoll_unasked (EventBackendEpoll *epoll, EventHandle *handle) {

	handle->readable = true;

	(void) epoll_unwatch (epoll, handle, EPOLLIN);

}

static int epoll_reported_connection (
	EventBackendEpoll *epoll, EventHandle *handle, const unsigned int events,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	const bool failed = (events & (EPOLLERR | EPOLLHUP)) != 0;

	if ((events & (EPOLLIN | EPOLLRDHUP)) || failed) {
		if (handle->operations & EVENT_OPERATION_RECEIVE) count += epoll_try_receive (epoll, handle, handler, args);
		else if (handle->interest & EPOLLIN) epoll_unasked (epoll, handle);
	}

	if ((epoll->current == handle) && ((events & EPOLLOUT) || failed)) {
		if (handle->operations & EVENT_OPERATION_SEND) count += epoll_try_send (epoll, handle, handler, args);
	}

	if ((epoll->current == handle) && ((events & EPOLLOUT) || failed)) {
		if (handle->operations & EVENT_OPERATION_WRITABLE) count += epoll_give_writable (epoll, handle, handler, args);
	}

	return count;

}

// the counter of the eventfd is read before the wake up is taken (event_backend_wake_taken ())
static void epoll_reported_wake (EventBackendEpoll *epoll) {

	uint64_t value = 0;
	if (read (epoll->backend.wake_fd, &value, sizeof (value)) < 0) {
		// another wait took it
	}

	event_backend_wake_taken (&epoll->backend);

}

// the event of a handle that a handler of this wait stopped is nobody's (epoll_batch_forget ()); one that was
// stopped and is used again is not registered yet: what epoll said of it is old
static int epoll_reported (
	EventBackendEpoll *epoll, const struct epoll_event *event,
	const EventCompletionHandler handler, void *args
) {

	int count = 0;

	EventHandle *handle = (EventHandle *) event->data.ptr;

	if (!handle) {
		epoll_reported_wake (epoll);
	}

	else if (((void *) handle != (void *) epoll) && handle->registered) {
		epoll->current = handle;

		count = (handle->type == EVENT_HANDLE_TYPE_LISTEN) ?
			epoll_accept_batch (epoll, handle, handler, args) :
			epoll_reported_connection (epoll, handle, event->events, handler, args);

		epoll->current = NULL;
	}

	return count;

}

// the operations that were asked since the last wait, and the ones that their handlers ask
static int epoll_tries (EventBackendEpoll *epoll, const EventCompletionHandler handler, void *args) {

	int count = 0;

	EventHandle *handle = NULL;
	while ((handle = epoll_tries_pop (epoll)) != NULL) {
		epoll->current = handle;

		if ((handle->operations & EVENT_OPERATION_RECEIVE) && !(handle->interest & EPOLLIN)) {
			count += epoll_try_receive (epoll, handle, handler, args);
		}

		if (
			(epoll->current == handle)
			&& (handle->operations & EVENT_OPERATION_SEND) && !(handle->interest & EPOLLOUT)
		) {
			count += epoll_try_send (epoll, handle, handler, args);
		}

		epoll->current = NULL;
	}

	return count;

}

static int epoll_wait_events (
	EventBackendEpoll *epoll, const int timeout, const EventCompletionHandler handler, void *args
) {

	int count = -1;

	const int n_events = epoll_wait (epoll->epoll_fd, epoll->events, epoll->max_events, timeout);

	epoll->backend.stats.n_waits += 1;

	if (n_events >= 0) {
		count = 0;

		epoll->batch_count = n_events;

		for (epoll->batch_position = 0; epoll->batch_position < n_events; epoll->batch_position++) {
			count += epoll_reported (epoll, &epoll->events[epoll->batch_position], handler, args);
		}

		epoll->batch_count = 0;
	}

	else if (errno == EINTR) {
		count = 0;
	}

	return count;

}

// what the operations that were tried completed is given before the wait, that then only looks
static int epoll_backend_wait (
	EventBackend *backend, const int timeout, const EventCompletionHandler handler, void *args
) {

	EventBackendEpoll *epoll = (EventBackendEpoll *) backend;

	const int tried = epoll_tries (epoll, handler, args);

	const int waited = epoll_wait_events (epoll, tried ? 0 : timeout, handler, args);

	return (waited < 0) ? -1 : (tried + waited);

}

#pragma endregion

#pragma region lifecycle

static void epoll_end (EventBackend *backend) {

	EventBackendEpoll *epoll = (EventBackendEpoll *) backend;

	if (epoll->epoll_fd >= 0) (void) close (epoll->epoll_fd);

	free (epoll->events);
	free (epoll->buffer);

	event_backend_clear (backend);

	free (epoll);

}

// the wake up has no handle: it is the event without a pointer
static unsigned int epoll_start (EventBackendEpoll *epoll, const EventBackendSettings *settings) {

	unsigned int retval = 1;

	epoll->max_events = (int) (settings->events ? settings->events : EVENT_BACKEND_EVENTS);
	epoll->buffer_size = settings->buffer_size ? settings->buffer_size : EVENT_BACKEND_BUFFER_SIZE;

	epoll->events = (struct epoll_event *) calloc ((size_t) epoll->max_events, sizeof (struct epoll_event));
	epoll->buffer = (char *) malloc (epoll->buffer_size);

	epoll->epoll_fd = epoll_create1 (EPOLL_CLOEXEC);

	if (epoll->events && epoll->buffer && (epoll->epoll_fd >= 0)) {
		struct epoll_event event;
		(void) memset (&event, 0, sizeof (struct epoll_event));
		event.events = EPOLLIN;
		event.data.ptr = NULL;

		retval = epoll_ctl (epoll->epoll_fd, EPOLL_CTL_ADD, epoll->backend.wake_fd, &event) ? 1 : 0;
	}

	return retval;

}

EventBackend *event_backend_epoll_create (const EventBackendSettings *settings) {

	static const EventBackendSettings defaults = { 0, 0, 0, 0, 0 };

	EventBackendEpoll *epoll = (EventBackendEpoll *) calloc (1, sizeof (EventBackendEpoll));
	if (epoll) {
		epoll->epoll_fd = -1;

		epoll->backend.end = epoll_end;
		epoll->backend.accept = epoll_accept;
		epoll->backend.receive = epoll_receive;
		epoll->backend.send = epoll_send;
		epoll->backend.writable = epoll_writable;
		epoll->backend.stop = epoll_stop;
		epoll->backend.wait = epoll_backend_wait;

		if (
			event_backend_init (&epoll->backend, CERVER_EVENTS_BACKEND_EPOLL)
			|| epoll_start (epoll, settings ? settings : &defaults)
		) {
			const int error = errno;
			epoll_end (&epoll->backend);
			errno = error;

			epoll = NULL;
		}
	}

	return (EventBackend *) epoll;

}

#pragma endregion
