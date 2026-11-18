#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/network/socket.h"

#include "cerver/crypto/random.h"

#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/internal.h"
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

// what is read and discarded of a peer that did not answer the close of cerver, before the socket is closed
#define HTTP_WEB_SOCKET_DRAIN_SIZE				(1 << 16)
#define HTTP_WEB_SOCKET_DRAIN_BUFFER_SIZE		(1 << 12)
#define HTTP_WEB_SOCKET_DRAIN_MILLISECONDS		(1 << 8)

#define HTTP_WEB_SOCKET_IP_FORMAT				"%s"

// A9: the ids of the connections, of the servers and of the clients: unique in the process
static u64 http_web_socket_next_id = 0;

// the string of a value out of the table of its enum
static const char *http_web_socket_undefined = { "Undefined" };

const char *http_web_socket_state_to_string (const HttpWebSocketState state) {

	switch (state) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_STATE_##name: return #string;
		HTTP_WEB_SOCKET_STATE_MAP(XX)
		#undef XX
	}

	return http_web_socket_undefined;

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

void http_web_socket_closed (HttpWebSocket *web_socket) {

	if (web_socket) {
		(void) pthread_mutex_lock (&web_socket->send_mutex);
		http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSED);
		(void) pthread_mutex_unlock (&web_socket->send_mutex);
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

const String *http_web_socket_get_query (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->query : NULL;

}

const char *http_web_socket_get_protocol (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->protocol : NULL;

}

bool http_web_socket_is_compressed (const HttpWebSocket *web_socket) {

	return web_socket && web_socket->compressed;

}

HttpCerver *http_web_socket_get_http_cerver (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->http_cerver : NULL;

}

const HttpEndpoint *http_web_socket_get_endpoint (const HttpWebSocket *web_socket) {

	return web_socket ? web_socket->endpoint : NULL;

}

const void *http_web_socket_get_endpoint_custom_data (const HttpWebSocket *web_socket) {

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
		web_socket->id = __atomic_add_fetch (&http_web_socket_next_id, 1, __ATOMIC_SEQ_CST);
		web_socket->role = role;
		web_socket->state = (unsigned int) HTTP_WEB_SOCKET_STATE_CONNECTING;
		web_socket->references = 1;

		if (settings) web_socket->settings = *settings;
		else http_web_socket_settings_init (&web_socket->settings);

		web_socket->wake_fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);

		(void) pthread_mutex_init (&web_socket->send_mutex, NULL);
		(void) pthread_mutex_init (&web_socket->message_mutex, NULL);
		(void) pthread_mutex_init (&web_socket->mutex, NULL);

		web_socket->close_code = HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL;
		web_socket->error = HTTP_WEB_SOCKET_ERROR_NONE;

		web_socket->source.fd = -1;

		// a client takes its masks from random bytes
		if (role == HTTP_WEB_SOCKET_ROLE_CLIENT) web_socket->masks_position = HTTP_WEB_SOCKET_MASKS_SIZE;

		const bool created = (web_socket->wake_fd >= 0);

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

	http_web_socket_queue_delete (web_socket);
	http_web_socket_batch_delete (&web_socket->batch);
	http_web_socket_batch_delete (&web_socket->running);

	free (web_socket->buffer);
	free (web_socket->pending);
	free (web_socket->protocol);

	str_delete (web_socket->path);
	str_delete (web_socket->query);

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
		web_socket->compressed = (web_socket->deflate != NULL);
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

// the loop of the connection handles the requests in its next step, a thread wakes up and checks everything
void http_web_socket_notify (HttpWebSocket *web_socket, const unsigned int requests) {

	if (web_socket->loop) http_web_socket_loop_request (web_socket, requests);
	else http_web_socket_wake (web_socket);

}

// keeps the first error, that the owner of the connection reports
void http_web_socket_set_error (
	HttpWebSocket *web_socket, const HttpWebSocketError error
) {

	(void) pthread_mutex_lock (&web_socket->mutex);
	if (web_socket->error == HTTP_WEB_SOCKET_ERROR_NONE) web_socket->error = error;
	(void) pthread_mutex_unlock (&web_socket->mutex);

}

HttpWebSocketError http_web_socket_get_error (HttpWebSocket *web_socket) {

	HttpWebSocketError error = HTTP_WEB_SOCKET_ERROR_NONE;

	if (web_socket) {
		(void) pthread_mutex_lock (&web_socket->mutex);
		error = web_socket->error;
		(void) pthread_mutex_unlock (&web_socket->mutex);
	}

	return error;

}

void http_web_socket_abort (HttpWebSocket *web_socket) {

	if (web_socket) {
		__atomic_store_n (&web_socket->aborted, true, __ATOMIC_SEQ_CST);

		// a recv () or a poll () that is waiting ends now,
		// the owner of the connection releases its socket with the same mutex
		(void) pthread_mutex_lock (&web_socket->mutex);
		if (web_socket->transport) web_socket->transport->shutdown (web_socket);
		(void) pthread_mutex_unlock (&web_socket->mutex);

		http_web_socket_notify (web_socket, HTTP_WEB_SOCKET_REQUEST_ABORT);
	}

}

// the connection failed, it ends without the closing handshake
void http_web_socket_fail (
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
		http_web_socket_notify (web_socket, HTTP_WEB_SOCKET_REQUEST_GO_AWAY);
	}

}

#pragma endregion

#pragma region send

// the next 4 random bytes of a client, renewed when they run out
void http_web_socket_next_mask (HttpWebSocket *web_socket, u8 *mask) {

	if ((web_socket->masks_position + HTTP_WEB_SOCKET_MASK_SIZE) > HTTP_WEB_SOCKET_MASKS_SIZE) {
		if (!cerver_crypto_random_bytes (web_socket->masks, HTTP_WEB_SOCKET_MASKS_SIZE)) {
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

// the milliseconds of each send of the connection
unsigned int http_web_socket_send_milliseconds (const HttpWebSocket *web_socket) {

	unsigned int timeout = web_socket->settings.send_timeout;

	if (!timeout && web_socket->http_cerver) timeout = web_socket->http_cerver->send_timeout;

	if (!timeout && web_socket->cerver && web_socket->cerver->send_timeout) {
		timeout = (unsigned int) web_socket->cerver->send_timeout * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND;
	}

	if (!timeout) timeout = CERVER_DEFAULT_SEND_TIMEOUT * HTTP_WEB_SOCKET_MILLISECONDS_PER_SECOND;

	return timeout;

}

// sends a control frame (a pong, a ping), that always fits in the queue
// returns 0 when it was taken, 1 when it was not
static u8 http_web_socket_send_control (
	HttpWebSocket *web_socket, const u8 opcode,
	const void *payload, const size_t size
) {

	u8 retval = 1;

	if (size <= HTTP_WEB_SOCKET_CONTROL_MAX_SIZE) {
		(void) pthread_mutex_lock (&web_socket->send_mutex);
		const HttpWebSocketSendResult result = http_web_socket_queue_frame (
			web_socket, true, false, opcode, (const u8 *) payload, size, true
		);
		(void) pthread_mutex_unlock (&web_socket->send_mutex);

		retval = http_web_socket_send_result_is_taken (result) ? 0 : 1;
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
	__atomic_add_fetch (&web_socket->n_bytes_sent, size, __ATOMIC_SEQ_CST);

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		CERVER_STATS_ADD (registry->n_messages_sent, 1);
		CERVER_STATS_ADD (registry->n_bytes_sent, size);
	}

}

static const char *http_web_socket_send_result_undefined = { "Undefined" };

const char *http_web_socket_send_result_to_string (const HttpWebSocketSendResult result) {

	switch (result) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_SEND_##name: return #string;
		HTTP_WEB_SOCKET_SEND_RESULT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_web_socket_send_result_undefined;

}

bool http_web_socket_send_result_is_taken (const HttpWebSocketSendResult result) {

	return (result == HTTP_WEB_SOCKET_SEND_QUEUED) || (result == HTTP_WEB_SOCKET_SEND_BACKPRESSURE);

}

// the payload of a whole message, compressed when it is large enough, into the queue
// the message mutex must be held
// a connection that sent its close while the message waited does not take it:
// that is an error of the send and not a failure of the connection
static HttpWebSocketSendResult http_web_socket_send_message_frame (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_QUEUED;

	const u8 *payload = (const u8 *) data;
	size_t payload_size = size;
	bool compressed = false;

	if (
		web_socket->deflate && (size >= web_socket->deflate->min_size)
		&& (size <= HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX)
	) {
		// the compressor only fails without memory
		if (http_web_socket_deflate_compress (web_socket->deflate, data, size, true, &payload, &payload_size)) {
			http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
			result = HTTP_WEB_SOCKET_SEND_FAILED;
		}

		compressed = true;
	}

	if (result == HTTP_WEB_SOCKET_SEND_QUEUED) {
		(void) pthread_mutex_lock (&web_socket->send_mutex);

		result = http_web_socket_can_send_data (web_socket) ?
			http_web_socket_queue_frame (web_socket, true, compressed, (u8) type, payload, payload_size, false) :
			HTTP_WEB_SOCKET_SEND_CLOSED;

		(void) pthread_mutex_unlock (&web_socket->send_mutex);
	}

	return result;

}

static bool http_web_socket_is_data_type (const HttpWebSocketOpcode type) {

	return (type == HTTP_WEB_SOCKET_OPCODE_TEXT) || (type == HTTP_WEB_SOCKET_OPCODE_BINARY);

}

HttpWebSocketSendResult http_web_socket_send (
	HttpWebSocket *web_socket,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_FAILED;

	if (web_socket && http_web_socket_is_data_type (type) && (data || !size)) {
		(void) pthread_mutex_lock (&web_socket->message_mutex);

		// the state is checked before anything of the cerver is used:
		// a connection that is kept after its cerver was released only returns its result
		result = http_web_socket_can_send_data (web_socket) ?
			http_web_socket_send_message_frame (web_socket, type, data, size) : HTTP_WEB_SOCKET_SEND_CLOSED;

		if (http_web_socket_send_result_is_taken (result)) http_web_socket_message_sent (web_socket, size);

		(void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	return result;

}

HttpWebSocketSendResult http_web_socket_send_text (
	HttpWebSocket *web_socket, const char *text, const size_t length
) {

	return http_web_socket_send (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT, text, length);

}

HttpWebSocketSendResult http_web_socket_send_binary (
	HttpWebSocket *web_socket, const void *data, const size_t size
) {

	return http_web_socket_send (web_socket, HTTP_WEB_SOCKET_OPCODE_BINARY, data, size);

}

HttpWebSocketSendResult http_web_socket_send_begin (
	HttpWebSocket *web_socket, const HttpWebSocketOpcode type
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_FAILED;

	if (web_socket && http_web_socket_is_data_type (type)) {
		(void) pthread_mutex_lock (&web_socket->message_mutex);

		// held until http_web_socket_send_end ()
		if (http_web_socket_can_send_data (web_socket)) {
			web_socket->streaming = true;
			web_socket->stream_owner = pthread_self ();
			web_socket->stream_type = (u8) type;
			web_socket->stream_started = false;
			web_socket->stream_compressed = (web_socket->deflate != NULL);
			web_socket->stream_size = 0;

			result = HTTP_WEB_SOCKET_SEND_QUEUED;
		}

		// a stream that does not begin holds nothing, so there is nothing to end
		else {
			result = HTTP_WEB_SOCKET_SEND_CLOSED;
			(void) pthread_mutex_unlock (&web_socket->message_mutex);
		}
	}

	return result;

}

// a frame of the stream, the first one has the type of the message; a piece always fits in the queue
static HttpWebSocketSendResult http_web_socket_send_stream_frame (
	HttpWebSocket *web_socket, const bool fin,
	const u8 *payload, const size_t size
) {

	const u8 opcode = web_socket->stream_started ?
		(u8) HTTP_WEB_SOCKET_OPCODE_CONTINUATION : web_socket->stream_type;

	const bool rsv1 = web_socket->stream_compressed && !web_socket->stream_started;

	(void) pthread_mutex_lock (&web_socket->send_mutex);

	// a connection that already sent its close does not take the frame: that is an error of the send,
	// not a failure of the connection, that keeps closing with its handshake
	const HttpWebSocketSendResult result = http_web_socket_can_send_data (web_socket) ?
		http_web_socket_queue_frame (web_socket, fin, rsv1, opcode, payload, size, true) : HTTP_WEB_SOCKET_SEND_CLOSED;

	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	web_socket->stream_started = true;

	return result;

}

// the stream is written and ended by the thread that began it, the one that holds the mutex of the messages
static bool http_web_socket_stream_owned (const HttpWebSocket *web_socket) {

	return web_socket && web_socket->streaming && pthread_equal (web_socket->stream_owner, pthread_self ());

}

// a piece that the compressor can not take only fails without memory
static HttpWebSocketSendResult http_web_socket_stream_compress (
	HttpWebSocket *web_socket, const void *data, const size_t size, const bool end,
	const u8 **payload, size_t *payload_size
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_QUEUED;

	if (http_web_socket_deflate_compress (web_socket->deflate, data, size, end, payload, payload_size)) {
		http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
		result = HTTP_WEB_SOCKET_SEND_FAILED;
	}

	return result;

}

HttpWebSocketSendResult http_web_socket_send_write (
	HttpWebSocket *web_socket, const void *data, const size_t size
) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_FAILED;

	if (http_web_socket_stream_owned (web_socket) && (data || !size)) {
		const u8 *payload = (const u8 *) data;
		size_t payload_size = size;

		result = web_socket->stream_compressed ?
			http_web_socket_stream_compress (web_socket, data, size, false, &payload, &payload_size) :
			HTTP_WEB_SOCKET_SEND_QUEUED;

		// the compressor can keep everything until the end of the message
		if ((result == HTTP_WEB_SOCKET_SEND_QUEUED) && payload_size) {
			result = http_web_socket_send_stream_frame (web_socket, false, payload, payload_size);
		}

		if (http_web_socket_send_result_is_taken (result)) web_socket->stream_size += size;
	}

	return result;

}

HttpWebSocketSendResult http_web_socket_send_end (HttpWebSocket *web_socket) {

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_FAILED;

	if (http_web_socket_stream_owned (web_socket)) {
		const u8 *payload = NULL;
		size_t payload_size = 0;

		result = web_socket->stream_compressed ?
			http_web_socket_stream_compress (web_socket, NULL, 0, true, &payload, &payload_size) :
			HTTP_WEB_SOCKET_SEND_QUEUED;

		if (result == HTTP_WEB_SOCKET_SEND_QUEUED) {
			result = http_web_socket_send_stream_frame (web_socket, true, payload, payload_size);
		}

		if (http_web_socket_send_result_is_taken (result)) http_web_socket_message_sent (web_socket, web_socket->stream_size);

		web_socket->streaming = false;

		(void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	return result;

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

u8 http_web_socket_close_frame_locked (
	HttpWebSocket *web_socket, const u16 code,
	const char *reason, const size_t length
) {

	u8 retval = 1;

	u8 payload[HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	size_t size = 0;
	if (code != HTTP_WEB_SOCKET_CLOSE_CODE_NO_STATUS) {
		payload[0] = (u8) ((code >> HTTP_WEB_SOCKET_BITS_PER_BYTE) & HTTP_WEB_SOCKET_BYTE_BITS);
		payload[1] = (u8) (code & HTTP_WEB_SOCKET_BYTE_BITS);
		size = HTTP_WEB_SOCKET_CLOSE_FRAME_CODE_SIZE;

		if (reason && length) {
			(void) memcpy (payload + size, reason, length);
			size += length;
		}
	}

	if (!http_web_socket_close_was_sent (web_socket)) {
		// the deadline is written once, before the flag that says it can be read:
		// the owner of the connection reads both without the send mutex
		socket_deadline_create_milliseconds (&web_socket->close_deadline, web_socket->settings.close_timeout);
		__atomic_store_n (&web_socket->close_sent, true, __ATOMIC_SEQ_CST);

		if (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN) {
			http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSING);
		}

		const HttpWebSocketSendResult result = http_web_socket_queue_frame (
			web_socket, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, payload, size, true
		);

		retval = http_web_socket_send_result_is_taken (result) ? 0 : 1;
		if (retval) http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);
	}

	return retval;

}

// sends the close frame once, after every data frame that was taken: the owner of the connection
// writes it and waits for the answer of the peer with its close timeout
// returns 0 when it was taken now, 1 when it was already sent or it failed
static u8 http_web_socket_send_close_frame (
	HttpWebSocket *web_socket, const u16 code,
	const char *reason, const size_t length
) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	const u8 retval = http_web_socket_close_frame_locked (web_socket, code, reason, length);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (!retval) http_web_socket_notify (web_socket, HTTP_WEB_SOCKET_REQUEST_TIMERS);

	return retval;

}

// a close without a code has no reason either (RFC 6455 section 5.5.1)
static bool http_web_socket_close_empty_is_valid (const char *reason) {

	return !reason || !reason[0];

}

bool http_web_socket_close_is_valid (const u16 code, const char *reason) {

	const size_t length = reason ? strlen (reason) : 0;

	return (code == HTTP_WEB_SOCKET_CLOSE_WITHOUT_CODE) ?
		http_web_socket_close_empty_is_valid (reason) :
		http_web_socket_close_code_is_valid (code)
			&& (length <= HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE)
			&& http_web_socket_text_check (reason, length);

}

u8 http_web_socket_close (
	HttpWebSocket *web_socket, const u16 code, const char *reason
) {

	u8 retval = 1;

	const size_t length = reason ? strlen (reason) : 0;
	if (web_socket && http_web_socket_close_is_valid (code, reason)) {
		const HttpWebSocketState state = http_web_socket_get_state (web_socket);
		if ((state == HTTP_WEB_SOCKET_STATE_OPEN) || (state == HTTP_WEB_SOCKET_STATE_CLOSING)) {
			// a close frame without a code is the one of 1005, that never has a payload
			retval = http_web_socket_send_close_frame (
				web_socket,
				(code == HTTP_WEB_SOCKET_CLOSE_WITHOUT_CODE) ? HTTP_WEB_SOCKET_CLOSE_CODE_NO_STATUS : code,
				reason, length
			);
		}
	}

	return retval;

}

#pragma endregion

#pragma region receive

// the code and the reason that on_close reports
void http_web_socket_set_close (
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
	__atomic_add_fetch (&web_socket->n_bytes_received, size, __ATOMIC_SEQ_CST);

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;
		if (last) CERVER_STATS_ADD (registry->n_messages_received, 1);
		CERVER_STATS_ADD (registry->n_bytes_received, size);
	}

}

static void http_web_socket_message_received (HttpWebSocket *web_socket, const size_t size) {

	http_web_socket_fragment_received (web_socket, size, true);

}

// with a loop, an event of the next job of the pool: it fails the connection without memory
static int http_web_socket_deliver (
	HttpWebSocket *web_socket, const HttpWebSocketEventType type, const HttpWebSocketOpcode opcode,
	const u8 *payload, const size_t size, const bool first, const bool last
) {

	if (http_web_socket_loop_event (web_socket, type, opcode, payload, size, first, last)) {
		http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
	}

	return http_web_socket_handled (web_socket);

}

static int http_web_socket_on_message (
	void *web_socket_ptr, HttpWebSocketOpcode type,
	const u8 *payload, size_t size
) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	http_web_socket_message_received (web_socket, size);

	if (web_socket->loop) {
		(void) http_web_socket_deliver (web_socket, HTTP_WEB_SOCKET_EVENT_MESSAGE, type, payload, size, true, true);
	}

	else if (web_socket->settings.handlers.on_message) {
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

	if (web_socket->loop) {
		(void) http_web_socket_deliver (web_socket, HTTP_WEB_SOCKET_EVENT_FRAGMENT, type, payload, size, first, last);
	}

	else {
		web_socket->settings.handlers.on_fragment (web_socket, type, payload, size, first, last);
	}

	return http_web_socket_handled (web_socket);

}

static int http_web_socket_on_ping (void *web_socket_ptr, const u8 *payload, size_t size) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	// a pong answers every ping, until the close frame was sent
	if (!http_web_socket_close_was_sent (web_socket)) {
		(void) http_web_socket_send_control (web_socket, HTTP_WEB_SOCKET_OPCODE_PONG, payload, size);
	}

	if (web_socket->settings.handlers.on_ping) {
		if (web_socket->loop) {
			(void) http_web_socket_deliver (
				web_socket, HTTP_WEB_SOCKET_EVENT_PING, HTTP_WEB_SOCKET_OPCODE_PING, payload, size, true, true
			);
		}

		else {
			web_socket->settings.handlers.on_ping (web_socket, payload, size);
		}
	}

	return http_web_socket_handled (web_socket);

}

// the next ping is sent after the ping interval without receiving anything
void http_web_socket_alive (HttpWebSocket *web_socket) {

	web_socket->ping_sent = false;

	if (web_socket->settings.ping_interval) {
		socket_deadline_create_milliseconds (&web_socket->pong_deadline, web_socket->settings.ping_interval);
	}

}

static int http_web_socket_on_pong (void *web_socket_ptr, const u8 *payload, size_t size) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	if (web_socket->settings.handlers.on_pong) {
		if (web_socket->loop) {
			(void) http_web_socket_deliver (
				web_socket, HTTP_WEB_SOCKET_EVENT_PONG, HTTP_WEB_SOCKET_OPCODE_PONG, payload, size, true, true
			);
		}

		else {
			web_socket->settings.handlers.on_pong (web_socket, payload, size);
		}
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
bool http_web_socket_receive_bytes (
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
				case HTTP_WEB_SOCKET_RECEIVE_RESULT_CLOSED: finished = http_web_socket_peer_closed (web_socket); parsing = false; break;
				case HTTP_WEB_SOCKET_RECEIVE_RESULT_FAILED: http_web_socket_peer_failed (web_socket); finished = true; break;
				case HTTP_WEB_SOCKET_RECEIVE_RESULT_STOPPED: parsing = (offset < size); break;
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

	const ssize_t received = web_socket->transport->read (web_socket, web_socket->buffer, web_socket->buffer_size);

	if (received > 0) {
		http_web_socket_alive (web_socket);
		finished = http_web_socket_receive_bytes (web_socket, web_socket->buffer, (size_t) received);
	}

	// the end of the TCP connection, after the closing handshake or without it (1006)
	else if (received == 0) {
		if (!web_socket->close_received) http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
		finished = true;
	}

	else if ((errno != EAGAIN) && (errno != EWOULDBLOCK) && (errno != EINTR)) {
		if (!web_socket->close_received) {
			http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
			http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_RECEIVE);
		}

		finished = true;
	}

	return finished;

}

#pragma endregion

#pragma region thread

// the deadline of the closing handshake, when the close frame was sent
// the send mutex is not taken: the thread of the connection keeps reading
// while another thread waits in a send, so two sides that write at once do not block each other
// returns true when the close was sent
static bool http_web_socket_close_deadline_get (
	const HttpWebSocket *web_socket, struct timespec *close_deadline
) {

	const bool close_sent = http_web_socket_close_was_sent (web_socket);

	// the deadline was written before the flag
	if (close_sent) *close_deadline = web_socket->close_deadline;

	return close_sent;

}

// the shortest of two timeouts, HTTP_WEB_SOCKET_POLL_FOREVER is none
static int http_web_socket_timeout_shortest (const int timeout, const int other) {

	return ((other != HTTP_WEB_SOCKET_POLL_FOREVER) && ((timeout == HTTP_WEB_SOCKET_POLL_FOREVER) || (other < timeout))) ?
		other : timeout;

}

// the milliseconds until the next timer of the connection, HTTP_WEB_SOCKET_POLL_FOREVER without one
int http_web_socket_next_timeout (const HttpWebSocket *web_socket, const bool pings) {

	int timeout = HTTP_WEB_SOCKET_POLL_FOREVER;

	struct timespec close_deadline = { 0, 0 };
	const bool close_sent = http_web_socket_close_deadline_get (web_socket, &close_deadline);

	if (close_sent) {
		timeout = socket_deadline_remaining (&close_deadline);
	}

	else if (pings && web_socket->settings.ping_interval) {
		// the pong deadline is also the time of the next ping
		timeout = socket_deadline_remaining (&web_socket->pong_deadline);
	}

	return http_web_socket_timeout_shortest (timeout, http_web_socket_queue_remaining (web_socket));

}

// the closing handshake ended without the answer of the peer: 1006
static bool http_web_socket_close_expired (HttpWebSocket *web_socket) {

	if (!web_socket->close_received) {
		http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
		http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_CLOSE_TIMEOUT);
	}

	return true;

}

// sends the next ping, the peer has its pong timeout to answer it
// returns true when the connection ends
static bool http_web_socket_ping_next (HttpWebSocket *web_socket) {

	u8 payload[HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE];
	web_socket->n_pings += 1;
	for (size_t i = 0; i < HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE; i++) {
		payload[i] = (u8) ((web_socket->n_pings >> (i * HTTP_WEB_SOCKET_BITS_PER_BYTE)) & HTTP_WEB_SOCKET_BYTE_BITS);
	}

	const bool finished = http_web_socket_send_control (
		web_socket, HTTP_WEB_SOCKET_OPCODE_PING, payload, HTTP_WEB_SOCKET_PING_PAYLOAD_SIZE
	) != 0;

	web_socket->ping_sent = true;
	socket_deadline_create_milliseconds (&web_socket->pong_deadline, web_socket->settings.pong_timeout);

	return finished;

}

// the ping was not answered in time: 1006
static bool http_web_socket_pong_expired (HttpWebSocket *web_socket) {

	http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
	http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_PONG_TIMEOUT);

	return true;

}

// the queue wrote nothing in its send timeout: the peer does not read, the connection fails (1006)
static bool http_web_socket_queue_expired (HttpWebSocket *web_socket) {

	http_web_socket_set_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL, 0);
	http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);

	return true;

}

// the timers that expired, the next ping, its pong, the closing handshake and the send timeout of the queue
// returns true when the connection ends
bool http_web_socket_timers (HttpWebSocket *web_socket, const bool pings) {

	bool finished = false;

	struct timespec close_deadline = { 0, 0 };
	const bool close_sent = http_web_socket_close_deadline_get (web_socket, &close_deadline);

	if (!http_web_socket_queue_remaining (web_socket)) {
		finished = http_web_socket_queue_expired (web_socket);
	}

	else if (close_sent) {
		if (!socket_deadline_remaining (&close_deadline)) finished = http_web_socket_close_expired (web_socket);
	}

	else if (pings && web_socket->settings.ping_interval && !socket_deadline_remaining (&web_socket->pong_deadline)) {
		finished = web_socket->ping_sent ?
			http_web_socket_pong_expired (web_socket) : http_web_socket_ping_next (web_socket);
	}

	return finished;

}

// the eventfd was written, by a send, a close, a go away or an abort
// returns true when the connection ends
static bool http_web_socket_woken (HttpWebSocket *web_socket) {

	u64 value = 0;
	while (read (web_socket->wake_fd, &value, sizeof (u64)) > 0);

	if (__atomic_load_n (&web_socket->going_away, __ATOMIC_SEQ_CST)) {
		(void) http_web_socket_send_close_frame (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL, 0);
	}

	return http_web_socket_is_aborted (web_socket);

}

// on_drain () runs once the queue is empty after a send went over half of it
static void http_web_socket_drain_check (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	const bool drained = web_socket->queue_drained;
	web_socket->queue_drained = false;
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (drained && web_socket->settings.handlers.on_drain) web_socket->settings.handlers.on_drain (web_socket);

}

// writes what the stream takes of the queue
// returns true when the connection ends
static bool http_web_socket_writable (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	const u8 failed = http_web_socket_queue_flush (web_socket);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	if (failed) http_web_socket_fail (web_socket, HTTP_WEB_SOCKET_ERROR_SEND);

	return failed != 0;

}

// waits for the socket, the eventfd or the next timer
// returns true when the connection ends
static bool http_web_socket_step (HttpWebSocket *web_socket) {

	bool finished = false;

	const int sock_fd = web_socket->transport->fd (web_socket);
	const short writes = http_web_socket_queue_is_empty (web_socket) ? 0 : POLLOUT;

	struct pollfd fds[HTTP_WEB_SOCKET_POLL_FDS];
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].fd = sock_fd;
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].events = (short) (POLLIN | writes);
	fds[HTTP_WEB_SOCKET_POLL_SOCKET].revents = 0;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].fd = web_socket->wake_fd;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].events = POLLIN;
	fds[HTTP_WEB_SOCKET_POLL_WAKE].revents = 0;

	// with TLS a record that was decrypted already never makes the fd readable: the poll () does not wait for it
	const bool pending = web_socket->transport->pending (web_socket);

	const int ready = poll (
		fds, HTTP_WEB_SOCKET_POLL_FDS, pending ? 0 : http_web_socket_next_timeout (web_socket, true)
	);

	if ((ready > 0) || (pending && !ready)) {
		if (fds[HTTP_WEB_SOCKET_POLL_WAKE].revents) finished = http_web_socket_woken (web_socket);

		const short revents = (short) (fds[HTTP_WEB_SOCKET_POLL_SOCKET].revents | (pending ? POLLIN : 0));
		if (!finished && (revents & POLLOUT)) finished = http_web_socket_writable (web_socket);
		if (!finished && (revents & ~POLLOUT)) finished = http_web_socket_receive (web_socket);
	}

	else if ((ready < 0) && (errno != EINTR)) {
		http_web_socket_set_error (web_socket, HTTP_WEB_SOCKET_ERROR_RECEIVE);
		finished = true;
	}

	if (!finished) http_web_socket_drain_check (web_socket);

	if (!finished) finished = http_web_socket_timers (web_socket, true) || http_web_socket_is_aborted (web_socket);

	return finished;

}

// writes the queue before the end of the stream: a close frame waits in it, until the stream takes it
// or the queue does not write anything in its send timeout
static void http_web_socket_flush_all (HttpWebSocket *web_socket) {

	bool flushing = !http_web_socket_is_aborted (web_socket);
	while (flushing && !http_web_socket_queue_is_empty (web_socket)) {
		struct pollfd fd = { web_socket->transport->fd (web_socket), POLLOUT, 0 };
		const int remaining = http_web_socket_queue_remaining (web_socket);

		flushing = (remaining > 0) && (poll (&fd, 1, remaining) > 0)
			&& !http_web_socket_writable (web_socket) && !http_web_socket_is_aborted (web_socket);
	}

}

// reads and discards what the peer sends, up to HTTP_WEB_SOCKET_DRAIN_SIZE bytes,
// until it ends its side or HTTP_WEB_SOCKET_DRAIN_MILLISECONDS pass
static void http_web_socket_drain (HttpWebSocket *web_socket) {

	u8 buffer[HTTP_WEB_SOCKET_DRAIN_BUFFER_SIZE];

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, HTTP_WEB_SOCKET_DRAIN_MILLISECONDS);

	size_t drained = 0;
	bool ended = false;
	while (!ended && (drained < HTTP_WEB_SOCKET_DRAIN_SIZE)) {
		struct pollfd fd = { web_socket->transport->fd (web_socket), POLLIN, 0 };
		const int remaining = socket_deadline_remaining (&deadline);

		ended = !remaining || (poll (&fd, 1, remaining) <= 0);
		if (!ended) {
			const ssize_t received = web_socket->transport->read (web_socket, buffer, HTTP_WEB_SOCKET_DRAIN_BUFFER_SIZE);

			if (received > 0) drained += (size_t) received;
			else ended = true;
		}
	}

}

// the peer can still be sending after the close of cerver, that it did not answer
bool http_web_socket_drain_needed (const HttpWebSocket *web_socket) {

	return http_web_socket_close_was_sent (web_socket)
		&& !web_socket->close_received && !http_web_socket_is_aborted (web_socket);

}

// ends the side of cerver of the TCP connection, after its queue was written
// when cerver sent its close and the peer did not answer it, the peer can still be sending
// (a message that was too large keeps arriving after its close): that is read and discarded for a moment,
// because a close () with unread data answers with a RST that can destroy the close frame of cerver
static void http_web_socket_end_writes (HttpWebSocket *web_socket) {

	http_web_socket_flush_all (web_socket);

	if (web_socket->transport->fd (web_socket) >= 0) {
		web_socket->transport->end_writes (web_socket);

		if (http_web_socket_drain_needed (web_socket)) http_web_socket_drain (web_socket);
	}

}

HttpWebSocketError http_web_socket_ended (HttpWebSocket *web_socket) {

	// a recv () or a poll () that is waiting ends now, and no send can start again
	web_socket->transport->shutdown (web_socket);

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSED);
	web_socket->writes_ended = true;
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

	return error;

}

// the connection ended and nothing reads it again: zlib, the buffer of its messages and its empty queue go now,
// not with its last reference, that an application can keep (W7 / S10); a stream that still holds the messages
// keeps zlib until the release of the connection
static void http_web_socket_release_buffers (HttpWebSocket *web_socket) {

	HttpWebSocketDeflate *deflate = NULL;
	if (!pthread_mutex_trylock (&web_socket->message_mutex)) {
		deflate = web_socket->deflate;
		web_socket->deflate = NULL;
		web_socket->receiver.deflate = NULL;
		(void) pthread_mutex_unlock (&web_socket->message_mutex);
	}

	http_web_socket_deflate_delete (deflate);

	http_web_socket_receiver_end (&web_socket->receiver);

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	if (http_web_socket_queue_is_empty (web_socket)) http_web_socket_queue_delete (web_socket);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

}

void http_web_socket_closed_methods (HttpWebSocket *web_socket, const HttpWebSocketError error) {

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

	// on_close () still saw its rooms
	http_web_socket_rooms_clear (web_socket);

	http_web_socket_release_buffers (web_socket);

}

// the connection ended, its methods run for the last time
static void http_web_socket_finish (HttpWebSocket *web_socket) {

	http_web_socket_end_writes (web_socket);

	http_web_socket_closed_methods (web_socket, http_web_socket_ended (web_socket));

}

void http_web_socket_receiver_setup (HttpWebSocket *web_socket) {

	http_web_socket_receiver_init (
		&web_socket->receiver, web_socket->role,
		web_socket->settings.max_message, web_socket->settings.max_message,
		web_socket->deflate,
		web_socket->settings.handlers.on_fragment ?
			&http_web_socket_receiver_fragment_handlers : &http_web_socket_receiver_handlers,
		web_socket
	);

}

// S7: the broadcasts and http_cerver_web_socket_for_each () see a connection once its on_open () ran
void http_web_socket_register (HttpWebSocket *web_socket) {

	if (web_socket->http_cerver && web_socket->reserved) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;

		web_socket->reserved = false;

		// a cerver that is going away does not take it: it goes away too
		if (http_web_socket_registry_add (registry, web_socket)) {
			http_web_socket_registry_unreserve (registry);
			http_web_socket_go_away (web_socket);
		}
	}

}

void http_web_socket_run (HttpWebSocket *web_socket) {

	if (web_socket && web_socket->transport) {
		http_web_socket_receiver_setup (web_socket);

		// a close that came before its thread started is kept
		http_web_socket_opened (web_socket);
		http_web_socket_alive (web_socket);

		if (web_socket->settings.handlers.on_open) web_socket->settings.handlers.on_open (web_socket);

		http_web_socket_register (web_socket);

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

	http_web_socket_stream_end (web_socket);

	http_web_socket_release (web_socket);

	return NULL;

}

u8 http_web_socket_stream_set (HttpWebSocket *web_socket, Socket *socket, const bool thread) {

	web_socket->socket = socket;
	web_socket->transport = socket->tls ? &http_web_socket_tls_transport : &http_web_socket_socket_transport;

	http_web_socket_queue_setup (web_socket);

	// a loop reads in its own buffer
	if (thread) {
		web_socket->buffer_size = HTTP_WEB_SOCKET_RECEIVE_BUFFER_SIZE;
		web_socket->buffer = (u8 *) malloc (web_socket->buffer_size);
	}

	return (!thread || web_socket->buffer) ? 0 : 1;

}

void http_web_socket_stream_end (HttpWebSocket *web_socket) {

	if (web_socket->http_cerver) {
		HttpWebSocketRegistry *registry = web_socket->http_cerver->web_sockets;

		http_web_socket_registry_remove (registry, web_socket);
		http_web_socket_rooms_clear (web_socket);

		// a connection that never got to the registry gives its place back
		if (web_socket->reserved) {
			web_socket->reserved = false;
			http_web_socket_registry_unreserve (registry);
		}
	}

	// the TCP connection ends with its owner, an abort () of another thread can not reach it after this
	(void) pthread_mutex_lock (&web_socket->mutex);
	Connection *connection = web_socket->connection;
	web_socket->connection = NULL;
	web_socket->socket = NULL;
	(void) pthread_mutex_unlock (&web_socket->mutex);

	if (connection) connection_drop (web_socket->cerver, connection);

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

	if (request && request->query && request->query->len) {
		web_socket->query = str_new (request->query->str);
	}

	const char *ip = (request && request->client_ip[0]) ? request->client_ip : web_socket->connection->ip;
	(void) snprintf (web_socket->ip, CONNECTION_IP_SIZE, HTTP_WEB_SOCKET_IP_FORMAT, ip);

}

// the connection did not start: its socket goes back to its owner, and its place to the registry
static void http_web_socket_start_failed (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->send_mutex);
	http_web_socket_set_state (web_socket, HTTP_WEB_SOCKET_STATE_CLOSED);
	(void) pthread_mutex_unlock (&web_socket->send_mutex);

	(void) pthread_mutex_lock (&web_socket->mutex);
	web_socket->connection = NULL;
	web_socket->socket = NULL;
	(void) pthread_mutex_unlock (&web_socket->mutex);

	if (web_socket->reserved && web_socket->http_cerver) {
		web_socket->reserved = false;
		http_web_socket_registry_unreserve (web_socket->http_cerver->web_sockets);
	}

	http_web_socket_release (web_socket);

}

// creates the connection of the server with what the handshake learned,
// a thread reads in a buffer of its own and a loop in its buffer
static HttpWebSocket *http_web_socket_server_create (
	CerverReceive *cr, HttpReceive *http_receive, HttpWebSocketHandshake *handshake, const bool thread
) {

	const HttpEndpoint *endpoint = http_receive->endpoint;

	HttpWebSocket *web_socket = http_web_socket_create (
		HTTP_WEB_SOCKET_ROLE_SERVER, &endpoint->resolved.web_socket
	);

	if (web_socket) {
		web_socket->connection = cr->connection;
		web_socket->cerver = cr->cerver;

		web_socket->http_cerver = http_receive->http_cerver;
		web_socket->endpoint = endpoint;

		// the place that the handshake took belongs to the connection now
		handshake->reserved = false;
		web_socket->reserved = true;

		http_web_socket_adopt (web_socket, handshake, http_receive->request);

		const bool failed = http_web_socket_stream_set (web_socket, cr->connection->socket, thread) || (
			handshake->compression
			&& http_web_socket_set_compression (web_socket, &handshake->deflate, &endpoint->resolved.web_socket.compression)
		);

		if (failed) {
			http_web_socket_start_failed (web_socket);
			web_socket = NULL;
		}
	}

	return web_socket;

}

// the thread holds the reference of the creation
static bool http_web_socket_start_thread (CerverReceive *cr, HttpWebSocket *web_socket) {

	return !thread_group_create (&cr->cerver->connection_threads, http_web_socket_thread, web_socket);

}

bool http_web_socket_start (CerverReceive *cr, HttpReceive *http_receive, EventLoop *loop) {

	bool started = false;

	HttpWebSocketHandshake *handshake = http_receive ? http_receive->web_socket : NULL;
	if (cr && cr->connection && handshake && http_receive->upgraded) {
		HttpWebSocket *web_socket = http_web_socket_server_create (cr, http_receive, handshake, !loop);
		if (web_socket) {
			// the loop holds the reference of the creation
			started = loop ? http_web_socket_loop_adopt (web_socket, loop) : http_web_socket_start_thread (cr, web_socket);

			if (!started) http_web_socket_start_failed (web_socket);
		}
	}

	return started;

}

#pragma endregion
