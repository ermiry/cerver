#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <sys/epoll.h>
#include <sys/eventfd.h>

#include "cerver/config.h"

#include "cerver/events/internal.h"

// the only backend of the poller is epoll, level triggered, with an eventfd to wake it up
// another one (kqueue with a pipe) only has to define this struct and the methods of this file
struct EventPoller {

	int epoll_fd;
	int wake_fd;

	// what epoll reports, converted to the sources that are ready
	struct epoll_event *events;
	int max_events;

};

static const uint64_t event_poller_wake_value = 1;

#pragma region source

void event_source_init (
	EventSource *source, const EventSourceType type, void *owner
) {

	if (source) {
		source->type = type;
		source->fd = -1;
		source->interest = EVENT_INTEREST_NONE;
		source->registered = false;
		source->owner = owner;
	}

}

#pragma endregion

#pragma region lifecycle

static EventPoller *event_poller_new (void) {

	EventPoller *poller = (EventPoller *) malloc (sizeof (EventPoller));
	if (poller) {
		poller->epoll_fd = -1;
		poller->wake_fd = -1;

		poller->events = NULL;
		poller->max_events = 0;
	}

	return poller;

}

void event_poller_delete (EventPoller *poller) {

	if (poller) {
		if (poller->wake_fd > -1) (void) close (poller->wake_fd);
		if (poller->epoll_fd > -1) (void) close (poller->epoll_fd);

		free (poller->events);

		free (poller);
	}

}

// the wake up is the only registration without a source: its event has a NULL pointer
static unsigned int event_poller_register_wake (EventPoller *poller) {

	struct epoll_event event;
	(void) memset (&event, 0, sizeof (struct epoll_event));
	event.events = EPOLLIN;
	event.data.ptr = NULL;

	return epoll_ctl (poller->epoll_fd, EPOLL_CTL_ADD, poller->wake_fd, &event) ? 1 : 0;

}

static unsigned int event_poller_open (EventPoller *poller, const int max_events) {

	unsigned int retval = 1;

	poller->epoll_fd = epoll_create1 (EPOLL_CLOEXEC);
	poller->wake_fd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);

	poller->events = (struct epoll_event *) calloc ((size_t) max_events, sizeof (struct epoll_event));
	poller->max_events = max_events;

	if ((poller->epoll_fd > -1) && (poller->wake_fd > -1) && poller->events) {
		retval = event_poller_register_wake (poller);
	}

	return retval;

}

EventPoller *event_poller_create (const unsigned int max_events) {

	EventPoller *poller = event_poller_new ();
	if (poller) {
		if (event_poller_open (poller, (int) (max_events ? max_events : EVENT_POLLER_DEFAULT_EVENTS))) {
			event_poller_delete (poller);
			poller = NULL;
		}
	}

	return poller;

}

#pragma endregion

#pragma region sources

// errors and hang ups are always reported by epoll, they do not need to be asked
static uint32_t event_poller_interest_events (const unsigned int interest) {

	uint32_t events = 0;

	if (interest & EVENT_INTEREST_READ) events |= EPOLLIN;
	if (interest & EVENT_INTEREST_WRITE) events |= EPOLLOUT;

	return events;

}

static unsigned int event_poller_control (
	const EventPoller *poller, const int operation,
	EventSource *source, const int fd, const unsigned int interest
) {

	struct epoll_event event;
	(void) memset (&event, 0, sizeof (struct epoll_event));
	event.events = event_poller_interest_events (interest);
	event.data.ptr = source;

	return epoll_ctl (poller->epoll_fd, operation, fd, &event) ? 1 : 0;

}

unsigned int event_poller_add (
	EventPoller *poller, EventSource *source, const int fd, const unsigned int interest
) {

	unsigned int retval = 1;

	if (poller && source && !source->registered && (fd > -1)) {
		retval = event_poller_control (poller, EPOLL_CTL_ADD, source, fd, interest);
		if (!retval) {
			source->fd = fd;
			source->interest = interest;
			source->registered = true;
		}
	}

	return retval;

}

unsigned int event_poller_modify (
	EventPoller *poller, EventSource *source, const unsigned int interest
) {

	unsigned int retval = 1;

	if (poller && source && source->registered) {
		retval = 0;

		if (source->interest != interest) {
			retval = event_poller_control (poller, EPOLL_CTL_MOD, source, source->fd, interest);
			if (!retval) source->interest = interest;
		}
	}

	return retval;

}

unsigned int event_poller_remove (EventPoller *poller, EventSource *source) {

	unsigned int retval = 1;

	if (poller && source) {
		retval = 0;

		if (source->registered) {
			retval = event_poller_control (poller, EPOLL_CTL_DEL, source, source->fd, EVENT_INTEREST_NONE);

			source->fd = -1;
			source->interest = EVENT_INTEREST_NONE;
			source->registered = false;
		}
	}

	return retval;

}

#pragma endregion

#pragma region wait

static unsigned int event_poller_ready_flags (const uint32_t events) {

	unsigned int ready = 0;

	// a peer that closed is found by the read
	if (events & (EPOLLIN | EPOLLRDHUP)) ready |= EVENT_READY_READ;
	if (events & EPOLLOUT) ready |= EVENT_READY_WRITE;
	if (events & (EPOLLERR | EPOLLHUP)) ready |= EVENT_READY_ERROR;

	return ready;

}

// the counter of the eventfd is read to set it to zero, many wake ups are one
static void event_poller_wake_consume (const EventPoller *poller) {

	uint64_t value = 0;
	while (read (poller->wake_fd, &value, sizeof (value)) < 0) {
		if (errno != EINTR) break;
	}

}

static int event_poller_collect (
	const EventPoller *poller, const int n_events, EventReady *ready
) {

	int n_ready = 0;

	for (int idx = 0; idx < n_events; idx++) {
		EventSource *source = (EventSource *) poller->events[idx].data.ptr;

		if (source) {
			ready[n_ready].source = source;
			ready[n_ready].ready = event_poller_ready_flags (poller->events[idx].events);
			n_ready += 1;
		}

		else {
			event_poller_wake_consume (poller);
		}
	}

	return n_ready;

}

int event_poller_wait (
	EventPoller *poller, EventReady *ready, const int max, const int timeout
) {

	int n_ready = -1;

	if (poller && ready && (max > 0)) {
		const int n_events = epoll_wait (
			poller->epoll_fd, poller->events,
			(max < poller->max_events) ? max : poller->max_events,
			timeout
		);

		if (n_events >= 0) n_ready = event_poller_collect (poller, n_events, ready);

		// a signal is not an error of the poller, its loop decides what to do
		else if (errno == EINTR) n_ready = 0;
	}

	return n_ready;

}

void event_poller_wake (const EventPoller *poller) {

	if (poller) {
		const int error = errno;

		// a full counter (EAGAIN) already wakes the poller up
		while (write (poller->wake_fd, &event_poller_wake_value, sizeof (event_poller_wake_value)) < 0) {
			if (errno != EINTR) break;
		}

		errno = error;
	}

}

#pragma endregion
