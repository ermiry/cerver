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

	// each wake up makes one wait return (the eventfd is a semaphore)
	bool counted;

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
		source->silent = false;
		source->owner = owner;
		source->poller = NULL;
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

		poller->counted = false;
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
	poller->wake_fd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC | (poller->counted ? EFD_SEMAPHORE : 0));

	poller->events = (struct epoll_event *) calloc ((size_t) max_events, sizeof (struct epoll_event));
	poller->max_events = max_events;

	if ((poller->epoll_fd > -1) && (poller->wake_fd > -1) && poller->events) {
		retval = event_poller_register_wake (poller);
	}

	return retval;

}

static EventPoller *event_poller_create_actual (const unsigned int max_events, const bool counted) {

	EventPoller *poller = event_poller_new ();
	if (poller) {
		poller->counted = counted;

		if (event_poller_open (poller, (int) (max_events ? max_events : EVENT_POLLER_DEFAULT_EVENTS))) {
			event_poller_delete (poller);
			poller = NULL;
		}
	}

	return poller;

}

EventPoller *event_poller_create (const unsigned int max_events) {

	return event_poller_create_actual (max_events, false);

}

EventPoller *event_poller_create_counted (const unsigned int max_events) {

	return event_poller_create_actual (max_events, true);

}

#pragma endregion

#pragma region sources

// errors and hang ups are always reported by epoll, they do not need to be asked
static uint32_t event_poller_interest_events (const unsigned int interest) {

	uint32_t events = 0;

	if (interest & EVENT_INTEREST_READ) events |= EPOLLIN;
	if (interest & EVENT_INTEREST_WRITE) events |= EPOLLOUT;
	if (interest & EVENT_INTEREST_ONCE) events |= EPOLLONESHOT;

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
			source->silent = false;
			source->poller = poller;
		}
	}

	return retval;

}

// a source that is reported once is silent after its report, so the same interest arms it again
static bool event_poller_modify_is_needed (const EventSource *source, const unsigned int interest) {

	return (source->interest != interest) || (interest & EVENT_INTEREST_ONCE);

}

unsigned int event_poller_modify (
	EventPoller *poller, EventSource *source, const unsigned int interest
) {

	unsigned int retval = 1;

	if (poller && source && source->registered) {
		retval = 0;

		if (event_poller_modify_is_needed (source, interest)) {
			retval = event_poller_control (source->poller, EPOLL_CTL_MOD, source, source->fd, interest);
			if (!retval) {
				source->interest = interest;
				source->silent = false;
			}
		}
	}

	return retval;

}

unsigned int event_poller_remove (EventPoller *poller, EventSource *source) {

	unsigned int retval = 1;

	if (poller && source) {
		retval = 0;

		if (source->registered) {
			retval = event_poller_control (
				source->poller, EPOLL_CTL_DEL, source, source->fd, EVENT_INTEREST_NONE
			);

			event_source_forget (source);
		}
	}

	return retval;

}

unsigned int event_poller_watch (
	EventPoller *poller, EventSource *source, const int fd, const unsigned int interest
) {

	unsigned int retval = 1;

	if (poller && source) {
		if (source->registered && (source->poller != poller)) (void) event_poller_remove (poller, source);

		retval = source->registered ?
			event_poller_modify (poller, source, interest) :
			event_poller_add (poller, source, fd, interest);
	}

	return retval;

}

void event_source_forget (EventSource *source) {

	if (source) {
		source->fd = -1;
		source->interest = EVENT_INTEREST_NONE;
		source->registered = false;
		source->silent = false;
		source->poller = NULL;
	}

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

// a wake up that is not consumed stays for the ones that wait on the poller
static int event_poller_collect (
	const EventPoller *poller, const struct epoll_event *events, const int n_events,
	EventReady *ready, const bool consume_wake
) {

	int n_ready = 0;

	for (int idx = 0; idx < n_events; idx++) {
		EventSource *source = (EventSource *) events[idx].data.ptr;

		if (source) {
			ready[n_ready].source = source;
			ready[n_ready].ready = event_poller_ready_flags (events[idx].events);
			n_ready += 1;
		}

		else if (consume_wake) {
			event_poller_wake_consume (poller);
		}
	}

	return n_ready;

}

// a signal is not an error of the poller, its loop decides what to do
static int event_poller_waited (
	const EventPoller *poller, const struct epoll_event *events, const int n_events,
	EventReady *ready, const bool consume_wake
) {

	int n_ready = -1;

	if (n_events >= 0) n_ready = event_poller_collect (poller, events, n_events, ready, consume_wake);
	else if (errno == EINTR) n_ready = 0;

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

		n_ready = event_poller_waited (poller, poller->events, n_events, ready, true);
	}

	return n_ready;

}

int event_poller_wait_one (const EventPoller *poller, EventReady *ready, const int timeout) {

	int result = EVENT_POLLER_WAITED_NOTHING;

	if (poller && ready) {
		struct epoll_event event;
		(void) memset (&event, 0, sizeof (struct epoll_event));

		// its only event is a source or the wake up
		if (epoll_wait (poller->epoll_fd, &event, 1, timeout) > 0) {
			EventSource *source = (EventSource *) event.data.ptr;

			if (source) {
				ready->source = source;
				ready->ready = event_poller_ready_flags (event.events);

				result = EVENT_POLLER_WAITED_SOURCE;
			}

			else {
				event_poller_wake_consume (poller);

				result = EVENT_POLLER_WAITED_WAKE;
			}
		}
	}

	return result;

}

int event_poller_take (const EventPoller *poller, EventReady *ready, const int max) {

	int n_ready = -1;

	if (poller && ready && (max > 0)) {
		struct epoll_event events[EVENT_POLLER_TAKE_EVENTS];
		(void) memset (events, 0, sizeof (events));

		const int n_events = epoll_wait (
			poller->epoll_fd, events, (max < EVENT_POLLER_TAKE_EVENTS) ? max : EVENT_POLLER_TAKE_EVENTS, 0
		);

		n_ready = event_poller_waited (poller, events, n_events, ready, false);
	}

	return n_ready;

}

static void event_poller_wake_write (const EventPoller *poller, const uint64_t value) {

	const int error = errno;

	// a full counter (EAGAIN) already wakes the poller up
	while (write (poller->wake_fd, &value, sizeof (value)) < 0) {
		if (errno != EINTR) break;
	}

	errno = error;

}

void event_poller_wake (const EventPoller *poller) {

	if (poller) event_poller_wake_write (poller, event_poller_wake_value);

}

void event_poller_wake_many (const EventPoller *poller, const unsigned int count) {

	if (poller && count) event_poller_wake_write (poller, (uint64_t) count);

}

#pragma endregion
