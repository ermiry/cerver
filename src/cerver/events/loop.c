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

static void event_loop_connections_delete (HttpConnection *connection) {

	while (connection) {
		HttpConnection *next = connection->list_next;

		http_connection_delete (connection);

		connection = next;
	}

}

static void event_loop_recycled_delete (EventLoop *loop) {

	event_loop_connections_delete (loop->recycled);

	loop->recycled = NULL;
	loop->n_recycled = 0;

	event_loop_connections_delete (loop->stripped);

	loop->stripped = NULL;

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
		(void) pthread_mutex_destroy (&loop->idle_mutex);

		free (loop);
	}

}

EventLoop *event_loop_create (struct _Cerver *cerver, EventDispatcher *dispatcher) {

	EventLoop *loop = (cerver && dispatcher) ? (EventLoop *) calloc (1, sizeof (EventLoop)) : NULL;
	if (loop) {
		loop->cerver = cerver;

		loop->lanes[EVENT_LOOP_LANE_DEFAULT].dispatcher = dispatcher;
		loop->n_lanes = EVENT_LOOP_LANE_DEFAULT + 1;

		loop->clock = event_clock_milliseconds;
		loop->now = loop->clock ();

		event_wheel_init (&loop->wheel, loop->now);

		event_source_init (&loop->listen, EVENT_SOURCE_TYPE_LISTEN, loop);
		loop->listen_fd = -1;

		loop->recycle_limit = EVENT_LOOP_RECYCLE_LIMIT;

		(void) pthread_mutex_init (&loop->completed_mutex, NULL);
		(void) pthread_mutex_init (&loop->recycled_mutex, NULL);
		(void) pthread_mutex_init (&loop->idle_mutex, NULL);

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

unsigned int event_loop_lane_set (
	EventLoop *loop, const unsigned int lane, EventDispatcher *dispatcher
) {

	unsigned int retval = 1;

	if (loop && dispatcher && (lane < EVENT_LOOP_LANES_SIZE)) {
		loop->lanes[lane].dispatcher = dispatcher;

		if (lane >= loop->n_lanes) loop->n_lanes = lane + 1;

		retval = 0;
	}

	return retval;

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

unsigned int event_loop_lane_parked (const EventLoop *loop, const unsigned int lane) {

	return (loop && (lane < EVENT_LOOP_LANES_SIZE)) ?
		__atomic_load_n (&loop->lanes[lane].n_parked, __ATOMIC_ACQUIRE) : 0;

}

unsigned int event_loop_idle (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_idle, __ATOMIC_ACQUIRE) : 0;

}

u64 event_loop_idle_closed_total (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_idle_closed, __ATOMIC_ACQUIRE) : 0;

}

void event_loop_idle_closed (EventLoop *loop) {

	(void) __atomic_add_fetch (&loop->n_idle_closed, 1, __ATOMIC_RELEASE);

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

static void event_loop_group_parked_add (EventLoopGroup *group, const unsigned int lane) {

	(void) __atomic_add_fetch (&group->n_parked, 1, __ATOMIC_RELEASE);
	(void) __atomic_add_fetch (&group->lanes_parked[lane], 1, __ATOMIC_RELEASE);

}

static void event_loop_group_parked_remove (EventLoopGroup *group, const unsigned int lane) {

	(void) __atomic_sub_fetch (&group->n_parked, 1, __ATOMIC_RELEASE);
	(void) __atomic_sub_fetch (&group->lanes_parked[lane], 1, __ATOMIC_RELEASE);

}

static void event_loop_parked_add (EventLoop *loop, const unsigned int lane) {

	(void) __atomic_add_fetch (&loop->n_parked, 1, __ATOMIC_RELEASE);
	(void) __atomic_add_fetch (&loop->lanes[lane].n_parked, 1, __ATOMIC_RELEASE);

	if (loop->group) event_loop_group_parked_add (loop->group, lane);

}

static void event_loop_parked_remove (EventLoop *loop, const unsigned int lane) {

	(void) __atomic_sub_fetch (&loop->n_parked, 1, __ATOMIC_RELEASE);
	(void) __atomic_sub_fetch (&loop->lanes[lane].n_parked, 1, __ATOMIC_RELEASE);

	if (loop->group) event_loop_group_parked_remove (loop->group, lane);

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

// with the mutex of the recycled connections
static HttpConnection *event_loop_stripped_pop (EventLoop *loop) {

	HttpConnection *connection = loop->stripped;
	if (connection) {
		loop->stripped = connection->list_next;

		connection->list_next = NULL;
	}

	return connection;

}

// a recycled connection, or one that was stripped with its arena and its buffers created again
static HttpConnection *event_loop_recycled_pop (EventLoop *loop) {

	(void) pthread_mutex_lock (&loop->recycled_mutex);

	HttpConnection *connection = loop->recycled;
	if (connection) {
		loop->recycled = connection->list_next;
		loop->n_recycled -= 1;

		connection->list_next = NULL;
	}

	else {
		connection = event_loop_stripped_pop (loop);
	}

	(void) pthread_mutex_unlock (&loop->recycled_mutex);

	if (connection && http_connection_restore (connection)) {
		http_connection_strip (connection);

		(void) pthread_mutex_lock (&loop->recycled_mutex);
		connection->list_next = loop->stripped;
		loop->stripped = connection;
		(void) pthread_mutex_unlock (&loop->recycled_mutex);

		connection = NULL;
	}

	return connection;

}

// a connection that the recycled ones do not keep is stripped of its arena and its buffers, its memory stays:
// a thread of a pool that waits for the idle connections can still have it
static void event_loop_recycled_push (EventLoop *loop, HttpConnection *connection) {

	(void) pthread_mutex_lock (&loop->recycled_mutex);

	if (connection->arena && (loop->n_recycled < loop->recycle_limit)) {
		connection->list_next = loop->recycled;
		loop->recycled = connection;
		loop->n_recycled += 1;
	}

	else {
		http_connection_strip (connection);

		connection->list_next = loop->stripped;
		loop->stripped = connection;
	}

	(void) pthread_mutex_unlock (&loop->recycled_mutex);

}

struct HttpConnection *event_loop_connection_take (EventLoop *loop) {

	HttpConnection *connection = event_loop_recycled_pop (loop);
	if (!connection) connection = http_connection_new ();

	if (connection) {
		// a thread that still has a connection that ended reads its loop: it is only written once
		if (connection->loop != loop) connection->loop = loop;

		event_loop_connection_attach (loop, connection);

		event_loop_connections_add (loop);
	}

	return connection;

}

// a connection whose arena could not be created again is stripped like one that the recycled ones do not keep
// the thread that ends the last connection of a loop that is stopping wakes it up:
// the loop waits in its poller for nothing else
void event_loop_connection_release (EventLoop *loop, struct HttpConnection *connection) {

	event_loop_recycled_push (loop, connection);

	event_loop_connections_remove (loop);

	if (__atomic_load_n (&loop->stopping, __ATOMIC_ACQUIRE)) event_poller_wake (loop->poller);

}

#pragma endregion

#pragma region idle

// the idle connections are kept in the order that they became idle: the first one is the one that has waited
// the longest for its next request, and the one whose time ends first (they all wait the same time)
// the list has its mutex: the thread of the pool that answered a request adds its connection by itself,
// and whoever takes a connection out of the list owns it

// with the mutex of the idle connections
static void event_loop_idle_link (EventLoop *loop, HttpConnection *connection) {

	connection->idle_next = NULL;
	connection->idle_previous = loop->idle_tail;

	if (loop->idle_tail) loop->idle_tail->idle_next = connection;
	else loop->idle_head = connection;

	loop->idle_tail = connection;

	connection->idle = true;

	(void) __atomic_add_fetch (&loop->n_idle, 1, __ATOMIC_RELEASE);

}

// with the mutex of the idle connections
static void event_loop_idle_unlink (EventLoop *loop, HttpConnection *connection) {

	if (connection->idle_previous) connection->idle_previous->idle_next = connection->idle_next;
	else loop->idle_head = connection->idle_next;

	if (connection->idle_next) connection->idle_next->idle_previous = connection->idle_previous;
	else loop->idle_tail = connection->idle_previous;

	connection->idle_next = NULL;
	connection->idle_previous = NULL;

	connection->idle = false;

	(void) __atomic_sub_fetch (&loop->n_idle, 1, __ATOMIC_RELEASE);

}

// where the socket of an idle connection waits: in the poller of the pool of its last request when the threads of
// that pool wait for it (events/idle.c), in the one of its loop otherwise
static EventPoller *event_loop_idle_poller (const EventLoop *loop, const HttpConnection *connection) {

	const EventIdle *idle = (connection->lane < loop->n_lanes) ? loop->lanes[connection->lane].idle : NULL;

	return idle ? idle->poller : loop->poller;

}

// the socket of an idle connection is reported once: the loop gives a connection that was reported
// to the pool without a system call, and the thread that gives it back arms its socket again
static unsigned int event_loop_idle_arm (EventLoop *loop, HttpConnection *connection) {

	return event_poller_watch (
		event_loop_idle_poller (loop, connection), &connection->source,
		connection->peer->socket->sock_fd, EVENT_LOOP_IDLE_INTEREST
	);

}

// the socket is armed with the mutex: the loop can only be told about the connection
// (and take it) once it is one of its idle ones
unsigned int event_loop_idle_enter (
	EventLoop *loop, struct HttpConnection *connection, const u64 timeout
) {

	(void) pthread_mutex_lock (&loop->idle_mutex);

	connection->idle_deadline = loop->clock () + timeout;

	event_loop_idle_link (loop, connection);

	const unsigned int retval = event_loop_idle_arm (loop, connection);
	if (retval) event_loop_idle_unlink (loop, connection);

	(void) pthread_mutex_unlock (&loop->idle_mutex);

	// the loop is woken up only for the first idle connection, to start checking them
	if (!retval && !__atomic_exchange_n (&loop->idle_watching, true, __ATOMIC_ACQ_REL)) {
		event_poller_wake (loop->poller);
	}

	return retval;

}

bool event_loop_idle_claim (EventLoop *loop, struct HttpConnection *connection) {

	(void) pthread_mutex_lock (&loop->idle_mutex);

	const bool claimed = connection->idle;
	if (claimed) event_loop_idle_unlink (loop, connection);

	(void) pthread_mutex_unlock (&loop->idle_mutex);

	return claimed;

}

// takes the connection that has been idle the longest out of the idle ones: the loop owns it again
// with expired, only when the time that it can wait ended
static HttpConnection *event_loop_idle_take (EventLoop *loop, const bool expired) {

	(void) pthread_mutex_lock (&loop->idle_mutex);

	HttpConnection *connection = loop->idle_head;
	if (connection && expired && (connection->idle_deadline > loop->now)) connection = NULL;

	if (connection) event_loop_idle_unlink (loop, connection);

	(void) pthread_mutex_unlock (&loop->idle_mutex);

	if (connection) event_loop_connection_attach (loop, connection);

	return connection;

}

// the idle connections are checked every few milliseconds while the loop has them: the thread of the pool
// that adds one does not wake the loop up, so the loop can not arm a timer for each one
static void event_loop_idle_watch (EventLoop *loop) {

	if (
		__atomic_load_n (&loop->idle_watching, __ATOMIC_ACQUIRE)
		&& !loop->idle_timer.armed
	) {
		event_wheel_add (&loop->wheel, &loop->idle_timer, loop->now + EVENT_LOOP_IDLE_SWEEP);
	}

}

// the loop stops checking its idle connections when it has none: the one that is added
// after the loop said so wakes it up, and one that was added right before is seen here
static void event_loop_idle_rest (EventLoop *loop) {

	if (!event_loop_idle (loop)) {
		__atomic_store_n (&loop->idle_watching, false, __ATOMIC_RELEASE);

		if (event_loop_idle (loop)) __atomic_store_n (&loop->idle_watching, true, __ATOMIC_RELEASE);
	}

}

// the idle connections whose socket is ready in the poller of a pool whose threads are all busy
// are read by their loops, so their requests wait for a place in the pool like any other
static void event_loop_idle_rescue (EventLoop *loop) {

	for (unsigned int idx = 0; idx < loop->n_lanes; idx++) {
		if (loop->lanes[idx].idle) event_idle_rescue (loop->lanes[idx].idle, loop);
	}

}

// ends the idle connections whose time ended
static void event_loop_idle_sweep (EventLoop *loop) {

	HttpConnection *connection = NULL;
	while ((connection = event_loop_idle_take (loop, true))) {
		http_connection_idle_expired (connection);
	}

	event_loop_idle_rescue (loop);

	event_loop_idle_rest (loop);

}

// at the limit of connections of the cerver, the one that has been idle the longest gives its place to a new one
static void event_loop_idle_evict (EventLoop *loop) {

	HttpConnection *connection = event_loop_idle_take (loop, false);
	if (connection) http_connection_evict (connection);

}

// a loop that stops does not keep the connections that wait for their next request:
// each one is closed once its client closes it too, a proxy takes it out of the ones that it reuses
static void event_loop_idle_end (EventLoop *loop) {

	HttpConnection *connection = NULL;
	while ((connection = event_loop_idle_take (loop, false))) {
		http_connection_abort (connection);
	}

}

#pragma endregion

#pragma region parked

void event_loop_park (EventLoop *loop, struct HttpConnection *connection) {

	EventLane *lane = &loop->lanes[connection->lane];

	connection->parked = true;
	connection->list_next = NULL;

	if (lane->parked_tail) lane->parked_tail->list_next = connection;
	else lane->parked_head = connection;

	lane->parked_tail = connection;

	event_loop_parked_add (loop, connection->lane);

}

// a connection that found no place when its turn came goes back to the front, it keeps its turn
static void event_loop_park_front (EventLoop *loop, HttpConnection *connection) {

	EventLane *lane = &loop->lanes[connection->lane];

	connection->parked = true;
	connection->list_next = lane->parked_head;

	lane->parked_head = connection;
	if (!lane->parked_tail) lane->parked_tail = connection;

	event_loop_parked_add (loop, connection->lane);

}

// takes the connection out of the parked ones of its lane, returns whether it was there
static bool event_loop_lane_remove (EventLane *lane, const HttpConnection *connection) {

	HttpConnection *previous = NULL;
	HttpConnection *current = lane->parked_head;
	while (current && (current != connection)) {
		previous = current;
		current = current->list_next;
	}

	if (current) {
		if (previous) previous->list_next = connection->list_next;
		else lane->parked_head = connection->list_next;

		if (lane->parked_tail == connection) lane->parked_tail = previous;
	}

	return current != NULL;

}

void event_loop_unpark (EventLoop *loop, struct HttpConnection *connection) {

	if (connection->parked) {
		if (event_loop_lane_remove (&loop->lanes[connection->lane], connection)) {
			event_loop_parked_remove (loop, connection->lane);
		}

		connection->list_next = NULL;
		connection->parked = false;
	}

}

bool event_loop_lane_has_parked (const EventLoop *loop, const unsigned int lane) {

	return loop->lanes[lane].parked_head != NULL;

}

// the parked connections of a lane take the places of its dispatcher in the order that they arrived
static void event_loop_lane_retry (EventLoop *loop, EventLane *lane) {

	EventDispatcher *dispatcher = lane->dispatcher;

	bool has_place = true;

	while (lane->parked_head && has_place && !dispatcher->is_full (dispatcher)) {
		HttpConnection *connection = lane->parked_head;

		event_loop_unpark (loop, connection);

		// another loop took the place first
		if (http_connection_unpark (connection)) {
			event_loop_park_front (loop, connection);

			has_place = false;
		}
	}

}

// each lane has its own turn: a pool that is full does not hold the requests of the others
static void event_loop_parked_retry (EventLoop *loop) {

	for (unsigned int idx = 0; idx < loop->n_lanes; idx++) {
		if (loop->lanes[idx].parked_head) event_loop_lane_retry (loop, &loop->lanes[idx]);
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

// only the loops with connections that wait for that pool: the place is of no use to the others
static void event_loop_space_wake (EventLoop *loop, const unsigned int lane) {

	if (__atomic_load_n (&loop->lanes[lane].n_parked, __ATOMIC_ACQUIRE)) event_loop_wake_waiting (loop);

}

void event_loop_space (EventLoop *loop, const unsigned int lane) {

	const EventLoopGroup *group = loop->group;

	if (!group) {
		event_loop_space_wake (loop, lane);
	}

	else if (__atomic_load_n (&group->lanes_parked[lane], __ATOMIC_ACQUIRE)) {
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			event_loop_space_wake (group->loops[idx], lane);
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
// and the loop brings it back when its time comes, a tick of its timers would be too long
static void event_loop_accept_pause (EventLoop *loop) {

	(void) event_poller_remove (loop->poller, &loop->listen);

	loop->accept_resume = loop->now + EVENT_LOOP_ACCEPT_BACKOFF;

}

static void event_loop_accept_resume (EventLoop *loop) {

	loop->accept_resume = 0;

	if (!event_loop_is_stopping (loop) && (loop->listen_fd > -1) && !loop->listen.registered) {
		if (event_poller_add (loop->poller, &loop->listen, loop->listen_fd, EVENT_INTEREST_READ)) {
			event_loop_accept_pause (loop);
		}
	}

}

// the pause of the accept ended
static void event_loop_accept_check (EventLoop *loop) {

	if (loop->accept_resume && (loop->now >= loop->accept_resume)) event_loop_accept_resume (loop);

}

// a loop that stops does not accept anymore
static void event_loop_accept_end (EventLoop *loop) {

	(void) event_poller_remove (loop->poller, &loop->listen);

	loop->accept_resume = 0;

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

static bool event_loop_is_at_limit (const EventLoop *loop) {

	return event_loop_total_connections (loop) >= loop->cerver->max_connections;

}

// at the limit of connections of the cerver, a new connection can only take the place of a connection
// of the loop that waits idle for its next request: the idle ones never keep new clients out
static bool event_loop_accept_has_place (const EventLoop *loop, const bool at_limit) {

	return !at_limit || event_loop_idle (loop);

}

// accepts the next connection that waits; at the limit of connections, the connection of the loop that has
// waited the longest for its next request is closed to give its place to it, once the new one was accepted:
// an idle connection is never closed for a client that is not there
// returns true when the loop can accept another connection, pause is the one of event_loop_accept_failed ()
static bool event_loop_accept_next (EventLoop *loop, const bool at_limit, bool *pause) {

	bool again = true;

	struct sockaddr_storage address;
	(void) memset (&address, 0, sizeof (struct sockaddr_storage));
	socklen_t address_length = sizeof (struct sockaddr_storage);

	const int fd = accept4 (
		loop->listen_fd, (struct sockaddr *) &address, &address_length,
		SOCK_NONBLOCK | SOCK_CLOEXEC
	);

	if (fd > -1) {
		if (at_limit) event_loop_idle_evict (loop);

		(void) event_loop_adopt (loop, fd, &address);
	}

	else {
		again = event_loop_accept_failed (loop, errno, pause);
	}

	return again;

}

// accepts the connections that wait, some at a time: the loop also has to handle the ones that it has
// without a place for a new one the loop stops accepting for a while, the new connections wait in the listen backlog
static void event_loop_accept (EventLoop *loop) {

	bool again = !event_loop_is_stopping (loop);
	bool pause = false;

	for (unsigned int count = 0; again && (count < EVENT_LOOP_ACCEPT_BATCH); count++) {
		const bool at_limit = event_loop_is_at_limit (loop);

		if (event_loop_accept_has_place (loop, at_limit)) {
			again = event_loop_accept_next (loop, at_limit, &pause);
		}

		else {
			pause = true;
			again = false;
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

		event_loop_idle_end (loop);

		if (__atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE)) event_loop_reject_pending (loop);
	}

}

#pragma endregion

#pragma region step

static void event_loop_timer_expired (EventTimer *timer, void *loop_ptr) {

	EventLoop *loop = (EventLoop *) loop_ptr;

	if (timer == &loop->idle_timer) event_loop_idle_sweep (loop);
	else http_connection_timeout ((HttpConnection *) timer->owner);

}

// the connections that were reported are handled before the new ones are accepted: a new connection
// at the limit closes an idle one, and its memory can be the one of the new connection in the same step,
// so nothing that was reported before is left for a connection that ended
static void event_loop_ready_handle (EventLoop *loop, const int n_ready) {

	bool accept = false;

	for (int idx = 0; idx < n_ready; idx++) {
		const EventReady *ready = &loop->ready[idx];

		if (ready->source->type == EVENT_SOURCE_TYPE_CONNECTION) {
			http_connection_ready ((HttpConnection *) ready->source->owner, ready->ready);
		}

		else if (ready->source->type == EVENT_SOURCE_TYPE_LISTEN) {
			accept = true;
		}
	}

	if (accept) event_loop_accept (loop);

}

// a place of the dispatcher that was freed after the loop gave its parked connections their places:
// the loop already told that it waits, so a place that is freed from now on wakes it up
static bool event_loop_lane_has_place (const EventLane *lane) {

	return lane->parked_head && !lane->dispatcher->is_full (lane->dispatcher);

}

static bool event_loop_parked_has_place (const EventLoop *loop) {

	bool has_place = false;

	for (unsigned int idx = 0; (idx < loop->n_lanes) && !has_place; idx++) {
		has_place = event_loop_lane_has_place (&loop->lanes[idx]);
	}

	return has_place;

}

// the milliseconds until the pause of the accept ends, EVENT_WHEEL_NEXT_NONE while the loop accepts
static int event_loop_accept_wait (const EventLoop *loop) {

	int wait = EVENT_WHEEL_NEXT_NONE;

	if (loop->accept_resume) {
		wait = (loop->accept_resume > loop->now) ? (int) (loop->accept_resume - loop->now) : 0;
	}

	return wait;

}

// the shortest of a wait and a time until something is due, EVENT_WHEEL_NEXT_NONE is nothing
static int event_loop_wait_shortest (const int wait, const int due) {

	return ((due != EVENT_WHEEL_NEXT_NONE) && ((wait < 0) || (due < wait))) ? due : wait;

}

// how long the poller waits: nothing when a connection completed or a parked one has a place,
// and never past the next tick of the timers nor the end of the pause of the accept
static int event_loop_wait_time (EventLoop *loop, const int timeout) {

	int wait = 0;

	if (event_loop_completed_wait (loop) && !event_loop_parked_has_place (loop)) {
		wait = event_loop_wait_shortest (timeout, event_wheel_next (&loop->wheel, loop->now));
		wait = event_loop_wait_shortest (wait, event_loop_accept_wait (loop));
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

		event_loop_ready_handle (loop, n_ready);

		handled += event_loop_completed_handle (loop);

		handled += (int) event_wheel_advance (&loop->wheel, loop->now, event_loop_timer_expired, loop);

		event_loop_accept_check (loop);

		event_loop_idle_watch (loop);

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
