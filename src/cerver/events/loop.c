#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/connection/internal.h"
#include "cerver/core/cerver.h"
#include "cerver/core/data.h"
#include "cerver/events/internal.h"
#include "cerver/handler/internal.h"

#include "cerver/http/core/connection.h"

#include "cerver/utils/log.h"

#pragma region lifecycle

static void event_loop_recycled_delete (EventLoop *loop) {

	HttpConnection *connection = loop->recycled;
	while (connection) {
		HttpConnection *next = connection->list_next;

		http_connection_delete (connection);

		connection = next;
	}

	loop->recycled = NULL;
	loop->n_recycled = 0;

}

void event_loop_delete (EventLoop *loop) {

	if (loop) {
		event_loop_recycled_delete (loop);

		if (loop->listen_owned && (loop->listen_fd > -1)) (void) close (loop->listen_fd);

		event_poller_delete (loop->poller);

		free (loop->ready);
		free (loop->buffer);

		(void) pthread_mutex_destroy (&loop->completed_mutex);
		(void) pthread_mutex_destroy (&loop->recycled_mutex);

		free (loop);
	}

}

EventLoop *event_loop_create (struct _Cerver *cerver, EventDispatcher *dispatcher) {

	EventLoop *loop = (cerver && dispatcher) ? (EventLoop *) calloc (1, sizeof (EventLoop)) : NULL;
	if (loop) {
		loop->cerver = cerver;
		loop->dispatcher = dispatcher;

		loop->clock = event_clock_milliseconds;
		loop->now = loop->clock ();

		event_wheel_init (&loop->wheel, loop->now);

		event_source_init (&loop->listen, EVENT_SOURCE_TYPE_LISTEN, loop);
		loop->listen_fd = -1;

		loop->recycle_limit = EVENT_LOOP_RECYCLE_LIMIT;

		(void) pthread_mutex_init (&loop->completed_mutex, NULL);
		(void) pthread_mutex_init (&loop->recycled_mutex, NULL);

		loop->poller = event_poller_create (EVENT_POLLER_DEFAULT_EVENTS);

		loop->max_ready = EVENT_POLLER_DEFAULT_EVENTS;
		loop->ready = (EventReady *) calloc ((size_t) loop->max_ready, sizeof (EventReady));

		loop->buffer_size = EVENT_LOOP_BUFFER_SIZE;
		loop->buffer = (char *) malloc (loop->buffer_size);

		if (!loop->poller || !loop->ready || !loop->buffer) {
			event_loop_delete (loop);
			loop = NULL;
		}
	}

	return loop;

}

void event_loop_set_clock (EventLoop *loop, u64 (*clock) (void)) {

	if (loop && clock) {
		loop->clock = clock;
		loop->now = clock ();

		event_wheel_init (&loop->wheel, loop->now);
	}

}

#pragma endregion

#pragma region counters

unsigned int event_loop_pending (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_pending, __ATOMIC_ACQUIRE) : 0;

}

unsigned int event_loop_connections (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_connections, __ATOMIC_ACQUIRE) : 0;

}

unsigned int event_loop_parked (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_parked, __ATOMIC_ACQUIRE) : 0;

}

void event_loop_connection_pending (EventLoop *loop, const bool pending) {

	if (pending) (void) __atomic_add_fetch (&loop->n_pending, 1, __ATOMIC_RELEASE);
	else (void) __atomic_sub_fetch (&loop->n_pending, 1, __ATOMIC_RELEASE);

}

// the connections that count against the limit of the cerver: the ones of every loop of its group
static unsigned int event_loop_total_connections (const EventLoop *loop) {

	return loop->group ?
		__atomic_load_n (&loop->group->n_connections, __ATOMIC_ACQUIRE) :
		__atomic_load_n (&loop->n_connections, __ATOMIC_ACQUIRE);

}

static void event_loop_connections_add (EventLoop *loop) {

	(void) __atomic_add_fetch (&loop->n_accepted, 1, __ATOMIC_RELEASE);

	(void) __atomic_add_fetch (&loop->n_connections, 1, __ATOMIC_RELEASE);
	if (loop->group) (void) __atomic_add_fetch (&loop->group->n_connections, 1, __ATOMIC_RELEASE);

}

static void event_loop_connections_remove (EventLoop *loop) {

	(void) __atomic_add_fetch (&loop->n_ended, 1, __ATOMIC_RELEASE);

	(void) __atomic_sub_fetch (&loop->n_connections, 1, __ATOMIC_RELEASE);
	if (loop->group) (void) __atomic_sub_fetch (&loop->group->n_connections, 1, __ATOMIC_RELEASE);

}

static void event_loop_parked_add (EventLoop *loop) {

	(void) __atomic_add_fetch (&loop->n_parked, 1, __ATOMIC_RELEASE);
	if (loop->group) (void) __atomic_add_fetch (&loop->group->n_parked, 1, __ATOMIC_RELEASE);

}

static void event_loop_parked_remove (EventLoop *loop) {

	(void) __atomic_sub_fetch (&loop->n_parked, 1, __ATOMIC_RELEASE);
	if (loop->group) (void) __atomic_sub_fetch (&loop->group->n_parked, 1, __ATOMIC_RELEASE);

}

#pragma endregion

#pragma region connections

void event_loop_connection_attach (EventLoop *loop, struct HttpConnection *connection) {

	if (!connection->attached) {
		connection->active_previous = NULL;
		connection->active_next = loop->active;
		if (loop->active) loop->active->active_previous = connection;
		loop->active = connection;

		connection->attached = true;
	}

}

void event_loop_connection_detach (EventLoop *loop, struct HttpConnection *connection) {

	if (connection->attached) {
		if (connection->active_previous) connection->active_previous->active_next = connection->active_next;
		else loop->active = connection->active_next;

		if (connection->active_next) connection->active_next->active_previous = connection->active_previous;

		connection->active_next = NULL;
		connection->active_previous = NULL;

		connection->attached = false;
	}

}

static HttpConnection *event_loop_recycled_pop (EventLoop *loop) {

	(void) pthread_mutex_lock (&loop->recycled_mutex);

	HttpConnection *connection = loop->recycled;
	if (connection) {
		loop->recycled = connection->list_next;
		loop->n_recycled -= 1;

		connection->list_next = NULL;
	}

	(void) pthread_mutex_unlock (&loop->recycled_mutex);

	return connection;

}

// returns true when the connection was kept
static bool event_loop_recycled_push (EventLoop *loop, HttpConnection *connection) {

	(void) pthread_mutex_lock (&loop->recycled_mutex);

	const bool kept = (loop->n_recycled < loop->recycle_limit);
	if (kept) {
		connection->list_next = loop->recycled;
		loop->recycled = connection;
		loop->n_recycled += 1;
	}

	(void) pthread_mutex_unlock (&loop->recycled_mutex);

	return kept;

}

struct HttpConnection *event_loop_connection_take (EventLoop *loop) {

	HttpConnection *connection = event_loop_recycled_pop (loop);
	if (!connection) connection = http_connection_new ();

	if (connection) {
		connection->loop = loop;

		event_loop_connection_attach (loop, connection);

		event_loop_connections_add (loop);
	}

	return connection;

}

// a connection whose arena could not be created again is not kept
// the thread that ends the last connection of a loop that is stopping wakes it up:
// the loop waits in its poller for nothing else
void event_loop_connection_release (EventLoop *loop, struct HttpConnection *connection) {

	if (!connection->arena || !event_loop_recycled_push (loop, connection)) {
		http_connection_delete (connection);
	}

	event_loop_connections_remove (loop);

	if (__atomic_load_n (&loop->stopping, __ATOMIC_ACQUIRE)) event_poller_wake (loop->poller);

}

#pragma endregion

#pragma region parked

void event_loop_park (EventLoop *loop, struct HttpConnection *connection) {

	connection->parked = true;
	connection->list_next = NULL;

	if (loop->parked_tail) loop->parked_tail->list_next = connection;
	else loop->parked_head = connection;

	loop->parked_tail = connection;

	event_loop_parked_add (loop);

}

// a connection that found no place when its turn came goes back to the front, it keeps its turn
static void event_loop_park_front (EventLoop *loop, HttpConnection *connection) {

	connection->parked = true;
	connection->list_next = loop->parked_head;

	loop->parked_head = connection;
	if (!loop->parked_tail) loop->parked_tail = connection;

	event_loop_parked_add (loop);

}

void event_loop_unpark (EventLoop *loop, struct HttpConnection *connection) {

	if (connection->parked) {
		HttpConnection *previous = NULL;
		HttpConnection *current = loop->parked_head;
		while (current && (current != connection)) {
			previous = current;
			current = current->list_next;
		}

		if (current) {
			if (previous) previous->list_next = connection->list_next;
			else loop->parked_head = connection->list_next;

			if (loop->parked_tail == connection) loop->parked_tail = previous;

			event_loop_parked_remove (loop);
		}

		connection->list_next = NULL;
		connection->parked = false;
	}

}

// the parked connections take the places of the dispatcher in the order that they arrived
static void event_loop_parked_retry (EventLoop *loop) {

	EventDispatcher *dispatcher = loop->dispatcher;

	bool has_place = true;

	while (loop->parked_head && has_place && !dispatcher->is_full (dispatcher)) {
		HttpConnection *connection = loop->parked_head;

		event_loop_unpark (loop, connection);

		// another loop took the place first
		if (http_connection_unpark (connection)) {
			event_loop_park_front (loop, connection);

			has_place = false;
		}
	}

}

// wakes the loop up only when it waits in its poller, or is about to:
// a loop that is running reaches the end of its step by itself, where it gives its parked connections
// the places of the dispatcher
static void event_loop_wake_waiting (EventLoop *loop) {

	(void) pthread_mutex_lock (&loop->completed_mutex);

	const bool wake = loop->waiting;
	loop->waiting = false;

	(void) pthread_mutex_unlock (&loop->completed_mutex);

	if (wake) event_poller_wake (loop->poller);

}

void event_loop_space (EventLoop *loop) {

	const EventLoopGroup *group = loop->group;

	if (!group) {
		if (__atomic_load_n (&loop->n_parked, __ATOMIC_ACQUIRE)) event_loop_wake_waiting (loop);
	}

	else if (__atomic_load_n (&group->n_parked, __ATOMIC_ACQUIRE)) {
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			EventLoop *other = group->loops[idx];
			if (__atomic_load_n (&other->n_parked, __ATOMIC_ACQUIRE)) event_loop_wake_waiting (other);
		}
	}

}

#pragma endregion

#pragma region completed

// the loop is woken up only when it waits: it takes the whole queue in every step
void event_loop_complete (EventLoop *loop, struct HttpConnection *connection) {

	connection->list_next = NULL;

	(void) pthread_mutex_lock (&loop->completed_mutex);

	if (loop->completed_tail) loop->completed_tail->list_next = connection;
	else loop->completed_head = connection;

	loop->completed_tail = connection;

	const bool wake = loop->waiting;
	loop->waiting = false;

	(void) pthread_mutex_unlock (&loop->completed_mutex);

	if (wake) event_poller_wake (loop->poller);

}

static HttpConnection *event_loop_completed_take (EventLoop *loop) {

	(void) pthread_mutex_lock (&loop->completed_mutex);

	HttpConnection *completed = loop->completed_head;

	loop->completed_head = NULL;
	loop->completed_tail = NULL;

	loop->waiting = false;

	(void) pthread_mutex_unlock (&loop->completed_mutex);

	return completed;

}

static int event_loop_completed_handle (EventLoop *loop) {

	int handled = 0;

	HttpConnection *connection = event_loop_completed_take (loop);
	while (connection) {
		HttpConnection *next = connection->list_next;
		connection->list_next = NULL;

		http_connection_completed (connection);

		handled += 1;

		connection = next;
	}

	return handled;

}

// the loop tells that it is going to wait only when there is nothing to take,
// under the mutex of the queue: a connection that completes after it wakes the poller up
// returns true when the loop can wait
static bool event_loop_completed_wait (EventLoop *loop) {

	(void) pthread_mutex_lock (&loop->completed_mutex);

	const bool empty = (loop->completed_head == NULL);
	loop->waiting = empty;

	(void) pthread_mutex_unlock (&loop->completed_mutex);

	return empty;

}

#pragma endregion

#pragma region accept

static bool event_loop_is_stopping (const EventLoop *loop) {

	return __atomic_load_n (&loop->stopping, __ATOMIC_ACQUIRE);

}

// stops accepting for a while: the listening socket leaves the poller (it would be reported in every step)
// and a timer brings it back
static void event_loop_accept_pause (EventLoop *loop) {

	(void) event_poller_remove (loop->poller, &loop->listen);

	event_wheel_add (&loop->wheel, &loop->accept_timer, loop->now + EVENT_LOOP_ACCEPT_BACKOFF);

}

static void event_loop_accept_resume (EventLoop *loop) {

	if (!event_loop_is_stopping (loop) && (loop->listen_fd > -1) && !loop->listen.registered) {
		if (event_poller_add (loop->poller, &loop->listen, loop->listen_fd, EVENT_INTEREST_READ)) {
			event_loop_accept_pause (loop);
		}
	}

}

// a loop that stops does not accept anymore
static void event_loop_accept_end (EventLoop *loop) {

	(void) event_poller_remove (loop->poller, &loop->listen);

	event_wheel_remove (&loop->wheel, &loop->accept_timer);

	if (loop->listen_owned && (loop->listen_fd > -1)) (void) close (loop->listen_fd);

	loop->listen_fd = -1;

}

unsigned int event_loop_listen (EventLoop *loop, const int listen_fd, const bool owned) {

	unsigned int retval = 1;

	if (loop && (listen_fd > -1) && (loop->listen_fd < 0)) {
		retval = event_poller_add (loop->poller, &loop->listen, listen_fd, EVENT_INTEREST_READ);
		if (!retval) {
			loop->listen_fd = listen_fd;
			loop->listen_owned = owned;

			cerver_accept_reserve_fd_open ();
		}
	}

	return retval;

}

unsigned int event_loop_adopt (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	if (loop && (fd > -1) && address) {
		Connection *peer = connection_create (fd, address, loop->cerver->protocol);
		if (peer) {
			connection_set_send_timeout (peer, loop->cerver->send_timeout);

			connection_set_active (peer, true);

			retval = http_connection_accepted (loop, peer);
		}

		else {
			cerver_log_error ("event_loop_adopt () - failed to create the connection of fd %d", fd);

			(void) close (fd);
		}
	}

	return retval;

}

static void event_loop_accept_rejected (EventLoop *loop, const int error) {

	CERVER_STATS_ADD (loop->cerver->stats->total_rejected_connections, 1);

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_ERROR, LOG_TYPE_CERVER,
		"event_loop_accept () - cerver %s is out of fds - errno %d (%s) - rejected a connection",
		loop->cerver->info->name, error,
		strerror_r (error, error_description, sizeof (error_description))
	);

}

// without fds the pending connection is accepted with the reserve fd and closed
// returns true when a connection waited: accept4 () also fails without fds when none does
static bool event_loop_accept_out_of_fds (EventLoop *loop, const int error) {

	const bool rejected = cerver_accept_reserve_fd_reject (loop->listen_fd);

	if (rejected) event_loop_accept_rejected (loop, error);

	return rejected;

}

static void event_loop_accept_failed_unknown (EventLoop *loop, const int error) {

	// the listening socket of a cerver that stops was shut down
	if (!event_loop_is_stopping (loop) && cerver_is_running (loop->cerver)) {
		char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
		cerver_log_with_date (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"event_loop_accept () - accept4 () failed in cerver %s - errno %d (%s)",
			loop->cerver->info->name, error,
			strerror_r (error, error_description, sizeof (error_description))
		);
	}

}

// returns true when the loop can accept the next connection;
// pause tells that it has to stop accepting for a while when its batch ends:
// the listening socket would be reported in every step
static bool event_loop_accept_failed (EventLoop *loop, const int error, bool *pause) {

	bool again = false;

	switch (error) {
		// a signal, or a client that went away before its connection was accepted
		case EINTR:
		case ECONNABORTED:
		case EPROTO:
			again = true;
			break;

		// no more connections wait
		case EAGAIN:
		#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
		#endif
			break;

		// the loop keeps rejecting the connections that wait, to the end of its batch:
		// with one for each pause the rest waited in the listen backlog for seconds
		case EMFILE:
		case ENFILE:
			again = event_loop_accept_out_of_fds (loop, error);
			*pause = true;
			break;

		case ENOBUFS:
		case ENOMEM:
			(void) event_loop_accept_out_of_fds (loop, error);
			*pause = true;
			break;

		default:
			event_loop_accept_failed_unknown (loop, error);
			*pause = true;
			break;
	}

	return again;

}

// at the limit of connections of the cerver the loop stops accepting for a while,
// the new connections wait in the listen backlog
static bool event_loop_is_at_limit (const EventLoop *loop) {

	return event_loop_total_connections (loop) >= loop->cerver->max_connections;

}

// accepts the connections that wait, some at a time: the loop also has to handle the ones that it has
static void event_loop_accept (EventLoop *loop) {

	bool again = !event_loop_is_stopping (loop);
	bool pause = false;

	for (unsigned int count = 0; again && (count < EVENT_LOOP_ACCEPT_BATCH); count++) {
		if (event_loop_is_at_limit (loop)) {
			pause = true;
			again = false;
		}

		else {
			struct sockaddr_storage address;
			(void) memset (&address, 0, sizeof (struct sockaddr_storage));
			socklen_t address_length = sizeof (struct sockaddr_storage);

			const int fd = accept4 (
				loop->listen_fd, (struct sockaddr *) &address, &address_length,
				SOCK_NONBLOCK | SOCK_CLOEXEC
			);

			if (fd > -1) (void) event_loop_adopt (loop, fd, &address);
			else again = event_loop_accept_failed (loop, errno, &pause);
		}
	}

	if (pause) event_loop_accept_pause (loop);

}

#pragma endregion

#pragma region stop

void event_loop_wake (const EventLoop *loop) {

	if (loop) event_poller_wake (loop->poller);

}

void event_loop_stop (EventLoop *loop, const bool reject) {

	if (loop) {
		if (reject) __atomic_store_n (&loop->rejecting, true, __ATOMIC_RELEASE);

		__atomic_store_n (&loop->stopping, true, __ATOMIC_RELEASE);

		event_poller_wake (loop->poller);
	}

}

void event_loop_finish (EventLoop *loop) {

	if (loop) {
		__atomic_store_n (&loop->stopping, true, __ATOMIC_RELEASE);
		__atomic_store_n (&loop->finishing, true, __ATOMIC_RELEASE);

		event_poller_wake (loop->poller);
	}

}

// a loop that rejects ends what it owns: the requests that have not gone to the pool are answered with 503
// (the ones that come back from the pool later are answered when they are submitted again)
static void event_loop_reject_pending (EventLoop *loop) {

	HttpConnection *connection = loop->active;
	while (connection) {
		HttpConnection *next = connection->active_next;

		http_connection_abort (connection);

		connection = next;
	}

}

static void event_loop_stop_check (EventLoop *loop) {

	if (event_loop_is_stopping (loop)) {
		if (loop->listen_fd > -1) event_loop_accept_end (loop);

		if (__atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE)) event_loop_reject_pending (loop);
	}

}

#pragma endregion

#pragma region step

static void event_loop_timer_expired (EventTimer *timer, void *loop_ptr) {

	EventLoop *loop = (EventLoop *) loop_ptr;

	if (timer == &loop->accept_timer) event_loop_accept_resume (loop);
	else http_connection_timeout ((HttpConnection *) timer->owner);

}

static void event_loop_ready_handle (EventLoop *loop, const EventReady *ready) {

	switch (ready->source->type) {
		case EVENT_SOURCE_TYPE_LISTEN:
			event_loop_accept (loop);
			break;

		case EVENT_SOURCE_TYPE_CONNECTION:
			http_connection_ready ((HttpConnection *) ready->source->owner, ready->ready);
			break;

		default: break;
	}

}

// a place of the dispatcher that was freed after the loop gave its parked connections their places:
// the loop already told that it waits, so a place that is freed from now on wakes it up
static bool event_loop_parked_has_place (const EventLoop *loop) {

	return loop->parked_head && !loop->dispatcher->is_full (loop->dispatcher);

}

// how long the poller waits: nothing when a connection completed or a parked one has a place,
// and never past the next tick of the timers
static int event_loop_wait_time (EventLoop *loop, const int timeout) {

	int wait = 0;

	if (event_loop_completed_wait (loop) && !event_loop_parked_has_place (loop)) {
		wait = timeout;

		const int next = event_wheel_next (&loop->wheel, loop->now);
		if ((next != EVENT_WHEEL_NEXT_NONE) && ((wait < 0) || (next < wait))) wait = next;
	}

	return wait;

}

int event_loop_step (EventLoop *loop, const int timeout) {

	int handled = -1;

	if (loop) {
		loop->now = loop->clock ();

		const int n_ready = event_poller_wait (
			loop->poller, loop->ready, loop->max_ready, event_loop_wait_time (loop, timeout)
		);

		loop->now = loop->clock ();

		handled = (n_ready > 0) ? n_ready : 0;

		for (int idx = 0; idx < n_ready; idx++) {
			event_loop_ready_handle (loop, &loop->ready[idx]);
		}

		handled += event_loop_completed_handle (loop);

		handled += (int) event_wheel_advance (&loop->wheel, loop->now, event_loop_timer_expired, loop);

		event_loop_stop_check (loop);

		event_loop_parked_retry (loop);

		if (n_ready < 0) handled = -1;
	}

	return handled;

}

static bool event_loop_is_finished (const EventLoop *loop) {

	return __atomic_load_n (&loop->finishing, __ATOMIC_ACQUIRE)
		&& !__atomic_load_n (&loop->n_connections, __ATOMIC_ACQUIRE);

}

void event_loop_run (EventLoop *loop) {

	if (loop) {
		while (!event_loop_is_finished (loop)) {
			(void) event_loop_step (loop, EVENT_POLLER_WAIT_FOREVER);
		}

		__atomic_store_n (&loop->ended, true, __ATOMIC_RELEASE);
	}

}

#pragma endregion
