#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
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

// an event loop asks its backend for the operations of its sockets and handles their completions:
// only its thread uses the sockets of its connections. The threads of the pools give it, through its queue,
// the connections whose job ended and the bytes of their responses

static const long event_loop_milliseconds_per_second = 1000L;
static const long event_loop_nanoseconds_per_millisecond = 1000000L;
static const long event_loop_nanoseconds_per_second = 1000000000L;

// the loop whose step the thread is taking, or took last: a thread runs one loop at a time
static __thread const EventLoop *event_loop_current = NULL;

// a loop that ended or that is deleted is not the one of the thread anymore: the memory of a loop that was
// released is given to the next one, that the thread never ran
static void event_loop_current_forget (const EventLoop *loop) {

	if (event_loop_current == loop) event_loop_current = NULL;

}

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

}

// the connections that the backend accepted over the limit and that never got a place
static void event_loop_held_close (EventLoop *loop) {

	for (unsigned int idx = 0; idx < loop->n_held; idx++) {
		(void) close (loop->held[idx]);
	}

	loop->n_held = 0;

}

void event_loop_delete (EventLoop *loop) {

	if (loop) {
		event_loop_current_forget (loop);

		event_loop_recycled_delete (loop);

		event_loop_held_close (loop);

		if (loop->listen_owned && (loop->listen_fd > -1)) (void) close (loop->listen_fd);

		event_backend_delete (loop->backend);

		(void) pthread_cond_destroy (&loop->output_condition);
		(void) pthread_mutex_destroy (&loop->output_mutex);

		free (loop);
	}

}

// the threads that wait for the output of a connection use the monotonic clock, like the loop
static void event_loop_output_init (EventLoop *loop) {

	pthread_condattr_t attributes;
	(void) pthread_condattr_init (&attributes);
	(void) pthread_condattr_setclock (&attributes, CLOCK_MONOTONIC);

	(void) pthread_mutex_init (&loop->output_mutex, NULL);
	(void) pthread_cond_init (&loop->output_condition, &attributes);

	(void) pthread_condattr_destroy (&attributes);

}

// a cerver that asked for io_uring does not start without it; one that did not ask for a backend
// uses epoll where io_uring is not available, and says why
static void event_loop_backend_report (const EventLoop *loop, const int unavailable) {

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };

	if (!loop->backend) {
		cerver_log_error (
			"Failed to create the %s backend of the event loops of cerver %s - errno %d (%s)",
			cerver_events_backend_to_string ((CerverEventsBackend) loop->cerver->events_backend),
			loop->cerver->info->name, errno,
			strerror_r (errno, error_description, sizeof (error_description))
		);
	}

	else if (unavailable) {
		cerver_log_warning (
			"io_uring is not available in cerver %s - errno %d (%s) - its event loops use %s",
			loop->cerver->info->name, unavailable,
			strerror_r (unavailable, error_description, sizeof (error_description)),
			loop->backend->name
		);
	}

}

// what every loop has before its backend: its clock, its timers and a socket that listens to nothing
static EventLoop *event_loop_new (void) {

	EventLoop *loop = (EventLoop *) calloc (1, sizeof (EventLoop));
	if (loop) {
		loop->clock = event_clock_milliseconds;
		loop->now = loop->clock ();

		event_wheel_init (&loop->wheel, loop->now);

		event_handle_init (&loop->listen, EVENT_HANDLE_TYPE_LISTEN, -1, loop);
		loop->listen_fd = -1;

		loop->recycle_limit = EVENT_LOOP_RECYCLE_LIMIT;

		event_loop_output_init (loop);
	}

	return loop;

}

EventLoop *event_loop_create (struct _Cerver *cerver, EventDispatcher *dispatcher) {

	EventLoop *loop = (cerver && dispatcher) ? event_loop_new () : NULL;
	if (loop) {
		loop->cerver = cerver;

		loop->lanes[EVENT_LOOP_LANE_DEFAULT].dispatcher = dispatcher;
		loop->n_lanes = EVENT_LOOP_LANE_DEFAULT + 1;

		int unavailable = 0;
		loop->backend = event_backend_create (
			(CerverEventsBackend) cerver->events_backend, NULL, &unavailable
		);

		event_loop_backend_report (loop, unavailable);

		if (!loop->backend) {
			event_loop_delete (loop);
			loop = NULL;
		}
	}

	return loop;

}

// a loop without a cerver has no lanes: nothing is ever parked in it, and it accepts nothing
EventLoop *event_loop_create_alone (const CerverEventsBackend backend) {

	EventLoop *loop = event_loop_new ();
	if (loop) {
		loop->backend = event_backend_create (backend, NULL, NULL);

		if (!loop->backend) {
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

void event_loop_connection_deferred (EventLoop *loop, const bool deferred) {

	if (deferred) (void) __atomic_add_fetch (&loop->n_deferred, 1, __ATOMIC_RELEASE);
	else (void) __atomic_sub_fetch (&loop->n_deferred, 1, __ATOMIC_RELEASE);

}

unsigned int event_loop_deferred (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_deferred, __ATOMIC_ACQUIRE) : 0;

}

bool event_loop_is_current (const EventLoop *loop) {

	return event_loop_current == loop;

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

static HttpConnection *event_loop_recycled_pop (EventLoop *loop) {

	HttpConnection *connection = loop->recycled;
	if (connection) {
		loop->recycled = connection->list_next;
		loop->n_recycled -= 1;

		connection->list_next = NULL;
	}

	return connection;

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

// a connection is counted out when it ends, also while its backend still ends its operations:
// its place is of the next connection from then on
void event_loop_connection_gone (EventLoop *loop) {

	event_loop_connections_remove (loop);

}

// a connection ended when nothing of it is left: its backend has none of its operations and its socket was
// closed, which can be some steps after it was counted out
void event_loop_connection_ended (EventLoop *loop) {

	(void) __atomic_add_fetch (&loop->n_ended, 1, __ATOMIC_RELEASE);

}

// only the loop ends its connections: the ones that it does not keep to be used again are released
void event_loop_connection_recycle (EventLoop *loop, struct HttpConnection *connection) {

	if (loop->n_recycled < loop->recycle_limit) {
		connection->list_next = loop->recycled;
		loop->recycled = connection;
		loop->n_recycled += 1;
	}

	else {
		http_connection_delete (connection);
	}

}

#pragma endregion

#pragma region idle

// the idle connections are kept in the order that they became idle: the first one is the one that has waited
// the longest for its next request, the one that gives its place to a new connection at the limit of the cerver.
// Only the loop uses the list, and each idle connection has its timer

void event_loop_idle_add (EventLoop *loop, struct HttpConnection *connection) {

	if (!connection->idle) {
		connection->idle_next = NULL;
		connection->idle_previous = loop->idle_tail;

		if (loop->idle_tail) loop->idle_tail->idle_next = connection;
		else loop->idle_head = connection;

		loop->idle_tail = connection;

		connection->idle = true;

		(void) __atomic_add_fetch (&loop->n_idle, 1, __ATOMIC_RELEASE);
	}

}

void event_loop_idle_remove (EventLoop *loop, struct HttpConnection *connection) {

	if (connection->idle) {
		if (connection->idle_previous) connection->idle_previous->idle_next = connection->idle_next;
		else loop->idle_head = connection->idle_next;

		if (connection->idle_next) connection->idle_next->idle_previous = connection->idle_previous;
		else loop->idle_tail = connection->idle_previous;

		connection->idle_next = NULL;
		connection->idle_previous = NULL;

		connection->idle = false;

		(void) __atomic_sub_fetch (&loop->n_idle, 1, __ATOMIC_RELEASE);
	}

}

// at the limit of connections of the cerver, the one that has been idle the longest gives its place to a new one
static void event_loop_idle_evict (EventLoop *loop) {

	if (loop->idle_head) http_connection_evict (loop->idle_head);

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

#pragma endregion

#pragma region wake

// wakes the loop up only when it waits in its backend, or is about to: a loop that is running reaches the end
// of its step by itself, where it takes its queue and gives its parked connections the places of the dispatcher.
// The loop says that it sleeps before it looks at its queue, and a thread adds to the queue before it looks at
// that flag: one of the two always sees the other
static void event_loop_wake_sleeping (EventLoop *loop) {

	if (__atomic_load_n (&loop->sleeping, __ATOMIC_SEQ_CST)) event_backend_wake (loop->backend);

}

void event_loop_wake (EventLoop *loop) {

	if (loop) event_backend_wake (loop->backend);

}

void event_loop_give (EventLoop *loop, EventNode *node) {

	event_queue_push (&loop->queue, node);

	event_loop_wake_sleeping (loop);

}

// only the loops with connections that wait for that pool: the place is of no use to the others
static void event_loop_space_wake (EventLoop *loop, const unsigned int lane) {

	if (__atomic_load_n (&loop->lanes[lane].n_parked, __ATOMIC_ACQUIRE)) event_loop_wake_sleeping (loop);

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

#pragma region output

// when the wait of a thread ends: a time of the monotonic clock
static void event_loop_output_deadline (struct timespec *deadline, const u64 milliseconds) {

	(void) clock_gettime (CLOCK_MONOTONIC, deadline);

	deadline->tv_sec += (time_t) (milliseconds / (u64) event_loop_milliseconds_per_second);
	deadline->tv_nsec += (long) (milliseconds % (u64) event_loop_milliseconds_per_second)
		* event_loop_nanoseconds_per_millisecond;

	if (deadline->tv_nsec >= event_loop_nanoseconds_per_second) {
		deadline->tv_sec += 1;
		deadline->tv_nsec -= event_loop_nanoseconds_per_second;
	}

}

static bool event_loop_output_is_over (const size_t *queued, const size_t limit, const bool *failed) {

	return (__atomic_load_n (queued, __ATOMIC_SEQ_CST) > limit) && !__atomic_load_n (failed, __ATOMIC_SEQ_CST);

}

// the thread counts itself before it looks at the bytes of its connection, and the loop changes those bytes
// before it looks at the count: the loop only takes the mutex for threads that wait
bool event_loop_output_wait (
	EventLoop *loop, const size_t *queued, const size_t limit, const bool *failed, const u64 milliseconds
) {

	struct timespec deadline;
	event_loop_output_deadline (&deadline, milliseconds);

	(void) pthread_mutex_lock (&loop->output_mutex);

	(void) __atomic_add_fetch (&loop->output_waiters, 1, __ATOMIC_SEQ_CST);

	int result = 0;
	while (!result && event_loop_output_is_over (queued, limit, failed)) {
		result = pthread_cond_timedwait (&loop->output_condition, &loop->output_mutex, &deadline);
	}

	(void) __atomic_sub_fetch (&loop->output_waiters, 1, __ATOMIC_SEQ_CST);

	(void) pthread_mutex_unlock (&loop->output_mutex);

	return (__atomic_load_n (queued, __ATOMIC_SEQ_CST) <= limit) && !__atomic_load_n (failed, __ATOMIC_SEQ_CST);

}

void event_loop_output_sent (EventLoop *loop) {

	if (__atomic_load_n (&loop->output_waiters, __ATOMIC_SEQ_CST)) {
		(void) pthread_mutex_lock (&loop->output_mutex);
		(void) pthread_cond_broadcast (&loop->output_condition);
		(void) pthread_mutex_unlock (&loop->output_mutex);
	}

}

#pragma endregion

#pragma region sources

void event_source_init (EventSource *source, const EventSourceMethods *methods) {

	if (source) {
		(void) memset (source, 0, sizeof (EventSource));

		source->node.type = EVENT_NODE_TYPE_SOURCE;

		event_handle_init (&source->handle, EVENT_HANDLE_TYPE_SOURCE, -1, source);

		source->timer.owner = source;
		source->timer.source = true;

		source->methods = methods;
	}

}

// the count goes first and the look at the end of the loop after it: a loop that ends looks at its sources after
// it was told to end, so either it sees this one or this thread sees that it ends
unsigned int event_loop_source_add (EventLoop *loop, EventSource *source) {

	unsigned int retval = 1;

	if (loop && source) {
		(void) __atomic_add_fetch (&loop->n_sources, 1, __ATOMIC_SEQ_CST);

		if (__atomic_load_n (&loop->finishing, __ATOMIC_SEQ_CST)) {
			(void) __atomic_sub_fetch (&loop->n_sources, 1, __ATOMIC_SEQ_CST);
		}

		else {
			source->loop = loop;

			retval = 0;
		}
	}

	return retval;

}

void event_loop_source_give (EventSource *source) {

	event_loop_give (source->loop, &source->node);

}

void event_loop_source_attach (EventSource *source) {

	EventLoop *loop = source->loop;

	source->previous = NULL;
	source->next = loop->sources;
	if (loop->sources) loop->sources->previous = source;
	loop->sources = source;

	source->attached = true;

}

static void event_loop_source_detach (EventSource *source) {

	EventLoop *loop = source->loop;

	if (source->previous) source->previous->next = source->next;
	else loop->sources = source->next;

	if (source->next) source->next->previous = source->previous;

	source->next = NULL;
	source->previous = NULL;

	source->attached = false;

}

void event_loop_source_timer_set (EventSource *source, const u64 milliseconds) {

	EventLoop *loop = source->loop;

	event_wheel_remove (&loop->wheel, &source->timer);

	event_wheel_add (&loop->wheel, &source->timer, loop->now + milliseconds);

}

void event_loop_source_timer_clear (EventSource *source) {

	event_wheel_remove (&source->loop->wheel, &source->timer);

}

void event_loop_source_remove (EventSource *source) {

	EventLoop *loop = source->loop;

	if (source->attached) event_loop_source_detach (source);

	event_loop_source_timer_clear (source);

	(void) __atomic_sub_fetch (&loop->n_sources, 1, __ATOMIC_SEQ_CST);

}

unsigned int event_loop_sources (const EventLoop *loop) {

	return loop ? __atomic_load_n (&loop->n_sources, __ATOMIC_SEQ_CST) : 0;

}

static void event_loop_source_given (EventSource *source) {

	if (source->methods->given) source->methods->given (source);

}

static void event_loop_source_expired (EventSource *source) {

	if (source->methods->expired) source->methods->expired (source);

}

static void event_loop_source_completed (EventSource *source, const EventCompletion *completion) {

	if (source->methods->completed) source->methods->completed (source, completion);

}

// a source that is told can remove itself, and others with it (a request ends with its connection): the list
// is read again from its start after each one, until every source that is left was told
static EventSource *event_loop_source_untold (const EventLoop *loop) {

	EventSource *source = loop->sources;

	while (source && source->stopped) source = source->next;

	return source;

}

// a loop that ends tells its sources, each one once: they end what they do and remove themselves
static void event_loop_sources_stop (EventLoop *loop) {

	EventSource *source = event_loop_source_untold (loop);
	while (source) {
		source->stopped = true;

		source->methods->stopped (source);

		source = event_loop_source_untold (loop);
	}

}

#pragma endregion

#pragma region queue

static void event_loop_node_handle (EventNode *node) {

	switch (node->type) {
		case EVENT_NODE_TYPE_CONNECTION:
			http_connection_completed (http_connection_of_node (node));
			break;

		case EVENT_NODE_TYPE_OUTPUT:
			http_connection_output_given (node);
			break;

		case EVENT_NODE_TYPE_DEFERRED:
			http_connection_deferred (http_connection_of_deferred_node (node));
			break;

		case EVENT_NODE_TYPE_SOURCE:
			event_loop_source_given ((EventSource *) node);
			break;

		default: break;
	}

}

// what the threads of the pools gave since the last step, in the order that they gave it
static int event_loop_queue_handle (EventLoop *loop) {

	int handled = 0;

	EventNode *node = event_queue_take (&loop->queue);
	while (node) {
		EventNode *next = node->next;
		node->next = NULL;

		event_loop_node_handle (node);

		handled += 1;

		node = next;
	}

	return handled;

}

#pragma endregion

#pragma region accept

static bool event_loop_is_stopping (const EventLoop *loop) {

	return __atomic_load_n (&loop->stopping, __ATOMIC_ACQUIRE);

}

static bool event_loop_is_at_limit (const EventLoop *loop) {

	return event_loop_total_connections (loop) >= loop->cerver->max_connections;

}

// at the limit of connections of the cerver, a new connection can only take the place of a connection
// of the loop that waits idle for its next request: the idle ones never keep new clients out
static bool event_loop_accept_has_place (const EventLoop *loop) {

	return !event_loop_is_at_limit (loop) || loop->idle_head;

}

unsigned int event_loop_adopt (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	if (loop && (fd > -1)) {
		// a backend that accepts many connections with one operation does not have the address of each one
		struct sockaddr_storage peer_address;
		if (!address) {
			socklen_t address_size = sizeof (struct sockaddr_storage);
			(void) memset (&peer_address, 0, sizeof (struct sockaddr_storage));
			(void) getpeername (fd, (struct sockaddr *) &peer_address, &address_size);
		}

		Connection *peer = connection_create (fd, address ? address : &peer_address, loop->cerver->protocol);
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

// stops accepting for a while: the loop asks its backend again when its time comes,
// a tick of its timers would be too long
static void event_loop_accept_pause (EventLoop *loop) {

	(void) event_backend_stop (loop->backend, &loop->listen);

	loop->accept_resume = loop->now + EVENT_LOOP_ACCEPT_BACKOFF;

}

// a connection that the backend accepted while the cerver was at its limit waits for a place in the loop,
// like the ones that wait in the socket that listens; one that does not fit there is taken over the limit
static void event_loop_accept_hold (EventLoop *loop, const int fd) {

	if (loop->n_held < EVENT_LOOP_ACCEPT_BATCH) {
		loop->held[loop->n_held] = fd;
		loop->n_held += 1;
	}

	else {
		(void) event_loop_adopt (loop, fd, NULL);
	}

	event_loop_accept_pause (loop);

}

// at the limit of connections, the connection of the loop that has waited the longest for its next request is
// closed to give its place to the new one: an idle connection is never closed for a client that is not there
static void event_loop_accept_place (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
) {

	if (event_loop_is_at_limit (loop)) event_loop_idle_evict (loop);

	(void) event_loop_adopt (loop, fd, address);

}

// a loop that stops does not take the connections that were on their way
static void event_loop_accepted (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
) {

	if (event_loop_is_stopping (loop)) (void) close (fd);
	else if (event_loop_accept_has_place (loop)) event_loop_accept_place (loop, fd, address);
	else event_loop_accept_hold (loop, fd);

}

// the connections that waited in the loop take the places that were freed, the oldest first
static void event_loop_held_place (EventLoop *loop) {

	unsigned int placed = 0;
	while ((placed < loop->n_held) && event_loop_accept_has_place (loop)) {
		event_loop_accept_place (loop, loop->held[placed], NULL);

		placed += 1;
	}

	if (placed) {
		loop->n_held -= placed;

		(void) memmove (loop->held, loop->held + placed, (size_t) loop->n_held * sizeof (int));
	}

}

// accepts again: after its pause, once the connections that waited in the loop have their place
// and its backend ended the accept that was stopped
static void event_loop_accept_resume (EventLoop *loop) {

	loop->accept_resume = 0;

	if (!event_loop_is_stopping (loop) && (loop->listen_fd > -1)) {
		event_loop_held_place (loop);

		if (
			loop->n_held || loop->listen.operations || !event_loop_accept_has_place (loop)
			|| event_backend_accept (loop->backend, &loop->listen)
		) {
			loop->accept_resume = loop->now + EVENT_LOOP_ACCEPT_BACKOFF;
		}
	}

}

// the pause of the accept ended
static void event_loop_accept_check (EventLoop *loop) {

	if (loop->accept_resume && (loop->now >= loop->accept_resume)) event_loop_accept_resume (loop);

}

// the listen fd is left when its accept ended in the backend, that still uses it until then
static void event_loop_listen_close (EventLoop *loop) {

	if (loop->listen_ended && !loop->listen.operations && (loop->listen_fd > -1)) {
		if (loop->listen_owned) (void) close (loop->listen_fd);

		loop->listen_fd = -1;
	}

}

// a loop that stops does not accept anymore
static void event_loop_accept_end (EventLoop *loop) {

	if (!loop->listen_ended) {
		loop->listen_ended = true;

		loop->accept_resume = 0;

		(void) event_backend_stop (loop->backend, &loop->listen);

		event_loop_held_close (loop);
	}

	event_loop_listen_close (loop);

}

// a loop sends each response whole, so its connections never wait to join small writes: a response that
// follows another one (a pipelined request) would wait for the ACK of the first, that its client delays.
// The connections take the option from the socket that accepts them, without a system call for each one;
// a socket that is not of TCP (the one of a test) does not have it
static void event_loop_listen_no_delay (const int listen_fd) {

	const int enabled = 1;

	(void) setsockopt (listen_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (enabled));

}

unsigned int event_loop_listen (EventLoop *loop, const int listen_fd, const bool owned) {

	unsigned int retval = 1;

	if (loop && (listen_fd > -1) && (loop->listen_fd < 0)) {
		event_loop_listen_no_delay (listen_fd);

		event_handle_init (&loop->listen, EVENT_HANDLE_TYPE_LISTEN, listen_fd, loop);

		retval = event_backend_accept (loop->backend, &loop->listen);
		if (!retval) {
			loop->listen_fd = listen_fd;
			loop->listen_owned = owned;
			loop->listen_ended = false;

			cerver_accept_reserve_fd_open ();
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

// without fds the connections that wait are accepted with the reserve fd and closed, a batch of them:
// with one for each pause the rest waited in the listen backlog for seconds
static void event_loop_accept_out_of_fds (EventLoop *loop, const int error, const unsigned int most) {

	unsigned int rejected = 0;
	while ((rejected < most) && cerver_accept_reserve_fd_reject (loop->listen_fd)) {
		event_loop_accept_rejected (loop, error);

		rejected += 1;
	}

}

static void event_loop_accept_failed_unknown (EventLoop *loop, const int error) {

	// the listening socket of a cerver that stops was shut down
	if (!event_loop_is_stopping (loop) && cerver_is_running (loop->cerver)) {
		char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
		cerver_log_with_date (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"event_loop_accept () - accept failed in cerver %s - errno %d (%s)",
			loop->cerver->info->name, error,
			strerror_r (error, error_description, sizeof (error_description))
		);
	}

}

// the accept of the backend ended with an error: the loop asks for it again, at once or after a pause
static void event_loop_accept_failed (EventLoop *loop, const int error) {

	u64 resume = loop->now + EVENT_LOOP_ACCEPT_BACKOFF;

	switch (error) {
		// a signal, or a client that went away before its connection was accepted: nothing to wait for
		case EINTR:
		case ECONNABORTED:
		case EPROTO:
		case EAGAIN:
		#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
		#endif
			resume = loop->now;
			break;

		case EMFILE:
		case ENFILE:
			event_loop_accept_out_of_fds (loop, error, EVENT_LOOP_ACCEPT_BATCH);
			break;

		case ENOBUFS:
		case ENOMEM:
			event_loop_accept_out_of_fds (loop, error, 1);
			break;

		default:
			event_loop_accept_failed_unknown (loop, error);
			break;
	}

	if (!loop->listen_ended) loop->accept_resume = resume ? resume : 1;

}

static void event_loop_listen_completion (EventLoop *loop, const EventCompletion *completion) {

	if (completion->operation == EVENT_OPERATION_ACCEPT) {
		if (completion->result >= 0) event_loop_accepted (loop, completion->result, completion->address);
		else event_loop_accept_failed (loop, -completion->result);
	}

	// the accept that was stopped ended in the backend
	else {
		event_loop_listen_close (loop);
	}

}

#pragma endregion

#pragma region stop

void event_loop_stop (EventLoop *loop, const bool reject) {

	if (loop) {
		if (reject) __atomic_store_n (&loop->rejecting, true, __ATOMIC_RELEASE);

		__atomic_store_n (&loop->stopping, true, __ATOMIC_RELEASE);

		event_backend_wake (loop->backend);
	}

}

void event_loop_finish (EventLoop *loop) {

	if (loop) {
		__atomic_store_n (&loop->stopping, true, __ATOMIC_RELEASE);

		// a thread that counts a source for the loop looks at this after its count (event_loop_source_add ())
		__atomic_store_n (&loop->finishing, true, __ATOMIC_SEQ_CST);

		event_backend_wake (loop->backend);
	}

}

// a loop that stops does not keep the connections that wait for their next request: each one is closed once
// its client closes it too, a proxy takes it out of the ones that it reuses. One that rejects also ends what
// it owns: the requests that have not gone to a pool are answered with 503 (the ones that come back from a
// pool later are answered when they are submitted again)
static void event_loop_stop_connections (EventLoop *loop) {

	const bool rejecting = __atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE);

	HttpConnection *connection = loop->active;
	while (connection) {
		HttpConnection *next = connection->active_next;

		if (rejecting || connection->idle) http_connection_abort (connection);

		connection = next;
	}

}

static void event_loop_stop_check (EventLoop *loop) {

	if (event_loop_is_stopping (loop)) {
		event_loop_accept_end (loop);

		event_loop_stop_connections (loop);
	}

	// the sources work while the loop only stops: the requests of its connections can wait for them
	if (__atomic_load_n (&loop->finishing, __ATOMIC_SEQ_CST)) event_loop_sources_stop (loop);

}

#pragma endregion

#pragma region step

static void event_loop_timer_expired (EventTimer *timer, void *loop_ptr) {

	(void) loop_ptr;

	if (timer->source) event_loop_source_expired ((EventSource *) timer->owner);
	else http_connection_timeout ((HttpConnection *) timer->owner);

}

// the completions of a step arrive after its wait, that can have slept any time (a loop without timers sleeps
// until something happens): the deadlines that they arm count from the time when they arrived, not from the
// one before the wait. A connection that arrived after an idle loop slept more than its timeouts would have
// been born with its deadline in the past
static void event_loop_completion_time (EventLoop *loop) {

	if (!loop->now_read) {
		loop->now = loop->clock ();

		loop->now_read = true;
	}

}

static void event_loop_completion (const EventCompletion *completion, void *loop_ptr) {

	EventLoop *loop = (EventLoop *) loop_ptr;

	event_loop_completion_time (loop);

	switch (completion->handle->type) {
		case EVENT_HANDLE_TYPE_LISTEN:
			event_loop_listen_completion (loop, completion);
			break;

		case EVENT_HANDLE_TYPE_SOURCE:
			event_loop_source_completed ((EventSource *) completion->handle->owner, completion);
			break;

		default:
			http_connection_completion ((HttpConnection *) completion->handle->owner, completion);
			break;
	}

}

// a place of the dispatcher that was freed after the loop gave its parked connections their places
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

// how long the backend waits: nothing when a thread gave something to the loop or a parked connection has a
// place, and never past the next tick of the timers nor the end of the pause of the accept.
// The loop says that it sleeps before it looks at its queue, so what a thread gives after that wakes it up
static int event_loop_wait_time (EventLoop *loop, const int timeout) {

	int wait = 0;

	__atomic_store_n (&loop->sleeping, true, __ATOMIC_SEQ_CST);

	if (!event_queue_has_nodes (&loop->queue) && !event_loop_parked_has_place (loop)) {
		wait = event_loop_wait_shortest (timeout, event_wheel_next (&loop->wheel, loop->now));
		wait = event_loop_wait_shortest (wait, event_loop_accept_wait (loop));
	}

	// a loop that only looks does not sleep: nobody has to wake it up
	if (!wait) __atomic_store_n (&loop->sleeping, false, __ATOMIC_SEQ_CST);

	return wait;

}

int event_loop_step (EventLoop *loop, const int timeout) {

	int handled = -1;

	if (loop) {
		event_loop_current = loop;

		loop->now = loop->clock ();
		loop->now_read = false;

		const int completions = event_backend_wait (
			loop->backend, event_loop_wait_time (loop, timeout), event_loop_completion, loop
		);

		__atomic_store_n (&loop->sleeping, false, __ATOMIC_SEQ_CST);

		loop->now = loop->clock ();

		handled = (completions > 0) ? completions : 0;

		handled += event_loop_queue_handle (loop);

		handled += (int) event_wheel_advance (&loop->wheel, loop->now, event_loop_timer_expired, loop);

		event_loop_accept_check (loop);

		event_loop_stop_check (loop);

		event_loop_parked_retry (loop);

		if (completions < 0) handled = -1;
	}

	return handled;

}

// a loop ends without connections and without sources, and once its backend ended the accept of its socket
static bool event_loop_is_finished (const EventLoop *loop) {

	return __atomic_load_n (&loop->finishing, __ATOMIC_SEQ_CST)
		&& !__atomic_load_n (&loop->n_connections, __ATOMIC_ACQUIRE)
		&& !loop->n_ending && !loop->listen.operations
		&& !__atomic_load_n (&loop->n_sources, __ATOMIC_SEQ_CST);

}

void event_loop_run (EventLoop *loop) {

	if (loop) {
		while (!event_loop_is_finished (loop)) {
			(void) event_loop_step (loop, EVENT_BACKEND_WAIT_FOREVER);
		}

		// before the loop says that it ended: another thread can delete it from then on
		event_loop_current_forget (loop);

		__atomic_store_n (&loop->ended, true, __ATOMIC_RELEASE);
	}

}

#pragma endregion
