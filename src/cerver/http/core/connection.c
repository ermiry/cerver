#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/core/cerver.h"
#include "cerver/core/data.h"
#include "cerver/core/types.h"
#include "cerver/events/internal.h"
#include "cerver/handler/handler.h"
#include "cerver/network/socket.h"

#include "cerver/http/core/access.h"
#include "cerver/http/core/connection.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/responses/internal.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/websockets/internal.h"

#include "cerver/utils/log.h"

// how much a buffer of a connection grows each time that it is full
#define HTTP_CONNECTION_BUFFER_GROWTH		2

// a valid pointer for the parser when a request continues without bytes
static const char http_connection_no_bytes[] = "";

static void http_connection_end (HttpConnection *connection);
static void http_connection_settle (HttpConnection *connection, const HttpConnectionNext next);
static void http_connection_unavailable (HttpConnection *connection);
static void http_connection_kept (HttpConnection *connection);
static void http_connection_idle (HttpConnection *connection);

#pragma region buffers

static unsigned int http_connection_buffer_reserve (
	HttpConnectionBuffer *buffer, const size_t needed, const size_t max
) {

	unsigned int retval = 1;

	if (needed <= max) {
		retval = 0;

		if (buffer->size < needed) {
			size_t size = buffer->size ? buffer->size : HTTP_CONNECTION_BUFFER_SIZE;
			while (size < needed) size *= HTTP_CONNECTION_BUFFER_GROWTH;

			char *data = (char *) realloc (buffer->data, size);
			if (data) {
				buffer->data = data;
				buffer->size = size;
			}

			else {
				retval = 1;
			}
		}
	}

	return retval;

}

static unsigned int http_connection_buffer_append (
	HttpConnectionBuffer *buffer, const char *data, const size_t length, const size_t max
) {

	unsigned int retval = http_connection_buffer_reserve (buffer, buffer->length + length, max);
	if (!retval) {
		(void) memcpy (buffer->data + buffer->length, data, length);
		buffer->length += length;
	}

	return retval;

}

static void http_connection_buffer_clear (HttpConnectionBuffer *buffer) {

	buffer->length = 0;
	buffer->offset = 0;

}

static bool http_connection_buffer_contains (
	const HttpConnectionBuffer *buffer, const char *data
) {

	return buffer->data && (data >= buffer->data) && (data <= (buffer->data + buffer->size));

}

#pragma endregion

#pragma region lifecycle

void http_connection_delete (HttpConnection *connection) {

	if (connection) {
		arena_delete (connection->arena);

		str_delete (connection->head_buffer);

		free (connection->pending.data);
		free (connection->output.data);

		free (connection);
	}

}

HttpConnection *http_connection_new (void) {

	HttpConnection *connection = (HttpConnection *) calloc (1, sizeof (HttpConnection));
	if (connection) {
		connection->arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);

		connection->head_buffer = str_new (NULL);

		event_source_init (&connection->source, EVENT_SOURCE_TYPE_CONNECTION, connection);
		connection->timer.owner = connection;

		if (!connection->arena || !connection->head_buffer) {
			http_connection_delete (connection);
			connection = NULL;
		}
	}

	return connection;

}

const char *http_connection_state_string (const HttpConnectionState state) {

	const char *name = "None";

	switch (state) {
		case HTTP_CONNECTION_STATE_NONE: break;
		case HTTP_CONNECTION_STATE_READ: name = "Read"; break;
		case HTTP_CONNECTION_STATE_AUTH: name = "Auth"; break;
		case HTTP_CONNECTION_STATE_PARKED: name = "Parked"; break;
		case HTTP_CONNECTION_STATE_DISPATCHED: name = "Dispatched"; break;
		case HTTP_CONNECTION_STATE_WRITE: name = "Write"; break;
		case HTTP_CONNECTION_STATE_CLOSING: name = "Closing"; break;
		case HTTP_CONNECTION_STATE_IDLE: name = "Idle"; break;
		default: break;
	}

	return name;

}

#pragma endregion

#pragma region state

// a connection is pending until a thread of the pool handles its request, or until it ends;
// one that waits idle for its next request has nothing that a drain has to wait for
static bool http_connection_state_is_pending (const HttpConnectionState state) {

	return (state != HTTP_CONNECTION_STATE_NONE)
		&& (state != HTTP_CONNECTION_STATE_DISPATCHED)
		&& (state != HTTP_CONNECTION_STATE_IDLE);

}

static void http_connection_set_state (
	HttpConnection *connection, const HttpConnectionState state
) {

	const bool was_pending = http_connection_state_is_pending (connection->state);
	const bool is_pending = http_connection_state_is_pending (state);

	connection->state = state;

	if (was_pending != is_pending) event_loop_connection_pending (connection->loop, is_pending);

}

bool http_connection_is_loop_owned (const HttpConnection *connection) {

	return (connection->state != HTTP_CONNECTION_STATE_AUTH)
		&& (connection->state != HTTP_CONNECTION_STATE_DISPATCHED);

}

static int http_connection_fd (const HttpConnection *connection) {

	return connection->peer->socket->sock_fd;

}

static bool http_connection_loop_is_stopping (const HttpConnection *connection) {

	return __atomic_load_n (&connection->loop->stopping, __ATOMIC_ACQUIRE);

}

#pragma endregion

#pragma region deadlines

// the timer is only moved when the new deadline is earlier: one that expires before its deadline
// is armed again, so a deadline that changes with every read does not touch the wheel
static void http_connection_deadline_set (HttpConnection *connection, const u64 deadline) {

	connection->deadline = deadline;

	if (!connection->timer.armed || (deadline < connection->timer.deadline)) {
		event_wheel_add (&connection->loop->wheel, &connection->timer, deadline);
	}

}

static void http_connection_deadline_clear (HttpConnection *connection) {

	connection->deadline = 0;

	event_wheel_remove (&connection->loop->wheel, &connection->timer);

}

// a deadline in the milliseconds of the loop after a timeout, 0 when the timeout is disabled
static u64 http_connection_deadline_after (const u64 now, const u64 timeout) {

	return timeout ? (now + timeout) : 0;

}

// the deadlines of a request start with it: when its connection is accepted,
// when the bytes of the next request of a connection that was kept arrive, or with a pipelined request
static void http_connection_request_deadlines (HttpConnection *connection) {

	const u64 now = connection->loop->now;
	const HttpCerver *http_cerver = connection->receive.http_cerver;

	connection->headers_deadline = http_connection_deadline_after (now, http_cerver->headers_timeout);
	connection->request_deadline = http_connection_deadline_after (now, http_cerver->request_timeout);

}

// what a read waits for at most: the idle timeout, the deadline of the request
// and, while they have not been completed, the one of the headers
static u64 http_connection_read_deadline (const HttpConnection *connection) {

	const HttpCerver *http_cerver = connection->receive.http_cerver;

	u64 deadline = connection->loop->now + (
		http_cerver->idle_timeout ? http_cerver->idle_timeout : HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS
	);

	if (connection->request_deadline && (connection->request_deadline < deadline)) {
		deadline = connection->request_deadline;
	}

	if (
		connection->headers_deadline && (connection->headers_deadline < deadline)
		&& http_receive_is_receiving_headers (&connection->receive)
	) {
		deadline = connection->headers_deadline;
	}

	return deadline;

}

// the milliseconds that the loop waits to write a response, the send timeout of a response
static u64 http_connection_send_timeout (const HttpConnection *connection) {

	const Cerver *cerver = connection->loop->cerver;
	const HttpCerver *http_cerver = connection->receive.http_cerver;

	u64 timeout = (u64) CERVER_DEFAULT_SEND_TIMEOUT * HTTP_CONNECTION_MILLISECONDS_PER_SECOND;
	if (http_cerver && http_cerver->send_timeout) timeout = http_cerver->send_timeout;
	else if (cerver->send_timeout) timeout = (u64) cerver->send_timeout * HTTP_CONNECTION_MILLISECONDS_PER_SECOND;

	return timeout;

}

#pragma endregion

#pragma region poller

// the socket leaves the poller before the connection is given to a thread of the pool or ended
// a socket that was reported once (the one of a connection that waited idle) is already silent:
// it stays registered, and the thread that gives the connection back to its loop arms it again
static void http_connection_unwatch (HttpConnection *connection) {

	if (!connection->source.silent) {
		(void) event_poller_remove (connection->loop->poller, &connection->source);
	}

}

// waits for the socket with an interest until a deadline
// returns 0 on success, 1 when the poller did not take it (the connection ended)
static unsigned int http_connection_wait (
	HttpConnection *connection, const unsigned int interest, const u64 deadline
) {

	EventPoller *poller = connection->loop->poller;

	const unsigned int retval = connection->source.registered ?
		event_poller_modify (poller, &connection->source, interest) :
		event_poller_add (poller, &connection->source, http_connection_fd (connection), interest);

	if (!retval) {
		http_connection_deadline_set (connection, deadline);
	}

	else {
		cerver_log_error (
			"http_connection_wait () - failed to watch fd %d - errno %d",
			http_connection_fd (connection), errno
		);

		http_connection_end (connection);
	}

	return retval;

}

#pragma endregion

#pragma region end

// what is left of a connection after its socket ended or was taken by a WebSocket:
// its request with the data and the files that it did not keep, and its reference to the socket
// it runs where the request ended: a thread of the pool or the loop
static void http_connection_request_release (HttpConnection *connection) {

	const EventLoop *loop = connection->loop;
	if (loop->observer) loop->observer (&connection->receive, loop->observer_args);

	http_cerver_connection_ended (connection->receive.http_cerver, connection->receive.n_requests);

	http_receive_delete (&connection->receive);

	socket_delete (connection->cerver_receive.socket);
	connection->cerver_receive.socket = NULL;

}

// a request that used more than a chunk of its arena leaves its chunks in it,
// so the arena is created again and a recycled connection never keeps the memory of its biggest request
static void http_connection_arena_reset (HttpConnection *connection) {

	if (connection->arena->used > connection->arena->chunk_size) {
		arena_delete (connection->arena);
		connection->arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	}

	else {
		arena_reset (connection->arena);
	}

}

// the memory of a connection that ended goes back to its loop, ready for another connection
// the thread that ended it calls it: the loop, or the thread of the pool that handled its request
// (the loop gave it away when it dispatched it, it does not see it again)
static void http_connection_recycle (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_NONE);

	http_connection_arena_reset (connection);

	http_connection_buffer_clear (&connection->pending);
	http_connection_buffer_clear (&connection->output);
	connection->output_failed = false;
	connection->next = HTTP_CONNECTION_NEXT_NONE;

	connection->drained = 0;

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	// the close of its socket took it out of the poller, if it was still there
	event_source_forget (&connection->source);

	connection->idle_deadline = 0;

	connection->follow_misses = 0;
	connection->follow_skip = 0;

	connection->peer = NULL;

	event_loop_connection_release (loop, connection);

}

// the memory of a connection that the loop ended
static void http_connection_give_back (HttpConnection *connection) {

	event_loop_connection_detach (connection->loop, connection);

	http_connection_recycle (connection);

}

// ends a connection that the loop owns: the request is logged and counted, its socket is closed
// a connection that ends between two requests has no request to log, it is counted as an idle one that was closed
static void http_connection_end (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_unwatch (connection);
	http_connection_deadline_clear (connection);

	event_loop_unpark (loop, connection);

	if (http_receive_is_between_requests (&connection->receive)) event_loop_idle_closed (loop);
	else http_access_request (&connection->receive);

	connection_drop (loop->cerver, connection->peer);

	http_connection_request_release (connection);

	http_connection_give_back (connection);

}

// closes a connection whose request ended: what the client already sent is read first,
// a close with data that was not read answers with a RST that can destroy the response
static void http_connection_close (HttpConnection *connection) {

	const int sock_fd = http_connection_fd (connection);

	(void) shutdown (sock_fd, SHUT_WR);
	(void) http_receive_drain_available (sock_fd);

	http_connection_end (connection);

}

// reads what the client of a closing connection still sends, without waiting
// the connection ends with the end of the client, an error or HTTP_RECEIVE_DRAIN_SIZE bytes
static void http_connection_drain (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	bool ended = false;
	bool waiting = false;

	while (!ended && !waiting) {
		const ssize_t received = recv (http_connection_fd (connection), loop->buffer, loop->buffer_size, 0);

		if (received > 0) {
			connection->drained += (size_t) received;
			ended = (connection->drained >= HTTP_RECEIVE_DRAIN_SIZE);
		}

		else if ((received < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))) {
			waiting = true;
		}

		else {
			ended = true;
		}
	}

	if (ended) http_connection_end (connection);

}

// closes a connection whose request was rejected before it was complete: its client can still be
// sending it, so its writes are shut down and the rest is read for a short time before the close
static void http_connection_linger (HttpConnection *connection) {

	(void) shutdown (http_connection_fd (connection), SHUT_WR);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_CLOSING);

	connection->drained = 0;

	const u64 deadline = connection->loop->now + (
		(u64) HTTP_RECEIVE_DRAIN_TIMEOUT * HTTP_CONNECTION_MILLISECONDS_PER_SECOND
	);

	// the deadline of the read that the connection had can be later
	http_connection_deadline_clear (connection);

	if (!http_connection_wait (connection, EVENT_INTEREST_READ, deadline)) {
		http_connection_drain (connection);
	}

}

#pragma endregion

#pragma region output

static bool http_connection_output_is_pending (const HttpConnection *connection) {

	return connection->output.offset < connection->output.length;

}

// keeps the bytes of the parts after the ones that the socket took
// returns 0 on success, 1 when they do not fit
static unsigned int http_connection_output_keep (
	HttpConnection *connection, const struct iovec *parts, const int n_parts, size_t skip
) {

	unsigned int retval = 0;

	for (int idx = 0; (idx < n_parts) && !retval; idx++) {
		const size_t length = parts[idx].iov_len;

		if (skip >= length) {
			skip -= length;
		}

		else {
			retval = http_connection_buffer_append (
				&connection->output,
				((const char *) parts[idx].iov_base) + skip, length - skip,
				HTTP_CONNECTION_OUTPUT_MAX_SIZE
			);

			skip = 0;
		}
	}

	return retval;

}

// one sendmsg () that never waits
// returns the bytes that the socket took, an errno in error when it failed
static size_t http_connection_send_once (
	const HttpConnection *connection, const struct iovec *parts, const int n_parts, int *error
) {

	struct msghdr message;
	(void) memset (&message, 0, sizeof (struct msghdr));
	message.msg_iov = (struct iovec *) parts;
	message.msg_iovlen = (size_t) n_parts;

	ssize_t result = -1;
	do {
		result = sendmsg (http_connection_fd (connection), &message, MSG_NOSIGNAL | MSG_DONTWAIT);
	} while ((result < 0) && (errno == EINTR));

	// a full socket takes nothing, it is not a failure
	if ((result < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) *error = errno;

	return (result > 0) ? (size_t) result : 0;

}

u8 http_connection_send (
	HttpConnection *connection, const struct iovec *parts, const int n_parts,
	size_t *sent, int *error
) {

	size_t total = 0;
	for (int idx = 0; idx < n_parts; idx++) total += parts[idx].iov_len;

	int failure = 0;

	// nothing is sent ahead of what is still kept
	const size_t taken = http_connection_output_is_pending (connection) ?
		0 : http_connection_send_once (connection, parts, n_parts, &failure);

	if (!failure && (taken < total) && http_connection_output_keep (connection, parts, n_parts, taken)) {
		failure = ENOBUFS;
	}

	if (failure) connection->output_failed = true;

	*sent = failure ? taken : total;
	*error = failure;

	return failure ? 1 : 0;

}

// what the loop does with a connection that has nothing left to write
static void http_connection_proceed (
	HttpConnection *connection, const HttpConnectionNext next
) {

	connection->next = HTTP_CONNECTION_NEXT_NONE;

	switch (next) {
		case HTTP_CONNECTION_NEXT_READ:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			(void) http_connection_wait (
				connection, EVENT_INTEREST_READ, http_connection_read_deadline (connection)
			);
			break;

		case HTTP_CONNECTION_NEXT_LINGER:
			http_connection_linger (connection);
			break;

		case HTTP_CONNECTION_NEXT_KEEP:
			http_connection_kept (connection);
			break;

		default:
			http_connection_close (connection);
			break;
	}

}

// after the loop answered or parsed: what is left of a short response is written first,
// then the connection reads again, starts its next request, lingers or closes
static void http_connection_settle (
	HttpConnection *connection, const HttpConnectionNext next
) {

	if (connection->output_failed) {
		http_connection_end (connection);
	}

	else if (http_connection_output_is_pending (connection)) {
		connection->next = next;

		http_connection_set_state (connection, HTTP_CONNECTION_STATE_WRITE);

		http_connection_deadline_clear (connection);

		(void) http_connection_wait (
			connection, EVENT_INTEREST_WRITE,
			connection->loop->now + http_connection_send_timeout (connection)
		);
	}

	else {
		http_connection_proceed (connection, next);
	}

}

// writes what is left of the output when the socket can be written
static void http_connection_flush (HttpConnection *connection) {

	HttpConnectionBuffer *output = &connection->output;

	int failure = 0;
	bool full = false;

	while (http_connection_output_is_pending (connection) && !failure && !full) {
		const ssize_t result = send (
			http_connection_fd (connection),
			output->data + output->offset, output->length - output->offset,
			MSG_NOSIGNAL | MSG_DONTWAIT
		);

		if (result >= 0) output->offset += (size_t) result;
		else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) full = true;
		else if (errno != EINTR) failure = errno;
	}

	if (failure) {
		http_receive_response_failed (&connection->receive, failure);

		http_connection_end (connection);
	}

	else if (!full) {
		const HttpConnectionNext next = connection->next;

		http_connection_buffer_clear (output);

		http_connection_deadline_clear (connection);

		http_connection_proceed (connection, next);
	}

}

#pragma endregion

#pragma region dispatch

// gives the job of the connection to the dispatcher
// the state changes first: the thread that takes the job can run it and give it back at once
// a request that is handled is ended by the thread that handles it: the loop gives the connection away,
// one that is only authorized comes back to the loop
// returns 0 when it was taken (the loop does not use the connection anymore), 1 when there is no place
static unsigned int http_connection_dispatch (HttpConnection *connection) {

	EventLoop *loop = connection->loop;
	EventDispatcher *dispatcher = loop->lanes[connection->lane].dispatcher;

	const HttpConnectionState previous = connection->state;
	const bool handled = (connection->job == HTTP_CONNECTION_JOB_DISPATCH);

	http_connection_set_state (
		connection, handled ? HTTP_CONNECTION_STATE_DISPATCHED : HTTP_CONNECTION_STATE_AUTH
	);

	if (handled) event_loop_connection_detach (loop, connection);

	const unsigned int retval = dispatcher->submit (dispatcher, connection);
	if (retval) {
		event_loop_connection_attach (loop, connection);

		http_connection_set_state (connection, previous);
	}

	return retval;

}

// a request that the cerver can not run is counted and answered with 503,
// a complete one that is not handled does not keep its files: its handler never got them
static void http_connection_answer_unavailable (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	CERVER_STATS_ADD (connection->loop->cerver->stats->total_rejected_connections, 1);

	receive->receive_status = HTTP_RECEIVE_STATUS_REJECTED;

	if (!receive->responded) (void) http_response_send_unavailable (receive);

}

// answers 503 to a request that the cerver can not run: the pool has no place,
// the wait for it ended, or the cerver is being drained
static void http_connection_unavailable (HttpConnection *connection) {

	http_connection_answer_unavailable (connection);

	// the body of a request that only has its headers is still arriving
	http_connection_settle (
		connection,
		(connection->job == HTTP_CONNECTION_JOB_DISPATCH) ?
			HTTP_CONNECTION_NEXT_CLOSE : HTTP_CONNECTION_NEXT_LINGER
	);

}

static bool http_connection_loop_has_lane (const EventLoop *loop, const unsigned int lane) {

	return (lane < loop->n_lanes) && loop->lanes[lane].dispatcher;

}

static struct HttpPool *http_connection_pool_default (const HttpConnection *connection) {

	const HttpCerver *http_cerver = connection->receive.http_cerver;

	return (http_cerver && http_cerver->n_pools) ? http_cerver->pools[EVENT_LOOP_LANE_DEFAULT] : NULL;

}

// the job of a request runs in the pool that its endpoint or one of its groups chose;
// the default one runs the ones without an endpoint, and the ones of a pool that the loop has no lane for
static void http_connection_pool_select (HttpConnection *connection) {

	const HttpEndpoint *endpoint = connection->receive.endpoint;

	struct HttpPool *pool = endpoint ? endpoint->resolved.pool : NULL;

	if (!pool || !http_connection_loop_has_lane (connection->loop, pool->index)) {
		pool = http_connection_pool_default (connection);
	}

	connection->pool = pool;
	connection->lane = pool ? pool->index : EVENT_LOOP_LANE_DEFAULT;

}

static bool http_connection_pool_is_full (const HttpConnection *connection) {

	const EventDispatcher *dispatcher = connection->loop->lanes[connection->lane].dispatcher;

	return dispatcher->is_full (dispatcher);

}

// the job waits in the loop for a place in its pool, without a thread
static void http_connection_park (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_PARKED);

	event_loop_park (loop, connection);

	if (connection->pool) (void) __atomic_add_fetch (&connection->pool->n_parked, 1, __ATOMIC_RELEASE);

	http_connection_deadline_set (
		connection,
		loop->now + (
			(u64) http_pool_wait_timeout (loop->cerver, connection->pool)
			* HTTP_CONNECTION_MILLISECONDS_PER_SECOND
		)
	);

}

// each pool has its own policy and its own wait, the ones of the cerver when they were not set
static bool http_connection_can_wait (const HttpConnection *connection) {

	const EventLoop *loop = connection->loop;

	return (http_pool_overload_policy (loop->cerver, connection->pool) == CERVER_OVERLOAD_POLICY_WAIT)
		&& http_pool_wait_timeout (loop->cerver, connection->pool)
		&& !__atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE);

}

// a request that its pool had no place for: at once, or when its wait ended
static void http_connection_pool_refused (const HttpConnection *connection) {

	if (connection->pool) (void) __atomic_add_fetch (&connection->pool->n_unavailable, 1, __ATOMIC_RELEASE);

}

bool http_connection_refuse (HttpConnection *connection) {

	http_connection_pool_select (connection);

	const bool rejecting = __atomic_load_n (&connection->loop->rejecting, __ATOMIC_ACQUIRE);

	const bool full = !rejecting
		&& !http_connection_can_wait (connection) && http_connection_pool_is_full (connection);

	if (full) http_connection_pool_refused (connection);

	if (rejecting || full) http_connection_answer_unavailable (connection);

	return rejecting || full;

}

// without a place in its pool the job waits for one in the loop, or the request is answered with 503
static void http_connection_no_place (HttpConnection *connection) {

	if (http_connection_can_wait (connection)) {
		http_connection_park (connection);
	}

	else {
		http_connection_pool_refused (connection);
		http_connection_unavailable (connection);
	}

}

// leaves the poller and the wheel, the thread that runs the job owns the connection
static void http_connection_submit (
	HttpConnection *connection, const HttpConnectionJob job
) {

	EventLoop *loop = connection->loop;

	connection->job = job;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	http_connection_unwatch (connection);
	http_connection_deadline_clear (connection);

	http_connection_pool_select (connection);

	if (__atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE)) {
		http_connection_unavailable (connection);
	}

	// the requests that already wait for a place in its pool go first: a new one never takes the place
	// that the pool just freed for them
	else if (event_loop_lane_has_parked (loop, connection->lane) && http_connection_can_wait (connection)) {
		http_connection_park (connection);
	}

	else if (http_connection_dispatch (connection)) {
		http_connection_no_place (connection);
	}

}

unsigned int http_connection_unpark (HttpConnection *connection) {

	// its timer leaves the wheel before the pool can own it,
	// a connection that stays parked keeps the deadline that it had
	const u64 deadline = connection->deadline;

	http_connection_deadline_clear (connection);

	const unsigned int retval = http_connection_dispatch (connection);
	if (retval) http_connection_deadline_set (connection, deadline);

	return retval;

}

#pragma endregion

#pragma region read

// keeps what followed a paused request, the buffer where it is belongs to the loop
// (or it is the same pending buffer, when the request continued from it)
// returns 0 on success, 1 on error
static unsigned int http_connection_pending_keep (
	HttpConnection *connection, const char *tail, const size_t length
) {

	unsigned int retval = 0;

	HttpConnectionBuffer *pending = &connection->pending;

	if (http_connection_buffer_contains (pending, tail)) {
		if (length) (void) memmove (pending->data, tail, length);

		pending->length = length;
		pending->offset = 0;
	}

	else {
		http_connection_buffer_clear (pending);

		if (length) {
			retval = http_connection_buffer_append (pending, tail, length, HTTP_CONNECTION_PENDING_MAX_SIZE);
		}
	}

	return retval;

}

// a complete request: cerver answers it with a short response in the loop,
// anything else (a handler, a file, an authentication) goes to the pool
static void http_connection_message (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	if (http_receive_dispatch_is_inline (receive)) {
		http_receive_dispatch (receive);

		http_connection_settle (
			connection,
			http_receive_keeps_connection (receive) ? HTTP_CONNECTION_NEXT_KEEP : HTTP_CONNECTION_NEXT_CLOSE
		);
	}

	else {
		http_connection_submit (connection, HTTP_CONNECTION_JOB_DISPATCH);
	}

}

// the job that the parser stopped for: the authentication of the request before its body, or the complete request
static void http_connection_pause (HttpConnection *connection) {

	switch (connection->receive.pause) {
		case HTTP_RECEIVE_PAUSE_HEADERS:
			http_connection_submit (connection, HTTP_CONNECTION_JOB_AUTHORIZE);
			break;

		case HTTP_RECEIVE_PAUSE_MESSAGE:
			http_connection_message (connection);
			break;

		default:
			http_connection_close (connection);
			break;
	}

}

// the parser stopped for the pool
static void http_connection_paused (
	HttpConnection *connection, const char *tail, const size_t length
) {

	HttpReceive *receive = &connection->receive;

	if (http_connection_pending_keep (connection, tail, length)) {
		connection->job = HTTP_CONNECTION_JOB_AUTHORIZE;

		http_connection_unavailable (connection);
	}

	else {
		receive->unparsed = connection->pending.length;

		http_connection_pause (connection);
	}

}

// a request that a callback rejected (it was answered) or that the parser could not parse,
// a client that is gone was not answered
static void http_connection_rejected (HttpConnection *connection) {

	if (connection->receive.receive_status == HTTP_RECEIVE_STATUS_CLOSED) {
		http_connection_end (connection);
	}

	else {
		http_connection_settle (connection, HTTP_CONNECTION_NEXT_LINGER);
	}

}

static void http_connection_parse (
	HttpConnection *connection, const char *buffer, const size_t length
) {

	HttpReceive *receive = &connection->receive;

	size_t n_parsed = 0;
	const HttpReceiveParse result = http_receive_execute (receive, buffer, length, &n_parsed);

	switch (result) {
		// the request is not complete; a "100 Continue" can be waiting to be written
		// the bytes that were kept from the request before were all parsed
		case HTTP_RECEIVE_PARSE_MORE:
			if (http_connection_buffer_contains (&connection->pending, buffer)) {
				http_connection_buffer_clear (&connection->pending);
			}

			http_connection_settle (connection, HTTP_CONNECTION_NEXT_READ);
			break;

		case HTTP_RECEIVE_PARSE_PAUSED:
		case HTTP_RECEIVE_PARSE_UPGRADE:
			http_connection_paused (connection, buffer + n_parsed, length - n_parsed);
			break;

		default:
			http_receive_parser_failed (receive, n_parsed, (ssize_t) length);

			http_connection_rejected (connection);
			break;
	}

}

static void http_connection_received (HttpConnection *connection, const size_t length) {

	EventLoop *loop = connection->loop;
	HttpReceive *receive = &connection->receive;

	connection->peer->socket->packet_buffer_size = length;

	CERVER_STATS_ADD (loop->cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (loop->cerver->stats->total_bytes_received, (u64) length);

	receive->received += length;

	http_connection_parse (connection, loop->buffer, length);

}

// the read of a request that failed: its client left with an error
static void http_connection_read_error (HttpConnection *connection, const int error) {

	HttpReceive *receive = &connection->receive;

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
		"http_connection_read () - recv () failed - fd %d - errno %d (%s) - received %zu - recv #%u",
		http_connection_fd (connection), error,
		strerror_r (error, error_description, sizeof (error_description)),
		receive->received, receive->diagnostics.n_recvs
	);

	http_receive_client_left (receive, error);

	http_connection_end (connection);

}

// the client of a request left, the request is counted where it was;
// the client of an idle connection only closed it, there is no request
static void http_connection_client_left (HttpConnection *connection, const int error) {

	if (connection->state == HTTP_CONNECTION_STATE_IDLE) {
		http_connection_end (connection);
	}

	else if (error) {
		http_connection_read_error (connection, error);
	}

	else {
		http_receive_client_left (&connection->receive, 0);

		http_connection_end (connection);
	}

}

static void http_connection_read_failed (HttpConnection *connection, const int error) {

	switch (error) {
		// nothing to read yet, or a signal: the client is still there
		// an idle connection keeps the time that it had to send its next request
		case EAGAIN:
		#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
		#endif
		case EINTR:
			if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_idle (connection);
			else (void) http_connection_wait (
				connection, EVENT_INTEREST_READ, http_connection_read_deadline (connection)
			);
			break;

		default:
			http_connection_client_left (connection, error);
			break;
	}

}

// the first bytes of the next request of a connection that waited idle arrived:
// the clock and the deadlines of the request start now, the time that it waited idle is not part of it
static void http_connection_idle_leave (HttpConnection *connection) {

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_receive_request_arrived (&connection->receive);

	http_connection_request_deadlines (connection);

}

// one read of what the client sent: a short read is not followed by another one,
// the poller reports the socket again when there is more
// the first bytes that an idle connection receives start its next request
static void http_connection_read (HttpConnection *connection) {

	EventLoop *loop = connection->loop;
	HttpReceive *receive = &connection->receive;

	const ssize_t received = recv (http_connection_fd (connection), loop->buffer, loop->buffer_size, 0);
	const int error = (received < 0) ? errno : 0;

	receive->diagnostics.last_rc = received;
	receive->diagnostics.last_errno = error;
	receive->diagnostics.n_recvs += 1;

	if (received > 0) {
		if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_idle_leave (connection);

		http_connection_received (connection, (size_t) received);
	}

	else if (!received) {
		http_connection_client_left (connection, 0);
	}

	else {
		http_connection_read_failed (connection, error);
	}

}

#pragma endregion

#pragma region keep

// the connection waits for the next request of its client, at most the keep alive timeout of the cerver:
// it becomes one of the idle connections of its loop, with its socket armed to be reported once
// the thread that owns it calls it, its loop or the thread of the pool that answered its request;
// after a success the loop owns it: it is not one of the connections that the loop walks to end them,
// the idle ones have their own list
// returns 0 on success, 1 when its socket could not be armed (the caller still owns it)
static unsigned int http_connection_idle_enter (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_IDLE);

	return event_loop_idle_enter (
		loop, connection, connection->receive.http_cerver->keep_alive_timeout
	);

}

// the loop gives a connection that it owns to its idle ones, one whose socket can not be armed is closed
static void http_connection_idle (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	event_loop_connection_detach (loop, connection);

	if (http_connection_idle_enter (connection)) {
		event_loop_connection_attach (loop, connection);

		http_connection_close (connection);
	}

}

// closes an idle connection whose client is still there, that its loop took out of its idle ones:
// it did not send its next request in time, or its cerver stops; its writes are shut down and it is closed
// when its client closes it too, so a request that its client was sending at that moment
// is not answered with a reset
static void http_connection_idle_close (HttpConnection *connection) {

	http_connection_linger (connection);

}

void http_connection_evict (HttpConnection *connection) {

	if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_close (connection);

}

void http_connection_idle_expired (HttpConnection *connection) {

	if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_idle_close (connection);

}

// continues with the request that arrived behind the one that ended, a pipelined request
static void http_connection_continue (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_connection_request_deadlines (connection);

	receive->received += connection->pending.length;

	http_connection_parse (connection, connection->pending.data, connection->pending.length);

}

// the next request of a connection that is kept, once the one that ended was logged and released:
// the bytes that followed it are a pipelined request, without them the connection waits idle
// a loop that stops does not keep its connections
static void http_connection_next (HttpConnection *connection) {

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	if (http_connection_loop_is_stopping (connection)) http_connection_close (connection);
	else if (connection->pending.length) http_connection_continue (connection);
	else http_connection_idle (connection);

}

// a pipelined request that follows one that the loop answered waits for the next step of the loop,
// like one that comes back from the pool: a client that sends many of them does not keep the loop
static void http_connection_requeue (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_unwatch (connection);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_DISPATCHED);

	event_loop_connection_detach (loop, connection);

	connection->outcome = HTTP_CONNECTION_OUTCOME_KEPT;

	event_loop_complete (loop, connection);

}

// the request of a connection that is kept ended in the loop, cerver answered it by itself:
// it is logged and counted, its data is released and its next request starts
static void http_connection_kept (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;
	const EventLoop *loop = connection->loop;

	http_connection_deadline_clear (connection);

	http_access_request (receive);

	if (loop->observer) loop->observer (receive, loop->observer_args);

	if (http_receive_reset (receive)) http_connection_close (connection);
	else if (connection->pending.length) http_connection_requeue (connection);
	else http_connection_next (connection);

}

#pragma endregion

#pragma region accepted

static void http_connection_cerver_receive_set (HttpConnection *connection, Connection *peer) {

	CerverReceive *cerver_receive = &connection->cerver_receive;

	(void) memset (cerver_receive, 0, sizeof (CerverReceive));

	cerver_receive->type = RECEIVE_TYPE_NORMAL;
	cerver_receive->cerver = connection->loop->cerver;
	cerver_receive->socket = socket_reference (peer->socket);
	cerver_receive->connection = peer;

}

// returns 0 on success, 1 on error
static unsigned int http_connection_start (HttpConnection *connection, Connection *peer) {

	connection->peer = peer;

	http_connection_cerver_receive_set (connection, peer);

	const HttpReceiveResources resources = { connection->arena, connection->head_buffer };

	unsigned int retval = http_receive_prepare (&connection->receive, &connection->cerver_receive, &resources);
	if (!retval) {
		connection->receive.connection = connection;

		http_connection_request_deadlines (connection);

		http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
	}

	else {
		socket_delete (connection->cerver_receive.socket);
		connection->cerver_receive.socket = NULL;

		connection->peer = NULL;
	}

	return retval;

}

unsigned int http_connection_accepted (EventLoop *loop, Connection *peer) {

	unsigned int retval = 1;

	HttpConnection *connection = event_loop_connection_take (loop);
	if (connection && !http_connection_start (connection, peer)) {
		// what the client sent with its connection is read now, most requests never wait for the poller
		http_connection_read (connection);

		retval = 0;
	}

	else {
		cerver_log_error (
			"http_connection_accepted () - failed to start the connection of fd %d",
			peer->socket->sock_fd
		);

		if (connection) {
			event_loop_connection_detach (loop, connection);
			event_loop_connection_release (loop, connection);
		}

		connection_drop (loop->cerver, peer);
	}

	return retval;

}

#pragma endregion

#pragma region work

// the connection of a request that a thread of the pool handled ended, or its WebSocket took it:
// its data is released, and nothing is left for the loop to do: its memory goes back without waking it up
static void http_connection_work_release (HttpConnection *connection) {

	if (!http_web_socket_start (&connection->cerver_receive, &connection->receive)) {
		connection_drop (connection->loop->cerver, connection->peer);
	}

	http_connection_request_release (connection);

	http_connection_recycle (connection);

}

// the end of a request that a thread of the pool handled and whose connection is not kept,
// what a thread does with its connection when the cerver handles them with threads:
// the connection is closed (or its WebSocket takes it), the request is logged and counted, and its data is released
static void http_connection_work_close (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	// the bytes that followed an upgrade are the first ones of its WebSocket, that reads its socket from now on:
	// a socket that stayed silent in the poller of the loop (the one of a connection that was kept) leaves it
	if (receive->upgraded) {
		http_receive_web_socket_pending (
			receive, connection->pending.data ? connection->pending.data : http_connection_no_bytes
		);

		(void) event_poller_remove (connection->loop->poller, &connection->source);
	}

	else {
		http_receive_close (receive, false);
	}

	http_access_request (receive);

	http_connection_work_release (connection);

}

// a connection that is kept and whose next request can not start ends in the thread that handled its request
static void http_connection_work_abandon (HttpConnection *connection) {

	http_receive_close (&connection->receive, false);

	http_connection_work_release (connection);

}

// the connection goes back to its loop through its queue, and the loop is woken up: the bytes that followed
// the request are its next one (a pipelined request), or the loop stops and closes the connection
static void http_connection_work_return (HttpConnection *connection) {

	connection->outcome = HTTP_CONNECTION_OUTCOME_KEPT;

	event_loop_complete (connection->loop, connection);

}

// the connection waits for its next request: the thread gives it to the idle connections of its loop
// and arms its socket by itself, the loop is not woken up and finds the connection
// when its client sends its next request
static void http_connection_work_idle (HttpConnection *connection) {

	if (http_connection_idle_enter (connection)) http_connection_work_abandon (connection);

}

#pragma endregion

#pragma region follow

// the thread of a pool that answered a request of a connection that is kept looks for its next request for a
// short time, and handles it by itself: a request that arrives then waits for no thread, when the loop that
// reads it and the thread that it wakes up cost more than the request of a quick client
// the thread owns the connection all that time: its loop does not know it until the thread gives it back

// what a thread found when it followed its connection
typedef enum HttpConnectionFollow {

	// no request arrived in time: the connection waits idle in its loop
	HTTP_CONNECTION_FOLLOW_NONE				= 0,
	// its next request is complete and the thread handles it
	HTTP_CONNECTION_FOLLOW_HANDLE			= 1,
	// the connection went back to its loop with what the thread found, the thread does not use it anymore
	HTTP_CONNECTION_FOLLOW_GIVEN			= 2

} HttpConnectionFollow;

static const u64 http_connection_microseconds_per_second = 1000000;
static const u64 http_connection_nanoseconds_per_microsecond = 1000;

// where a thread reads what it finds, its bytes are parsed before the thread uses it again:
// a request that does not fit in it is not complete, and its loop reads the rest
static __thread char http_connection_follow_buffer[HTTP_CONNECTION_FOLLOW_BUFFER_SIZE] = { 0 };

// the monotonic clock in microseconds, the time of a thread that follows its connection
static u64 http_connection_microseconds (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((u64) now.tv_sec * http_connection_microseconds_per_second)
		+ ((u64) now.tv_nsec / http_connection_nanoseconds_per_microsecond);

}

// a job waits for a thread of the pool of the connection, its dispatcher knows
static bool http_connection_work_is_awaited (const HttpConnection *connection) {

	const EventDispatcher *dispatcher = connection->loop->lanes[connection->lane].dispatcher;

	return dispatcher->is_awaited && dispatcher->is_awaited (dispatcher);

}

// a connection whose client did not send its next request in time is not followed for some requests
// returns true when this request is one of them
static bool http_connection_follow_skips (HttpConnection *connection) {

	const bool skips = (connection->follow_skip > 0);
	if (skips) connection->follow_skip -= 1;

	return skips;

}

// the thread follows its connection when the cerver gives it a time for it, the connection is not waiting
// out its misses, and no job waits for the thread: the time that the thread looks for a request
// is time that it does not give to a request that is already there
static bool http_connection_follow_is_allowed (HttpConnection *connection) {

	return connection->receive.http_cerver->keep_alive_follow
		&& !http_connection_follow_skips (connection)
		&& !http_connection_work_is_awaited (connection);

}

// the client did not send its next request in the time of the cerver: each miss in a row doubles
// the requests that are not followed, up to a limit
// a thread that stopped looking because a job arrived for it did not give its client that time: it is not a miss
static void http_connection_follow_missed (HttpConnection *connection) {

	if (http_connection_work_is_awaited (connection)) return;

	if (connection->follow_misses < HTTP_CONNECTION_FOLLOW_MISSES) connection->follow_misses += 1;

	connection->follow_skip = ((unsigned int) 1 << connection->follow_misses) - 1;

}

// the thread keeps looking for the request while the time of the cerver has not ended, its loop does not stop
// and no job arrived for the thread meanwhile
static bool http_connection_follow_waits (const HttpConnection *connection, const u64 deadline) {

	return (http_connection_microseconds () < deadline)
		&& !http_connection_loop_is_stopping (connection)
		&& !http_connection_work_is_awaited (connection);

}

// reads what the client sent, looking for it without sleeping until the time of the cerver ends
// returns the bytes, 0 when its client left and less than 0 without them
static ssize_t http_connection_follow_read (const HttpConnection *connection) {

	const int sock_fd = http_connection_fd (connection);
	const u64 deadline = http_connection_microseconds () + connection->receive.http_cerver->keep_alive_follow;

	ssize_t received = 0;
	do {
		received = recv (sock_fd, http_connection_follow_buffer, HTTP_CONNECTION_FOLLOW_BUFFER_SIZE, MSG_DONTWAIT);
	} while (
		(received < 0) && ((errno == EAGAIN) || (errno == EINTR))
		&& http_connection_follow_waits (connection, deadline)
	);

	return received;

}

// the connection goes back to its loop with what the thread found of its next request
static HttpConnectionFollow http_connection_follow_give (
	HttpConnection *connection, const HttpConnectionOutcome outcome
) {

	connection->outcome = outcome;

	event_loop_complete (connection->loop, connection);

	return HTTP_CONNECTION_FOLLOW_GIVEN;

}

// the thread handles the next request of its connection when it is complete, its loop takes requests,
// it belongs to the pool of the thread and no request waits in its loop for a place in that pool:
// the loop decides for the others, like for any request
static bool http_connection_follow_handles (HttpConnection *connection, const unsigned int lane) {

	const EventLoop *loop = connection->loop;

	http_connection_pool_select (connection);

	return (connection->receive.pause == HTTP_RECEIVE_PAUSE_MESSAGE)
		&& (connection->lane == lane)
		&& !__atomic_load_n (&loop->rejecting, __ATOMIC_ACQUIRE)
		&& !event_loop_lane_parked (loop, lane);

}

// the parser stopped: the bytes behind the request are kept like the loop keeps them
static HttpConnectionFollow http_connection_follow_paused (
	HttpConnection *connection, const char *tail, const size_t length, const unsigned int lane
) {

	if (http_connection_pending_keep (connection, tail, length)) {
		connection->job = HTTP_CONNECTION_JOB_AUTHORIZE;

		return http_connection_follow_give (connection, HTTP_CONNECTION_OUTCOME_DROPPED);
	}

	connection->receive.unparsed = connection->pending.length;

	return http_connection_follow_handles (connection, lane) ?
		HTTP_CONNECTION_FOLLOW_HANDLE :
		http_connection_follow_give (connection, HTTP_CONNECTION_OUTCOME_PAUSED);

}

// parses what the thread read, like its loop does with what it reads
static HttpConnectionFollow http_connection_follow_parse (
	HttpConnection *connection, const char *buffer, const size_t length, const unsigned int lane
) {

	HttpReceive *receive = &connection->receive;

	size_t n_parsed = 0;
	const HttpReceiveParse result = http_receive_execute (receive, buffer, length, &n_parsed);

	switch (result) {
		case HTTP_RECEIVE_PARSE_MORE:
			return http_connection_follow_give (connection, HTTP_CONNECTION_OUTCOME_READING);

		case HTTP_RECEIVE_PARSE_PAUSED:
		case HTTP_RECEIVE_PARSE_UPGRADE:
			return http_connection_follow_paused (connection, buffer + n_parsed, length - n_parsed, lane);

		default:
			http_receive_parser_failed (receive, n_parsed, (ssize_t) length);

			return http_connection_follow_give (connection, HTTP_CONNECTION_OUTCOME_FAILED);
	}

}

// the first bytes of the next request arrived: they are counted like the ones that the loop reads,
// and the clock of the request starts
static HttpConnectionFollow http_connection_follow_received (
	HttpConnection *connection, const char *buffer, const size_t length, const unsigned int lane
) {

	HttpReceive *receive = &connection->receive;
	HttpCerver *http_cerver = receive->http_cerver;
	const Cerver *cerver = connection->loop->cerver;

	connection->follow_misses = 0;

	receive->diagnostics.last_rc = (ssize_t) length;
	receive->diagnostics.last_errno = 0;
	receive->diagnostics.n_recvs += 1;

	connection->peer->socket->packet_buffer_size = length;

	CERVER_STATS_ADD (cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (cerver->stats->total_bytes_received, (u64) length);

	http_receive_request_arrived (receive);

	receive->received += length;

	const HttpConnectionFollow follow = http_connection_follow_parse (connection, buffer, length, lane);
	if (follow == HTTP_CONNECTION_FOLLOW_HANDLE) CERVER_STATS_ADD (http_cerver->n_followed_requests, 1);

	return follow;

}

// looks for the next request of the connection, with the buffer of the thread
static HttpConnectionFollow http_connection_follow_look (HttpConnection *connection) {

	HttpConnectionFollow follow = HTTP_CONNECTION_FOLLOW_NONE;

	// the pool of the thread, the next request can choose another one
	const unsigned int lane = connection->lane;

	const ssize_t received = http_connection_follow_read (connection);
	if (received > 0) {
		follow = http_connection_follow_received (connection, http_connection_follow_buffer, (size_t) received, lane);
	}

	else {
		http_connection_follow_missed (connection);
	}

	return follow;

}

static HttpConnectionFollow http_connection_work_follow (HttpConnection *connection) {

	return http_connection_follow_is_allowed (connection) ?
		http_connection_follow_look (connection) : HTTP_CONNECTION_FOLLOW_NONE;

}

#pragma endregion

#pragma region work

// the next request of a connection that is kept: the thread looks for it, and without it
// the connection waits idle in its loop (a loop that started to stop meanwhile closes its idle connections)
// returns true when the thread has that request, complete, to handle it
static bool http_connection_work_next (HttpConnection *connection) {

	const HttpConnectionFollow follow = http_connection_work_follow (connection);
	if (follow == HTTP_CONNECTION_FOLLOW_NONE) http_connection_work_idle (connection);

	return (follow == HTTP_CONNECTION_FOLLOW_HANDLE);

}

// the end of a request that a thread of the pool handled and whose connection is kept:
// the request is logged and counted and its data is released here, so the methods of the application
// that delete it never run in the thread of the loop; then the thread follows the connection or gives it back
// returns true when the thread has the next request of the connection to handle it
static bool http_connection_work_keep (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;
	const EventLoop *loop = connection->loop;

	bool handles = false;

	http_access_request (receive);

	if (loop->observer) loop->observer (receive, loop->observer_args);

	if (http_receive_reset (receive)) http_connection_work_abandon (connection);
	else if (connection->pending.length || http_connection_loop_is_stopping (connection)) http_connection_work_return (connection);
	else handles = http_connection_work_next (connection);

	return handles;

}

// returns true when the thread has the next request of the connection to handle it
static bool http_connection_work_end (HttpConnection *connection) {

	bool handles = false;

	if (http_receive_keeps_connection (&connection->receive)) handles = http_connection_work_keep (connection);
	else http_connection_work_close (connection);

	return handles;

}

// handles the request of the connection, and the ones that the thread finds while it follows it
static void http_connection_work_dispatch (HttpConnection *connection) {

	bool handles = true;
	while (handles) {
		http_receive_dispatch (&connection->receive);

		handles = http_connection_work_end (connection);
	}

}

// the request was authorized or rejected: the loop continues with its connection
static void http_connection_work_authorize (HttpConnection *connection) {

	connection->outcome = http_receive_headers_authorize (&connection->receive) ?
		HTTP_CONNECTION_OUTCOME_REJECTED : HTTP_CONNECTION_OUTCOME_RESUME;

	event_loop_complete (connection->loop, connection);

}

void http_connection_work (void *connection_ptr) {

	HttpConnection *connection = (HttpConnection *) connection_ptr;

	// the job left the queue of its pool: a connection that is parked for that pool can take its place
	event_loop_space (connection->loop, connection->lane);

	switch (connection->job) {
		case HTTP_CONNECTION_JOB_DISPATCH:
			http_connection_work_dispatch (connection);
			break;

		default:
			http_connection_work_authorize (connection);
			break;
	}

}

void http_connection_work_dropped (void *connection_ptr) {

	HttpConnection *connection = (HttpConnection *) connection_ptr;

	connection->outcome = HTTP_CONNECTION_OUTCOME_DROPPED;

	event_loop_complete (connection->loop, connection);

}

#pragma endregion

#pragma region completed

// the request was authorized: the parser continues with the bytes of the body that arrived with the headers,
// without them it still has to be told to continue
static void http_connection_resume (HttpConnection *connection) {

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_receive_resume (&connection->receive);

	http_connection_parse (
		connection,
		connection->pending.data ? connection->pending.data : http_connection_no_bytes,
		connection->pending.length
	);

}

// a connection that a thread followed comes back in the middle of its next request, that started when
// the thread read its first bytes: the loop can end it again, and it has the time of any request
static void http_connection_followed (HttpConnection *connection) {

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	event_loop_connection_attach (connection->loop, connection);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_connection_request_deadlines (connection);

}

void http_connection_completed (HttpConnection *connection) {

	switch (connection->outcome) {
		// the thread that followed it read a part of its next request: the loop reads the rest
		case HTTP_CONNECTION_OUTCOME_READING:
			http_connection_followed (connection);
			http_connection_settle (connection, HTTP_CONNECTION_NEXT_READ);
			break;

		// its next request needs another job: the loop gives it to its pool, like one that it parsed
		case HTTP_CONNECTION_OUTCOME_PAUSED:
			http_connection_followed (connection);
			http_connection_pause (connection);
			break;

		// its next request was rejected or could not be parsed, the thread that followed it answered it
		case HTTP_CONNECTION_OUTCOME_FAILED:
			http_connection_followed (connection);
			http_connection_rejected (connection);
			break;

		case HTTP_CONNECTION_OUTCOME_RESUME:
			http_connection_resume (connection);
			break;

		// the thread that ran the job answered it, the body can still be arriving
		case HTTP_CONNECTION_OUTCOME_REJECTED:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_rejected (connection);
			break;

		// the loop can end it again, the job of its request never ran
		case HTTP_CONNECTION_OUTCOME_DROPPED:
			event_loop_connection_attach (connection->loop, connection);
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_unavailable (connection);
			break;

		// its request was answered and its connection is kept: the loop can end it again
		case HTTP_CONNECTION_OUTCOME_KEPT:
			event_loop_connection_attach (connection->loop, connection);
			http_connection_next (connection);
			break;

		default:
			event_loop_connection_attach (connection->loop, connection);
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_end (connection);
			break;
	}

}

#pragma endregion

#pragma region events

// the client of an idle connection sent something (or it left): the loop takes the connection out
// of its idle ones and owns it again; its socket was reported once, so it is silent from now on
static void http_connection_idle_claim (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	if (event_loop_idle_claim (loop, connection)) {
		connection->source.silent = true;

		event_loop_connection_attach (loop, connection);
	}

}

void http_connection_ready (HttpConnection *connection, const unsigned int ready) {

	http_connection_idle_claim (connection);

	switch (connection->state) {
		// an error or a hang up is found by the read
		case HTTP_CONNECTION_STATE_READ:
		case HTTP_CONNECTION_STATE_IDLE:
			http_connection_read (connection);
			break;

		case HTTP_CONNECTION_STATE_WRITE:
			if (ready & (EVENT_READY_WRITE | EVENT_READY_ERROR)) http_connection_flush (connection);
			break;

		case HTTP_CONNECTION_STATE_CLOSING:
			http_connection_drain (connection);
			break;

		// a connection in another state is not watched
		default:
			http_connection_unwatch (connection);
			break;
	}

}

// a request that did not arrive in time is answered with 408 when its client sent a part of it
static void http_connection_read_timeout (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	receive->diagnostics.timed_out = true;

	http_receive_timed_out (receive, (receive->received > 0));

	http_connection_settle (connection, HTTP_CONNECTION_NEXT_CLOSE);

}

static void http_connection_expired (HttpConnection *connection) {

	switch (connection->state) {
		case HTTP_CONNECTION_STATE_READ:
			http_connection_read_timeout (connection);
			break;

		// no place in its pool in time
		case HTTP_CONNECTION_STATE_PARKED:
			event_loop_unpark (connection->loop, connection);
			http_connection_pool_refused (connection);
			http_connection_unavailable (connection);
			break;

		// a client that does not read its response
		case HTTP_CONNECTION_STATE_WRITE:
			http_receive_response_failed (&connection->receive, EAGAIN);
			http_connection_end (connection);
			break;

		case HTTP_CONNECTION_STATE_CLOSING:
			http_connection_end (connection);
			break;

		// an idle connection has no timer: its loop checks its idle ones (http_connection_idle_expired ())
		default: break;
	}

}

void http_connection_timeout (HttpConnection *connection) {

	const u64 deadline = connection->deadline;

	if (deadline) {
		// the deadline moved after the timer was armed
		if (connection->loop->now < deadline) {
			event_wheel_add (&connection->loop->wheel, &connection->timer, deadline);
		}

		else {
			connection->deadline = 0;

			http_connection_expired (connection);
		}
	}

}

void http_connection_abort (HttpConnection *connection) {

	switch (connection->state) {
		// a request that started is answered, a client that sent nothing is only closed
		case HTTP_CONNECTION_STATE_READ:
			if (connection->receive.received > 0) {
				connection->job = HTTP_CONNECTION_JOB_AUTHORIZE;
				http_connection_unavailable (connection);
			}

			else {
				http_receive_timed_out (&connection->receive, false);
				http_connection_end (connection);
			}
			break;

		case HTTP_CONNECTION_STATE_PARKED:
			event_loop_unpark (connection->loop, connection);
			http_connection_unavailable (connection);
			break;

		// a connection that waits for its next request has nothing to be answered
		case HTTP_CONNECTION_STATE_IDLE:
			http_connection_idle_close (connection);
			break;

		// the other states end by themselves: a write, a close, or a job of the pool
		default: break;
	}

}

#pragma endregion
