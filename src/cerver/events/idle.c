#include <stdbool.h>
#include <stdlib.h>

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/core/connection.h"
#include "cerver/http/websockets/internal.h"

#include "cerver/threads/thpool.h"

// the sources that the poller of a pool reports at once: each thread waits for one, the loop takes a few
#define EVENT_IDLE_EVENTS			EVENT_POLLER_TAKE_EVENTS

#pragma region lifecycle

EventIdle *event_idle_create (Thpool *thpool, const unsigned int lane) {

	EventIdle *idle = thpool ? (EventIdle *) calloc (1, sizeof (EventIdle)) : NULL;
	if (idle) {
		idle->thpool = thpool;
		idle->lane = lane;
		idle->started = false;

		idle->poller = event_poller_create_counted (EVENT_IDLE_EVENTS);
		if (!idle->poller) {
			free (idle);
			idle = NULL;
		}
	}

	return idle;

}

void event_idle_delete (EventIdle *idle) {

	if (idle) {
		event_poller_delete (idle->poller);

		free (idle);
	}

}

#pragma endregion

#pragma region waiter

// the thread that got the socket of an idle connection takes the connection, and reads its next request
static ThpoolWaited event_idle_reported (HttpConnection *connection, ThpoolJob *job) {

	ThpoolWaited waited = THPOOL_WAITED_NOTHING;

	if (http_connection_reported (connection)) {
		job->work = http_connection_work_reported;
		job->reject = NULL;
		job->args = connection;

		waited = THPOOL_WAITED_WORK;
	}

	return waited;

}

// the thread that got the socket of a WebSocket connection that waits between its jobs reads its next frames (F6b)
static ThpoolWaited event_idle_web_socket (HttpWebSocketPark *park, ThpoolJob *job) {

	ThpoolWaited waited = THPOOL_WAITED_NOTHING;

	HttpWebSocket *web_socket = http_web_socket_park_take (park);
	if (web_socket) {
		job->work = http_web_socket_park_work;
		job->reject = NULL;
		job->args = web_socket;

		waited = THPOOL_WAITED_WORK;
	}

	return waited;

}

// the owner of a source of the poller of a pool is an idle connection or the park of a WebSocket connection
static ThpoolWaited event_idle_source (const EventSource *source, ThpoolJob *job) {

	return (source->type == EVENT_SOURCE_TYPE_WEB_SOCKET) ?
		event_idle_web_socket ((HttpWebSocketPark *) source->owner, job) :
		event_idle_reported ((HttpConnection *) source->owner, job);

}

// a thread of the pool waits for the socket of an idle connection, or for a wake up of its thpool (a job in its
// queue, its end)
static ThpoolWaited event_idle_wait (void *idle_ptr, ThpoolJob *job) {

	const EventIdle *idle = (const EventIdle *) idle_ptr;

	ThpoolWaited waited = THPOOL_WAITED_NOTHING;

	EventReady ready = { NULL, 0 };
	switch (event_poller_wait_one (idle->poller, &ready, EVENT_POLLER_WAIT_FOREVER)) {
		case EVENT_POLLER_WAITED_SOURCE:
			waited = event_idle_source (ready.source, job);
			break;

		case EVENT_POLLER_WAITED_WAKE:
			waited = THPOOL_WAITED_WOKEN;
			break;

		default: break;
	}

	return waited;

}

// each wake up makes one thread return from its wait
static void event_idle_wake (void *idle_ptr, const unsigned int count) {

	event_poller_wake_many (((const EventIdle *) idle_ptr)->poller, count);

}

unsigned int event_idle_start (EventIdle *idle) {

	unsigned int retval = 1;

	if (idle && !idle->started) {
		const ThpoolWaiter waiter = { event_idle_wait, event_idle_wake, idle };

		retval = thpool_set_waiter (idle->thpool, &waiter);
		if (!retval) idle->started = true;
	}

	return retval;

}

void event_idle_end (EventIdle *idle) {

	if (idle && idle->started) {
		(void) thpool_set_waiter (idle->thpool, NULL);

		idle->started = false;
	}

}

#pragma endregion

#pragma region rescue

void event_idle_rescue (EventIdle *idle, EventLoop *loop) {

	if (idle && loop && !thpool_get_num_threads_waiting (idle->thpool)) {
		EventReady ready[EVENT_IDLE_EVENTS];

		const int n_ready = event_poller_take (idle->poller, ready, EVENT_IDLE_EVENTS);
		for (int idx = 0; idx < n_ready; idx++) {
			const EventSource *source = ready[idx].source;

			if (source->type == EVENT_SOURCE_TYPE_WEB_SOCKET) http_web_socket_park_rescued ((HttpWebSocketPark *) source->owner);
			else http_connection_rescued (loop, (HttpConnection *) source->owner);
		}
	}

}

#pragma endregion
