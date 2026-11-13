#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <openssl/rand.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/connection.h"
#include "cerver/handler.h"
#include "cerver/socket.h"

#include "cerver/http/http.h"
#include "cerver/http/request.h"
#include "cerver/http/tree.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/message.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/text.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

#define HTTP_WEB_SOCKET_BITS_PER_BYTE			8
#define HTTP_WEB_SOCKET_BYTE_BITS				0xFF

// the parts of a frame: its header and its payload
#define HTTP_WEB_SOCKET_FRAME_PARTS				2

// the poll () of a connection: its socket and its eventfd
#define HTTP_WEB_SOCKET_POLL_FDS				2
#define HTTP_WEB_SOCKET_POLL_SOCKET				0
#define HTTP_WEB_SOCKET_POLL_WAKE				1

// poll () without a timer waits forever
#define HTTP_WEB_SOCKET_POLL_FOREVER			(-1)

// the value that wakes up the thread of a connection
#define HTTP_WEB_SOCKET_WAKE_VALUE				1

#define HTTP_WEB_SOCKET_IP_FORMAT				"%s"

// the ids of the connections of the clients
static u64 http_web_socket_next_client_id = 0;

const char *http_web_socket_state_to_string (const HttpWebSocketState state) {

	switch (state) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_STATE_##name: return #string;
		HTTP_WEB_SOCKET_STATE_MAP(XX)
		#undef XX
	}

	return http_web_socket_state_to_string (HTTP_WEB_SOCKET_STATE_CLOSED);

}

#pragma region getters

u64 http_web_socket_get_id (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->id : 0;

}

HttpWebSocketState http_web_socket_get_state (const HttpWebSocket *web_socket) {

	return web_socket ?
		(HttpWebSocketState) __atomic_load_n (&web_socket->state, __ATOMIC_SEQ_CST) :
		HTTP_WEB_SOCKET_STATE_CLOSED;

}

static void http_web_socket_set_state (
	HttpWebSocket *web_socket, const HttpWebSocketState state
) {

	__atomic_store_n (&web_socket->state, (unsigned int) state, __ATOMIC_SEQ_CST);

}

void http_web_socket_opened (HttpWebSocket *web_socket) {

	if (web_socket) {
		unsigned int expected = (unsigned int) HTTP_WEB_SOCKET_STATE_CONNECTING;
		(void) __atomic_compare_exchange_n (
			&web_socket->state, &expected, (unsigned int) HTTP_WEB_SOCKET_STATE_OPEN,
			false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
		);
	}

}

HttpWebSocketRole http_web_socket_get_role (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->role : HTTP_WEB_SOCKET_ROLE_SERVER;

}

const char *http_web_socket_get_ip (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->ip : NULL;

}

const String *http_web_socket_get_path (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->path : NULL;

}

const char *http_web_socket_get_protocol (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->protocol : NULL;

}

bool http_web_socket_is_compressed (const HttpWebSocket *web_socket) {

	return web_socket && web_socket->deflate;

}

HttpCerver *http_web_socket_get_http_cerver (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->http_cerver : NULL;

}

const HttpEndpoint *http_web_socket_get_endpoint (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->endpoint : NULL;

}

void *http_web_socket_get_endpoint_data (const HttpWebSocket *web_socket) {

	return (web_socket && web_socket->endpoint) ? web_socket->endpoint->resolved.custom_data : NULL;

}

void *http_web_socket_get_decoded_data (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->decoded_data : NULL;

}

void http_web_socket_set_data (
	HttpWebSocket *web_socket, void *data, void (*delete_data)(void *)
) {

	if (web_socket) {
		if (web_socket->data && web_socket->delete_data && (web_socket->data != data)) {
			web_socket->delete_data (web_socket->data);
		}

		web_socket->data = data;
		web_socket->delete_data = delete_data;
	}

}

void *http_web_socket_get_data (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->data : NULL;

}

#pragma endregion

#pragma region lifecycle

HttpWebSocket *http_web_socket_create (
	const HttpWebSocketRole role, const HttpWebSocketSettings *settings
) {

	HttpWebSocket *web_socket = (HttpWebSocket *) calloc (1, sizeof (HttpWebSocket));
	if (web_socket) {
		web_socket->role = role;
		web_socket->state = (unsigned int) HTTP_WEB_SOCKET_STATE_CONNECTING;
		web_socket->references = 1;

		if (settings) web_socket->settings = *settings;
		else http_web_socket_settings_init (&web_socket->settings);

		web_socket->wake_fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);

		(void) pthread_mutex_init (&web_socket->send_mutex, NULL);
		(void) pthread_mutex_init (&web_socket->message_mutex, NULL);
		(void) pthread_mutex_init (&web_socket->mutex, NULL);

		web_socket->buffer_size = HTTP_WEB_SOCKET_RECEIVE_BUFFER_SIZE;
		web_socket->buffer = (u8 *) malloc (web_socket->buffer_size);

		web_socket->close_code = HTTP_WEB_SOCKET_CLOSE_ABNORMAL;
		web_socket->error = HTTP_WEB_SOCKET_ERROR_NONE;

		// a client takes its masks from random bytes
		if (role == HTTP_WEB_SOCKET_ROLE_CLIENT) {
			web_socket->id = __atomic_add_fetch (&http_web_socket_next_client_id, 1, __ATOMIC_SEQ_CST);
			web_socket->scratch = (u8 *) malloc (HTTP_WEB_SOCKET_SEND_CHUNK_SIZE);
			web_socket->masks_position = HTTP_WEB_SOCKET_MASKS_SIZE;
		}

		const bool created = (web_socket->wake_fd >= 0) && web_socket->buffer
			&& ((role == HTTP_WEB_SOCKET_ROLE_SERVER) || web_socket->scratch);

		if (!created) {
			http_web_socket_release (web_socket);
			web_socket = NULL;
		}
	}

	return web_socket;

}

static void http_web_socket_delete (HttpWebSocket *web_socket) {

	http_web_socket_receiver_end (&web_socket->receiver);
	http_web_socket_deflate_delete (web_socket->deflate);

	free (web_socket->buffer);
	free (web_socket->pending);
	free (web_socket->scratch);
	free (web_socket->protocol);

	str_delete (web_socket->path);

	if (web_socket->data && web_socket->delete_data) web_socket->delete_data (web_socket->data);

	if (web_socket->decoded_data && web_socket->delete_decoded_data) {
		web_socket->delete_decoded_data (web_socket->decoded_data);
	}

	if (web_socket->wake_fd >= 0) (void) close (web_socket->wake_fd);

	(void) pthread_mutex_destroy (&web_socket->send_mutex);
	(void) pthread_mutex_destroy (&web_socket->message_mutex);
	(void) pthread_mutex_destroy (&web_socket->mutex);

	free (web_socket);

}

HttpWebSocket *http_web_socket_retain (HttpWebSocket *web_socket) {

	if (web_socket) (void) __atomic_add_fetch (&web_socket->references, 1, __ATOMIC_SEQ_CST);

	return web_socket;

}

void http_web_socket_release (HttpWebSocket *web_socket) {

	if (web_socket && !__atomic_sub_fetch (&web_socket->references, 1, __ATOMIC_SEQ_CST)) {
		http_web_socket_delete (web_socket);
	}

}

u8 http_web_socket_set_compression (
	HttpWebSocket *web_socket,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
) {

	u8 retval = 1;

	if (web_socket && parameters && options) {
		web_socket->deflate = http_web_socket_deflate_create (web_socket->role, parameters, options);
		if (web_socket->deflate) retval = 0;
	}

	return retval;

}

void http_web_socket_set_pending (
	HttpWebSocket *web_socket, u8 *pending, const size_t pending_size
) {

	if (web_socket) {
		free (web_socket->pending);
		web_socket->pending = pending;
		web_socket->pending_size = pending_size;
	}

}

#pragma endregion

#pragma region flags

static bool http_web_socket_is_aborted (const HttpWebSocket *web_socket) {

	return __atomic_load_n (&web_socket->aborted, __ATOMIC_SEQ_CST);

}

// it is written with the send mutex, and read from any thread
static bool http_web_socket_close_was_sent (const HttpWebSocket *web_socket) {

	return __atomic_load_n (&web_socket->close_sent, __ATOMIC_SEQ_CST);

}

void http_web_socket_wake (HttpWebSocket *web_socket) {

	if (web_socket && (web_socket->wake_fd >= 0)) {
		const u64 value = HTTP_WEB_SOCKET_WAKE_VALUE;
		ssize_t written = 0;
		do {
			written = write (web_socket->wake_fd, &value, sizeof (u64));
		} while ((written < 0) && (errno == EINTR));
	}

}

// keeps the first error, that the thread of the connection reports
static void http_web_socket_set_error (
	HttpWebSocket *web_socket, const HttpWebSocketError error
) {

	(void) pthread_mutex_lock (&web_socket->mutex);
	if (web_socket->error == HTTP_WEB_SOCKET_ERROR_NONE) web_socket->error = error;
	(void) pthread_mutex_unlock (&web_socket->mutex);

}

void http_web_socket_abort (HttpWebSocket *web_socket) {

	if (web_socket) {
		__atomic_store_n (&web_socket->aborted, true, __ATOMIC_SEQ_CST);

		// a send or a recv () that is waiting ends now,
		// the thread of the connection releases its socket with the same mutex
		(void) pthread_mutex_lock (&web_socket->mutex);
		if (web_socket->socket) (void) socket_shutdown (web_socket->socket);
		(void) pthread_mutex_unlock (&web_socket->mutex);

		http_web_socket_wake (web_socket);
	}

}

// the connection failed, it ends without the closing handshake
static void http_web_socket_fail (
	HttpWebSocket *web_socket, const HttpWebSocketError error
) {

	http_web_socket_set_error (web_socket, error);

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		if (error == HTTP_WEB_SOCKET_ERROR_SEND) CERVER_STATS_ADD (registry->n_slow, 1);
	}

	http_web_socket_abort (web_socket);

}

void http_web_socket_go_away (HttpWebSocket *web_socket) {

	if (web_socket) {
		__atomic_store_n (&web_socket->going_away, true, __ATOMIC_SEQ_CST);
		http_web_socket_wake (web_socket);
	}

}

#pragma endregion

#pragma region send

// the next 4 random bytes of a client, renewed when they run out
static void http_web_socket_next_mask (HttpWebSocket *web_socket, u8 *mask) {

	if ((web_socket->masks_position + HTTP_WEB_SOCKET_MASK_SIZE) > HTTP_WEB_SOCKET_MASKS_SIZE) {
		if (RAND_bytes (web_socket->masks, HTTP_WEB_SOCKET_MASKS_SIZE) != 1) {
			// the masks only protect the proxies of the path, never the content
			for (size_t i = 0; i < HTTP_WEB_SOCKET_MASKS_SIZE; i++) {
				web_socket->masks[i] = (u8) (rand () & HTTP_WEB_SOCKET_BYTE_BITS);
			}
		}

		web_socket->masks_position = 0;
	}

	(void) memcpy (mask, web_socket->masks + web_socket->masks_position, HTTP_WEB_SOCKET_MASK_SIZE);
	web_socket->masks_position += HTTP_WEB_SOCKET_MASK_SIZE;

}

// the payload of a client, masked a piece at a time in its scratch buffer
static u8 http_web_socket_write_masked (
	HttpWebSocket *web_socket,
	u8 *header, const size_t header_size,
	const u8 *mask, const u8 *payload, const size_t size,
	const struct timespec *deadline
) {

	u8 retval = 0;

	// the header goes with the first piece
	struct iovec parts[HTTP_WEB_SOCKET_FRAME_PARTS];
	parts[0].iov_base = header;
	parts[0].iov_len = header_size;

	size_t position = 0;
	bool first = true;
	while (!retval && (first || (position < size))) {
		const size_t remaining = size - position;
		const size_t piece = (remaining < HTTP_WEB_SOCKET_SEND_CHUNK_SIZE) ? remaining : HTTP_WEB_SOCKET_SEND_CHUNK_SIZE;

		if (piece) {
			(void) memcpy (web_socket->scratch, payload + position, piece);
			http_web_socket_mask (web_socket->scratch, piece, mask, position);
		}

		struct iovec *start = first ? parts : (parts + 1);
		const int n_parts = first ? HTTP_WEB_SOCKET_FRAME_PARTS : 1;
		parts[1].iov_base = web_socket->scratch;
		parts[1].iov_len = piece;

		retval = socket_send_vector_until (web_socket->socket, start, n_parts, 0, deadline, NULL, NULL);

		position += piece;
		first = false;
	}

	return retval;

}

// writes a whole frame, the send mutex must be held
// returns 0 on success, 1 on error
static u8 http_web_socket_write_frame (
	HttpWebSocket *web_socket,
	const bool fin, const bool rsv1, const u8 opcode,
	const u8 *payload, const size_t size,
	const struct timespec *deadline
) {

	u8 retval = 1;

	// the state is set to closed with the send mutex, before the socket is released
	if (
		(http_web_socket_get_state (web_socket) != HTTP_WEB_SOCKET_STATE_CLOSED)
		&& !http_web_socket_is_aborted (web_socket) && web_socket->socket
	) {
		u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];

		if (web_socket->role == HTTP_WEB_SOCKET_ROLE_CLIENT) {
			u8 mask[HTTP_WEB_SOCKET_MASK_SIZE];
			http_web_socket_next_mask (web_socket, mask);

			const size_t header_size = http_web_socket_frame_header (header, fin, rsv1, opcode, size, mask);
			retval = http_web_socket_write_masked (web_socket, header, header_size, mask, payload, size, deadline);
		}

		else {
			const size_t header_size = http_web_socket_frame_header (header, fin, rsv1, opcode, size, NULL);

			struct iovec parts[HTTP_WEB_SOCKET_FRAME_PARTS] = {
				{ header, header_size },
				{ (void *) payload, size }
			};

			retval = socket_send_vector_until (
				web_socket->socket, parts, size ? HTTP_WEB_SOCKET_FRAME_PARTS : 1, 0, deadline, NULL, NULL
			);
		}

		if (!retval) {
			__atomic_add_fetch (&web_socket->n_bytes_sent, size, __ATOMIC_SEQ_CST);
		}
	}

	return retval;

}

// the milliseconds of each send of the connection
static unsigned int http_web_socket_send_milliseconds (const HttpWebSocket *web_socket) {

	unsigned int timeout = web_socket->settings.send_timeout;

	if (!timeout && web_socket->http_cerver) timeout = web_socket->http_cerver->send_timeout;

	if (!timeout && web_socket->cerver && web_socket->cerver->send_timeout) {
		timeout = (unsigned int) web_socket->cerver->send_timeout * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND;
	}

	if (!timeout) timeout = CERVER_DEFAULT_SEND_TIMEOUT * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND;

	return timeout;

}

// sends a control frame, with the close frames and the pings of the thread of the connection
static u8 http_web_socket_send_control (
	HttpWebSocket *web_socket, const u8 opcode,
	const void *payload, const size_t size
) {

	u8 retval = 1;

	if (size <= HTTP_WEB_SOCKET_CONTROL_MAX_SIZE) {
		struct timespec deadline;
		socket_deadline_create_milliseconds (&deadline, http_web_socket_send_milliseconds (web_socket));

		(void) pthread_mutex_lock (&web_socket->send_mutex);
		retval = http_web_socket_write_frame (web_socket, true, false, opcode, (const u8 *) payload, size, &deadline);
		(void) pthread_mutex_unlock (&web_socket->send_mutex);

		if (retval) http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
	}

	return retval;

}

// a data frame can only be sent while the connection is open and before its close frame
static bool http_web_socket_can_send_data (const HttpWebSocket *web_socket) {

	return (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN)
		&& !http_web_socket_close_was_sent (web_socket) && !http_web_socket_is_aborted (web_socket);

}

// the stats of a message that was sent
static void http_web_socket_message_sent (HttpWebSocket *web_socket, const size_t size) {

	__atomic_add_fetch (&web_socket->n_messages_sent, 1, __ATOMIC_SEQ_CST);

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		CERVER_STATS_ADD (registry->n_messages_sent, 1);
		CERVER_STATS_ADD (registry->n_bytes_sent, size);
	}

}

// the payload of a whole message, compressed when it is large enough
// the message mutex must be held
static u8 http_web_socket_send_message_frame (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const struct timespec *deadline
) {

	u8 retval = 0;

	const u8 *payload = (const u8 *) data;
	size_t payload_size = size;
	bool compressed = false;

	if (web_socket->deflate && (size >= web_socket->deflate->min_size)) {
		retval = http_web_socket_deflate_compress (web_socket->deflate, data, size, true, &payload, &payload_size);
		compressed = true;
	}

	if (!retval) {
		(void) pthread_mutex_lock (&web_socket->send_mutex);

		retval = http_web_socket_can_send_data (web_socket) ?
			http_web_socket_write_frame (web_socket, true, compressed, (u8) type, payload, payload_size, deadline) : 1;

		(void) pthread_mutex_unlock (&web_socket->send_mutex);
	}

	return retval;

}

static bool http_web_socket_is_data_type (const HttpWebSocketOpcode type) {

	return (type == HTTP_WEB_SOCKET_OPCODE_TEXT) || (type == HTTP_WEB_SOCKET_OPCODE_BINARY);

}

u8 http_web_socket_send_timeout (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const unsigned int timeout
) {

	u8 retval = 1;

	if (web_socket && http_web_socket_is_data_type (type) && (data || !size)) {
		(void) pthread_mutex_lock (&web_socket->message_mutex);

		if (http_web_socket_can_send_data (web_socket)) {
			// the time of the send starts when the message can be written:
			// a deadline from before the wait for other messages could be already over
			struct timespec deadline;
			socket_deadline_create_milliseconds (&deadline, timeout);

			retval = http_web_socket_send_message_frame (web_socket, type, data, size, &deadline);

			if (retval) http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
			else http_web_socket_message_sent (web_socket, size);
		}

		(void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	return retval;

}

u8 http_web_socket_send (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	return web_socket ? http_web_socket_send_timeout (
		web_socket, type, data, size, http_web_socket_send_milliseconds (web_socket)
	) : 1;

}

u8 http_web_socket_send_text (
	HttpWebSocket *web_socket, const char *text, const size_t length
) {

	return http_web_socket_send (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT, text, length);

}

u8 http_web_socket_send_binary (
	HttpWebSocket *web_socket, const void *data, const size_t size
) {

	return http_web_socket_send (web_socket, HTTP_WEB_SOCKET_OPCODE_BINARY, data, size);

}

u8 http_web_socket_send_begin (
	HttpWebSocket *web_socket, const HttpWebSocketOpcode type
) {

	u8 retval = 1;

	if (web_socket && http_web_socket_is_data_type (type)) {
		(void) pthread_mutex_lock (&web_socket->message_mutex);

		// held until http_web_socket_send_end ()
		if (http_web_socket_can_send_data (web_socket)) {
			web_socket->streaming = true;
			web_socket->stream_type = (u8) type;
			web_socket->stream_started = false;
			web_socket->stream_compressed = (web_socket->deflate != NULL);
			web_socket->stream_size = 0;

			retval = 0;
		}

		// a stream that does not begin holds nothing, so there is nothing to end
		else (void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	return retval;

}

// a frame of the stream, the first one has the type of the message
static u8 http_web_socket_send_stream_frame (
	HttpWebSocket *web_socket, const bool fin,
	const u8 *payload, const size_t size
) {

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, http_web_socket_send_milliseconds (web_socket));

	const u8 opcode = web_socket->stream_started ?
		(u8) HTTP_WEB_SOCKET_OPCODE_CONTINUATION : web_socket->stream_type;

	const bool rsv1 = web_socket->stream_compressed && !web_socket->stream_started;

	(void) pthread_mutex_lock (&web_socket->send_mutex);

	// a connection that already sent its close does not take the frame: that is an error of the send,
	// not a failure of the connection, that keeps closing with its handshake
	const bool attempted = http_web_socket_can_send_data (web_socket);

	const u8 retval = attempted ?
		http_web_socket_write_frame (web_socket, fin, rsv1, opcode, payload, size, &deadline) : 1;

	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	web_socket->stream_started = true;

	if (attempted && retval) http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);

	return retval;

}

u8 http_web_socket_send_write (
	HttpWebSocket *web_socket, const void *data, const size_t size
) {

	u8 retval = 1;

	if (web_socket && web_socket->streaming && (data || !size)) {
		const u8 *payload = (const u8 *) data;
		size_t payload_size = size;

		retval = web_socket->stream_compressed ?
			http_web_socket_deflate_compress (web_socket->deflate, data, size, false, &payload, &payload_size) : 0;

		// the compressor can keep everything until the end of the message
		if (!retval && payload_size) {
			retval = http_web_socket_send_stream_frame (web_socket, false, payload, payload_size);
		}

		if (!retval) web_socket->stream_size += size;
	}

	return retval;

}

u8 http_web_socket_send_end (HttpWebSocket *web_socket) {

	u8 retval = 1;

	if (web_socket && web_socket->streaming) {
		const u8 *payload = NULL;
		size_t payload_size = 0;

		retval = web_socket->stream_compressed ?
			http_web_socket_deflate_compress (web_socket->deflate, NULL, 0, true, &payload, &payload_size) : 0;

		if (!retval) retval = http_web_socket_send_stream_frame (web_socket, true, payload, payload_size);
		if (!retval) http_web_socket_message_sent (web_socket, web_socket->stream_size);

		web_socket->streaming = false;

		(void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	return retval;

}

u8 http_web_socket_ping (
	HttpWebSocket *web_socket, const void *data, const size_t size
) {

	u8 retval = 1;

	if (
		web_socket && (data || !size)
		&& (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN)
	) {
		retval = http_web_socket_send_control (web_socket, HTTP_WEB_SOCKET_OPCODE_PING, data, size);
	}

	return retval;

}

// sends the close frame once, the send mutex keeps it after every data frame that started
// returns 0 when it was sent now, 1 when it was already sent or it failed
static u8 http_web_socket_send_close_frame (
	HttpWebSocket *web_socket, const u16 code,
	const char *reason, const size_t length
) {

	u8 retval = 1;

	u8 payload[HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	size_t size = 0;
	if (code != HTTP_WEB_SOCKET_CLOSE_NO_STATUS) {
		payload[0] = (u8) ((code >> HTTP_WEB_SOCKET_BITS_PER_BYTE) & HTTP_WEB_SOCKET_BYTE_BITS);
		payload[1] = (u8) (code & HTTP_WEB_SOCKET_BYTE_BITS);
		size = HTTP_WEB_SOCKET_CLOSE_CODE_SIZE;

		if (reason && length) {
			(void) memcpy (payload + size, reason, length);
			size += length;
		}
	}

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, http_web_socket_send_milliseconds (web_socket));

	(void) pthread_mutex_lock (&web_socket->send_mutex);

	if (!http_web_socket_close_was_sent (web_socket)) {
		__atomic_store_n (&web_socket->close_sent, true, __ATOMIC_SEQ_CST);
		socket_deadline_create_milliseconds (&web_socket->close_deadline, web_socket->settings.close_timeout);

		if (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN) {
			http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSING);
		}

		retval = http_web_socket_write_frame (
			web_socket, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, payload, size, &deadline
		);

		if (retval) http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
	}

	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	return retval;

}

u8 http_web_socket_close (
	HttpWebSocket *web_socket, const u16 code, const char *reason
) {

	u8 retval = 1;

	const size_t length = reason ? strlen (reason) : 0;
	if (
		web_socket
		&& http_web_socket_close_code_is_valid (code)
		&& (length <= HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE)
		&& http_web_socket_text_check (reason, length)
	) {
		const HttpWebSocketState state = http_web_socket_get_state (web_socket);
		if ((state == HTTP_WEB_SOCKET_STATE_OPEN) || (state == HTTP_WEB_SOCKET_STATE_CLOSING)) {
			(void) http_web_socket_send_close_frame (web_socket, code, reason, length);
			retval = 0;

			// the thread of the connection waits for the answer with its close timeout
			http_web_socket_wake (web_socket);
		}
	}

	return retval;

}

#pragma endregion

#pragma region receive

// the code and the reason that on_close reports
static void http_web_socket_set_close (
	HttpWebSocket *web_socket, const u16 code,
	const char *reason, const size_t length
) {

	web_socket->close_code = code;

	const size_t copied = (length <= HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE) ? length : HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE;
	if (reason && copied) (void) memcpy (web_socket->close_reason, reason, copied);

	web_socket->close_reason[copied] = '\0';
	web_socket->close_reason_length = copied;

}

// the result of a method of the application, it stops the receiver when the connection was aborted
static int http_web_socket_handled (const HttpWebSocket *web_socket) {

	return http_web_socket_is_aborted (web_socket) ? 1 : 0;

}

// the stats of the bytes of a message, that can arrive in fragments, and of the message when it ends
static void http_web_socket_fragment_received (
	HttpWebSocket *web_socket, const size_t size, const bool last
) {

	if (last) __atomic_add_fetch (&web_socket->n_messages_received, 1, __ATOMIC_SEQ_CST);

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		if (last) CERVER_STATS_ADD (registry->n_messages_received, 1);
		CERVER_STATS_ADD (registry->n_bytes_received, size);
	}

}

static void http_web_socket_message_received (HttpWebSocket *web_socket, const size_t size) {

	http_web_socket_fragment_received (web_socket, size, true);

}

static int http_web_socket_on_message (
	void *web_socket_ptr, HttpWebSocketOpcode type,
	const u8 *payload, size_t size
) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_message_received (web_socket, size);

	if (web_socket->settings.handlers.on_message) {
		web_socket->settings.handlers.on_message (web_socket, type, payload, size);
	}

	return http_web_socket_handled (web_socket);

}

static int http_web_socket_on_fragment (
	void *web_socket_ptr, HttpWebSocketOpcode type,
	const u8 *payload, size_t size,
	bool first, bool last
) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_fragment_received (web_socket, size, last);

	web_socket->settings.handlers.on_fragment (web_socket, type, payload, size, first, last);

	return http_web_socket_handled (web_socket);

}

static int http_web_socket_on_ping (void *web_socket_ptr, const u8 *payload, size_t size) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	// a pong answers every ping, until the close frame was sent
	if (!http_web_socket_close_was_sent (web_socket)) {
		(void) http_web_socket_send_control (web_socket, HTTP_WEB_SOCKET_OPCODE_PONG, payload, size);
	}

	if (web_socket->settings.handlers.on_ping) {
		web_socket->settings.handlers.on_ping (web_socket, payload, size);
	}

	return http_web_socket_handled (web_socket);

}

// the next ping is sent after the ping interval without receiving anything
static void http_web_socket_alive (HttpWebSocket *web_socket) {

	web_socket->ping_sent = false;

	if (web_socket->settings.ping_interval) {
		socket_deadline_create_milliseconds (&web_socket->pong_deadline, web_socket->settings.ping_interval);
	}

}

static int http_web_socket_on_pong (void *web_socket_ptr, const u8 *payload, size_t size) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	if (web_socket->settings.handlers.on_pong) {
		web_socket->settings.handlers.on_pong (web_socket, payload, size);
	}

	return http_web_socket_handled (web_socket);

}

static int http_web_socket_on_close (
	void *web_socket_ptr, u16 code, const char *reason, size_t length
) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_set_close (web_socket, code, reason, length);

	return 0;

}

static const HttpWebSocketReceiverHandlers http_web_socket_receiver_handlers = {
	http_web_socket_on_message,
	NULL,
	http_web_socket_on_ping,
	http_web_socket_on_pong,
	http_web_socket_on_close
};

static const HttpWebSocketReceiverHandlers http_web_socket_receiver_fragment_handlers = {
	http_web_socket_on_message,
	http_web_socket_on_fragment,
	http_web_socket_on_ping,
	http_web_socket_on_pong,
	http_web_socket_on_close
};

// the peer sent its close frame, it is answered with the same code
// returns true when the connection ends now
static bool http_web_socket_peer_closed (HttpWebSocket *web_socket) {

	web_socket->close_received = true;

	// a close without a code is answered with an empty one
	(void) http_web_socket_send_close_frame (web_socket, web_socket->close_code, NULL, 0);

	// the server closes the TCP connection first, a client waits for it
	return web_socket->role == HTTP_WEB_SOCKET_ROLE_SERVER;

}

// the peer broke the protocol, the connection fails with the code of the receiver
static void http_web_socket_peer_failed (HttpWebSocket *web_socket) {

	const u16 code = web_socket->receiver.close_code;

	(void) http_web_socket_send_close_frame (web_socket, code, NULL, 0);

	http_web_socket_set_close (web_socket, code, NULL, 0);
	http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_PROTOCOL);

}

// parses the bytes that were received
// returns true when the connection ends
static bool http_web_socket_receive_bytes (
	HttpWebSocket *web_socket, u8 *data, const size_t size
) {

	bool finished = false;

	// a client that already has the close frame only waits for the end of the TCP connection
	if (!web_socket->close_received) {
		size_t offset = 0;
		bool parsing = true;
		while (parsing && !finished && !http_web_socket_is_aborted (web_socket)) {
			size_t parsed = 0;
			const HttpWebSocketReceiveResult result = http_web_socket_receiver_execute (
				&web_socket->receiver, data + offset, size - offset, &parsed
			);

			offset += parsed;

			switch (result) {
				case HTTP_WEB_SOCKET_RECEIVE_CLOSED: finished = http_web_socket_peer_closed (web_socket); parsing = false; break;
				case HTTP_WEB_SOCKET_RECEIVE_FAILED: http_web_socket_peer_failed (web_socket); finished = true; break;
				case HTTP_WEB_SOCKET_RECEIVE_STOPPED: parsing = (offset < size); break;
				default: parsing = false; break;
			}
		}
	}

	return finished || http_web_socket_is_aborted (web_socket);

}

// a recv () of the socket
// returns true when the connection ends
static bool http_web_socket_receive (HttpWebSocket *web_socket) {

	bool finished = false;

	const ssize_t received = recv (
		web_socket->socket->sock_fd, web_socket->buffer, web_socket->buffer_size, MSG_DONTWAIT
	);

	if (received > 0) {
		__atomic_add_fetch (&web_socket->n_bytes_received, (u64) received, __ATOMIC_SEQ_CST);
		http_web_socket_alive (web_socket);
		finished = http_web_socket_receive_bytes (web_socket, web_socket->buffer, (size_t) received);
	}

	// the end of the TCP connection, after the closing handshake or without it (1006)
	else if (received == 0) {
		if (!web_socket->close_received) http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL, 0);
		finished = true;
	}

	else if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)) {
		if (!web_socket->close_received) {
			http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL, 0);
			http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_RECEIVE);
		}

		finished = true;
	}

	return finished;

}

#pragma endregion

#pragma region thread

// the milliseconds until the next timer of the connection, HTTP_WEB_SOCKET_POLL_FOREVER without one
static int http_web_socket_next_timeout (HttpWebSocket *web_socket) {

	int timeout = HTTP_WEB_SOCKET_POLL_FOREVER;

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	const bool close_sent = web_socket->close_sent;
	const struct timespec close_deadline = web_socket->close_deadline;
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (close_sent) {
		timeout = socket_deadline_remaining (&close_deadline);
	}

	else if (web_socket->settings.ping_interval) {
		// the pong deadline is also the time of the next ping
		timeout = socket_deadline_remaining (&web_socket->pong_deadline);
	}

	return timeout;

}

// the timers that expired, the next ping, its pong and the closing handshake
// returns true when the connection ends
static bool http_web_socket_timers (HttpWebSocket *web_socket) {

	bool finished = false;

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	const bool close_sent = web_socket->close_sent;
	const struct timespec close_deadline = web_socket->close_deadline;
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (close_sent) {
		if (!socket_deadline_remaining (&close_deadline)) {
			if (!web_socket->close_received) {
				http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL, 0);
				http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_CLOSE_TIMEOUT);
			}

			finished = true;
		}
	}

	else if (web_socket->settings.ping_interval && !socket_deadline_remaining (&web_socket->pong_deadline)) {
		if (web_socket->ping_sent) {
			http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL, 0);
			http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_PONG_TIMEOUT);
			finished = true;
		}

		else {
			u8 payload[HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE];
			web_socket->n_pings += 1;
			for (size_t i = 0; i < HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE; i++) {
				payload[i] = (u8) ((web_socket->n_pings >> (i * HTTP_WEB_SOCKET_BITS_PER_BYTE)) & HTTP_WEB_SOCKET_BYTE_BITS);
			}

			finished = http_web_socket_send_control (
				web_socket, HTTP_WEB_SOCKET_OPCODE_PING, payload, HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE
			) != 0;

			web_socket->ping_sent = true;
			socket_deadline_create_milliseconds (&web_socket->pong_deadline, web_socket->settings.pong_timeout);
		}
	}

	return finished;

}

// the eventfd was written, by a close, a go away or an abort
// returns true when the connection ends
static bool http_web_socket_woken (HttpWebSocket *web_socket) {

	u64 value = 0;
	while (read (web_socket->wake_fd, &value, sizeof (u64)) > 0);

	if (__atomic_load_n (&web_socket->going_away, __ATOMIC_SEQ_CST)) {
		(void) http_web_socket_send_close_frame (web_socket, HTTP_WEB_SOCKET_CLOSE_GOING_AWAY, NULL, 0);
	}

	return http_web_socket_is_aborted (web_socket);

}

// waits for the socket, the eventfd or the next timer
// returns true when the connection ends
static bool http_web_socket_step (HttpWebSocket *web_socket) {

	bool finished = false;

	struct pollfd fds[HTTP_WEB_SOCKET_POLL_FDS];
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].fd = web_socket->socket->sock_fd;
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].events = POLLIN;
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].revents = 0;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].fd = web_socket->wake_fd;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].events = POLLIN;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].revents = 0;

	const int ready = poll (fds, HTTP_WEB_SOCKET_POLL_FDS, http_web_socket_next_timeout (web_socket));

	if (ready > 0) {
		if (fds[HTTP_WEB_SOCKET_POLL_WAKE].revents) finished = http_web_socket_woken (web_socket);

		if (!finished && fds[HTTP_WEB_SOCKET_POLL_SOCKET].revents) finished = http_web_socket_receive (web_socket);
	}

	else if ((ready < 0) && (errno != EINTR)) {
		http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_RECEIVE);
		finished = true;
	}

	if (!finished) finished = http_web_socket_timers (web_socket) || http_web_socket_is_aborted (web_socket);

	return finished;

}

// the connection ended, its methods run for the last time
static void http_web_socket_finish (HttpWebSocket *web_socket) {

	// a send that is waiting ends now, and no other one can start
	(void) socket_shutdown (web_socket->socket);

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSED);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	(void) pthread_mutex_lock (&web_socket->mutex);
	const HttpWebSocketError error = web_socket->error;
	(void) pthread_mutex_unlock (&web_socket->mutex);

	// the connection leaves the registry before its last methods, so they see it gone
	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		http_web_socket_registry_count_close (registry, web_socket->close_code);
		if (error != HTTP_WEB_SOCKET_ERROR_NONE) CERVER_STATS_ADD (registry->n_errors, 1);

		http_web_socket_registry_remove (registry, web_socket);
	}

	if ((error != HTTP_WEB_SOCKET_ERROR_NONE) && web_socket->settings.handlers.on_error) {
		web_socket->settings.handlers.on_error (web_socket, error);
	}

	if (web_socket->settings.handlers.on_close) {
		web_socket->settings.handlers.on_close (
			web_socket, web_socket->close_code,
			web_socket->close_reason_length ? web_socket->close_reason : NULL,
			web_socket->close_reason_length
		);
	}

}

void http_web_socket_run (HttpWebSocket *web_socket) {

	if (web_socket && web_socket->socket) {
		http_web_socket_receiver_init (
			&web_socket->receiver, web_socket->role,
			web_socket->settings.max_message, web_socket->settings.max_message,
			web_socket->deflate,
			web_socket->settings.handlers.on_fragment ?
				&http_web_socket_receiver_fragment_handlers : &http_web_socket_receiver_handlers,
			web_socket
		);

		// a close that came before its thread started is kept
		http_web_socket_opened (web_socket);
		http_web_socket_alive (web_socket);

		if (web_socket->settings.handlers.on_open) web_socket->settings.handlers.on_open (web_socket);

		// a cerver that started to go away before the connection
		if (__atomic_load_n (&web_socket->going_away, __ATOMIC_SEQ_CST)) http_web_socket_wake (web_socket);

		bool finished = http_web_socket_is_aborted (web_socket);
		if (!finished && web_socket->pending_size) {
			finished = http_web_socket_receive_bytes (web_socket, web_socket->pending, web_socket->pending_size);
			web_socket->pending_size = 0;
		}

		while (!finished) finished = http_web_socket_step (web_socket);

		http_web_socket_finish (web_socket);
	}

}

#pragma endregion

#pragma region server

void http_web_socket_handshake_delete (HttpWebSocketHandshake *handshake) {

	if (handshake) {
		if (handshake->data && handshake->delete_data) handshake->delete_data (handshake->data);

		if (handshake->decoded_data && handshake->delete_decoded_data) {
			handshake->delete_decoded_data (handshake->decoded_data);
		}

		free (handshake->pending);
		free (handshake->protocol);

		free (handshake);
	}

}

static void *http_web_socket_thread (void *web_socket_ptr) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_run (web_socket);

	http_web_socket_registry_remove (web_socket->http_cerver->web_sockets, web_socket);

	// the TCP connection ends with its thread, an abort () of another thread can not reach it after this
	(void) pthread_mutex_lock (&web_socket->mutex);
	Connection *connection = web_socket->connection;
	web_socket->connection = NULL;
	web_socket->socket = NULL;
	(void) pthread_mutex_unlock (&web_socket->mutex);

	connection_drop (web_socket->cerver, connection);

	http_web_socket_release (web_socket);

	return NULL;

}

// the connection takes what the request learned in the handshake
static void http_web_socket_adopt (
	HttpWebSocket *web_socket, HttpWebSocketHandshake *handshake,
	const HttpRequest *request
) {

	web_socket->data = handshake->data;
	web_socket->delete_data = handshake->delete_data;
	handshake->data = NULL;
	handshake->delete_data = NULL;

	web_socket->decoded_data = handshake->decoded_data;
	web_socket->delete_decoded_data = handshake->delete_decoded_data;
	handshake->decoded_data = NULL;
	handshake->delete_decoded_data = NULL;

	http_web_socket_set_pending (web_socket, handshake->pending, handshake->pending_size);
	handshake->pending = NULL;
	handshake->pending_size = 0;

	web_socket->protocol = handshake->protocol;
	handshake->protocol = NULL;

	if (request && request->url) web_socket->path = str_new (request->url->str);

	const char *ip = (request && request->client_ip[0]) ? request->client_ip : web_socket->connection->ip;
	(void) snprintf (web_socket->ip, CONNECTION_IP_SIZE, HTTP_WEB_SOCKET_IP_FORMAT, ip);

}

// creates the connection of the server with what the handshake learned
static HttpWebSocket *http_web_socket_server_create (
	CerverReceive *cr, HttpReceive *http_receive, HttpWebSocketHandshake *handshake
) {

	const HttpEndpoint *endpoint = http_receive->endpoint;

	HttpWebSocket *web_socket = http_web_socket_create (
		HTTP_WEB_SOCKET_ROLE_SERVER, &endpoint->resolved.web_socket
	);

	if (web_socket) {
		web_socket->connection = cr->connection;
		web_socket->cerver = cr->cerver;
		web_socket->socket = cr->connection->socket;

		web_socket->http_cerver = http_receive->http_cerver;
		web_socket->endpoint = endpoint;

		http_web_socket_adopt (web_socket, handshake, http_receive->request);

		if (
			handshake->compression
			&& http_web_socket_set_compression (web_socket, &handshake->deflate, &endpoint->resolved.web_socket.compression)
		) {
			web_socket->connection = NULL;
			web_socket->socket = NULL;
			http_web_socket_release (web_socket);
			web_socket = NULL;
		}
	}

	return web_socket;

}

bool http_web_socket_start (CerverReceive *cr, HttpReceive *http_receive) {

	bool started = false;

	HttpWebSocketHandshake *handshake = http_receive ? http_receive->web_socket : NULL;
	if (cr && cr->connection && handshake && http_receive->upgraded) {
		HttpWebSocketRegistry *registry = http_receive->http_cerver->web_sockets;

		HttpWebSocket *web_socket = http_web_socket_server_create (cr, http_receive, handshake);
		if (web_socket) {
			// the place that the handshake took belongs to the connection now
			handshake->reserved = false;

			// it can send since its 101, other threads find it in the registry
			http_web_socket_opened (web_socket);

			if (!http_web_socket_registry_add (registry, web_socket)) {
				// the thread holds the reference of the creation
				if (!thread_group_create (&cr->cerver->connection_threads, http_web_socket_thread, web_socket)) {
					started = true;
				}

				else {
					http_web_socket_registry_remove (registry, web_socket);
				}
			}

			else {
				http_web_socket_registry_unreserve (registry);
			}

			if (!started) {
				web_socket->connection = NULL;
				web_socket->socket = NULL;
				http_web_socket_release (web_socket);
			}
		}
	}

	return started;

}

#pragma endregion
