#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/network/socket.h"

#include "cerver/http/core/http.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

// the parts of a frame that is written without the queue: its header and its payload
#define HTTP_WEB_SOCKET_QUEUE_PARTS					2

// the queue doubles its size until a frame fits
#define HTTP_WEB_SOCKET_QUEUE_GROWTH				2

// a send over half of the queue tells the application to wait for on_drain ()
#define HTTP_WEB_SOCKET_QUEUE_MARK_DIVISOR			2

// a queue without a deadline
#define HTTP_WEB_SOCKET_QUEUE_NO_DEADLINE			(-1)

#pragma region setup

// the size and the policy of the cerver, the ones of an endpoint that did not set its own
static void http_web_socket_queue_cerver_defaults (
	const HttpWebSocket *web_socket, size_t *max_queue, HttpWebSocketOverflow *overflow
) {

	const HttpWebSocketRegistry *registry = web_socket->http_cerver ? web_socket->http_cerver->web_sockets : NULL;

	*max_queue = registry ? registry->max_queue : HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE;
	*overflow = registry ? registry->overflow : HTTP_WEB_SOCKET_OVERFLOW_DROP;

}

void http_web_socket_queue_setup (HttpWebSocket *web_socket) {

	size_t max_queue = 0;
	HttpWebSocketOverflow overflow = HTTP_WEB_SOCKET_OVERFLOW_DROP;
	http_web_socket_queue_cerver_defaults (web_socket, &max_queue, &overflow);

	if (web_socket->settings.set & HTTP_WEB_SOCKET_SETTING_QUEUE) {
		if (web_socket->settings.max_queue) max_queue = web_socket->settings.max_queue;
		overflow = web_socket->settings.overflow;
	}

	web_socket->queue_max = max_queue ? max_queue : HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE;
	web_socket->overflow = overflow;

}

void http_web_socket_queue_delete (HttpWebSocket *web_socket) {

	free (web_socket->queue);

	web_socket->queue = NULL;
	web_socket->queue_capacity = 0;
	web_socket->queue_start = 0;
	web_socket->queue_end = 0;

}

#pragma endregion

#pragma region state

static size_t http_web_socket_queue_used (const HttpWebSocket *web_socket) {

	return web_socket->queue_end - web_socket->queue_start;

}

// the size that other threads read without the send mutex
static void http_web_socket_queue_publish (HttpWebSocket *web_socket) {

	__atomic_store_n (&web_socket->queue_size, http_web_socket_queue_used (web_socket), __ATOMIC_SEQ_CST);

}

bool http_web_socket_queue_is_empty (const HttpWebSocket *web_socket) {

	return __atomic_load_n (&web_socket->queue_size, __ATOMIC_SEQ_CST) == 0;

}

size_t http_web_socket_get_buffered_size (const HttpWebSocket *web_socket) {

	return web_socket ? __atomic_load_n (&web_socket->queue_size, __ATOMIC_SEQ_CST) : 0;

}

// the queue has its send timeout again: it wrote a byte, or it got its first one
static void http_web_socket_queue_progress (HttpWebSocket *web_socket) {

	socket_deadline_create_milliseconds (&web_socket->queue_deadline, http_web_socket_send_milliseconds (web_socket));

}

int http_web_socket_queue_remaining (const HttpWebSocket *web_socket) {

	return http_web_socket_queue_is_empty (web_socket) ?
		HTTP_WEB_SOCKET_QUEUE_NO_DEADLINE : socket_deadline_remaining (&web_socket->queue_deadline);

}

// the connection can still write: it did not end, it was not aborted and its writes were not ended
static bool http_web_socket_queue_is_writable (const HttpWebSocket *web_socket) {

	return (http_web_socket_get_state (web_socket) != HTTP_WEB_SOCKET_STATE_CLOSED)
		&& !__atomic_load_n (&web_socket->aborted, __ATOMIC_SEQ_CST)
		&& web_socket->transport && !web_socket->writes_ended;

}

#pragma endregion

#pragma region buffer

// the bytes that wait move to the start of the buffer
static void http_web_socket_queue_compact (HttpWebSocket *web_socket) {

	const size_t used = http_web_socket_queue_used (web_socket);

	if (used && web_socket->queue_start) {
		(void) memmove (web_socket->queue, web_socket->queue + web_socket->queue_start, used);
	}

	web_socket->queue_start = 0;
	web_socket->queue_end = used;

}

static size_t http_web_socket_queue_grown_capacity (const size_t capacity, const size_t needed) {

	size_t grown = capacity ? capacity : HTTP_WEB_SOCKET_QUEUE_INITIAL_SIZE;
	while (grown < needed) grown *= HTTP_WEB_SOCKET_QUEUE_GROWTH;

	return grown;

}

// returns true when there is a place for more bytes at the end of the queue
static bool http_web_socket_queue_reserve (HttpWebSocket *web_socket, const size_t bytes) {

	bool reserved = (web_socket->queue_end + bytes) <= web_socket->queue_capacity;

	if (!reserved) {
		http_web_socket_queue_compact (web_socket);

		const size_t needed = web_socket->queue_end + bytes;
		reserved = needed <= web_socket->queue_capacity;

		if (!reserved) {
			const size_t capacity = http_web_socket_queue_grown_capacity (web_socket->queue_capacity, needed);
			u8 *queue = (u8 *) realloc (web_socket->queue, capacity);
			if (queue) {
				web_socket->queue = queue;
				web_socket->queue_capacity = capacity;
				reserved = true;
			}
		}
	}

	return reserved;

}

// copies bytes at the end of the queue, a payload of a client is masked from its position in the frame
static bool http_web_socket_queue_append (
	HttpWebSocket *web_socket, const u8 *bytes, const size_t size,
	const u8 *mask, const u64 offset
) {

	const bool appended = !size || http_web_socket_queue_reserve (web_socket, size);

	if (appended && size) {
		u8 *destination = web_socket->queue + web_socket->queue_end;
		(void) memcpy (destination, bytes, size);
		if (mask) http_web_socket_mask (destination, size, mask, offset);

		web_socket->queue_end += size;
	}

	return appended;

}

void http_web_socket_queue_shrink (HttpWebSocket *web_socket) {

	if (!http_web_socket_queue_used (web_socket) && (web_socket->queue_capacity > HTTP_WEB_SOCKET_SHRINK_SIZE)) {
		http_web_socket_queue_delete (web_socket);
	}

}

#pragma endregion

#pragma region frame

// a data frame fits when the queue has a place for it; one larger than the queue fits only when nothing waits
static bool http_web_socket_queue_fits (const HttpWebSocket *web_socket, const size_t frame_size) {

	const size_t used = http_web_socket_queue_used (web_socket);

	return !used || ((used + frame_size) <= web_socket->queue_max);

}

// a message that does not fit is not taken, and the policy of the connection can close it with 1008
static HttpWebSocketSendResult http_web_socket_queue_overflow (HttpWebSocket *web_socket) {

	if (web_socket->overflow == HTTP_WEB_SOCKET_OVERFLOW_CLOSE) {
		if (!http_web_socket_close_frame_locked (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_POLICY_VIOLATION, NULL, 0)) {
			http_web_socket_notify (web_socket, HTTP_WEB_SOCKET_REQUEST_TIMERS);
		}
	}

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		CERVER_STATS_ADD (registry->n_dropped, 1);
	}

	return HTTP_WEB_SOCKET_SEND_DROPPED;

}

// a write of a whole frame of a server without the queue, when nothing waits before it
// returns the bytes that the stream took, or -1 when it failed
static ssize_t http_web_socket_queue_write_direct (
	HttpWebSocket *web_socket, u8 *header, const size_t header_size,
	const u8 *payload, const size_t size
) {

	struct iovec parts[HTTP_WEB_SOCKET_QUEUE_PARTS] = {
		{ header, header_size },
		{ (void *) payload, size }
	};

	ssize_t written = web_socket->transport->write (web_socket, parts, size ? HTTP_WEB_SOCKET_QUEUE_PARTS : 1);

	// a stream that takes nothing now is written by the owner of the connection
	if ((written < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) written = 0;

	return written;

}

// what the stream did not take of a frame goes to the queue: the rest of its header and of its payload
static bool http_web_socket_queue_rest (
	HttpWebSocket *web_socket, const u8 *header, const size_t header_size,
	const u8 *payload, const size_t size, const u8 *mask, const size_t written
) {

	const size_t header_written = (written < header_size) ? written : header_size;
	const size_t payload_written = written - header_written;

	return http_web_socket_queue_append (web_socket, header + header_written, header_size - header_written, NULL, 0)
		&& http_web_socket_queue_append (
			web_socket, payload + payload_written, size - payload_written, mask, (u64) payload_written
		);

}

// the result of a frame that was taken: over half of the queue the application waits for on_drain ()
// (the control frames of cerver never tell it)
static HttpWebSocketSendResult http_web_socket_queue_taken (HttpWebSocket *web_socket, const u8 opcode) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_QUEUED;

	if (
		!http_web_socket_opcode_is_control (opcode)
		&& (http_web_socket_queue_used (web_socket) > (web_socket->queue_max / HTTP_WEB_SOCKET_QUEUE_MARK_DIVISOR))
	) {
		web_socket->queue_backpressure = true;
		result = HTTP_WEB_SOCKET_SEND_BACKPRESSURE;
	}

	return result;

}

// a frame that was written in part, or that has no memory, breaks the stream of the connection: it fails
static HttpWebSocketSendResult http_web_socket_queue_failed (HttpWebSocket *web_socket, const HttpWebSocketError error) {

	web_socket->writes_ended = true;
	http_web_socket_fail (web_socket, error);

	return HTTP_WEB_SOCKET_SEND_FAILED;

}

// the frame goes to the queue (a client masks it there) or straight to the stream, and the rest to the queue
static HttpWebSocketSendResult http_web_socket_queue_take (
	HttpWebSocket *web_socket, const u8 opcode, u8 *header, const size_t header_size,
	const u8 *payload, const size_t size, const u8 *mask
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_QUEUED;

	const bool was_empty = !http_web_socket_queue_used (web_socket);

	ssize_t written = 0;
	if (was_empty && !mask) written = http_web_socket_queue_write_direct (web_socket, header, header_size, payload, size);

	if (written < 0) {
		result = http_web_socket_queue_failed (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
	}

	else if (!http_web_socket_queue_rest (web_socket, header, header_size, payload, size, mask, (size_t) written)) {
		result = http_web_socket_queue_failed (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
	}

	else {
		// a client writes its masked frame now, the owner writes what the stream did not take
		if (was_empty && mask && http_web_socket_queue_flush (web_socket)) {
			result = http_web_socket_queue_failed (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
		}

		else {
			if (was_empty && http_web_socket_queue_used (web_socket)) {
				http_web_socket_queue_progress (web_socket);
				http_web_socket_notify (web_socket, HTTP_WEB_SOCKET_REQUEST_OUTPUT);
			}

			result = http_web_socket_queue_taken (web_socket, opcode);
		}
	}

	http_web_socket_queue_publish (web_socket);

	return result;

}

HttpWebSocketSendResult http_web_socket_queue_frame (
	HttpWebSocket *web_socket,
	const bool fin, const bool rsv1, const u8 opcode,
	const u8 *payload, const size_t size, const bool exempt
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_CLOSED;

	if (http_web_socket_queue_is_writable (web_socket)) {
		u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];
		u8 mask[HTTP_WEB_SOCKET_MASK_SIZE];

		const bool masked = (web_socket->role == HTTP_WEB_SOCKET_ROLE_CLIENT);
		if (masked) http_web_socket_next_mask (web_socket, mask);

		const size_t header_size = http_web_socket_frame_header (header, fin, rsv1, opcode, size, masked ? mask : NULL);

		result = (exempt || http_web_socket_queue_fits (web_socket, header_size + size)) ?
			http_web_socket_queue_take (web_socket, opcode, header, header_size, payload, size, masked ? mask : NULL) :
			http_web_socket_queue_overflow (web_socket);
	}

	return result;

}

#pragma endregion

#pragma region flush

// the queue is empty: its buffer starts again, and a send that went over its mark has its on_drain ()
static void http_web_socket_queue_emptied (HttpWebSocket *web_socket) {

	web_socket->queue_start = 0;
	web_socket->queue_end = 0;

	if (web_socket->queue_backpressure) {
		web_socket->queue_backpressure = false;
		web_socket->queue_drained = true;

		if (web_socket->http_cerver) CERVER_STATS_ADD (web_socket->http_cerver->web_sockets->n_drains, 1);
	}

}

u8 http_web_socket_queue_flush (HttpWebSocket *web_socket) {

	u8 retval = 0;

	bool writing = (web_socket->transport != NULL);
	while (!retval && writing && http_web_socket_queue_used (web_socket)) {
		struct iovec part = {
			web_socket->queue + web_socket->queue_start, http_web_socket_queue_used (web_socket)
		};

		const ssize_t written = web_socket->transport->write (web_socket, &part, 1);

		if (written > 0) {
			web_socket->queue_start += (size_t) written;
			http_web_socket_queue_progress (web_socket);
		}

		else if ((written < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) writing = false;

		else retval = 1;
	}

	if (!http_web_socket_queue_used (web_socket)) http_web_socket_queue_emptied (web_socket);

	http_web_socket_queue_publish (web_socket);

	return retval;

}

#pragma endregion
