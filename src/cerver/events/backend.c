#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/backend.h"

// what every backend has: its handles start the same way, and it is woken up with an eventfd

static const uint64_t event_backend_wake_value = 1;

void event_handle_init (
	EventHandle *handle, const EventHandleType type, const int fd, void *owner
) {

	if (handle) {
		(void) memset (handle, 0, sizeof (EventHandle));

		handle->type = type;
		handle->fd = fd;
		handle->owner = owner;
	}

}

// the name of a backend is the string of its row in the table of the backends
unsigned int event_backend_init (EventBackend *backend, const CerverEventsBackend type) {

	backend->name = cerver_events_backend_to_string (type);
	backend->type = type;

	backend->wake_fd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);
	backend->wake_pending = false;

	(void) memset (&backend->stats, 0, sizeof (EventBackendStats));

	return (backend->wake_fd >= 0) ? 0 : 1;

}

void event_backend_clear (EventBackend *backend) {

	if (backend->wake_fd >= 0) {
		(void) close (backend->wake_fd);
		backend->wake_fd = -1;
	}

}

// io_uring first; where it is not available (a kernel without it, a process that is not allowed to use it),
// epoll gives the same completions
static EventBackend *event_backend_create_auto (const EventBackendSettings *settings, int *unavailable) {

	EventBackend *backend = event_backend_uring_create (settings);
	if (!backend) {
		if (unavailable) *unavailable = errno;

		backend = event_backend_epoll_create (settings);
	}

	return backend;

}

EventBackend *event_backend_create (
	const CerverEventsBackend type, const EventBackendSettings *settings, int *unavailable
) {

	EventBackend *backend = NULL;

	if (unavailable) *unavailable = 0;

	switch (type) {
		case CERVER_EVENTS_BACKEND_URING: backend = event_backend_uring_create (settings); break;
		case CERVER_EVENTS_BACKEND_EPOLL: backend = event_backend_epoll_create (settings); break;
		default: backend = event_backend_create_auto (settings, unavailable); break;
	}

	return backend;

}

void event_backend_delete (EventBackend *backend) {

	if (backend) {
		backend->end (backend);
	}

}

// only the first wake up after the backend took the last one writes: the others are the same wake up
void event_backend_wake (EventBackend *backend) {

	if (backend && !__atomic_exchange_n (&backend->wake_pending, true, __ATOMIC_SEQ_CST)) {
		// a signal handler calls this: the errno of what it interrupted is kept
		const int error = errno;

		if (write (backend->wake_fd, &event_backend_wake_value, sizeof (event_backend_wake_value)) < 0) {
			// the counter of the eventfd is at its limit: the backend is already woken up
		}

		errno = error;
	}

}

// called after the counter of the eventfd was read: a wake up that is asked from now on writes again,
// and what its caller did before it is seen by the loop, that looks after its wait
void event_backend_wake_taken (EventBackend *backend) {

	__atomic_store_n (&backend->wake_pending, false, __ATOMIC_SEQ_CST);

	backend->stats.n_wakes += 1;

}
