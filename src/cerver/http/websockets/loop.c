#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/thpool.h"

// F6b: a WebSocket connection of the server in the event loop (CERVER_HANDLER_TYPE_EVENTS)
// the loop reads it and parses its frames, it answers the pings and the closing handshake, and it writes its
// queue; the methods of the application run in jobs of the pool of its endpoint, one job at a time and in order,
// and the loop does not read the connection while one of them runs

// the first places of the events of a batch, they double when they run out
#define HTTP_WEB_SOCKET_BATCH_EVENTS				(1 << 4)
#define HTTP_WEB_SOCKET_BATCH_GROWTH				2

// a timer that is not armed
#define HTTP_WEB_SOCKET_LOOP_NO_TIMEOUT				(-1)

// what is read and discarded of a peer that did not answer the close of cerver, before the socket is closed
#define HTTP_WEB_SOCKET_LOOP_DRAIN_SIZE				(1 << 16)
#define HTTP_WEB_SOCKET_LOOP_DRAIN_MILLISECONDS		(1 << 8)

// the frames that a thread of a pool reads of a connection that it took from its park
#define HTTP_WEB_SOCKET_PARK_BUFFER_SIZE			(1 << 16)

// what a thread of a pool waits for after a job, its connection or its pool, and for how long (2^18 ns)
#define HTTP_WEB_SOCKET_PARK_LINGER_FDS				2
#define HTTP_WEB_SOCKET_PARK_LINGER_ALONE			1
#define HTTP_WEB_SOCKET_PARK_LINGER_SOCKET			0
#define HTTP_WEB_SOCKET_PARK_LINGER_POOL			1
#define HTTP_WEB_SOCKET_PARK_LINGER_NANOSECONDS		(1L << 18)

// a thread that keeps its connection looks at its pool once each 2^4 messages
#define HTTP_WEB_SOCKET_PARK_LOOK_MASK				((1u << 4) - 1)

static void http_web_socket_loop_end (HttpWebSocket *web_socket);
static void http_web_socket_loop_update (HttpWebSocket *web_socket);
static void http_web_socket_loop_job_done (HttpWebSocket *web_socket);
static void http_web_socket_job_end (HttpWebSocket *web_socket);
static bool http_web_socket_loop_unpark (HttpWebSocket *web_socket);
static void http_web_socket_park_get (HttpWebSocket *web_socket, EventLoop *loop);
static void http_web_socket_park_put (HttpWebSocket *web_socket);

#pragma region batch

void http_web_socket_batch_delete (HttpWebSocketBatch *batch) {

	free (batch->events);
	free (batch->data);

	(void) memset (batch, 0, sizeof (HttpWebSocketBatch));

}

static void http_web_socket_batch_clear (HttpWebSocketBatch *batch) {

	batch->n_events = 0;
	batch->size = 0;

}

// a batch that grew for a large message goes back to its first size (W7, S10)
static void http_web_socket_batch_shrink (HttpWebSocketBatch *batch) {

	if (batch->data_capacity > HTTP_WEB_SOCKET_SHRINK_SIZE) {
		free (batch->data);

		batch->data = NULL;
		batch->data_capacity = 0;
	}

}

static bool http_web_socket_batch_reserve_events (HttpWebSocketBatch *batch) {

	bool reserved = batch->n_events < batch->capacity;

	if (!reserved) {
		const unsigned int capacity = batch->capacity ?
			(batch->capacity * HTTP_WEB_SOCKET_BATCH_GROWTH) : HTTP_WEB_SOCKET_BATCH_EVENTS;

		HttpWebSocketEvent *events = (HttpWebSocketEvent *) realloc (
			batch->events, capacity * sizeof (HttpWebSocketEvent)
		);

		if (events) {
			batch->events = events;
			batch->capacity = capacity;
			reserved = true;
		}
	}

	return reserved;

}

static bool http_web_socket_batch_reserve_data (HttpWebSocketBatch *batch, const size_t size) {

	bool reserved = (batch->size + size) <= batch->data_capacity;

	if (!reserved) {
		size_t capacity = batch->data_capacity ? batch->data_capacity : HTTP_WEB_SOCKET_QUEUE_INITIAL_SIZE;
		while (capacity < (batch->size + size)) capacity *= HTTP_WEB_SOCKET_BATCH_GROWTH;

		u8 *data = (u8 *) realloc (batch->data, capacity);
		if (data) {
			batch->data = data;
			batch->data_capacity = capacity;
			reserved = true;
		}
	}

	return reserved;

}

u8 http_web_socket_loop_event (
	HttpWebSocket *web_socket, const HttpWebSocketEventType type, const HttpWebSocketOpcode opcode,
	const u8 *payload, const size_t size, const bool first, const bool last
) {

	u8 retval = 1;

	HttpWebSocketBatch *batch = &web_socket->batch;
	if (http_web_socket_batch_reserve_events (batch) && http_web_socket_batch_reserve_data (batch, size)) {
		HttpWebSocketEvent *event = &batch->events[batch->n_events];
		event->type = type;
		event->opcode = opcode;
		event->first = first;
		event->last = last;
		event->offset = batch->size;
		event->size = size;

		if (size) (void) memcpy (batch->data + batch->size, payload, size);

		batch->size += size;
		batch->n_events += 1;

		retval = 0;
	}

	return retval;

}

// the events of the batch swap with the ones of the job: the loop fills the empty one while the job runs
static void http_web_socket_batch_swap (HttpWebSocket *web_socket) {

	const HttpWebSocketBatch batch = web_socket->batch;
	web_socket->batch = web_socket->running;
	web_socket->running = batch;

	http_web_socket_batch_clear (&web_socket->batch);

}

#pragma endregion

#pragma region job

static bool http_web_socket_loop_is_aborted (const HttpWebSocket *web_socket) {

	return __atomic_load_n (&web_socket->aborted, __ATOMIC_SEQ_CST);

}

// on_open () first: the connection goes to the registry after it (S7)
static void http_web_socket_event_open (HttpWebSocket *web_socket) {

	if (web_socket->settings.handlers.on_open) web_socket->settings.handlers.on_open (web_socket);

	http_web_socket_register (web_socket);

}

static void http_web_socket_event_data (HttpWebSocket *web_socket, const HttpWebSocketEvent *event, const u8 *data) {

	const HttpWebSocketHandlers *handlers = &web_socket->settings.handlers;

	switch (event->type) {
		case HTTP_WEB_SOCKET_EVENT_MESSAGE:
			if (handlers->on_message) handlers->on_message (web_socket, event->opcode, data, event->size);
			break;

		case HTTP_WEB_SOCKET_EVENT_FRAGMENT:
			if (handlers->on_fragment) {
				handlers->on_fragment (web_socket, event->opcode, data, event->size, event->first, event->last);
			}
			break;

		case HTTP_WEB_SOCKET_EVENT_PING:
			if (handlers->on_ping) handlers->on_ping (web_socket, data, event->size);
			break;

		case HTTP_WEB_SOCKET_EVENT_PONG:
			if (handlers->on_pong) handlers->on_pong (web_socket, data, event->size);
			break;

		default: break;
	}

}

static void http_web_socket_event_run (HttpWebSocket *web_socket, const HttpWebSocketEvent *event, const u8 *data) {

	switch (event->type) {
		case HTTP_WEB_SOCKET_EVENT_OPEN: http_web_socket_event_open (web_socket); break;

		case HTTP_WEB_SOCKET_EVENT_DRAIN:
			if (web_socket->settings.handlers.on_drain) web_socket->settings.handlers.on_drain (web_socket);
			break;

		case HTTP_WEB_SOCKET_EVENT_END:
			http_web_socket_closed_methods (web_socket, http_web_socket_get_error (web_socket));
			break;

		default: http_web_socket_event_data (web_socket, event, data); break;
	}

}

// the payload of an empty message is never NULL, like the one of a thread
static const u8 http_web_socket_loop_empty[1] = { 0 };

// the methods of the events of the job, in order; a connection that was aborted only has its last methods
static void http_web_socket_events_run (HttpWebSocket *web_socket) {

	const HttpWebSocketBatch *batch = &web_socket->running;

	for (unsigned int idx = 0; idx < batch->n_events; idx++) {
		const HttpWebSocketEvent *event = &batch->events[idx];

		const bool runs = (event->type == HTTP_WEB_SOCKET_EVENT_END)
			|| (event->type == HTTP_WEB_SOCKET_EVENT_OPEN) || !http_web_socket_loop_is_aborted (web_socket);

		const u8 *data = batch->data ? (batch->data + event->offset) : http_web_socket_loop_empty;

		if (runs) http_web_socket_event_run (web_socket, event, data);
	}

}

// a job of the pool: the loop does not touch the events of the job until it ends
static void http_web_socket_loop_job (void *web_socket_ptr) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_events_run (web_socket);

	http_web_socket_job_end (web_socket);

}

// the pool did not run the job (it is draining or it was destroyed): the loop runs it
static void http_web_socket_loop_job_dropped (void *web_socket_ptr) {

	http_web_socket_loop_request ((HttpWebSocket *) web_socket_ptr, HTTP_WEB_SOCKET_REQUEST_JOB_REJECTED);

}

static bool http_web_socket_loop_is_stopping (const EventLoop *loop) {

	return __atomic_load_n (&loop->stopping, __ATOMIC_ACQUIRE);

}

// a loop that stops runs the jobs that its pools do not take: the pools are being drained
static void http_web_socket_loop_inline (HttpWebSocket *web_socket) {

	http_web_socket_events_run (web_socket);

	http_web_socket_loop_job_done (web_socket);

}

// returns true when the pool took the job
static bool http_web_socket_loop_submit (HttpWebSocket *web_socket) {

	EventDispatcher *dispatcher = web_socket->loop->lanes[web_socket->lane].dispatcher;

	return dispatcher && dispatcher->submit_work && !dispatcher->submit_work (
		dispatcher, http_web_socket_loop_job, http_web_socket_loop_job_dropped, web_socket
	);

}

static void http_web_socket_loop_wait (HttpWebSocket *web_socket) {

	EventLoop *loop = web_socket->loop;

	web_socket->waiting = true;
	web_socket->waiting_next = loop->web_sockets_waiting;
	loop->web_sockets_waiting = web_socket;

}

// a dispatcher without jobs of its own (the one of a test) runs the methods in the loop,
// and so does an endpoint whose methods are short (http_web_socket_settings_set_loop_callbacks ())
static bool http_web_socket_loop_runs_inline (const HttpWebSocket *web_socket) {

	const EventDispatcher *dispatcher = web_socket->loop->lanes[web_socket->lane].dispatcher;

	return web_socket->settings.loop_callbacks
		|| !dispatcher || !dispatcher->submit_work || http_web_socket_loop_is_stopping (web_socket->loop);

}

// the job of the events that the loop has: one at a time, the pool of the endpoint runs it
static void http_web_socket_loop_dispatch (HttpWebSocket *web_socket) {

	if (!web_socket->job && !web_socket->waiting && web_socket->batch.n_events) {
		http_web_socket_batch_swap (web_socket);
		web_socket->job = true;

		if (http_web_socket_loop_runs_inline (web_socket)) http_web_socket_loop_inline (web_socket);
		else if (!http_web_socket_loop_submit (web_socket)) http_web_socket_loop_wait (web_socket);
	}

}

void http_web_socket_loop_retry (EventLoop *loop) {

	HttpWebSocket *web_socket = loop->web_sockets_waiting;
	loop->web_sockets_waiting = NULL;

	while (web_socket) {
		HttpWebSocket *next = web_socket->waiting_next;
		web_socket->waiting_next = NULL;
		web_socket->waiting = false;

		if (http_web_socket_loop_is_stopping (loop)) {
			// the job can be its last one, that releases the connection
			(void) http_web_socket_retain (web_socket);
			http_web_socket_loop_inline (web_socket);
			http_web_socket_loop_update (web_socket);
			http_web_socket_release (web_socket);
		}

		else if (!http_web_socket_loop_submit (web_socket)) http_web_socket_loop_wait (web_socket);

		web_socket = next;
	}

}

#pragma endregion

#pragma region watch

// the loop reads the connection while no job runs, it writes while its queue has bytes,
// and it drains a peer after the writes ended
static unsigned int http_web_socket_loop_interest (const HttpWebSocket *web_socket) {

	unsigned int interest = EVENT_INTEREST_NONE;

	// an aborted connection waits for nothing, it only finishes
	if (!http_web_socket_loop_is_aborted (web_socket)) {
		if ((web_socket->reading && !web_socket->job) || web_socket->draining) interest |= EVENT_INTEREST_READ;
		if (!http_web_socket_queue_is_empty (web_socket) && !web_socket->writes_ended) interest |= EVENT_INTEREST_WRITE;
	}

	return interest;

}

// a connection that waits for nothing leaves the poller: epoll always reports its hang up,
// and the loop would see it in every step while a job runs
static void http_web_socket_loop_watch (HttpWebSocket *web_socket) {

	EventLoop *loop = web_socket->loop;
	const unsigned int interest = http_web_socket_loop_interest (web_socket);

	if (interest == EVENT_INTEREST_NONE) (void) event_poller_remove (loop->poller, &web_socket->source);
	else if (web_socket->source.registered) (void) event_poller_modify (loop->poller, &web_socket->source, interest);
	else (void) event_poller_add (loop->poller, &web_socket->source, web_socket->transport->fd (web_socket), interest);

}

// the next timer of the connection: its closing handshake, its pings (not while a job runs, the connection
// is not read then), the send timeout of its queue and the end of its drain
static int http_web_socket_loop_next_timeout (const HttpWebSocket *web_socket) {

	int timeout = web_socket->draining ? HTTP_WEB_SOCKET_LOOP_NO_TIMEOUT :
		http_web_socket_next_timeout (web_socket, !web_socket->job);

	// a connection that a thread of its pool reads is checked once each interval of its pings: the loop takes it
	// back from its park to ping it (its pong deadline belongs to the thread that reads it)
	if (!web_socket->draining && web_socket->job && web_socket->park && web_socket->settings.ping_interval) {
		const int check = (int) web_socket->settings.ping_interval;
		if ((timeout == HTTP_WEB_SOCKET_LOOP_NO_TIMEOUT) || (check < timeout)) timeout = check;
	}

	if (web_socket->draining) {
		const u64 now = web_socket->loop->now;
		timeout = (web_socket->drain_deadline > now) ? (int) (web_socket->drain_deadline - now) : 0;
	}

	return timeout;

}

static void http_web_socket_loop_timer (HttpWebSocket *web_socket) {

	EventLoop *loop = web_socket->loop;
	const int timeout = web_socket->finished ? HTTP_WEB_SOCKET_LOOP_NO_TIMEOUT :
		http_web_socket_loop_next_timeout (web_socket);

	if (timeout == HTTP_WEB_SOCKET_LOOP_NO_TIMEOUT) event_wheel_remove (&loop->wheel, &web_socket->timer);
	else event_wheel_add (&loop->wheel, &web_socket->timer, loop->now + (u64) timeout);

}

#pragma endregion

#pragma region end

// the loop gives the connection back: it leaves its registry, its TCP connection is dropped, and the loop
// releases the reference that it had
static void http_web_socket_loop_release (HttpWebSocket *web_socket) {

	EventLoop *loop = web_socket->loop;

	web_socket->released = true;

	// its socket leaves the poller of its pool before it is closed
	http_web_socket_park_put (web_socket);

	http_web_socket_stream_end (web_socket);

	(void) __atomic_sub_fetch (&loop->n_web_sockets, 1, __ATOMIC_ACQ_REL);

	http_web_socket_release (web_socket);

}

// the connection ended: it leaves the poller and the wheel, and its last methods run in its last job
static void http_web_socket_loop_finalize (HttpWebSocket *web_socket) {

	if (!web_socket->finished && !web_socket->job && !web_socket->waiting) {
		EventLoop *loop = web_socket->loop;

		web_socket->finished = true;
		web_socket->draining = false;

		(void) event_poller_remove (loop->poller, &web_socket->source);
		event_wheel_remove (&loop->wheel, &web_socket->timer);

		const HttpWebSocketError error = http_web_socket_ended (web_socket);
		(void) error;

		const HttpWebSocketHandlers *handlers = &web_socket->settings.handlers;
		const bool methods = handlers->on_close || handlers->on_error;

		// without memory for its last job, its last methods run now
		if (methods && http_web_socket_loop_event (
			web_socket, HTTP_WEB_SOCKET_EVENT_END, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL, 0, true, true
		)) {
			http_web_socket_closed_methods (web_socket, http_web_socket_get_error (web_socket));
		}

		if (web_socket->batch.n_events) http_web_socket_loop_dispatch (web_socket);
		else http_web_socket_loop_release (web_socket);
	}

}

// the writes of the connection end once its queue was written; a peer that did not answer the close of cerver
// can still be sending: it is read and discarded for a moment, because a close () with unread data answers
// with a RST that can destroy the close frame (S5)
static void http_web_socket_loop_end_writes (HttpWebSocket *web_socket) {

	if (!web_socket->writes_ended) {
		web_socket->transport->end_writes (web_socket);

		(void) pthread_mutex_lock (&web_socket->send_mutex);
		web_socket->writes_ended = true;
		(void) pthread_mutex_unlock (&web_socket->send_mutex);

		if (http_web_socket_drain_needed (web_socket)) {
			web_socket->draining = true;
			web_socket->drained = 0;
			web_socket->drain_deadline = web_socket->loop->now + HTTP_WEB_SOCKET_LOOP_DRAIN_MILLISECONDS;
		}
	}

}

// the end goes on: the queue is written, then the writes end, then the peer is drained, then it finishes
static void http_web_socket_loop_end_progress (HttpWebSocket *web_socket) {

	if (web_socket->ending && !web_socket->finished) {
		const bool aborted = http_web_socket_loop_is_aborted (web_socket);

		if (!aborted && !http_web_socket_queue_is_empty (web_socket)) {
			// the owner writes it with EPOLLOUT, until it is empty or its send timeout fails it
		}

		else {
			if (!aborted) http_web_socket_loop_end_writes (web_socket);

			if (aborted || !web_socket->draining) http_web_socket_loop_finalize (web_socket);
		}
	}

}

static void http_web_socket_loop_end (HttpWebSocket *web_socket) {

	if (!web_socket->ending) {
		web_socket->ending = true;
		web_socket->reading = false;
	}

	http_web_socket_loop_end_progress (web_socket);

}

#pragma endregion

#pragma region io

// the queue was empty again after its backpressure: on_drain () is the next event
static void http_web_socket_loop_drained (HttpWebSocket *web_socket) {

	if (web_socket->settings.handlers.on_drain && !web_socket->ending) {
		if (http_web_socket_loop_event (
			web_socket, HTTP_WEB_SOCKET_EVENT_DRAIN, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL, 0, true, true
		)) {
			http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
		}
	}

}

// a write of the queue without waiting, a drained queue tells its on_drain ()
static void http_web_socket_loop_flush (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);

	const u8 failed = http_web_socket_queue_flush (web_socket);

	const bool drained = web_socket->queue_drained;
	web_socket->queue_drained = false;

	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (failed) http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);

	// the events belong to the job while it runs (or while the connection waits in its park): on_drain () goes
	// after it, and the thread of the job that has not parked the connection yet gives it back instead
	else if (drained && web_socket->job) __atomic_store_n (&web_socket->drain_pending, true, __ATOMIC_SEQ_CST);

	else if (drained) http_web_socket_loop_drained (web_socket);

}

// what a peer that did not answer the close of cerver sends after the writes ended is discarded
static void http_web_socket_loop_drain (HttpWebSocket *web_socket, const ssize_t received) {

	if (received > 0) web_socket->drained += (size_t) received;

	const bool ended = (received == 0)
		|| ((received < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK))
		|| (web_socket->drained >= HTTP_WEB_SOCKET_LOOP_DRAIN_SIZE);

	if (ended) {
		web_socket->draining = false;
		http_web_socket_loop_finalize (web_socket);
	}

}

// the end of the TCP connection, after the closing handshake or without it (1006)
static void http_web_socket_loop_peer_ended (HttpWebSocket *web_socket, const bool failed) {

	if (!web_socket->close_received) {
		http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
		if (failed) http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_RECEIVE);
	}

	// nothing more can be written to a peer that is gone
	if (failed) __atomic_store_n (&web_socket->aborted, true, __ATOMIC_SEQ_CST);

	http_web_socket_loop_end (web_socket);

}

// a recv () in the buffer of the loop, what it parses goes to the next job
static void http_web_socket_loop_read (HttpWebSocket *web_socket) {

	EventLoop *loop = web_socket->loop;

	const ssize_t received = web_socket->transport->read (web_socket, loop->buffer, loop->buffer_size);

	if (web_socket->draining) http_web_socket_loop_drain (web_socket, received);

	else if (received > 0) {
		http_web_socket_alive (web_socket);
		if (http_web_socket_receive_bytes (web_socket, (u8 *) loop->buffer, (size_t) received)) {
			http_web_socket_loop_end (web_socket);
		}
	}

	else if (received == 0) http_web_socket_loop_peer_ended (web_socket, false);

	else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) http_web_socket_loop_peer_ended (web_socket, true);

}

// a write that is reported, also with the hang up of the peer: the queue tells if it failed
static void http_web_socket_loop_writable (HttpWebSocket *web_socket, const unsigned int ready) {

	if ((ready & (EVENT_READY_WRITE | EVENT_READY_ERROR)) && !http_web_socket_queue_is_empty (web_socket)) {
		http_web_socket_loop_flush (web_socket);
	}

}

void http_web_socket_loop_ready (HttpWebSocket *web_socket, const unsigned int ready) {

	// its end can release it, the loop still uses it until the step ends
	(void) http_web_socket_retain (web_socket);

	if (!web_socket->finished && !http_web_socket_loop_is_aborted (web_socket)) {
		http_web_socket_loop_writable (web_socket, ready);

		const bool readable = (ready & (EVENT_READY_READ | EVENT_READY_ERROR))
			&& ((web_socket->reading && !web_socket->job) || web_socket->draining);

		if (readable && !http_web_socket_loop_is_aborted (web_socket)) http_web_socket_loop_read (web_socket);
	}

	// a write or a read that failed aborted the connection
	if (!web_socket->finished && http_web_socket_loop_is_aborted (web_socket)) http_web_socket_loop_end (web_socket);

	http_web_socket_loop_update (web_socket);

	http_web_socket_release (web_socket);

}

void http_web_socket_loop_timeout (HttpWebSocket *web_socket) {

	(void) http_web_socket_retain (web_socket);

	// its timers are the ones of its loop: a connection that waits in its park comes back to have them
	if (!web_socket->finished) (void) http_web_socket_loop_unpark (web_socket);

	if (!web_socket->finished) {
		if (http_web_socket_loop_is_aborted (web_socket)) {
			http_web_socket_loop_end (web_socket);
		}

		else if (web_socket->draining) {
			if (web_socket->loop->now >= web_socket->drain_deadline) {
				web_socket->draining = false;
				http_web_socket_loop_finalize (web_socket);
			}
		}

		else if (http_web_socket_timers (web_socket, !web_socket->job)) {
			http_web_socket_loop_end (web_socket);
		}

		http_web_socket_loop_update (web_socket);
	}

	http_web_socket_release (web_socket);

}

// after anything that the loop did with the connection: its next job, its interest, its timer and its end
static void http_web_socket_loop_update (HttpWebSocket *web_socket) {

	// a loop that stops, or an on_drain () that waits, takes the connection back from its park
	if (
		!web_socket->released && web_socket->job
		&& (
			http_web_socket_loop_is_stopping (web_socket->loop)
			|| __atomic_load_n (&web_socket->drain_pending, __ATOMIC_SEQ_CST)
		)
	) {
		(void) http_web_socket_loop_unpark (web_socket);
	}

	if (!web_socket->released) {
		if (!web_socket->finished) {
			http_web_socket_loop_end_progress (web_socket);
		}

		if (!web_socket->released) {
			http_web_socket_loop_dispatch (web_socket);

			if (!web_socket->finished) {
				http_web_socket_loop_watch (web_socket);
				http_web_socket_loop_timer (web_socket);
			}
		}
	}

}

#pragma endregion

#pragma region requests

// the connection that a thread of the pool upgraded: its first job is its on_open ()
static void http_web_socket_loop_adopted (HttpWebSocket *web_socket) {

	web_socket->reading = false;

	// the parks are of the loop, it gives them out in its own thread
	http_web_socket_park_get (web_socket, web_socket->loop);

	if (http_web_socket_loop_event (
		web_socket, HTTP_WEB_SOCKET_EVENT_OPEN, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL, 0, true, true
	)) {
		http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
	}

	http_web_socket_alive (web_socket);

}

// after its on_open (), the bytes that came with the request are its first frames, then it is read
static void http_web_socket_loop_opened (HttpWebSocket *web_socket) {

	web_socket->opened = true;

	bool finished = http_web_socket_loop_is_aborted (web_socket);
	if (!finished && web_socket->pending_size) {
		finished = http_web_socket_receive_bytes (web_socket, web_socket->pending, web_socket->pending_size);
		web_socket->pending_size = 0;
	}

	if (finished) http_web_socket_loop_end (web_socket);
	else if (!web_socket->ending) web_socket->reading = true;

}

// what a thread of the pool read that ends the connection, it ends in its loop
static void http_web_socket_loop_read_ended (HttpWebSocket *web_socket) {

	const unsigned int read_end = web_socket->read_end;
	web_socket->read_end = HTTP_WEB_SOCKET_READ_END_NONE;

	switch (read_end) {
		case HTTP_WEB_SOCKET_READ_END_FRAMES: http_web_socket_loop_end (web_socket); break;
		case HTTP_WEB_SOCKET_READ_END_PEER: http_web_socket_loop_peer_ended (web_socket, false); break;
		case HTTP_WEB_SOCKET_READ_END_FAILED: http_web_socket_loop_peer_ended (web_socket, true); break;
		default: break;
	}

}

// the connection is the one of its loop again: its job ended, or it came back from its park
static void http_web_socket_loop_owned (HttpWebSocket *web_socket) {

	if (__atomic_exchange_n (&web_socket->drain_pending, false, __ATOMIC_SEQ_CST)) {
		http_web_socket_loop_drained (web_socket);
	}

	http_web_socket_loop_read_ended (web_socket);

}

static void http_web_socket_loop_job_done (HttpWebSocket *web_socket) {

	web_socket->job = false;

	http_web_socket_batch_clear (&web_socket->running);
	http_web_socket_batch_shrink (&web_socket->running);

	if (web_socket->finished) {
		if (!web_socket->batch.n_events) http_web_socket_loop_release (web_socket);
	}

	else if (!web_socket->opened) {
		http_web_socket_loop_opened (web_socket);
	}

	else {
		http_web_socket_loop_owned (web_socket);
	}

}

// a cerver that goes away sends 1001 and waits for the answer of the peer with its close timeout
static void http_web_socket_loop_go_away (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	(void) http_web_socket_close_frame_locked (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL, 0);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

}

void http_web_socket_loop_requests (HttpWebSocket *web_socket) {

	const unsigned int requests = __atomic_exchange_n (&web_socket->loop_requests, 0, __ATOMIC_SEQ_CST);

	// a connection that ends, or whose cerver goes away, comes back from its park
	const bool recalled = (requests & (HTTP_WEB_SOCKET_REQUEST_ABORT | HTTP_WEB_SOCKET_REQUEST_GO_AWAY))
		|| http_web_socket_loop_is_aborted (web_socket);

	if (!web_socket->released && web_socket->job && recalled) (void) http_web_socket_loop_unpark (web_socket);

	if (!web_socket->released) {
		if (requests & HTTP_WEB_SOCKET_REQUEST_ADOPT) http_web_socket_loop_adopted (web_socket);

		if (requests & HTTP_WEB_SOCKET_REQUEST_JOB_REJECTED) http_web_socket_loop_inline (web_socket);

		if (requests & HTTP_WEB_SOCKET_REQUEST_JOB_DONE) http_web_socket_loop_job_done (web_socket);
	}

	if (!web_socket->released && !web_socket->finished) {
		if (requests & HTTP_WEB_SOCKET_REQUEST_OUTPUT) http_web_socket_loop_flush (web_socket);

		if ((requests & HTTP_WEB_SOCKET_REQUEST_GO_AWAY) && !web_socket->ending) http_web_socket_loop_go_away (web_socket);

		if ((requests & HTTP_WEB_SOCKET_REQUEST_ABORT) || http_web_socket_loop_is_aborted (web_socket)) {
			http_web_socket_loop_end (web_socket);
		}
	}

	http_web_socket_loop_update (web_socket);

	// the reference of the request
	http_web_socket_release (web_socket);

}

void http_web_socket_loop_request (HttpWebSocket *web_socket, const unsigned int requests) {

	const unsigned int previous = __atomic_fetch_or (&web_socket->loop_requests, requests, __ATOMIC_SEQ_CST);

	// the first request puts the connection in the list of its loop, with a reference
	if (!previous) {
		(void) http_web_socket_retain (web_socket);
		event_loop_web_socket_push (web_socket->loop, web_socket);
	}

}

void http_web_socket_loop_discard (HttpWebSocket *web_socket) {

	while (web_socket) {
		HttpWebSocket *next = web_socket->loop_next;
		web_socket->loop_next = NULL;

		http_web_socket_release (web_socket);

		web_socket = next;
	}

}

#pragma endregion

#pragma region park

// F6b: between two jobs a connection waits for its next frames in the poller of its pool, like an idle connection
// that is kept (F4b): a thread of the pool that has no job takes its socket, reads it, runs its methods and parks it
// again, without its loop; the loop takes it back for its timers, its end, the stop and its on_drain (), and reads it
// itself when the threads of the pool do not wait

// the frames that a thread of a pool reads, in a buffer of its own
static __thread u8 http_web_socket_park_buffer[HTTP_WEB_SOCKET_PARK_BUFFER_SIZE];

// the lingers of the thread, it looks at its pool once each few of them
static __thread unsigned int http_web_socket_park_lingers = 0;

// the state is changed with the mutex, and it is atomic for the ones that only look at it
static unsigned int http_web_socket_park_state (const HttpWebSocketPark *park) {

	return __atomic_load_n (&park->state, __ATOMIC_SEQ_CST);

}

static void http_web_socket_park_set (HttpWebSocketPark *park, const unsigned int state) {

	__atomic_store_n (&park->state, state, __ATOMIC_SEQ_CST);

}

static HttpWebSocketPark *http_web_socket_park_create (EventLoop *loop) {

	HttpWebSocketPark *park = (HttpWebSocketPark *) calloc (1, sizeof (HttpWebSocketPark));
	if (park) {
		(void) pthread_mutex_init (&park->mutex, NULL);

		// its source is the one of the park forever: a thread with an old report only reads its state
		event_source_init (&park->source, EVENT_SOURCE_TYPE_WEB_SOCKET, park);
		http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);

		park->all_next = loop->web_socket_parks_all;
		loop->web_socket_parks_all = park;
	}

	return park;

}

// a free park of the loop, or a new one; without memory the connection is always read by its loop
static void http_web_socket_park_get (HttpWebSocket *web_socket, EventLoop *loop) {

	HttpWebSocketPark *park = loop->web_socket_parks_free;
	if (park) loop->web_socket_parks_free = park->next;
	else park = http_web_socket_park_create (loop);

	if (park) {
		park->next = NULL;

		(void) pthread_mutex_lock (&park->mutex);
		park->web_socket = web_socket;
		http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);
		(void) pthread_mutex_unlock (&park->mutex);
	}

	web_socket->park = park;

}

// the loop gives the park back with the connection, that is its own
static void http_web_socket_park_put (HttpWebSocket *web_socket) {

	HttpWebSocketPark *park = web_socket->park;
	if (park) {
		EventLoop *loop = web_socket->loop;

		(void) event_poller_remove (park->source.poller, &park->source);

		(void) pthread_mutex_lock (&park->mutex);
		park->web_socket = NULL;
		http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);
		(void) pthread_mutex_unlock (&park->mutex);

		park->next = loop->web_socket_parks_free;
		loop->web_socket_parks_free = park;

		web_socket->park = NULL;
	}

}

void http_web_socket_parks_delete (EventLoop *loop) {

	HttpWebSocketPark *park = loop->web_socket_parks_all;
	while (park) {
		HttpWebSocketPark *next = park->all_next;

		(void) pthread_mutex_destroy (&park->mutex);
		free (park);

		park = next;
	}

	loop->web_socket_parks_all = NULL;
	loop->web_socket_parks_free = NULL;

}

// returns true when the caller took the connection from its park, and owns it
static bool http_web_socket_park_claim (HttpWebSocketPark *park) {

	(void) pthread_mutex_lock (&park->mutex);

	const bool claimed = (http_web_socket_park_state (park) == HTTP_WEB_SOCKET_PARK_PARKED);
	if (claimed) {
		http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);
		park->source.silent = true;
	}

	(void) pthread_mutex_unlock (&park->mutex);

	return claimed;

}

HttpWebSocket *http_web_socket_park_take (HttpWebSocketPark *park) {

	HttpWebSocket *web_socket = NULL;

	(void) pthread_mutex_lock (&park->mutex);

	if (http_web_socket_park_state (park) == HTTP_WEB_SOCKET_PARK_PARKED) {
		http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);
		park->source.silent = true;
		web_socket = park->web_socket;
	}

	(void) pthread_mutex_unlock (&park->mutex);

	return web_socket;

}

void http_web_socket_park_rescued (HttpWebSocketPark *park) {

	HttpWebSocket *web_socket = http_web_socket_park_take (park);
	if (web_socket) http_web_socket_loop_request (web_socket, HTTP_WEB_SOCKET_REQUEST_JOB_DONE);

}

// the loop wants the connection back: it ends, its cerver goes away, its loop stops, or its on_drain () waits (the
// loop empties the queue while the job runs, and nothing else takes back a connection whose peer does not write)
static bool http_web_socket_park_recalled (const HttpWebSocket *web_socket) {

	return http_web_socket_loop_is_aborted (web_socket)
		|| __atomic_load_n (&web_socket->going_away, __ATOMIC_SEQ_CST)
		|| http_web_socket_loop_is_stopping (web_socket->loop)
		|| __atomic_load_n (&web_socket->drain_pending, __ATOMIC_SEQ_CST);

}

// the connection can wait for its next frames in the poller of its pool: it is open and nothing of it is left for
// its loop (its on_open (), an end that was read, the events of the loop)
static bool http_web_socket_park_allowed (const HttpWebSocket *web_socket) {

	const EventLane *lane = &web_socket->loop->lanes[web_socket->lane];

	return web_socket->park && lane->idle && web_socket->opened
		&& (web_socket->read_end == HTTP_WEB_SOCKET_READ_END_NONE)
		&& !web_socket->batch.n_events
		&& (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN)
		&& !http_web_socket_park_recalled (web_socket);

}

// the socket is armed once in the poller of the pool; another thread can take the connection and give it back to
// its loop before this one returns, so it holds a reference
// returns true when the connection waits in its park, or another thread took it from there
static bool http_web_socket_park (HttpWebSocket *web_socket) {

	HttpWebSocketPark *park = web_socket->park;
	EventIdle *idle = web_socket->loop->lanes[web_socket->lane].idle;

	(void) http_web_socket_retain (web_socket);

	(void) pthread_mutex_lock (&park->mutex);

	http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_PARKED);
	const bool armed = !event_poller_watch (
		idle->poller, &park->source, web_socket->transport->fd (web_socket), EVENT_LOOP_IDLE_INTEREST
	);

	if (!armed) http_web_socket_park_set (park, HTTP_WEB_SOCKET_PARK_TAKEN);

	(void) pthread_mutex_unlock (&park->mutex);

	// a loop that asked for it after the check of this thread did not find it parked: this thread gives it back
	bool parked = armed;
	if (parked && http_web_socket_park_recalled (web_socket)) parked = !http_web_socket_park_claim (park);

	http_web_socket_release (web_socket);

	return parked;

}

static void http_web_socket_park_read (HttpWebSocket *web_socket);
static void http_web_socket_park_run (HttpWebSocket *web_socket);
static bool http_web_socket_park_linger_wait (const HttpWebSocket *web_socket, const EventIdle *idle);

// the thread waits a moment for the next frames of the connection, while nothing else of its pool waits for it (a
// connection that is ready in the poller of the pool, a job of its queue, that wakes that poller up): the next
// message of a peer that answers at once goes to the thread that is still running, like the thread of a connection
// in THREADS, instead of a thread of the pool that has slept the longest
// returns true when the connection has something to read first
static bool http_web_socket_park_linger (const HttpWebSocket *web_socket, const EventIdle *idle) {

	// once each few messages the thread looks if something of the pool is ready now, and goes to it: the threads
	// that wait take the rest, and a thread that keeps its connection does not leave the others waiting (a look
	// without a wait does not register the thread in the poller of the pool)
	http_web_socket_park_lingers += 1;

	bool busy = false;
	if (!(http_web_socket_park_lingers & HTTP_WEB_SOCKET_PARK_LOOK_MASK)) {
		struct pollfd pool = { event_poller_fd (idle->poller), POLLIN, 0 };
		busy = (poll (&pool, 1, 0) > 0);
	}

	return !busy && http_web_socket_park_linger_wait (web_socket, idle);

}

// the next frames of the connection, or something of the pool when no other thread of the pool waits for it
static bool http_web_socket_park_linger_wait (const HttpWebSocket *web_socket, const EventIdle *idle) {

	struct pollfd fds[HTTP_WEB_SOCKET_PARK_LINGER_FDS];
	fds[HTTP_WEB_SOCKET_PARK_LINGER_SOCKET].fd = web_socket->transport->fd (web_socket);
	fds[HTTP_WEB_SOCKET_PARK_LINGER_SOCKET].events = POLLIN;
	fds[HTTP_WEB_SOCKET_PARK_LINGER_SOCKET].revents = 0;
	fds[HTTP_WEB_SOCKET_PARK_LINGER_POOL].fd = event_poller_fd (idle->poller);
	fds[HTTP_WEB_SOCKET_PARK_LINGER_POOL].events = POLLIN;
	fds[HTTP_WEB_SOCKET_PARK_LINGER_POOL].revents = 0;

	const struct timespec linger = { 0, HTTP_WEB_SOCKET_PARK_LINGER_NANOSECONDS };

	// other threads of the pool already wait for the rest: this one only waits for its connection
	const nfds_t n_fds = thpool_get_num_threads_waiting (idle->thpool) ?
		HTTP_WEB_SOCKET_PARK_LINGER_ALONE : HTTP_WEB_SOCKET_PARK_LINGER_FDS;

	return (ppoll (fds, n_fds, &linger, NULL) > 0)
		&& fds[HTTP_WEB_SOCKET_PARK_LINGER_SOCKET].revents
		&& !fds[HTTP_WEB_SOCKET_PARK_LINGER_POOL].revents;

}

// the end of a job: the thread keeps the connection while its peer answers at once, then the connection waits in
// its park, or it goes back to its loop
static void http_web_socket_job_end (HttpWebSocket *web_socket) {

	while (
		http_web_socket_park_allowed (web_socket)
		&& http_web_socket_park_linger (web_socket, web_socket->loop->lanes[web_socket->lane].idle)
	) {
		http_web_socket_park_read (web_socket);
		http_web_socket_park_run (web_socket);
	}

	if (!http_web_socket_park_allowed (web_socket) || !http_web_socket_park (web_socket)) {
		http_web_socket_loop_request (web_socket, HTTP_WEB_SOCKET_REQUEST_JOB_DONE);
	}

}

// what the thread reads goes to the events of the connection, the ones that its loop does not touch while a job
// has it; an end is left for its loop
static void http_web_socket_park_read (HttpWebSocket *web_socket) {

	const ssize_t received = web_socket->transport->read (
		web_socket, http_web_socket_park_buffer, HTTP_WEB_SOCKET_PARK_BUFFER_SIZE
	);

	if (received > 0) {
		http_web_socket_alive (web_socket);
		if (http_web_socket_receive_bytes (web_socket, http_web_socket_park_buffer, (size_t) received)) {
			web_socket->read_end = HTTP_WEB_SOCKET_READ_END_FRAMES;
		}
	}

	else if (received == 0) web_socket->read_end = HTTP_WEB_SOCKET_READ_END_PEER;

	else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) web_socket->read_end = HTTP_WEB_SOCKET_READ_END_FAILED;

}

// the methods of what the thread read, in order
static void http_web_socket_park_run (HttpWebSocket *web_socket) {

	if (web_socket->batch.n_events) {
		http_web_socket_batch_swap (web_socket);
		http_web_socket_events_run (web_socket);

		http_web_socket_batch_clear (&web_socket->running);
		http_web_socket_batch_shrink (&web_socket->running);
	}

}

void http_web_socket_park_work (void *web_socket_ptr) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_park_read (web_socket);

	http_web_socket_park_run (web_socket);

	http_web_socket_job_end (web_socket);

}

// the loop takes a connection that waits in its park: it is the one of its loop again
// returns true when it was in its park
static bool http_web_socket_loop_unpark (HttpWebSocket *web_socket) {

	HttpWebSocketPark *park = web_socket->park;

	const bool unparked = park && http_web_socket_park_claim (park);
	if (unparked) {
		(void) event_poller_remove (park->source.poller, &park->source);

		http_web_socket_loop_job_done (web_socket);
	}

	return unparked;

}

#pragma endregion

#pragma region adopt

// the pool of the endpoint runs the jobs of its connections, or the default one when the loop has no lane for it
static unsigned int http_web_socket_loop_lane (const HttpWebSocket *web_socket, const EventLoop *loop) {

	const struct HttpPool *pool = web_socket->endpoint ? web_socket->endpoint->resolved.pool : NULL;

	const bool has_lane = pool && (pool->index < loop->n_lanes) && loop->lanes[pool->index].dispatcher;

	return has_lane ? pool->index : EVENT_LOOP_LANE_DEFAULT;

}

bool http_web_socket_loop_adopt (HttpWebSocket *web_socket, EventLoop *loop) {

	web_socket->loop = loop;
	web_socket->lane = http_web_socket_loop_lane (web_socket, loop);

	event_source_init (&web_socket->source, EVENT_SOURCE_TYPE_WEB_SOCKET, web_socket);

	web_socket->timer.owner = web_socket;
	web_socket->timer.owner_type = EVENT_TIMER_OWNER_WEB_SOCKET;

	http_web_socket_receiver_setup (web_socket);

	// it can send since its 101, its on_open () is its first job
	http_web_socket_opened (web_socket);

	// the loop does not end while it has it
	(void) __atomic_add_fetch (&loop->n_web_sockets, 1, __ATOMIC_ACQ_REL);

	http_web_socket_loop_request (web_socket, HTTP_WEB_SOCKET_REQUEST_ADOPT);

	return true;

}

#pragma endregion
