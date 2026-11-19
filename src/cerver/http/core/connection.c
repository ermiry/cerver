#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

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
#include "cerver/http/core/recycle.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/responses/internal.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/websockets/internal.h"

#include "cerver/utils/log.h"

// how much a buffer of a connection grows each time that it is full
#define HTTP_CONNECTION_BUFFER_GROWTH		2

// the parts of a file that the loop gives to a socket before it handles its other connections
#define HTTP_CONNECTION_FILE_ROUNDS			4

// a valid pointer for the parser when a request continues without bytes
static const char http_connection_no_bytes[] = "";

static void http_connection_end (HttpConnection *connection);
static void http_connection_settle (HttpConnection *connection, const HttpConnectionNext next);
static void http_connection_unavailable (HttpConnection *connection);
static void http_connection_kept (HttpConnection *connection);
static void http_connection_idle (HttpConnection *connection);
static void http_connection_parked_count (const HttpConnection *connection);
static void http_connection_rejected (HttpConnection *connection);
static void http_connection_resume (HttpConnection *connection);
static void http_connection_continue (HttpConnection *connection);
static void http_connection_submit (HttpConnection *connection, const HttpConnectionJob job);
static bool http_connection_output_send (HttpConnection *connection);
static void http_connection_output_fail (HttpConnection *connection, const int error);
static int http_connection_output_error (const HttpConnection *connection);
static void http_connection_output_rested (HttpConnection *connection);
static u64 http_connection_send_timeout (const HttpConnection *connection);

// what happened with the part of the output that the loop was sending
typedef enum HttpConnectionProgress {

	// it was sent, the next one follows
	HTTP_CONNECTION_PROGRESS_DONE			= 0,
	// an operation of the backend sends it, its completion goes on
	HTTP_CONNECTION_PROGRESS_WAITS			= 1,
	// the send failed: the connection can have ended, nothing uses it anymore
	HTTP_CONNECTION_PROGRESS_FAILED			= 2

} HttpConnectionProgress;

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

		free (connection);
	}

}

HttpConnection *http_connection_new (void) {

	HttpConnection *connection = (HttpConnection *) calloc (1, sizeof (HttpConnection));
	if (connection) {
		event_handle_init (&connection->handle, EVENT_HANDLE_TYPE_CONNECTION, -1, connection);
		connection->timer.owner = connection;
		connection->node.type = EVENT_NODE_TYPE_CONNECTION;
		connection->deferred_node.type = EVENT_NODE_TYPE_DEFERRED;

		connection->arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
		connection->head_buffer = str_new (NULL);

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
		case HTTP_CONNECTION_STATE_ENDING: name = "Ending"; break;
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
		&& (state != HTTP_CONNECTION_STATE_IDLE)
		&& (state != HTTP_CONNECTION_STATE_ENDING);

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
// is armed again, so a deadline that changes with every read does not touch the wheel.
// The wait of a deferred response ends with the deadline of its request, whatever the one of its output
static void http_connection_deadline_set (HttpConnection *connection, const u64 deadline) {

	const u64 deferred = connection->deferred_deadline;

	connection->deadline = (deferred && (deferred < deadline)) ? deferred : deadline;

	if (!connection->timer.armed || (connection->deadline < connection->timer.deadline)) {
		event_wheel_add (&connection->loop->wheel, &connection->timer, connection->deadline);
	}

}

// a deferred response keeps the deadline of its request when the one of its output is not needed anymore
static void http_connection_deadline_clear (HttpConnection *connection) {

	connection->deadline = 0;

	event_wheel_remove (&connection->loop->wheel, &connection->timer);

	if (connection->deferred_deadline) {
		http_connection_deadline_set (connection, connection->deferred_deadline);
	}

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

	// its response has its whole send timeout, and none of its bytes left yet
	connection->output_budget = http_connection_send_timeout (connection);
	connection->output_waits = false;
	__atomic_store_n (&connection->output_sent, 0, __ATOMIC_SEQ_CST);

}

// what a read waits for at most: the idle timeout, the deadline of the request
// and, while they have not been completed, the one of the headers
static u64 http_connection_read_deadline (const HttpConnection *connection) {

	const HttpCerver *http_cerver = connection->receive.http_cerver;

	u64 deadline = connection->loop->now + (
		http_cerver->idle_timeout ? http_cerver->idle_timeout : CERVER_HTTP_DEFAULT_IDLE_TIMEOUT_MILLISECONDS
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

// the milliseconds that the loop waits to send a response, the send timeout of a response
static u64 http_connection_send_timeout (const HttpConnection *connection) {

	const Cerver *cerver = connection->loop->cerver;
	const HttpCerver *http_cerver = connection->receive.http_cerver;

	u64 timeout = (u64) CERVER_DEFAULT_SEND_TIMEOUT * HTTP_CONNECTION_MILLISECONDS_PER_SECOND;
	if (http_cerver && http_cerver->send_timeout) timeout = http_cerver->send_timeout;
	else if (cerver->send_timeout) timeout = (u64) cerver->send_timeout * HTTP_CONNECTION_MILLISECONDS_PER_SECOND;

	return timeout;

}

#pragma endregion

#pragma region read

// asks the backend for the next bytes of the socket, until a deadline; a read that was already asked
// only gets the deadline
// returns 0 on success, 1 when the backend did not take it (the connection ended)
static unsigned int http_connection_read_ask (HttpConnection *connection, const u64 deadline) {

	unsigned int retval = 0;

	if (!(connection->handle.operations & EVENT_OPERATION_RECEIVE)) {
		retval = event_backend_receive (connection->loop->backend, &connection->handle);
	}

	if (!retval) {
		http_connection_deadline_set (connection, deadline);
	}

	else {
		cerver_log_error (
			"http_connection_read_ask () - failed to ask for the bytes of fd %d - errno %d",
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
static void http_connection_request_release (HttpConnection *connection) {

	const EventLoop *loop = connection->loop;
	if (loop->observer) loop->observer (&connection->receive, loop->observer_args);

	cerver_http_connection_ended (connection->receive.http_cerver, connection->receive.n_requests);

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

static void http_connection_output_delete (HttpConnectionOutput *output) {

	if (output->file >= 0) (void) close (output->file);

	http_recycle_buffer_give_back (output, output->capacity);

}

// what was not sent of the output of a connection that ended or failed.
// The part whose bytes the backend is sending stays until that send ends: the kernel reads its memory
static void http_connection_output_drop (HttpConnection *connection) {

	HttpConnectionOutput *sending = (connection->handle.operations & EVENT_OPERATION_SEND) ?
		connection->output_head : NULL;

	HttpConnectionOutput *output = sending ? sending->next : connection->output_head;
	while (output) {
		HttpConnectionOutput *next = output->next;

		http_connection_output_delete (output);

		output = next;
	}

	if (sending) sending->next = NULL;

	connection->output_head = sending;
	connection->output_tail = sending;

	__atomic_store_n (&connection->output_queued, 0, __ATOMIC_SEQ_CST);

}

// the memory of a connection that ended goes back to its loop, ready for another connection;
// a connection whose arena could not be created again is released
static void http_connection_recycle (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_NONE);

	http_connection_arena_reset (connection);

	http_connection_buffer_clear (&connection->pending);

	http_connection_output_drop (connection);
	__atomic_store_n (&connection->output_failed, false, __ATOMIC_SEQ_CST);
	__atomic_store_n (&connection->output_error, 0, __ATOMIC_SEQ_CST);
	connection->next = HTTP_CONNECTION_NEXT_NONE;

	connection->drained = 0;

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	event_handle_init (&connection->handle, EVENT_HANDLE_TYPE_CONNECTION, -1, connection);

	connection->peer = NULL;

	event_loop_connection_ended (loop);

	if (connection->arena) {
		event_loop_connection_recycle (loop, connection);
	}

	else {
		http_connection_delete (connection);
	}

}

// the socket of a connection that ended is closed once the backend has none of its operations
static void http_connection_finalize (HttpConnection *connection) {

	connection_drop (connection->loop->cerver, connection->peer);

	http_connection_recycle (connection);

}

// the backend ended the operations of a connection that had ended
static void http_connection_stopped (HttpConnection *connection) {

	if (connection->state == HTTP_CONNECTION_STATE_ENDING) {
		connection->loop->n_ending -= 1;

		http_connection_finalize (connection);
	}

}

// a connection that ends leaves the lists of its loop and is counted out at once: its place is free.
// Its socket is closed when the backend has none of its operations: at once, or when it tells
static void http_connection_leave (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	event_loop_idle_remove (loop, connection);

	event_loop_connection_detach (loop, connection);

	event_loop_connection_gone (loop);

	if (event_backend_stop (loop->backend, &connection->handle)) {
		http_connection_set_state (connection, HTTP_CONNECTION_STATE_ENDING);

		loop->n_ending += 1;
	}

	else {
		http_connection_finalize (connection);
	}

}

// ends a connection that the loop owns: the request is logged and counted, its socket is closed
// a connection that ends between two requests has no request to log, it is counted as an idle one that was closed
static void http_connection_end (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_deadline_clear (connection);

	if (connection->parked) http_connection_parked_count (connection);
	event_loop_unpark (loop, connection);

	http_connection_output_drop (connection);

	if (http_receive_is_between_requests (&connection->receive)) event_loop_idle_closed (loop);
	else http_access_request (&connection->receive);

	http_connection_request_release (connection);

	http_connection_leave (connection);

}

// closes a connection whose request ended: what the client already sent is read first,
// a close with data that was not read answers with a RST that can destroy the response.
// A socket whose read is in the backend is not read here: the backend ends that read
static void http_connection_close (HttpConnection *connection) {

	const int sock_fd = http_connection_fd (connection);

	(void) shutdown (sock_fd, SHUT_WR);

	if (!(connection->handle.operations & EVENT_OPERATION_RECEIVE)) {
		(void) http_receive_drain_available (sock_fd);
	}

	http_connection_end (connection);

}

// what the client of a closing connection still sends is read and dropped:
// the connection ends with the end of the client, an error or HTTP_RECEIVE_DRAIN_SIZE bytes
static void http_connection_drained (HttpConnection *connection, const int received) {

	if (received > 0) connection->drained += (size_t) received;

	if ((received <= 0) || (connection->drained >= HTTP_RECEIVE_DRAIN_SIZE)) {
		http_connection_end (connection);
	}

	else {
		(void) http_connection_read_ask (connection, connection->deadline);
	}

}

// closes a connection whose request was rejected before it was complete: its client can still be
// sending it, so its writes are shut down and the rest is read for a short time before the close
static void http_connection_linger (HttpConnection *connection) {

	(void) shutdown (http_connection_fd (connection), SHUT_WR);

	event_loop_idle_remove (connection->loop, connection);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_CLOSING);

	connection->drained = 0;

	const u64 deadline = connection->loop->now + (
		(u64) HTTP_RECEIVE_DRAIN_TIMEOUT * HTTP_CONNECTION_MILLISECONDS_PER_SECOND
	);

	// the deadline of the read that the connection had can be later
	http_connection_deadline_clear (connection);

	(void) http_connection_read_ask (connection, deadline);

}

// the 101 of an upgrade was sent: its WebSocket takes the socket to its own thread, and the bytes that
// followed the request are its first ones. The backend of the loop does not watch the socket anymore
static void http_connection_upgrade (HttpConnection *connection) {

	EventLoop *loop = connection->loop;
	HttpReceive *receive = &connection->receive;

	http_connection_deadline_clear (connection);

	http_receive_web_socket_pending (
		receive, connection->pending.data ? connection->pending.data : http_connection_no_bytes
	);

	http_access_request (receive);

	event_loop_connection_detach (loop, connection);

	event_loop_connection_gone (loop);

	(void) event_backend_stop (loop->backend, &connection->handle);

	if (!http_web_socket_start (&connection->cerver_receive, receive)) {
		connection_drop (loop->cerver, connection->peer);
	}

	http_connection_request_release (connection);

	http_connection_recycle (connection);

}

#pragma endregion

#pragma region output

static char *http_connection_output_data (HttpConnectionOutput *output) {

	return (char *) (output + 1);

}

// a part of the output with bytes, in memory for at least reserve of them
static HttpConnectionOutput *http_connection_output_new (
	HttpConnection *connection, const size_t length, const size_t reserve
) {

	size_t capacity = 0;
	HttpConnectionOutput *output = (HttpConnectionOutput *) http_recycle_buffer_take (
		sizeof (HttpConnectionOutput) + ((reserve > length) ? reserve : length), &capacity
	);

	if (output) {
		(void) memset (output, 0, sizeof (HttpConnectionOutput));

		output->node.type = EVENT_NODE_TYPE_OUTPUT;
		output->connection = connection;
		output->capacity = capacity;
		output->length = length;
		output->file = -1;
	}

	return output;

}

bool http_connection_output_is_failed (const HttpConnection *connection) {

	return __atomic_load_n (&connection->output_failed, __ATOMIC_SEQ_CST);

}

static bool http_connection_output_is_pending (const HttpConnection *connection) {

	return connection->output_head != NULL;

}

// the output is sent in the order that it was written
static void http_connection_output_append (HttpConnection *connection, HttpConnectionOutput *output) {

	output->next = NULL;

	if (connection->output_tail) connection->output_tail->next = output;
	else connection->output_head = output;

	connection->output_tail = output;

}

static void http_connection_output_pop (HttpConnection *connection) {

	HttpConnectionOutput *output = connection->output_head;

	connection->output_head = output->next;
	if (!connection->output_head) connection->output_tail = NULL;

	http_connection_output_delete (output);

}

// the bytes that left: a thread that waits for its client to read can go on
static void http_connection_output_count (HttpConnection *connection, const size_t bytes) {

	(void) __atomic_sub_fetch (&connection->output_queued, bytes, __ATOMIC_SEQ_CST);

	// only its loop adds; the thread that owns its request reads it when one of its writes fails
	(void) __atomic_add_fetch (&connection->output_sent, bytes, __ATOMIC_SEQ_CST);

	event_loop_output_sent (connection->loop);

}

// the bytes of what left are counted, without the status of the response, and the request has a response that
// failed: in its response stage when some of its bytes left, and in the one of its handler when none did (its
// client was gone before its response). Once: the thread whose write failed counts it, or its loop does
void http_connection_response_failed (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	if (!receive->response_failed) {
		http_receive_response_cut (receive, __atomic_load_n (&connection->output_sent, __ATOMIC_SEQ_CST));

		http_receive_response_failed (receive, http_connection_output_error (connection));
	}

}

// a response that could not be written whole (without memory) is one that failed: its client would not find
// its end. The thread that writes marks it, the loop of the connection ends it
static void http_connection_output_refuse (HttpConnection *connection, const int error) {

	__atomic_store_n (&connection->output_error, error, __ATOMIC_SEQ_CST);
	__atomic_store_n (&connection->output_failed, true, __ATOMIC_SEQ_CST);

}

// a thread of a pool waits for its loop to send what it wrote; a job that its dispatcher runs in the thread
// of the loop (a test, the fuzzer) would wait for itself, and so would the loop when it writes the response
// that its own handler deferred
static bool http_connection_output_can_wait (const HttpConnection *connection) {

	const EventDispatcher *dispatcher = connection->loop->lanes[connection->lane].dispatcher;

	return (!dispatcher || !dispatcher->in_loop) && !event_loop_is_current (connection->loop);

}

// a part of the output that a thread wrote for the loop to send: a thread of a pool gives it through the
// queue of the loop, and waits when its client has too much to read; the loop adds it to its output itself
static void http_connection_output_give (HttpConnection *connection, HttpConnectionOutput *output, const size_t bytes) {

	(void) __atomic_add_fetch (&connection->output_queued, bytes, __ATOMIC_SEQ_CST);

	if (http_connection_is_loop_owned (connection)) {
		http_connection_output_append (connection, output);
	}

	else {
		event_loop_give (connection->loop, &output->node);

		if (
			http_connection_output_can_wait (connection)
			&& (__atomic_load_n (&connection->output_queued, __ATOMIC_SEQ_CST) > HTTP_CONNECTION_OUTPUT_LIMIT)
		) {
			(void) event_loop_output_wait (
				connection->loop, &connection->output_queued, HTTP_CONNECTION_OUTPUT_RESUME,
				&connection->output_failed, http_connection_send_timeout (connection)
			);
		}
	}

}

// the errno of a write that can not be taken: the one of the send that failed before
static int http_connection_output_error (const HttpConnection *connection) {

	const int error = __atomic_load_n (&connection->output_error, __ATOMIC_SEQ_CST);

	return error ? error : EPIPE;

}

static void http_connection_output_copy (char *data, const struct iovec *parts, const int n_parts) {

	for (int idx = 0; idx < n_parts; idx++) {
		if (parts[idx].iov_len) (void) memcpy (data, parts[idx].iov_base, parts[idx].iov_len);

		data += parts[idx].iov_len;
	}

}

// what the loop writes while more requests of a pipeline wait goes to a part with room for their responses
static size_t http_connection_output_reserve (const HttpConnection *connection) {

	return (http_connection_is_loop_owned (connection) && connection->pending.length) ?
		(HTTP_CONNECTION_OUTPUT_BATCH_SIZE - sizeof (HttpConnectionOutput)) : 0;

}

// what the loop writes fits in the last part of its output: the responses of a pipeline are one send.
// Only the loop writes there: its memory does not move, so the bytes of that part that are being sent stay
static bool http_connection_output_joins (const HttpConnection *connection, const size_t bytes) {

	const HttpConnectionOutput *tail = connection->output_tail;

	return http_connection_is_loop_owned (connection) && tail && (tail->file < 0)
		&& ((tail->capacity - sizeof (HttpConnectionOutput) - tail->length) >= bytes);

}

static void http_connection_output_join (
	HttpConnection *connection, const struct iovec *parts, const int n_parts, const size_t bytes
) {

	HttpConnectionOutput *tail = connection->output_tail;

	http_connection_output_copy (http_connection_output_data (tail) + tail->length, parts, n_parts);

	tail->length += bytes;

	(void) __atomic_add_fetch (&connection->output_queued, bytes, __ATOMIC_SEQ_CST);

}

u8 http_connection_send (
	HttpConnection *connection, const struct iovec *parts, const int n_parts,
	size_t *sent, int *error
) {

	u8 retval = 1;

	size_t total = 0;
	for (int idx = 0; idx < n_parts; idx++) total += parts[idx].iov_len;

	*sent = 0;
	*error = 0;

	if (http_connection_output_is_failed (connection)) {
		*error = http_connection_output_error (connection);
	}

	else if (http_connection_output_joins (connection, total)) {
		http_connection_output_join (connection, parts, n_parts, total);

		*sent = total;

		retval = 0;
	}

	else {
		HttpConnectionOutput *output = http_connection_output_new (
			connection, total, http_connection_output_reserve (connection)
		);

		if (output) {
			http_connection_output_copy (http_connection_output_data (output), parts, n_parts);

			http_connection_output_give (connection, output, total);

			// a thread that waited for its loop to send can find that its client did not read in time
			if (http_connection_output_is_failed (connection)) {
				*error = http_connection_output_error (connection);
			}

			else {
				*sent = total;

				retval = 0;
			}
		}

		else {
			*error = ENOMEM;

			http_connection_output_refuse (connection, ENOMEM);
		}
	}

	return retval;

}

u8 http_connection_send_file (
	HttpConnection *connection, const int file, const off_t offset, const size_t length, int *error
) {

	u8 retval = 1;

	*error = 0;

	if (http_connection_output_is_failed (connection)) {
		*error = http_connection_output_error (connection);
	}

	else {
		// the caller closes its file when it returns, the loop sends it later
		const int own = dup (file);

		HttpConnectionOutput *output = (own >= 0) ? http_connection_output_new (connection, 0, 0) : NULL;
		if (output) {
			output->file = own;
			output->file_offset = offset;
			output->file_length = length;

			http_connection_output_give (connection, output, length);

			retval = 0;
		}

		else {
			*error = (own >= 0) ? ENOMEM : errno;

			if (own >= 0) (void) close (own);

			http_connection_output_refuse (connection, *error);
		}
	}

	return retval;

}

// what was sent before the request goes on (a "100 Continue", the responses of the requests before it in its
// pipeline) is not its response
static bool http_connection_next_ends_response (const HttpConnectionNext next) {

	return (next != HTTP_CONNECTION_NEXT_READ) && (next != HTTP_CONNECTION_NEXT_JOB);

}

// what the loop does with a connection that has nothing left to send: the response of its request was sent
// whole, and it is counted now, with its status
static void http_connection_proceed (
	HttpConnection *connection, const HttpConnectionNext next
) {

	connection->next = HTTP_CONNECTION_NEXT_NONE;

	if (http_connection_next_ends_response (next)) http_receive_response_sent (&connection->receive);

	switch (next) {
		case HTTP_CONNECTION_NEXT_READ:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			(void) http_connection_read_ask (connection, http_connection_read_deadline (connection));
			break;

		case HTTP_CONNECTION_NEXT_LINGER:
			http_connection_linger (connection);
			break;

		case HTTP_CONNECTION_NEXT_KEEP:
			http_connection_kept (connection);
			break;

		case HTTP_CONNECTION_NEXT_JOB:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_submit (connection, connection->job);
			break;

		default:
			if (connection->receive.upgraded) http_connection_upgrade (connection);
			else http_connection_close (connection);
			break;
	}

}

// after a request was answered or parsed: what is left of its response is sent first,
// then the connection reads again, starts its next request, lingers or closes
static void http_connection_settle (
	HttpConnection *connection, const HttpConnectionNext next
) {

	if (http_connection_output_is_failed (connection)) {
		http_connection_response_failed (connection);

		http_connection_end (connection);
	}

	else if (http_connection_output_is_pending (connection)) {
		connection->next = next;

		http_connection_set_state (connection, HTTP_CONNECTION_STATE_WRITE);

		http_connection_deadline_clear (connection);

		(void) http_connection_output_send (connection);
	}

	else {
		http_connection_proceed (connection, next);
	}

}

bool http_connection_is_in_loop (const HttpConnection *connection) {

	return event_loop_is_current (connection->loop);

}

// the stream of a deferred response that waits for its next piece: its handler returned, nobody completed its
// request, and it asked to be told when what it wrote was sent
static bool http_connection_stream_waits (const HttpConnection *connection) {

	return connection->receive.stream_drained
		&& (__atomic_load_n (&connection->deferred, __ATOMIC_SEQ_CST) == HTTP_CONNECTION_DEFERRED_WAITING)
		&& !connection->receive.authentication_deferred;

}

// the output was sent: a connection whose request ended does what follows, one whose handler still runs
// waits for it; the stream of a deferred response is told, so it writes its next piece
static void http_connection_output_flushed (HttpConnection *connection) {

	http_connection_output_rested (connection);

	http_connection_deadline_clear (connection);

	if (connection->state == HTTP_CONNECTION_STATE_WRITE) {
		http_connection_proceed (connection, connection->next);
	}

	else if (http_connection_stream_waits (connection)) {
		connection->receive.stream_drained (&connection->receive, connection->receive.stream_drained_data);
	}

}

static void http_connection_output_failed_log (
	const HttpConnection *connection, const int error
) {

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_HTTP,
		"http_connection_output_send () - the send failed - fd %d - errno %d (%s) - pending %zu",
		http_connection_fd (connection), error,
		strerror_r (error, error_description, sizeof (error_description)),
		__atomic_load_n (&connection->output_queued, __ATOMIC_SEQ_CST)
	);

}

// a send failed, or its client did not read in time: nothing more is sent. A connection whose request ended
// ends now; one whose handler still runs ends when its thread gives it back, and what the thread writes
// until then fails
static void http_connection_output_fail (HttpConnection *connection, const int error) {

	http_connection_output_failed_log (connection, error);

	__atomic_store_n (&connection->output_error, error, __ATOMIC_SEQ_CST);
	__atomic_store_n (&connection->output_failed, true, __ATOMIC_SEQ_CST);

	http_connection_output_drop (connection);

	event_loop_output_sent (connection->loop);

	// nothing is left to wait for: a response that is deferred ends when it is completed
	connection->deferred_deadline = 0;

	http_connection_deadline_clear (connection);

	if (connection->state == HTTP_CONNECTION_STATE_WRITE) {
		http_connection_response_failed (connection);

		http_connection_end (connection);
	}

}

// gives the next bytes of a file to the socket; a file that ends before its length has no more
static HttpConnectionProgress http_connection_output_file_once (
	HttpConnection *connection, HttpConnectionOutput *output
) {

	HttpConnectionProgress progress = HTTP_CONNECTION_PROGRESS_DONE;

	const size_t chunk = (output->file_length < HTTP_CONNECTION_FILE_CHUNK) ?
		output->file_length : HTTP_CONNECTION_FILE_CHUNK;

	const ssize_t sent = sendfile (http_connection_fd (connection), output->file, &output->file_offset, chunk);

	if (sent > 0) {
		output->file_length -= (size_t) sent;

		http_connection_output_count (connection, (size_t) sent);
	}

	else if (!sent) {
		http_connection_output_count (connection, output->file_length);

		output->file_length = 0;
	}

	else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
		(void) event_backend_writable (connection->loop->backend, &connection->handle);

		progress = HTTP_CONNECTION_PROGRESS_WAITS;
	}

	else if (errno != EINTR) {
		http_connection_output_fail (connection, errno);

		progress = HTTP_CONNECTION_PROGRESS_FAILED;
	}

	return progress;

}

// the file of a part goes to the socket without passing through the memory of cerver: some of it each time,
// then the loop handles its other connections and comes back when the socket can be written
static HttpConnectionProgress http_connection_output_file (
	HttpConnection *connection, HttpConnectionOutput *output
) {

	HttpConnectionProgress progress = HTTP_CONNECTION_PROGRESS_DONE;

	for (
		unsigned int round = 0;
		(progress == HTTP_CONNECTION_PROGRESS_DONE) && output->file_length && (round < HTTP_CONNECTION_FILE_ROUNDS);
		round++
	) {
		progress = http_connection_output_file_once (connection, output);
	}

	// the socket still takes bytes: the loop comes back for the rest
	if ((progress == HTTP_CONNECTION_PROGRESS_DONE) && output->file_length) {
		(void) event_backend_writable (connection->loop->backend, &connection->handle);

		progress = HTTP_CONNECTION_PROGRESS_WAITS;
	}

	return progress;

}

// a part of the output: its bytes are sent by the backend, then its file by the loop
static HttpConnectionProgress http_connection_output_part (
	HttpConnection *connection, HttpConnectionOutput *output
) {

	HttpConnectionProgress progress = HTTP_CONNECTION_PROGRESS_DONE;

	if (output->offset < output->length) {
		progress = HTTP_CONNECTION_PROGRESS_WAITS;

		if (event_backend_send (
			connection->loop->backend, &connection->handle,
			http_connection_output_data (output) + output->offset, output->length - output->offset
		)) {
			http_connection_output_fail (connection, errno ? errno : EIO);

			progress = HTTP_CONNECTION_PROGRESS_FAILED;
		}
	}

	else if (output->file_length) {
		progress = http_connection_output_file (connection, output);
	}

	return progress;

}

static bool http_connection_output_is_busy (const HttpConnection *connection) {

	return (connection->handle.operations & (EVENT_OPERATION_SEND | EVENT_OPERATION_WRITABLE)) != 0;

}

// all the sends of a response take its send timeout at most, together: the time that its output waits for its
// client counts from the part that started the wait until the output is sent, and the next wait has what is left.
// A handler that writes many parts, or the end of its request, never gives the response more time
static void http_connection_output_deadline (HttpConnection *connection) {

	if (!connection->output_waits) {
		connection->output_waits = true;
		connection->output_since = connection->loop->now;
	}

	const u64 deadline = connection->output_since + connection->output_budget;

	http_connection_deadline_clear (connection);

	http_connection_deadline_set (connection, deadline ? deadline : HTTP_CONNECTION_DEADLINE_FIRST);

}

// the output was sent: the time that it waited is taken from what the response of its request has left
static void http_connection_output_rested (HttpConnection *connection) {

	if (connection->output_waits) {
		const u64 waited = connection->loop->now - connection->output_since;

		connection->output_budget = (waited < connection->output_budget) ? (connection->output_budget - waited) : 0;
		connection->output_waits = false;
	}

}

// sends the output, a part after the other, one operation at a time: the next one starts when it completes
// returns false when the send failed: the connection can have ended, the caller does not use it anymore
static bool http_connection_output_send (HttpConnection *connection) {

	bool alive = true;
	bool waits = http_connection_output_is_busy (connection);

	while (alive && !waits && connection->output_head) {
		switch (http_connection_output_part (connection, connection->output_head)) {
			case HTTP_CONNECTION_PROGRESS_DONE: http_connection_output_pop (connection); break;
			case HTTP_CONNECTION_PROGRESS_WAITS: waits = true; break;
			default: alive = false; break;
		}
	}

	if (alive && waits && !connection->deadline) http_connection_output_deadline (connection);

	return alive;

}

// what follows a part that the socket took: the next ones, and the end of the output
static void http_connection_output_continue (HttpConnection *connection) {

	if (
		http_connection_output_send (connection)
		&& !connection->output_head && !http_connection_output_is_busy (connection)
	) {
		http_connection_output_flushed (connection);
	}

}

// the socket took some bytes of the first part; a connection that failed while its send was in the backend
// has nothing to go on with
static void http_connection_output_sent (HttpConnection *connection, const int result) {

	if (http_connection_output_is_failed (connection)) {
		http_connection_output_drop (connection);
	}

	else if (result < 0) {
		http_connection_output_fail (connection, -result);
	}

	else if (connection->output_head) {
		connection->output_head->offset += (size_t) result;

		http_connection_output_count (connection, (size_t) result);

		http_connection_output_continue (connection);
	}

}

// the socket can be written again: the file of the first part goes on
static void http_connection_output_writable (HttpConnection *connection, const int result) {

	if (!http_connection_output_is_failed (connection)) {
		if (result < 0) http_connection_output_fail (connection, -result);
		else http_connection_output_continue (connection);
	}

}

// a thread of a pool wrote a part of a response: the loop sends it after the ones before it, while the
// handler runs. A connection that failed has no use for it
void http_connection_output_given (EventNode *node) {

	// the node of a part of the output is its first field
	HttpConnectionOutput *output = (HttpConnectionOutput *) node;
	HttpConnection *connection = output->connection;

	if (http_connection_output_is_failed (connection)) {
		http_connection_output_delete (output);
	}

	else {
		http_connection_output_append (connection, output);

		http_connection_output_deadline (connection);

		(void) http_connection_output_send (connection);
	}

}

#pragma endregion

#pragma region dispatch

// gives the job of the connection to the dispatcher
// the state changes first: the thread that takes the job can run it and give it back at once
// returns 0 when it was taken (the loop does not use its request until it comes back), 1 when there is no place
static unsigned int http_connection_dispatch (HttpConnection *connection) {

	EventLoop *loop = connection->loop;
	EventDispatcher *dispatcher = loop->lanes[connection->lane].dispatcher;

	const HttpConnectionState previous = connection->state;

	http_connection_set_state (
		connection,
		(connection->job == HTTP_CONNECTION_JOB_DISPATCH) ?
			HTTP_CONNECTION_STATE_DISPATCHED : HTTP_CONNECTION_STATE_AUTH
	);

	const unsigned int retval = dispatcher->submit (dispatcher, connection);
	if (retval) http_connection_set_state (connection, previous);

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

// the pool of the event loops has no threads and no lane: its requests never leave their loop
static bool http_connection_pool_is_loop (const struct HttpPool *pool) {

	return pool && pool->in_loop;

}

// what uses the disk never runs in an event loop: a file of a static path is served by the default pool of
// threads, and the body of a request that is saved to the disk is parsed and written there
static bool http_connection_reads_disk (const HttpConnection *connection) {

	return (connection->receive.type == HTTP_RECEIVE_TYPE_FILE)
		|| (connection->job == HTTP_CONNECTION_JOB_BODY);

}

// the job of a request runs in the pool that its endpoint or one of its groups chose, or in the one that its
// cerver set for the others (the event loops, until it chooses another one); the default pool of threads runs
// what reads the disk, and the requests of a pool that the loop has no lane for
static void http_connection_pool_select (HttpConnection *connection) {

	const HttpCerver *http_cerver = connection->receive.http_cerver;
	const HttpEndpoint *endpoint = connection->receive.endpoint;

	struct HttpPool *pool = endpoint ? endpoint->resolved.pool : NULL;

	if (!pool && http_cerver && !http_connection_reads_disk (connection)) {
		pool = http_cerver->handlers_pool;
	}

	if (
		!pool
		|| (!http_connection_pool_is_loop (pool) && !http_connection_loop_has_lane (connection->loop, pool->index))
	) {
		pool = http_connection_pool_default (connection);
	}

	connection->pool = pool;
	connection->lane = (pool && !pool->in_loop) ? pool->index : EVENT_LOOP_LANE_DEFAULT;

}

// the request of the connection is handled by its loop: the pool that it chose is the one of the event loops
static bool http_connection_runs_in_loop (HttpConnection *connection) {

	http_connection_pool_select (connection);

	return http_connection_pool_is_loop (connection->pool);

}

static bool http_connection_pool_is_full (const HttpConnection *connection) {

	const EventDispatcher *dispatcher = connection->loop->lanes[connection->lane].dispatcher;

	return !http_connection_pool_is_loop (connection->pool) && dispatcher->is_full (dispatcher);

}

// the job waits in the loop for a place in its pool, without a thread
static void http_connection_park (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_PARKED);

	connection->parked_since = loop->now;

	event_loop_park (loop, connection);

	if (connection->pool) (void) __atomic_add_fetch (&connection->pool->n_parked, 1, __ATOMIC_RELEASE);

	http_connection_deadline_set (
		connection,
		loop->now + (
			(u64) cerver_http_pool_wait_timeout (loop->cerver, connection->pool)
			* HTTP_CONNECTION_MILLISECONDS_PER_SECOND
		)
	);

}

// each pool has its own policy and its own wait, the ones of the cerver when they were not set
static bool http_connection_can_wait (const HttpConnection *connection) {

	const EventLoop *loop = connection->loop;

	return (cerver_http_pool_overload_policy (loop->cerver, connection->pool) == CERVER_OVERLOAD_POLICY_WAIT)
		&& cerver_http_pool_wait_timeout (loop->cerver, connection->pool)
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

// leaves the wheel: the thread that runs the job owns the request of the connection
static void http_connection_submit (
	HttpConnection *connection, const HttpConnectionJob job
) {

	EventLoop *loop = connection->loop;

	connection->job = job;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

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

	// its timer leaves the wheel before the pool can own its request,
	// a connection that stays parked keeps the deadline that it had
	const u64 deadline = connection->deadline;

	// what is counted once it got its place: the thread that takes its job owns its request from then on
	struct HttpPool *pool = connection->pool;
	const u64 waited = connection->loop->now - connection->parked_since;

	http_connection_deadline_clear (connection);

	const unsigned int retval = http_connection_dispatch (connection);
	if (retval) http_connection_deadline_set (connection, deadline);
	else cerver_http_pool_parked_count (pool, waited);

	return retval;

}

// a request that stops waiting for a place in its pool without getting it: its wait ended, the cerver stops,
// or its connection ended
static void http_connection_parked_count (const HttpConnection *connection) {

	cerver_http_pool_parked_count (connection->pool, connection->loop->now - connection->parked_since);

}

#pragma endregion

#pragma region deferred

// a handler defers the response of its request, returns, and the request waits without a thread until any
// thread completes it. The handler and who completes it race: the state of the deferred response changes
// with one atomic exchange each time, and only who wins an exchange touches the request after it

// one exchange of the state of a deferred response, false when it was not the expected one
static bool http_connection_deferred_exchange (
	HttpConnection *connection, const HttpConnectionDeferred from, const HttpConnectionDeferred to
) {

	unsigned int expected = (unsigned int) from;

	return __atomic_compare_exchange_n (
		&connection->deferred, &expected, (unsigned int) to, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
	);

}

bool http_connection_is_deferred (const HttpConnection *connection) {

	return __atomic_load_n (&connection->deferred, __ATOMIC_SEQ_CST) != HTTP_CONNECTION_DEFERRED_NONE;

}

// a handler that its loop runs: from here its request is of who completes it, like the one of a pool,
// and what is written of its response goes through the queue of the loop, in the order that it is written
static void http_connection_defer_in_loop (HttpConnection *connection) {

	// the step that the loop was running: the authorization of a request before its body, or its dispatch
	const bool authorizing = (connection->receive.pause == HTTP_RECEIVE_PAUSE_HEADERS);

	connection->job = authorizing ? HTTP_CONNECTION_JOB_AUTHORIZE : HTTP_CONNECTION_JOB_DISPATCH;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	http_connection_deadline_clear (connection);

	http_connection_set_state (
		connection, authorizing ? HTTP_CONNECTION_STATE_AUTH : HTTP_CONNECTION_STATE_DISPATCHED
	);

}

u8 http_connection_defer (HttpConnection *connection) {

	u8 retval = 1;

	if (http_connection_deferred_exchange (
		connection, HTTP_CONNECTION_DEFERRED_NONE, HTTP_CONNECTION_DEFERRED_ASKED
	)) {
		event_loop_connection_deferred (connection->loop, true);
		__atomic_store_n (&connection->deferred_counted, true, __ATOMIC_SEQ_CST);

		if (http_connection_is_loop_owned (connection)) http_connection_defer_in_loop (connection);

		retval = 0;
	}

	return retval;

}

// the verdict of the authentication of a request is deferred like a response: its handler returns, the request
// waits without a thread, and who gives the verdict races with that return in the same way. The request says
// what it waits for before the exchange: who gives the verdict can do it as soon as the exchange is made
u8 http_connection_defer_authentication (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	const bool was_deferred = receive->authentication_deferred;

	receive->authentication_deferred = true;

	const u8 retval = http_connection_defer (connection);

	if (retval) receive->authentication_deferred = was_deferred;

	return retval;

}

// the wait of a deferred response ends with the deadline of its request; the one of its output, when it has
// one, stays if it is earlier
static void http_connection_deferred_bound (HttpConnection *connection) {

	connection->deferred_deadline = connection->request_deadline;

	if (connection->deferred_deadline) {
		http_connection_deadline_set (
			connection, connection->deadline ? connection->deadline : connection->deferred_deadline
		);
	}

}

// a thread of a pool left a request with its response deferred; the request can have been completed,
// and ended, before the loop took the node
void http_connection_deferred (HttpConnection *connection) {

	const bool is_of_thread = (connection->state == HTTP_CONNECTION_STATE_DISPATCHED)
		|| (connection->state == HTTP_CONNECTION_STATE_AUTH);

	if (is_of_thread && http_connection_is_deferred (connection)) http_connection_deferred_bound (connection);

}

// how the job of a request that was deferred ended, once it was completed: a response was answered, and the
// verdict of an authentication makes its loop run again the step that waited for it
static HttpConnectionOutcome http_connection_deferred_outcome (const HttpConnection *connection) {

	return connection->receive.authentication_deferred ?
		HTTP_CONNECTION_OUTCOME_AUTHENTICATED : HTTP_CONNECTION_OUTCOME_ANSWERED;

}

// the handler returned: its request waits for who completes it. One that was completed before its handler
// returned ends now, and what follows its handler runs here; the request of a verdict has not been handled
static bool http_connection_deferred_waits (HttpConnection *connection) {

	const bool waits = http_connection_deferred_exchange (
		connection, HTTP_CONNECTION_DEFERRED_ASKED, HTTP_CONNECTION_DEFERRED_WAITING
	);

	if (!waits) {
		__atomic_store_n (&connection->deferred, HTTP_CONNECTION_DEFERRED_NONE, __ATOMIC_SEQ_CST);

		if (!connection->receive.authentication_deferred) http_receive_dispatch_end (&connection->receive);
	}

	return waits;

}

// the handler of a request returned in a thread of a pool: a request whose response it deferred is left to who
// completes it. The loop is told first, so it bounds the wait: the node is given before the request can be
// completed and its connection given back, so it is never given twice
static bool http_connection_deferred_leaves (HttpConnection *connection) {

	bool leaves = false;

	if (http_connection_is_deferred (connection)) {
		event_loop_give (connection->loop, &connection->deferred_node);

		leaves = http_connection_deferred_waits (connection);
	}

	return leaves;

}

// what a handler of the loop wrote before it deferred its response (the start of a stream) does not wait for
// who completes its request: the loop sends it now, like what a thread of a pool writes while its handler runs
static void http_connection_deferred_flush (HttpConnection *connection) {

	if (http_connection_output_is_pending (connection)) {
		http_connection_output_deadline (connection);

		(void) http_connection_output_send (connection);
	}

}

// the handler of a request returned in its loop: a request whose response it deferred waits there. What was
// written of a response that was completed before that is in the queue of the loop, in the order it was written,
// and its connection follows it there
static bool http_connection_deferred_parks (HttpConnection *connection) {

	const bool parks = http_connection_is_deferred (connection);

	if (parks) {
		if (http_connection_deferred_waits (connection)) {
			http_connection_deferred_bound (connection);

			http_connection_deferred_flush (connection);
		}

		else {
			connection->outcome = http_connection_deferred_outcome (connection);

			event_loop_give (connection->loop, &connection->node);
		}
	}

	return parks;

}

// the request waited for this: what follows its handler runs in the thread that completes it, and its
// connection goes back to its loop behind what was written of its response. Once the node is given the loop can
// end the request, and a cerver that stops can release the loop: this thread is not one of a pool, that a drain
// waits for, so the request stops counting as deferred here, after the last time that this thread uses the loop
static void http_connection_complete_waiting (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	// the request of a verdict has not been handled: its loop goes on with it
	if (!connection->receive.authentication_deferred) http_receive_dispatch_end (&connection->receive);

	connection->outcome = http_connection_deferred_outcome (connection);

	const bool counted = __atomic_exchange_n (&connection->deferred_counted, false, __ATOMIC_SEQ_CST);

	event_loop_give (loop, &connection->node);

	if (counted) event_loop_connection_deferred (loop, false);

}

u8 http_connection_complete (HttpConnection *connection) {

	u8 retval = 1;

	// the handler can return between the two exchanges: they are tried again while the response is deferred
	bool done = false;
	while (!done) {
		if (http_connection_deferred_exchange (
			connection, HTTP_CONNECTION_DEFERRED_WAITING, HTTP_CONNECTION_DEFERRED_NONE
		)) {
			http_connection_complete_waiting (connection);

			retval = 0;
			done = true;
		}

		// its handler has not returned: the request ends when it does
		else if (http_connection_deferred_exchange (
			connection, HTTP_CONNECTION_DEFERRED_ASKED, HTTP_CONNECTION_DEFERRED_COMPLETED
		)) {
			retval = 0;
			done = true;
		}

		else {
			const unsigned int deferred = __atomic_load_n (&connection->deferred, __ATOMIC_SEQ_CST);

			done = (deferred == HTTP_CONNECTION_DEFERRED_NONE) || (deferred == HTTP_CONNECTION_DEFERRED_COMPLETED);
		}
	}

	return retval;

}

// the verdict is written before the exchange that gives it: the thread that wins the request reads it
u8 http_connection_authenticate (HttpConnection *connection, const bool authenticated) {

	u8 retval = 1;

	HttpReceive *receive = &connection->receive;

	if (receive->authentication_deferred) {
		receive->authentication_verdict = authenticated ?
			HTTP_RECEIVE_VERDICT_ACCEPTED : HTTP_RECEIVE_VERDICT_REJECTED;

		retval = http_connection_complete (connection);
	}

	return retval;

}

// the request of a deferred response ended in its loop
static void http_connection_deferred_ended (HttpConnection *connection) {

	connection->deferred_deadline = 0;

	if (__atomic_exchange_n (&connection->deferred_counted, false, __ATOMIC_SEQ_CST)) {
		event_loop_connection_deferred (connection->loop, false);
	}

}

// a deferred response that was not completed before the deadline of its request: its client is not kept
// waiting, what is written of the response from now on fails, and the connection ends when it is completed
static void http_connection_deferred_expired (HttpConnection *connection) {

	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_HTTP,
		"http_connection_timeout () - the deferred response of a request was not completed in time - fd %d",
		http_connection_fd (connection)
	);

	CERVER_STATS_ADD (connection->receive.http_cerver->n_timeout_requests, 1);

	http_connection_output_fail (connection, ETIMEDOUT);

	(void) shutdown (http_connection_fd (connection), SHUT_RDWR);

}

// the deadline of a connection whose request is of a thread of a pool, or of who completes it: the one of a
// deferred response that nobody completed, or the one of a client that does not read what was written
static void http_connection_dispatched_expired (HttpConnection *connection) {

	const u64 deferred = connection->deferred_deadline;

	if (deferred && (connection->loop->now >= deferred)) http_connection_deferred_expired (connection);
	else http_connection_output_fail (connection, EAGAIN);

}

#pragma endregion

#pragma region parse

// keeps what followed a paused request, the buffer where it is belongs to the backend of the loop
// (or it is the same pending buffer, when the request continued from it)
// returns 0 on success, 1 on error
static unsigned int http_connection_pending_keep (
	HttpConnection *connection, const char *tail, const size_t length
) {

	unsigned int retval = 0;

	HttpConnectionBuffer *pending = &connection->pending;

	// a buffer without memory contains nothing: the bytes are moved only inside the memory that it has
	char *kept = pending->data;

	if (kept && http_connection_buffer_contains (pending, tail)) {
		if (length) (void) memmove (kept, tail, length);

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

// the loop goes on with the next request of a pipeline before the response of the one that it handled leaves:
// while requests wait behind it, up to a batch of them, and the loop keeps its connections
static bool http_connection_batch_goes_on (const HttpConnection *connection) {

	return connection->pending.length
		&& (connection->batch < HTTP_CONNECTION_PIPELINE_BATCH)
		&& !http_connection_output_is_failed (connection)
		&& !http_connection_loop_is_stopping (connection);

}

// a request of a pipeline that the loop handled ends when its response was written: it is logged and counted,
// its data is released, and the loop continues with the request behind it. Their responses leave together
static void http_connection_kept_batch (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;
	const EventLoop *loop = connection->loop;

	connection->batch += 1;

	http_receive_response_sent (receive);

	http_access_request (receive);

	if (loop->observer) loop->observer (receive, loop->observer_args);

	if (http_receive_reset (receive)) http_connection_settle (connection, HTTP_CONNECTION_NEXT_CLOSE);
	else http_connection_continue (connection);

}

// a job that leaves for a pool: the responses of the requests of its pipeline that the loop handled before it
// are sent first, so they do not wait for its handler; the job is given when they left
static void http_connection_hand (HttpConnection *connection, const HttpConnectionJob job) {

	if (http_connection_output_is_pending (connection)) {
		connection->job = job;

		http_connection_settle (connection, HTTP_CONNECTION_NEXT_JOB);
	}

	else {
		http_connection_submit (connection, job);
	}

}

// the loop handled the request: its response is sent, and then the request ends
static void http_connection_handled (HttpConnection *connection, const bool in_loop) {

	const bool keeps = http_receive_keeps_connection (&connection->receive);

	if (keeps && in_loop && http_connection_batch_goes_on (connection)) {
		http_connection_kept_batch (connection);
	}

	else {
		http_connection_settle (
			connection, keeps ? HTTP_CONNECTION_NEXT_KEEP : HTTP_CONNECTION_NEXT_CLOSE
		);
	}

}

// a complete request: cerver answers it with a short response in the loop, and the loop handles the ones
// of its own pool; anything else (a handler, a file, an authentication) goes to its pool of threads
static void http_connection_message (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	const bool is_short = http_receive_dispatch_is_inline (receive);
	const bool in_loop = !is_short && http_connection_runs_in_loop (connection);

	if (is_short || in_loop) {
		http_receive_dispatch (receive);

		// a handler of the loop that deferred its response: its request waits there for who completes it
		if (!http_connection_deferred_parks (connection)) http_connection_handled (connection, in_loop);
	}

	else {
		http_connection_hand (connection, HTTP_CONNECTION_JOB_DISPATCH);
	}

}

// the authentication of a request before its body: a thread of its pool runs it, or its loop when the request
// is of the pool of the event loops; one that is rejected was answered
static void http_connection_authorize (HttpConnection *connection) {

	if (!http_connection_runs_in_loop (connection)) {
		http_connection_hand (connection, HTTP_CONNECTION_JOB_AUTHORIZE);
	}

	// an authentication that deferred its verdict was not rejected: its request waits there for it
	else if (http_receive_headers_authorize (&connection->receive)) {
		if (!http_connection_deferred_parks (connection)) http_connection_rejected (connection);
	}

	else {
		http_connection_resume (connection);
	}

}

// the job that the parser stopped for: the authentication of the request before its body, or the complete request
static void http_connection_pause (HttpConnection *connection) {

	switch (connection->receive.pause) {
		case HTTP_RECEIVE_PAUSE_HEADERS:
			http_connection_authorize (connection);
			break;

		case HTTP_RECEIVE_PAUSE_MESSAGE:
			http_connection_message (connection);
			break;

		// the request was authorized, and its body is not parsed by the loop: the bytes that arrived with its
		// headers go to a thread of a pool
		case HTTP_RECEIVE_PAUSE_BODY:
			http_connection_resume (connection);
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

// what the parser said of the bytes of a connection, in its loop
static void http_connection_parse_result (
	HttpConnection *connection, const char *buffer, const size_t length,
	const HttpReceiveParse result, const size_t n_parsed
) {

	HttpReceive *receive = &connection->receive;

	switch (result) {
		// the request is not complete; a "100 Continue" can be waiting to be sent
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

// the bytes of the body of a request that is saved to the disk: the loop keeps them, and a thread of a pool
// parses them and writes the files of the request. One job for each read: the loop asks for the next bytes of
// the connection when the thread gave it back, so a client never sends faster than the disk takes
static void http_connection_body (
	HttpConnection *connection, const char *buffer, const size_t length
) {

	if (http_connection_pending_keep (connection, buffer, length)) {
		connection->job = HTTP_CONNECTION_JOB_AUTHORIZE;

		http_connection_unavailable (connection);
	}

	else {
		http_connection_hand (connection, HTTP_CONNECTION_JOB_BODY);
	}

}

// the bytes of a request are parsed by the loop that read them, except the body of one that is saved to the disk
static void http_connection_parse (
	HttpConnection *connection, const char *buffer, const size_t length
) {

	HttpReceive *receive = &connection->receive;

	if (receive->body_off_loop) {
		http_connection_body (connection, buffer, length);
	}

	else {
		size_t n_parsed = 0;
		const HttpReceiveParse result = http_receive_execute (receive, buffer, length, &n_parsed);

		http_connection_parse_result (connection, buffer, length, result, n_parsed);
	}

}

#pragma endregion

#pragma region received

// the read of a request that failed: its client left with an error
static void http_connection_read_error (HttpConnection *connection, const int error) {

	HttpReceive *receive = &connection->receive;

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
		"http_connection_received () - the read failed - fd %d - errno %d (%s) - received %zu - recv #%u",
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

// the first bytes of the next request of a connection that waited idle arrived:
// the clock and the deadlines of the request start now, the time that it waited idle is not part of it
static void http_connection_idle_leave (HttpConnection *connection) {

	event_loop_idle_remove (connection->loop, connection);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_receive_request_arrived (&connection->receive);

	http_connection_request_deadlines (connection);

}

static void http_connection_received_bytes (
	HttpConnection *connection, const char *bytes, const size_t length
) {

	EventLoop *loop = connection->loop;
	HttpReceive *receive = &connection->receive;

	if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_idle_leave (connection);

	// the bytes of a new step: the batch of requests that the loop handles in a row starts again
	connection->batch = 0;

	connection->peer->socket->packet_buffer_size = length;

	CERVER_STATS_ADD (loop->cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (loop->cerver->stats->total_bytes_received, (u64) length);

	receive->received += length;

	http_connection_parse (connection, bytes, length);

}

// what the client sent, its end or its error: the first bytes that an idle connection receives start
// its next request
static void http_connection_received_request (
	HttpConnection *connection, const char *bytes, const int received
) {

	HttpReceive *receive = &connection->receive;

	receive->diagnostics.last_rc = received;
	receive->diagnostics.last_errno = (received < 0) ? -received : 0;
	receive->diagnostics.n_recvs += 1;

	if (received > 0) http_connection_received_bytes (connection, bytes, (size_t) received);
	else http_connection_client_left (connection, (received < 0) ? -received : 0);

}

// the read of the socket completed
static void http_connection_received (
	HttpConnection *connection, const char *bytes, const int received
) {

	switch (connection->state) {
		case HTTP_CONNECTION_STATE_READ:
		case HTTP_CONNECTION_STATE_IDLE:
			http_connection_received_request (connection, bytes, received);
			break;

		case HTTP_CONNECTION_STATE_CLOSING:
			http_connection_drained (connection, received);
			break;

		// a connection in another state asked for no read
		default: break;
	}

}

#pragma endregion

#pragma region keep

// the connection waits for the next request of its client, at most the keep alive timeout of the cerver:
// it is one of the idle connections of its loop, with its read asked and its own timer
static void http_connection_idle (HttpConnection *connection) {

	EventLoop *loop = connection->loop;

	connection->job = HTTP_CONNECTION_JOB_NONE;
	connection->outcome = HTTP_CONNECTION_OUTCOME_NONE;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_IDLE);

	event_loop_idle_add (loop, connection);

	http_connection_deadline_clear (connection);

	(void) http_connection_read_ask (
		connection, loop->now + connection->receive.http_cerver->keep_alive_timeout
	);

}

// closes an idle connection whose client is still there: it did not send its next request in time,
// or its cerver stops; its writes are shut down and it is closed when its client closes it too,
// so a request that its client was sending at that moment is not answered with a reset
static void http_connection_idle_close (HttpConnection *connection) {

	http_connection_linger (connection);

}

void http_connection_evict (HttpConnection *connection) {

	if (connection->state == HTTP_CONNECTION_STATE_IDLE) http_connection_close (connection);

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

// a pipelined request waits for the next step of the loop, like a connection that comes back from a pool:
// a client that sends many of them does not keep the loop
static void http_connection_requeue (HttpConnection *connection) {

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_DISPATCHED);

	connection->outcome = HTTP_CONNECTION_OUTCOME_KEPT;

	event_loop_give (connection->loop, &connection->node);

}

// the request of a connection that is kept ended, its response was sent:
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

	event_handle_init (
		&connection->handle, EVENT_HANDLE_TYPE_CONNECTION, peer->socket->sock_fd, connection
	);

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

// a connection that could not start gives its memory back, its socket was never asked for anything
static void http_connection_start_failed (EventLoop *loop, HttpConnection *connection) {

	event_loop_connection_detach (loop, connection);

	event_loop_connection_gone (loop);

	event_loop_connection_ended (loop);

	event_loop_connection_recycle (loop, connection);

}

unsigned int http_connection_accepted (EventLoop *loop, Connection *peer) {

	unsigned int retval = 1;

	HttpConnection *connection = event_loop_connection_take (loop);
	if (connection && !http_connection_start (connection, peer)) {
		// what the client sent with its connection is asked for now
		(void) http_connection_read_ask (connection, http_connection_read_deadline (connection));

		retval = 0;
	}

	else {
		cerver_log_error (
			"http_connection_accepted () - failed to start the connection of fd %d",
			peer->socket->sock_fd
		);

		if (connection) http_connection_start_failed (loop, connection);

		connection_drop (loop->cerver, peer);
	}

	return retval;

}

#pragma endregion

#pragma region work

// the job of a connection, in a thread of its pool: what its handler writes goes to its loop as it is written,
// and the connection goes back when the handler ends. The thread does not use the socket
// how the authorization of a request before its body ended in a thread of a pool: the verdict of one whose
// authentication deferred it was given before its handler returned, and its loop authorizes it again with it
static HttpConnectionOutcome http_connection_work_authorized (const HttpConnection *connection, const int stopped) {

	HttpConnectionOutcome outcome = stopped ? HTTP_CONNECTION_OUTCOME_REJECTED : HTTP_CONNECTION_OUTCOME_RESUME;

	if (connection->receive.authentication_deferred) outcome = HTTP_CONNECTION_OUTCOME_AUTHENTICATED;

	return outcome;

}

// the bytes of the body of a request that its loop kept: this thread parses them, and the files of the request
// are written here. The loop goes on with what the parser said
static void http_connection_work_body (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	const HttpConnectionBuffer *pending = &connection->pending;

	http_receive_resume (receive);

	size_t n_parsed = 0;
	connection->parse_result = (unsigned int) http_receive_execute (
		receive, pending->data ? pending->data : http_connection_no_bytes, pending->length, &n_parsed
	);

	connection->parsed = n_parsed;

	connection->outcome = HTTP_CONNECTION_OUTCOME_PARSED;

}

void http_connection_work (void *connection_ptr) {

	HttpConnection *connection = (HttpConnection *) connection_ptr;

	// the job left the queue of its pool: a connection that is parked for that pool can take its place
	event_loop_space (connection->loop, connection->lane);

	if (connection->job == HTTP_CONNECTION_JOB_DISPATCH) {
		http_receive_dispatch (&connection->receive);

		// a request whose response, or the verdict of its authentication, was deferred is not of this thread
		// anymore: who completes it gives it back
		if (http_connection_deferred_leaves (connection)) return;

		connection->outcome = http_connection_deferred_outcome (connection);
	}

	else if (connection->job == HTTP_CONNECTION_JOB_BODY) {
		http_connection_work_body (connection);
	}

	else {
		const int stopped = http_receive_headers_authorize (&connection->receive);

		if (http_connection_deferred_leaves (connection)) return;

		connection->outcome = http_connection_work_authorized (connection, stopped);
	}

	event_loop_give (connection->loop, &connection->node);

}

void http_connection_work_dropped (void *connection_ptr) {

	HttpConnection *connection = (HttpConnection *) connection_ptr;

	connection->outcome = HTTP_CONNECTION_OUTCOME_DROPPED;

	event_loop_give (connection->loop, &connection->node);

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

// the handler of the request ended: a response that could not be sent is counted in its request, what is left
// of it is sent, and then the request ends and its connection is kept or closed
static void http_connection_answered (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	http_connection_deferred_ended (connection);

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	if (http_connection_output_is_failed (connection)) http_connection_response_failed (connection);

	http_connection_settle (
		connection,
		http_receive_keeps_connection (receive) ? HTTP_CONNECTION_NEXT_KEEP : HTTP_CONNECTION_NEXT_CLOSE
	);

}

// the verdict of the authentication of a request arrived: the step that waited for it runs again where it ran
// (its loop, or a thread of its pool), and its authentication takes the verdict instead of calling its handler.
// A request that lost its client while it waited is rejected, whatever its verdict
static void http_connection_authenticated (HttpConnection *connection) {

	HttpReceive *receive = &connection->receive;

	http_connection_deferred_ended (connection);

	receive->authentication_deferred = false;

	if (http_connection_output_is_failed (connection)) {
		receive->authentication_verdict = HTTP_RECEIVE_VERDICT_REJECTED;
	}

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	if (connection->job == HTTP_CONNECTION_JOB_AUTHORIZE) http_connection_authorize (connection);
	else http_connection_message (connection);

}

// a thread of a pool parsed the bytes of the body that the loop kept: the loop goes on like after it parses
// by itself, with the bytes that it kept
static void http_connection_parsed (HttpConnection *connection) {

	const HttpConnectionBuffer *pending = &connection->pending;

	// the job of the body ended: what follows it (the request, when it is complete) runs where its endpoint does
	connection->job = HTTP_CONNECTION_JOB_NONE;

	http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);

	http_connection_parse_result (
		connection,
		pending->data ? pending->data : http_connection_no_bytes, pending->length,
		(HttpReceiveParse) connection->parse_result, connection->parsed
	);

}

void http_connection_completed (HttpConnection *connection) {

	switch (connection->outcome) {
		case HTTP_CONNECTION_OUTCOME_RESUME:
			http_connection_resume (connection);
			break;

		case HTTP_CONNECTION_OUTCOME_AUTHENTICATED:
			http_connection_authenticated (connection);
			break;

		case HTTP_CONNECTION_OUTCOME_PARSED:
			http_connection_parsed (connection);
			break;

		// the thread that ran the job answered it, the body can still be arriving
		case HTTP_CONNECTION_OUTCOME_REJECTED:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_rejected (connection);
			break;

		// the job of its request never ran
		case HTTP_CONNECTION_OUTCOME_DROPPED:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_unavailable (connection);
			break;

		case HTTP_CONNECTION_OUTCOME_ANSWERED:
			http_connection_answered (connection);
			break;

		// its request ended in the loop, that continues with the bytes that followed it in a new step
		case HTTP_CONNECTION_OUTCOME_KEPT:
			connection->batch = 0;
			http_connection_next (connection);
			break;

		default:
			http_connection_set_state (connection, HTTP_CONNECTION_STATE_READ);
			http_connection_end (connection);
			break;
	}

}

#pragma endregion

#pragma region events

void http_connection_completion (HttpConnection *connection, const EventCompletion *completion) {

	switch (completion->operation) {
		case EVENT_OPERATION_RECEIVE:
			http_connection_received (connection, completion->data, completion->result);
			break;

		case EVENT_OPERATION_SEND:
			http_connection_output_sent (connection, completion->result);
			break;

		case EVENT_OPERATION_WRITABLE:
			http_connection_output_writable (connection, completion->result);
			break;

		case EVENT_OPERATION_STOP:
			http_connection_stopped (connection);
			break;

		default: break;
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
			http_connection_parked_count (connection);
			event_loop_unpark (connection->loop, connection);
			http_connection_pool_refused (connection);
			http_connection_unavailable (connection);
			break;

		// a client that does not read its response
		case HTTP_CONNECTION_STATE_WRITE:
			http_connection_output_fail (connection, EAGAIN);
			break;

		// also while its handler still runs; or a response, or the verdict of an authentication, that was
		// deferred and that nobody completed in time
		case HTTP_CONNECTION_STATE_AUTH:
		case HTTP_CONNECTION_STATE_DISPATCHED:
			http_connection_dispatched_expired (connection);
			break;

		case HTTP_CONNECTION_STATE_CLOSING:
			http_connection_end (connection);
			break;

		// its client did not send its next request in time
		case HTTP_CONNECTION_STATE_IDLE:
			http_connection_idle_close (connection);
			break;

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
			http_connection_parked_count (connection);
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
