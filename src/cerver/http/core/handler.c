#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/pairs.h"
#include "cerver/http/core/public.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static const char *form_urlencoded_value = { "application/x-www-form-urlencoded" };
static const size_t form_urlencoded_value_len = 33;

static const char *query_separator = { "?" };

// a space in a query and in the values of a form
static const char query_encoded_space = '+';
static const char query_space = ' ';

static const char static_file_extension_separator = '.';

static const char *expect_continue_value = { "100-continue" };
static const char *continue_response = { "HTTP/1.1 100 Continue\r\n\r\n" };
static const size_t continue_response_len = 25;

static const char *http_receive_parser_no_reason = { "No reason" };

#pragma region handler

static int http_receive_handle_headers_completed (llhttp_t *parser);

static int http_receive_handle_message_completed (llhttp_t *parser);

const char *http_receive_status_str (
	const HttpReceiveStatus status
) {

	switch (status) {
		#define XX(num, name, string) case HTTP_RECEIVE_STATUS_##name: return #string;
		HTTP_RECEIVE_STATUS_MAP(XX)
		#undef XX
	}

	return http_receive_status_str (HTTP_RECEIVE_STATUS_NONE);

}

static const char *http_parser_lenient_none = { "none" };

const char *http_parser_lenient_string (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #string;
		LLHTTP_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_parser_lenient_description (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #description;
		LLHTTP_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_stage_string (const HttpStage stage) {

	switch (stage) {
		#define XX(num, name, string, label) case HTTP_STAGE_##name: return #string;
		HTTP_STAGE_MAP(XX)
		#undef XX
	}

	return http_stage_string (HTTP_STAGE_QUEUED);

}

const char *http_stage_label (const HttpStage stage) {

	switch (stage) {
		#define XX(num, name, string, label) case HTTP_STAGE_##name: return #label;
		HTTP_STAGE_MAP(XX)
		#undef XX
	}

	return http_stage_label (HTTP_STAGE_QUEUED);

}

const char *http_abort_reason_string (const HttpAbortReason reason) {

	switch (reason) {
		#define XX(num, name, string, label) case HTTP_ABORT_REASON_##name: return #string;
		HTTP_ABORT_REASON_MAP(XX)
		#undef XX
	}

	return http_abort_reason_string (HTTP_ABORT_REASON_CLOSED);

}

const char *http_abort_reason_label (const HttpAbortReason reason) {

	switch (reason) {
		#define XX(num, name, string, label) case HTTP_ABORT_REASON_##name: return #label;
		HTTP_ABORT_REASON_MAP(XX)
		#undef XX
	}

	return http_abort_reason_label (HTTP_ABORT_REASON_CLOSED);

}

const char *http_client_state_string (const HttpClientState state) {

	switch (state) {
		#define XX(num, name, string) case HTTP_CLIENT_STATE_##name: return #string;
		HTTP_CLIENT_STATE_MAP(XX)
		#undef XX
	}

	return http_client_state_string (HTTP_CLIENT_STATE_CONNECTED);

}

static void http_receive_init (HttpReceive *http_receive);

HttpReceive *http_receive_new (void) {

	HttpReceive *http_receive = (HttpReceive *) malloc (sizeof (HttpReceive));
	if (http_receive) {
		http_receive_init (http_receive);
	}

	return http_receive;

}

// what belongs to a request: every request of a connection starts with these values
static void http_receive_request_init (HttpReceive *http_receive) {

	http_receive->receive_status = HTTP_RECEIVE_STATUS_NONE;

	http_receive->keep_alive_requested = false;
	http_receive->unparsed = 0;

	http_receive->protocol_length = 0;
	http_receive->protocol_other = false;

	http_receive->mpart_parser = NULL;
	(void) memset (&http_receive->mpart_settings, 0, sizeof (multipart_parser_settings));

	http_receive->request = NULL;

	http_receive->type = HTTP_RECEIVE_TYPE_NONE;
	http_receive->dispatch = HTTP_RECEIVE_DISPATCH_NONE;
	http_receive->node = NULL;
	http_receive->endpoint = NULL;
	http_receive->route = NULL;
	http_receive->served_file = NULL;

	http_receive->head = false;
	http_receive->max_body_size = 0;

	http_receive->status = HTTP_STATUS_NONE;
	http_receive->sent = 0;

	http_receive->is_multi_part = false;
	http_receive->file_stats = NULL;

	http_receive->upload_sequence = 0;
	http_receive->upload_directory_created = false;

	http_receive->header_size = 0;
	http_receive->n_headers = 0;
	http_receive->body_size = 0;
	http_receive->n_parts = 0;

	http_receive->responded = false;
	http_receive->framed = false;

	http_receive->stream.state = HTTP_STREAM_STATE_NONE;
	http_receive->stream.chunked = false;

	http_receive->start_time = 0;
	http_receive->received = 0;

	http_receive->diagnostics.last_rc = 0;
	http_receive->diagnostics.last_errno = 0;
	http_receive->diagnostics.n_recvs = 0;
	http_receive->diagnostics.timed_out = false;

	http_receive->headers_deadline = 0;
	http_receive->request_deadline = 0;

	http_receive->stage = HTTP_STAGE_QUEUED;
	http_receive->abort_reason = HTTP_ABORT_REASON_CLOSED;

	http_receive->response_failed = false;
	http_receive->response_error = 0;

	http_receive->send_deadline.tv_sec = 0;
	http_receive->send_deadline.tv_nsec = 0;
	http_receive->send_deadline_set = false;

	http_receive->web_socket = NULL;

}

// what belongs to the connection, and the values of its first request
static void http_receive_init (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive->cr = NULL;

		http_receive->keep_alive = false;

		http_receive->handler = NULL;

		http_receive->http_cerver = NULL;

		(void) memset (&http_receive->parser, 0, sizeof (llhttp_t));
		llhttp_settings_init (&http_receive->settings);

		http_receive->url_buffer = NULL;
		http_receive->field_buffer = NULL;
		http_receive->value_buffer = NULL;

		http_receive->arena = NULL;
		http_receive->borrowed_buffers = false;

		http_receive->sock_fd = -1;

		http_receive->upgraded = false;

		http_receive->client_state = HTTP_CLIENT_STATE_CONNECTED;

		http_receive_request_init (http_receive);
	}

}

static void http_receive_parser_init (HttpReceive *http_receive);

static const double http_receive_milliseconds_per_second = 1000.0;
static const double http_receive_nanoseconds_per_second = 1000000000.0;

static double http_receive_monotonic_time (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / http_receive_nanoseconds_per_second);

}

// a deadline in monotonic secs after the milliseconds of a timeout, 0 when it is disabled
static double http_receive_deadline (const double now, const unsigned int timeout) {

	return timeout ? now + ((double) timeout / http_receive_milliseconds_per_second) : 0;

}

// the clock and the deadlines of a request start when its receive is ready for it
static void http_receive_request_begin (HttpReceive *http_receive) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	http_receive->start_time = timer_get_current_time ();

	if (http_cerver) {
		const double now = http_receive_monotonic_time ();

		http_receive->headers_deadline = http_receive_deadline (now, http_cerver->headers_timeout);
		http_receive->request_deadline = http_receive_deadline (now, http_cerver->request_timeout);
	}

}

// creates the request and the stats of its uploads,
// in the arena that was lent to the receive or allocated
// returns 0 on success, 1 on error
static u8 http_receive_request_create (HttpReceive *http_receive) {

	u8 retval = 1;

	Arena *arena = http_receive->arena;

	HttpRequest *request = arena ? http_request_new_arena (arena) : http_request_new ();
	HttpRouteFileStats *file_stats = arena ?
		(HttpRouteFileStats *) arena_alloc (arena, sizeof (HttpRouteFileStats)) :
		http_route_file_stats_new ();

	if (request && file_stats) {
		request->keep_files = !http_receive->http_cerver->uploads_delete_when_done;

		// the stats in an arena are released with it
		if (arena) http_route_file_stats_init (file_stats);

		http_receive->request = request;
		http_receive->file_stats = file_stats;

		retval = 0;
	}

	else if (!arena) {
		if (request) http_request_delete (request);
		if (file_stats) http_route_file_stats_delete (file_stats);
	}

	return retval;

}

// what a receive takes from its connection: its cerver receive, its cerver,
// its fd and the parser of its requests
static void http_receive_connection_set (
	HttpReceive *http_receive, CerverReceive *cerver_receive
) {

	http_receive->cr = cerver_receive;

	http_receive->handler = http_receive_handle;

	http_receive->http_cerver = (HttpCerver *) cerver_receive->cerver->cerver_data;

	http_receive->sock_fd = cerver_receive->socket ? cerver_receive->socket->sock_fd : -1;

	http_receive_parser_init (http_receive);

}

HttpReceive *http_receive_create (CerverReceive *cerver_receive) {

	HttpReceive *http_receive = http_receive_new ();
	if (http_receive) {
		http_receive_connection_set (http_receive, cerver_receive);

		if (!http_receive_request_create (http_receive)) {
			http_receive_request_begin (http_receive);
		}

		else {
			free (http_receive);
			http_receive = NULL;
		}
	}

	return http_receive;

}

static void http_receive_borrow_buffer (String **destination, String *buffer) {

	http_receive_buffer_reset (buffer);
	*destination = buffer;

}

static bool http_receive_resources_are_complete (const HttpReceiveResources *resources) {

	return resources && resources->arena
		&& resources->url_buffer && resources->field_buffer && resources->value_buffer;

}

// the receive is not in the arena of its request, so the arena can be released
// after each one of them while the receive stays with its connection
u8 http_receive_prepare (
	HttpReceive *http_receive,
	CerverReceive *cerver_receive, const HttpReceiveResources *resources
) {

	u8 retval = 1;

	if (http_receive && cerver_receive && http_receive_resources_are_complete (resources)) {
		http_receive_init (http_receive);

		http_receive->arena = resources->arena;

		http_receive->borrowed_buffers = true;
		http_receive_borrow_buffer (&http_receive->url_buffer, resources->url_buffer);
		http_receive_borrow_buffer (&http_receive->field_buffer, resources->field_buffer);
		http_receive_borrow_buffer (&http_receive->value_buffer, resources->value_buffer);

		http_receive_connection_set (http_receive, cerver_receive);

		retval = http_receive_request_create (http_receive);

		if (!retval) http_receive_request_begin (http_receive);

		// what a request that could not be created left in the arena
		else arena_reset (resources->arena);
	}

	return retval;

}

// only the callbacks that cerver uses, the rest stay NULL
static void http_receive_settings_init (llhttp_settings_t *settings) {

	llhttp_settings_init (settings);

	settings->on_url = http_receive_handle_url;
	settings->on_url_complete = http_receive_handle_url_complete;
	settings->on_protocol = http_receive_handle_protocol;
	settings->on_protocol_complete = http_receive_handle_protocol_complete;
	settings->on_version_complete = http_receive_handle_version_complete;
	settings->on_header_field = http_receive_handle_header_field;
	settings->on_header_value = http_receive_handle_header_value;
	settings->on_header_value_complete = http_receive_handle_header_value_complete;
	settings->on_chunk_extension_name = http_receive_handle_discarded;
	settings->on_chunk_extension_value = http_receive_handle_discarded;
	settings->on_headers_complete = http_receive_handle_headers_completed;
	settings->on_body = http_receive_handle_body;
	settings->on_message_complete = http_receive_handle_message_completed;

}

// the parser is embedded in the receive and it uses the receive's settings,
// so its on_body can change for each request
static void http_receive_parser_init (HttpReceive *http_receive) {

	http_receive_settings_init (&http_receive->settings);

	llhttp_init (&http_receive->parser, HTTP_REQUEST, &http_receive->settings);
	http_receive->parser.data = http_receive;

	if (http_receive->http_cerver) {
		llhttp_set_lenient_flags (&http_receive->parser, (uint16_t) http_receive->http_cerver->parser_lenient);
	}

}

static void http_receive_delete_buffers (HttpReceive *http_receive) {

	if (!http_receive->borrowed_buffers) {
		str_delete (http_receive->url_buffer);
		str_delete (http_receive->field_buffer);
		str_delete (http_receive->value_buffer);
	}

	http_receive->url_buffer = NULL;
	http_receive->field_buffer = NULL;
	http_receive->value_buffer = NULL;

}

// a request that was completed keeps its files unless the cerver deletes them when it is done,
// the files of any other request are always discarded, its handler never got them
static bool http_receive_keeps_uploads (const HttpReceive *http_receive) {

	return (http_receive->receive_status == HTTP_RECEIVE_STATUS_COMPLETED)
		|| (http_receive->receive_status == HTTP_RECEIVE_STATUS_RESPONSE_FAILED);

}

// removes the directory that the request created for its uploads,
// rmdir () only removes it if it is empty (a kept file stays with its directory)
static void http_receive_uploads_directory_remove (const HttpReceive *http_receive) {

	const HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;

	if (http_receive->upload_directory_created && http_cerver && request && (request->dirname_len > 0)) {
		char dirname[HTTP_MULTI_PART_DIRNAME_SIZE] = { 0 };
		const int written = snprintf (
			dirname, HTTP_MULTI_PART_DIRNAME_SIZE,
			"%s/%s", http_cerver->uploads_path, request->dirname
		);

		if ((written > 0) && (written < HTTP_MULTI_PART_DIRNAME_SIZE)) (void) rmdir (dirname);
	}

}

static void http_receive_uploads_release (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;

	if (request) {
		if (!http_receive_keeps_uploads (http_receive)) request->keep_files = false;

		http_request_destroy (request);

		http_receive_uploads_directory_remove (http_receive);
	}

}

// what a request leaves when it ends: its uploads, the place of its upgrade,
// the request with its data, the stats of its uploads and the parser of its parts
static void http_receive_request_end (HttpReceive *http_receive) {

	http_receive_uploads_release (http_receive);

	http_receive_web_socket_release (http_receive);

	http_request_delete (http_receive->request);
	http_receive->request = NULL;

	http_receive->node = NULL;
	http_receive->endpoint = NULL;
	http_receive->route = NULL;

	// the stats in an arena are released with it
	if (!http_receive->arena) http_route_file_stats_delete (http_receive->file_stats);
	http_receive->file_stats = NULL;

	if (http_receive->mpart_parser) {
		multipart_parser_free (http_receive->mpart_parser);
		http_receive->mpart_parser = NULL;
	}

}

u8 http_receive_reset (HttpReceive *http_receive) {

	u8 retval = 1;

	if (http_receive) {
		http_receive_request_end (http_receive);

		// the data of the request that ended is released all at once
		if (http_receive->arena) arena_reset (http_receive->arena);

		http_receive_request_init (http_receive);

		// the parser keeps its type and its lenient flags,
		// and the callbacks that a request changed are the ones of a new request
		http_receive_settings_init (&http_receive->settings);
		llhttp_reset (&http_receive->parser);

		http_receive_buffer_reset (http_receive->url_buffer);
		http_receive_buffer_reset (http_receive->field_buffer);
		http_receive_buffer_reset (http_receive->value_buffer);

		retval = http_receive_request_create (http_receive);

		if (!retval) http_receive_request_begin (http_receive);
	}

	return retval;

}

void http_receive_delete (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive_request_end (http_receive);

		http_receive->cr = NULL;

		http_receive->http_cerver = NULL;

		http_receive_delete_buffers (http_receive);

		// a receive that was prepared lives in the memory of its owner
		if (!http_receive->arena) free (http_receive);
	}

}

const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
) {

	return http_receive->cr;

}

const int http_receive_get_sock_fd (
	const HttpReceive *http_receive
) {

	return http_receive->cr->connection->socket->sock_fd;

}

const HttpCerver *http_receive_get_cerver (
	const HttpReceive *http_receive
) {

	return http_receive->http_cerver;

}

const HttpEndpoint *http_receive_get_endpoint (
	const HttpReceive *http_receive
) {

	return http_receive ? http_receive->endpoint : NULL;

}

const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
) {

	const void *custom_data = NULL;

	if (http_receive) {
		if (http_receive->endpoint) {
			custom_data = http_endpoint_get_custom_data (http_receive->endpoint);
		}

		else if (http_receive->route) {
			custom_data = http_receive->route->custom_data;
		}
	}

	return custom_data;

}

const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
) {

	return (http_receive && http_receive->http_cerver) ?
		http_receive->http_cerver->custom_data : NULL;

}

bool http_receive_is_head (
	const HttpReceive *http_receive
) {

	return http_receive ? http_receive->head : false;

}

const char *http_receive_get_allow (
	const HttpReceive *http_receive
) {

	return (http_receive && http_receive->node) ? http_receive->node->allow : NULL;

}

HttpStage http_receive_get_stage (const HttpReceive *http_receive) {

	return http_receive ? http_receive->stage : HTTP_STAGE_QUEUED;

}

// an EOF (error 0) is a close, any error of a recv () or a send () is a failed connection
static inline HttpAbortReason http_receive_abort_reason (const int error) {

	return error ? HTTP_ABORT_REASON_RESET : HTTP_ABORT_REASON_CLOSED;

}

// a send () that waited the whole send timeout, the client stopped reading
static inline bool http_receive_error_is_timeout (const int error) {

	return (error == EAGAIN) || (error == EWOULDBLOCK);

}

void http_receive_client_left (HttpReceive *http_receive, const int error) {

	if (http_receive) {
		switch (http_receive->receive_status) {
			case HTTP_RECEIVE_STATUS_NONE:
			case HTTP_RECEIVE_STATUS_HEADERS:
			case HTTP_RECEIVE_STATUS_BODY:
				http_receive->receive_status = HTTP_RECEIVE_STATUS_CLOSED;
				http_receive->abort_reason = http_receive_abort_reason (error);
				break;

			default: break;
		}

		http_receive->client_state = error ? HTTP_CLIENT_STATE_RESET : HTTP_CLIENT_STATE_CLOSED;
	}

}

// counts the request where its client left, once
static void http_receive_count_gone (
	const HttpReceive *http_receive, const int error
) {

	HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpStage stage = http_receive->stage;

	if (http_receive_error_is_timeout (error)) {
		CERVER_STATS_ADD (http_cerver->n_vanished_requests[stage], 1);
	}

	else {
		CERVER_STATS_ADD (http_cerver->n_aborted_requests[stage][http_receive_abort_reason (error)], 1);
	}

}

void http_receive_response_failed (
	const HttpReceive *http_receive, const int error
) {

	HttpReceive *receive = (HttpReceive *) http_receive;

	if (receive && !receive->response_failed) {
		receive->response_failed = true;
		receive->response_error = error;

		if (!http_receive_error_is_timeout (error)) {
			receive->client_state = HTTP_CLIENT_STATE_RESET;
		}

		if (receive->http_cerver) {
			CERVER_STATS_ADD (receive->http_cerver->n_failed_responses, 1);

			http_receive_count_gone (receive, error);
		}
	}

}

// the state that the events of a poll () on the client's socket tell,
// a peek at its data tells an EOF from a failed connection
static HttpClientState http_receive_client_state_from_events (
	const int sock_fd, const short events
) {

	HttpClientState state = HTTP_CLIENT_STATE_CONNECTED;

	char byte = 0;
	const ssize_t peeked = recv (sock_fd, &byte, sizeof (byte), MSG_PEEK | MSG_DONTWAIT);

	if (peeked < 0) {
		if (!http_receive_error_is_timeout (errno)) state = HTTP_CLIENT_STATE_RESET;
		else if (events & (POLLRDHUP | POLLHUP)) state = HTTP_CLIENT_STATE_CLOSED;
	}

	else if (!peeked || (events & (POLLRDHUP | POLLHUP))) {
		state = HTTP_CLIENT_STATE_CLOSED;
	}

	return state;

}

// a gone client never comes back, a closed one can still reset its connection
static HttpClientState http_receive_client_state_check (const HttpReceive *http_receive) {

	HttpClientState state = HTTP_CLIENT_STATE_RESET;

	const Connection *connection = http_receive->cr ? http_receive->cr->connection : NULL;
	const int sock_fd = (connection && connection->socket) ? connection->socket->sock_fd : -1;

	if (sock_fd > -1) {
		struct pollfd descriptor = { sock_fd, POLLIN | POLLRDHUP, 0 };

		const int ready = poll (&descriptor, 1, 0);
		if (!ready) state = HTTP_CLIENT_STATE_CONNECTED;
		else if (ready > 0) state = http_receive_client_state_from_events (sock_fd, descriptor.revents);
	}

	return state;

}

HttpClientState http_receive_get_client_state (const HttpReceive *http_receive) {

	HttpClientState state = HTTP_CLIENT_STATE_RESET;

	HttpReceive *receive = (HttpReceive *) http_receive;
	if (receive) {
		if (receive->client_state != HTTP_CLIENT_STATE_RESET) {
			const HttpClientState found = http_receive_client_state_check (receive);
			if (found > receive->client_state) receive->client_state = found;
		}

		state = receive->client_state;
	}

	return state;

}

bool http_receive_is_client_gone (const HttpReceive *http_receive) {

	bool gone = true;

	if (http_receive) {
		const HttpClientState state = http_receive_get_client_state (http_receive);

		gone = (state == HTTP_CLIENT_STATE_RESET)
			|| ((state == HTTP_CLIENT_STATE_CLOSED) && http_receive->http_cerver->half_close_is_gone)
			|| http_receive->response_failed;
	}

	return gone;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_response_send (catch_all, http_receive);

}

void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_response_send (not_found_error, http_receive);

}

#pragma GCC diagnostic pop

// catch all mismatches and handle with cath all route
static void http_receive_handle_catch_all (
	HttpCerver *http_cerver, HttpReceive *http_receive, HttpRequest *request
) {

	http_receive->receive_status = HTTP_RECEIVE_STATUS_UNHANDLED;

	#ifdef HTTP_RECEIVE_DEBUG
	cerver_log_warning (
		"No matching route for %s %s",
		http_request_method_str (request->method),
		request->url->str
	);
	#endif

	if (http_cerver->not_found_handler) {
		// handle with not found route
		http_cerver->not_found (http_receive, request);
	}

	else {
		// handle with default route
		http_cerver->default_handler (http_receive, request);

		CERVER_STATS_ADD (http_cerver->n_catch_all_requests, 1);
	}

}

// a key or a value of a query is decoded in its own memory, the decoded one is never longer:
// a '+' is a space and then the escapes are decoded, so an escaped '+' is kept
static void http_query_piece_decode (String *piece) {

	for (size_t idx = 0; idx < piece->len; idx++) {
		if (piece->str[idx] == query_encoded_space) piece->str[idx] = query_space;
	}

	piece->len = http_url_decode_path (piece->str, piece->str, piece->len);
	piece->str[piece->len] = '\0';

}

// the pairs of a query are split by the separators of the original text
// and then each key and each value is decoded, so an escaped separator is part of its value:
// decoding the whole text first made a new pair out of "%26" and "%3D"
static DoubleList *http_query_pairs_decoded (const char *query) {

	DoubleList *pairs = query ? http_parse_query_into_pairs (query, query + strlen (query)) : NULL;
	if (pairs) {
		KeyValuePair *pair = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			pair = (KeyValuePair *) le->data;

			http_query_piece_decode (pair->key);
			http_query_piece_decode (pair->value);
		}
	}

	return pairs;

}

// parses the query of the request into its query params
static void http_receive_parse_query (HttpRequest *request) {

	request->query_params = http_query_pairs_decoded (request->query->str);

}

// a x-www-form-urlencoded body is parsed into the request's body values
static void http_receive_parse_body_values (HttpRequest *request) {

	const String *content_type = request->headers[HTTP_HEADER_CONTENT_TYPE];
	if (
		content_type
		&& !strncmp (form_urlencoded_value, content_type->str, form_urlencoded_value_len)
	) {
		request->body_values = http_query_pairs_decoded (request->body->str);
	}

}

// runs the before middlewares, the handler and the after middlewares

static void http_receive_handle_endpoint (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	const HttpMiddlewares *middlewares = &endpoint->resolved.middlewares;

	if (http_middlewares_run_before (middlewares, http_receive, request)) {
		endpoint->handler (http_receive, request);
	}

	http_middlewares_run_after (middlewares, http_receive, request);

	// a stream that the handler did not end is ended by cerver
	if (http_response_stream_is_active (http_receive)) {
		(void) http_response_stream_end (http_receive);
	}

}

// an upgrade that passed its checks, a request without one goes to the handler of the endpoint,
// a route of the first API with the modifier is answered with 501
static void http_receive_handle_match_web_socket (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	if (http_receive->web_socket) {
		http_receive_web_socket_upgrade (http_receive, request, endpoint);
	}

	else if (endpoint->handler && http_endpoint_is_web_socket (endpoint)) {
		http_receive_handle_endpoint (http_receive, request, endpoint);
	}

	else {
		(void) http_response_send (not_implemented_error, http_receive);
	}

}

// handles the request with its endpoint
static void http_receive_handle_match (
	HttpReceive *http_receive,
	HttpRequest *request,
	const HttpEndpoint *endpoint
) {

	if (request->query) http_receive_parse_query (request);

	if (request->body) http_receive_parse_body_values (request);

	switch (endpoint->resolved.modifier) {
		case HTTP_ROUTE_MODIFIER_WEB_SOCKET:
			http_receive_handle_match_web_socket (http_receive, request, endpoint);
			break;

		default:
			http_receive_handle_endpoint (http_receive, request, endpoint);
			break;
	}

}

static void http_receive_handle_select_failed_auth (
	HttpReceive *http_receive
) {

	(void) http_receive_reject (
		http_receive, bad_auth_error, HTTP_RECEIVE_STATUS_REJECTED
	);

	CERVER_STATS_ADD (http_receive->http_cerver->n_failed_auth_requests, 1);

}

// returns true if the request has a valid bearer JWT
// the route's decode_data method sets the request's decoded data
static bool http_receive_handle_select_auth_bearer (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	// the header must be "Bearer <token>"
	// and a token can only be validated with the cerver's public key
	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];
	const char *token = authorization ?
		http_cerver_auth_bearer_token (authorization->str, authorization->len) : NULL;

	jwt_t *jwt = token ? http_cerver_auth_jwt_verify (
		http_cerver, token, authorization->len - HTTP_JWT_BEARER_PREFIX_LENGTH
	) : NULL;

	if (jwt) {
		if (found->decode_data) {
			request->decoded_data = found->decode_data (jwt->grants);
			request->delete_decoded_data = found->delete_decoded_data;
		}

		jwt_free (jwt);

		authenticated = true;
	}

	else {
		http_receive_handle_select_failed_auth (http_receive);
	}

	return authenticated;

}

// returns true if the route's custom authentication handler accepts the request
static bool http_receive_handle_select_auth_custom (
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	if (found->authentication_handler) {
		if (!found->authentication_handler (
			http_receive, request
		)) {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_success ("Success authentication with custom method!");
			#endif

			authenticated = true;
		}

		else {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_error ("Failed authentication with custom method!");
			#endif

			http_receive_handle_select_failed_auth (http_receive);
		}
	}

	// no authentication method was provided
	else {
		#ifdef HTTP_AUTH_DEBUG
		cerver_log_error ("No authentication method was provided!");
		#endif

		http_receive_handle_select_failed_auth (http_receive);
	}

	return authenticated;

}

// authenticates the request with the found route's method
// a failed authentication is answered here
// returns true if the request can be handled by the route
static bool http_receive_handle_authenticate (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	switch (found->auth_type) {
		// no authentication, handle the request directly
		case HTTP_ROUTE_AUTH_TYPE_NONE: {
			authenticated = true;
		} break;

		// a bearer JWT is expected in the authorization header
		case HTTP_ROUTE_AUTH_TYPE_BEARER: {
			authenticated = http_receive_handle_select_auth_bearer (
				http_cerver, http_receive, found, request
			);
		} break;

		// a custom method is used to handle authentication
		case HTTP_ROUTE_AUTH_TYPE_CUSTOM: {
			authenticated = http_receive_handle_select_auth_custom (
				http_receive, found, request
			);
		} break;

		default: {
			http_receive_handle_select_failed_auth (http_receive);
		} break;
	}

	return authenticated;

}

// matches the request's path with the routes tree, with the trailing slash policy
// redirect is set when the path only matches without its trailing slash
static bool http_receive_select_node (
	const HttpTree *tree, const HttpRequest *request,
	HttpRequestParam *params, HttpRouteMatch *match, bool *redirect
) {

	const char *path = request->url->str;
	const size_t path_len = request->url->len;
	const bool trailing = (path_len > 1) && (path[path_len - 1] == HTTP_TREE_PATH_SEPARATOR);

	bool found = false;

	switch (tree->trailing_slash) {
		case HTTP_TRAILING_SLASH_TOLERANT:
			found = http_tree_match (tree, path, trailing ? path_len - 1 : path_len, params, match);
			break;

		case HTTP_TRAILING_SLASH_REDIRECT:
			found = http_tree_match (tree, path, path_len, params, match);
			*redirect = !found && trailing && http_tree_match (tree, path, path_len - 1, params, match);
			found = found || *redirect;
			break;

		default:
			found = http_tree_match (tree, path, path_len, params, match);
			break;
	}

	return found;

}

// selects the endpoint of the request's method in the node that matched its path
// a HEAD request uses the GET endpoint, an OPTIONS request without endpoint is answered by the cerver
static void http_receive_select_method (
	HttpReceive *http_receive, const HttpRequest *request
) {

	const HttpRouteNode *node = http_receive->node;
	const RequestMethod method = request->method;

	HttpEndpoint *endpoint = http_request_method_is_valid (method) ? node->endpoints[method] : NULL;
	if (!endpoint && (method == REQUEST_METHOD_HEAD) && node->endpoints[REQUEST_METHOD_GET]) {
		endpoint = node->endpoints[REQUEST_METHOD_GET];
		http_receive->head = true;
	}

	http_receive->endpoint = endpoint;
	http_receive->route = endpoint ? endpoint->route : NULL;

	if (endpoint) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_ENDPOINT;
	else if (method == REQUEST_METHOD_OPTIONS) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_OPTIONS;
	else if (http_receive->http_cerver->tree->method_not_allowed) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED;
	else http_receive->dispatch = HTTP_RECEIVE_DISPATCH_CATCH_ALL;

}

// selects how the request will be answered with the routes tree
static void http_receive_select (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;
	const HttpTree *tree = http_receive->http_cerver->tree;

	http_receive->dispatch = HTTP_RECEIVE_DISPATCH_CATCH_ALL;

	Arena *arena = http_request_get_arena (request);
	HttpRequestParam *params = arena ? (HttpRequestParam *) arena_alloc (
		arena, tree->max_params * sizeof (HttpRequestParam)
	) : NULL;

	bool redirect = false;
	HttpRouteMatch match = { NULL, 0 };
	if (params && http_receive_select_node (tree, request, params, &match, &redirect)) {
		http_receive->node = match.node;

		if (redirect) {
			http_receive->dispatch = HTTP_RECEIVE_DISPATCH_REDIRECT;
		}

		else {
			http_receive_select_method (http_receive, request);

			request->params = params;
			request->n_params = match.n_params;
			request->params_names = http_receive->endpoint ? http_receive->endpoint->params_names : NULL;
		}
	}

}

// a GET (or HEAD) with a file extension that no endpoint handles is served from the static paths
static bool http_receive_is_static_file (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	return (http_receive->dispatch != HTTP_RECEIVE_DISPATCH_ENDPOINT)
		&& ((request->method == REQUEST_METHOD_GET) || (request->method == REQUEST_METHOD_HEAD))
		&& http_receive->http_cerver->static_paths->size
		&& strrchr (request->url->str, static_file_extension_separator);

}

// the directory of the request's uploads: it asks the dirname generator for its name
// returns true if the request has a directory, its path is written in dirname
static bool http_receive_uploads_directory_path (
	HttpReceive *http_receive, char *dirname
) {

	bool has_directory = false;

	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	if (http_cerver->uploads_dirname_generator && (http_cerver->uploads_path_len > 0)) {
		http_cerver->uploads_dirname_generator (http_receive, request);

		// a name that did not fit is not used
		if (request->dirname_len >= REQUEST_DIRNAME_SIZE) {
			request->dirname_len = 0;
			request->dirname[0] = '\0';
		}

		if (request->dirname_len > 0) {
			const int written = snprintf (
				dirname, HTTP_MULTI_PART_DIRNAME_SIZE,
				"%s/%s", http_cerver->uploads_path, request->dirname
			);

			has_directory = (written > 0) && (written < HTTP_MULTI_PART_DIRNAME_SIZE);
		}
	}

	return has_directory;

}

// creates the directory of the request's uploads if it does not exist yet,
// one that the request creates is removed at the end if it is empty
// returns 0 on success, 1 if it could not be created (the request is answered with 500)
static int http_receive_uploads_directory_create (
	HttpReceive *http_receive, const char *dirname
) {

	int retval = 0;

	struct stat status = CERVER_ZERO_INITIALIZER;
	if (stat (dirname, &status)) {
		if (files_create_recursive_dir (dirname, http_receive->http_cerver->uploads_dir_mode)) {
			const int error = errno;

			char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
			cerver_log_error (
				"Failed to create the uploads directory %s - errno %d (%s)",
				dirname, error, strerror_r (error, error_description, sizeof (error_description))
			);

			retval = http_receive_reject (
				http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
			);
		}

		else {
			http_receive->upload_directory_created = true;
		}
	}

	return retval;

}

// returns 0 on success, 1 if the request was rejected
int http_receive_init_mpart_parser (
	HttpReceive *http_receive,
	const char *boundary
) {

	char dirname[HTTP_MULTI_PART_DIRNAME_SIZE] = { 0 };

	http_receive->mpart_settings.on_header_field = http_receive_handle_mpart_header_field;
	http_receive->mpart_settings.on_header_value = http_receive_handle_mpart_header_value;
	http_receive->mpart_settings.on_part_data = http_receive_handle_mpart_data;

	http_receive->mpart_settings.on_part_data_begin = http_receive_handle_mpart_part_data_begin;
	http_receive->mpart_settings.on_headers_complete = http_receive_handle_mpart_headers_completed;
	http_receive->mpart_settings.on_part_data_end = http_receive_handle_mpart_data_end;
	http_receive->mpart_settings.on_body_end = NULL;

	http_receive->mpart_parser = multipart_parser_init (boundary, &http_receive->mpart_settings);
	http_receive->request->multi_parts = dlist_init (http_multi_parts_return, NULL);

	int retval = (http_receive->mpart_parser && http_receive->request->multi_parts) ? 0 : 1;

	if (!retval) {
		http_receive->mpart_parser->data = http_receive;

		if (http_receive_uploads_directory_path (http_receive, dirname)) {
			retval = http_receive_uploads_directory_create (http_receive, dirname);
		}
	}

	return retval;

}

// answers "100 Continue" to a client that waits for it before sending the body
// returns 0 on success, 1 if the client is gone
static int http_receive_handle_expect_continue (HttpReceive *http_receive) {

	int retval = 0;

	const String *expect = http_receive->request->headers[HTTP_HEADER_EXPECT];
	if (expect && !strcasecmp (expect->str, expect_continue_value)) {
		const ssize_t sent = send (
			http_receive->cr->connection->socket->sock_fd,
			continue_response, continue_response_len, MSG_NOSIGNAL
		);

		if (sent != (ssize_t) continue_response_len) {
			http_receive_client_left (http_receive, (sent < 0) ? errno : EPIPE);
			retval = 1;
		}
	}

	return retval;

}

// prepares the multi-part parser of an authenticated request to a route
// returns 0 on success, 1 if the request was rejected
static int http_receive_handle_headers_multi_part (HttpReceive *http_receive) {

	int retval = 0;

	const String *content_type = http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE];
	if (content_type && is_multipart (content_type->str)) {
		http_receive->is_multi_part = true;

		char boundary[HTTP_MULTI_PART_BOUNDARY_MAX_SIZE] = { 0 };
		if (!http_mpart_get_boundary (
			boundary, content_type->str, (unsigned int) content_type->len
		)) {
			http_receive->settings.on_body = http_receive_handle_mpart_body;

			if (http_receive_init_mpart_parser (http_receive, boundary)) {
				retval = http_receive->responded ? 1 : http_receive_reject (
					http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
				);
			}
		}

		// a multi-part request without a boundary can not be parsed
		else {
			retval = http_receive_reject (
				http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
			);
		}
	}

	return retval;

}

// a request without a route is answered by the catch all handler,
// so its body is only checked against the limit, but it is not saved
static int http_receive_handle_body_discard (
	llhttp_t *parser, const char *at, size_t length
) {

	(void) at;

	return http_receive_parser_result (
		http_receive_check_body_size ((HttpReceive *) parser->data, length)
	);

}

// the request's id and the client's address,
// they are resolved with the headers, so the rejected requests also have them
static void http_receive_identify (HttpReceive *http_receive) {

	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	if (http_cerver->request_id) http_request_resolve_id (request);

	http_proxy_resolve_client_ip (
		http_cerver,
		http_receive->cr->connection ? http_receive->cr->connection->ip : NULL,
		request, request->client_ip
	);

}

// a body bigger than the limit is rejected before receiving it
// (the parser only has a content length when the request has a Content-Length)
static bool http_receive_body_too_large (
	const HttpReceive *http_receive, const llhttp_t *parser
) {

	return http_receive->max_body_size
		&& (parser->flags & F_CONTENT_LENGTH)
		&& (parser->content_length > http_receive->max_body_size);

}

// a request whose Transfer-Encoding does not end with chunked can not tell the length of its body
// (RFC 9112 section 6.3): the parser rejects it, but with its lenient flags of the transfer encoding
// it reads the body until the connection is closed, a request that cerver never completes nor answers
static bool http_receive_body_has_no_end (const llhttp_t *parser) {

	return (parser->flags & F_TRANSFER_ENCODING) && !(parser->flags & F_CHUNKED);

}

static bool http_receive_handle_static_authenticate (
	HttpReceive *http_receive, HttpRequest *request
);

// the url and the headers are complete (each header was saved with its value):
// the route is selected and the request is authenticated before its body is received,
// so the files of a rejected request are never saved
// HTTP_RECEIVE_PARSER_STOP stops the parser (1 would only skip the body)
static int http_receive_handle_headers_completed (llhttp_t *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_HEADERS;

	// the headers are complete, what follows is the body
	http_receive->stage = HTTP_STAGE_BODY;

	const uint8_t method = llhttp_get_method (parser);
	request->method = (RequestMethod) method;

	http_receive_identify (http_receive);

	if (http_receive_body_has_no_end (parser)) {
		(void) http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);

		return HTTP_RECEIVE_PARSER_STOP;
	}

	// a method that the parser accepts but cerver does not know (RFC 9110 section 9.1)
	if (!http_request_method_is_valid (request->method)) {
		(void) http_receive_reject (
			http_receive, not_implemented_error, HTTP_RECEIVE_STATUS_REJECTED
		);

		return HTTP_RECEIVE_PARSER_STOP;
	}

	#ifdef HTTP_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HTTP,
		"%s %s",
		http_request_method_str (request->method),
		request->url->str
	);
	#endif

	http_receive->type = HTTP_RECEIVE_TYPE_ROUTE;
	http_receive_select (http_receive);

	const HttpEndpoint *endpoint = http_receive->endpoint;

	// an endpoint can have its own body limit
	http_receive->max_body_size = (endpoint && endpoint->resolved.max_body_size) ?
		endpoint->resolved.max_body_size : http_cerver->max_body_size;

	if (http_receive_body_too_large (http_receive, parser)) {
		(void) http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);

		return HTTP_RECEIVE_PARSER_STOP;
	}

	if (endpoint) {
		// the authentication handler can use the request's endpoint
		if (
			!endpoint->resolved.authentication_with_body
			&& !http_receive_handle_authenticate (http_cerver, http_receive, &endpoint->resolved, request)
		) {
			return HTTP_RECEIVE_PARSER_STOP;
		}

		if (http_receive_handle_headers_web_socket (http_receive, parser)) {
			return HTTP_RECEIVE_PARSER_STOP;
		}

		if (http_receive_handle_headers_multi_part (http_receive)) {
			return HTTP_RECEIVE_PARSER_STOP;
		}
	}

	// a request that no endpoint handles never saves its body
	else {
		if (http_receive_is_static_file (http_receive, request)) {
			http_receive->type = HTTP_RECEIVE_TYPE_FILE;
			http_receive->head = (request->method == REQUEST_METHOD_HEAD);

			if (!http_receive_handle_static_authenticate (http_receive, request)) return HTTP_RECEIVE_PARSER_STOP;
		}

		http_receive->settings.on_body = http_receive_handle_body_discard;
	}

	// a client that is gone stops the parser, without a response
	return http_receive_parser_result (http_receive_handle_expect_continue (http_receive));

}

// sets in real_filename (PATH_MAX bytes) the real path of the url inside the static path
// returns false if the path does not fit or if it is outside the static path ("..", symbolic links)
static bool http_static_path_resolve (
	const HttpStaticPath *static_path, const char *url, char *real_filename
) {

	char filename[HTTP_CERVER_STATIC_FILE_SIZE] = { 0 };
	char real_static_path[PATH_MAX] = { 0 };

	// a path that does not fit is never truncated
	const int written = snprintf (
		filename, HTTP_CERVER_STATIC_FILE_SIZE,
		"%s%s", static_path->path->str, url
	);

	if ((written < 0) || (written >= HTTP_CERVER_STATIC_FILE_SIZE)) return false;

	if (!realpath (filename, real_filename)) return false;
	if (!realpath (static_path->path->str, real_static_path)) return false;

	const size_t real_static_path_len = strlen (real_static_path);

	return !strncmp (real_filename, real_static_path, real_static_path_len)
		&& (real_filename[real_static_path_len] == '/');

}

// the first static path that has the file of the url, with its real path and its status,
// only regular files are served
// returns NULL if none has it
static const HttpStaticPath *http_static_path_find (
	const HttpCerver *http_cerver, const char *url,
	char *real_filename, struct stat *filestatus
) {

	const HttpStaticPath *found = NULL;

	for (ListElement *le = dlist_start (http_cerver->static_paths); le && !found; le = le->next) {
		const HttpStaticPath *static_path = (const HttpStaticPath *) le->data;

		(void) memset (filestatus, 0, sizeof (struct stat));
		if (
			http_static_path_resolve (static_path, url, real_filename)
			&& !stat (real_filename, filestatus) && S_ISREG (filestatus->st_mode)
		) {
			found = static_path;
		}
	}

	return found;

}

// the first static path that has authentication, NULL if every one is public
static const HttpStaticPath *http_static_path_first_protected (const HttpCerver *http_cerver) {

	const HttpStaticPath *found = NULL;

	for (ListElement *le = dlist_start (http_cerver->static_paths); le && !found; le = le->next) {
		const HttpStaticPath *static_path = (const HttpStaticPath *) le->data;
		if (static_path->auth_type != HTTP_ROUTE_AUTH_TYPE_NONE) found = static_path;
	}

	return found;

}

// the request of a file is authenticated with the static path that has it,
// or with the first protected one when none has it,
// so a file that does not exist is answered like a protected one
// returns true if the request continues, a failed authentication was already answered
static bool http_receive_handle_static_authenticate (
	HttpReceive *http_receive, HttpRequest *request
) {

	bool authenticated = true;

	char real_filename[PATH_MAX] = { 0 };
	struct stat filestatus = CERVER_ZERO_INITIALIZER;

	const HttpStaticPath *static_path = http_static_path_find (
		http_receive->http_cerver, request->url->str, real_filename, &filestatus
	);

	if (!static_path) static_path = http_static_path_first_protected (http_receive->http_cerver);

	if (static_path && (static_path->auth_type != HTTP_ROUTE_AUTH_TYPE_NONE)) {
		HttpRouteSettings settings;
		(void) memset (&settings, 0, sizeof (HttpRouteSettings));
		settings.auth_type = static_path->auth_type;
		settings.decode_data = static_path->decode_data;
		settings.delete_decoded_data = static_path->delete_decoded_data;
		settings.authentication_handler = static_path->authentication_handler;

		authenticated = http_receive_handle_authenticate (
			http_receive->http_cerver, http_receive, &settings, request
		);
	}

	return authenticated;

}

// the request was authenticated in on_headers_complete with its static path
static void http_receive_handle_serve_file (HttpReceive *http_receive) {

	char real_filename[PATH_MAX] = { 0 };
	struct stat filestatus = CERVER_ZERO_INITIALIZER;

	const bool found = http_static_path_find (
		http_receive->http_cerver, http_receive->request->url->str, real_filename, &filestatus
	) != NULL;

	if (found) {
		http_receive->served_file = http_receive->request->url->str;

		(void) http_response_send_file_internal (
			http_receive, HTTP_STATUS_OK,
			real_filename, &filestatus
		);
	}

	else {
		#ifdef HTTP_RECEIVE_DEBUG
		cerver_log_warning (
			"Unable to find file %s",
			http_receive->request->url->str
		);
		#endif

		http_receive_handle_catch_all (
			http_receive->http_cerver,
			http_receive, http_receive->request
		);
	}

}

// an endpoint whose authentication needs the body is authenticated now,
// any other endpoint was authenticated in on_headers_complete
static void http_receive_handle_route (HttpReceive *http_receive) {

	const HttpEndpoint *endpoint = http_receive->endpoint;

	if (
		endpoint->resolved.authentication_with_body
		&& !http_receive_handle_authenticate (
			http_receive->http_cerver, http_receive, &endpoint->resolved, http_receive->request
		)
	) {
		return;
	}

	http_receive_handle_match (http_receive, http_receive->request, endpoint);

}

// adds the CORS headers of a preflight request from a whitelisted origin
static void http_receive_handle_preflight (
	const HttpReceive *http_receive, HttpResponse *response
) {

	const HttpRequest *request = http_receive->request;

	if (
		request->headers[HTTP_HEADER_ACCESS_CONTROL_REQUEST_METHOD]
		&& !http_response_add_whitelist_cors_header_from_request (http_receive, response)
	) {
		(void) http_response_add_cors_allow_methods_header (response, http_receive->node->allow);

		const String *request_headers = request->headers[HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS];
		if (request_headers) {
			(void) http_response_add_header (
				response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_HEADERS, request_headers->str
			);
		}
	}

}

// a path without an OPTIONS handler is answered with its Allow header
// and, when its endpoints enabled CORS, with the preflight headers
static void http_receive_handle_options (HttpReceive *http_receive) {

	HttpResponse *response = http_response_create (HTTP_STATUS_NO_CONTENT, NULL, 0);
	if (response) {
		(void) http_response_add_header (response, HTTP_HEADER_ALLOW, http_receive->node->allow);

		if (http_receive->node->cors) http_receive_handle_preflight (http_receive, response);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// a path without a handler for the request's method,
// answered with the custom handler or with the default 405 response
static void http_receive_handle_method_not_allowed (HttpReceive *http_receive) {

	const HttpHandler handler = http_receive->http_cerver->tree->method_not_allowed_handler;
	if (handler) {
		handler (http_receive, http_receive->request);
		return;
	}

	HttpResponse *response = http_response_create (
		HTTP_STATUS_METHOD_NOT_ALLOWED,
		method_not_allowed_error->data, method_not_allowed_error->data_len
	);

	if (response) {
		(void) http_response_add_header (response, HTTP_HEADER_ALLOW, http_receive->node->allow);
		(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (response, response->data_len);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// a path with a trailing slash is redirected with 308 (that keeps the method and the body)
static void http_receive_handle_redirect (HttpReceive *http_receive) {

	const HttpRequest *request = http_receive->request;

	HttpResponse *response = http_response_create (HTTP_STATUS_PERMANENT_REDIRECT, NULL, 0);
	if (response) {
		(void) http_response_add_custom_header (
			response, HTTP_HEADER_LOCATION, "%.*s%s%s",
			(int) (request->url->len - 1), request->url->str,
			request->query ? query_separator : "",
			request->query ? request->query->str : ""
		);

		(void) http_response_add_content_length_header (response, 0);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// answers a request that matched a path of the routes tree, or the catch all handler
static void http_receive_handle_dispatch (HttpReceive *http_receive) {

	switch (http_receive->dispatch) {
		case HTTP_RECEIVE_DISPATCH_ENDPOINT:
			http_receive_handle_route (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_OPTIONS:
			http_receive_handle_options (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED:
			http_receive_handle_method_not_allowed (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_REDIRECT:
			http_receive_handle_redirect (http_receive);
			break;

		default:
			http_receive_handle_catch_all (
				http_receive->http_cerver,
				http_receive, http_receive->request
			);
			break;
	}

}

static const char *http_receive_url (const HttpReceive *http_receive) {

	const HttpRequest *request = http_receive->request;

	return (request && request->url) ? request->url->str : "";

}

// a handler that returned without sending a response is answered with 500,
// instead of closing the connection without one (a proxy answers it with 502)
static void http_receive_handle_unanswered (HttpReceive *http_receive) {

	if (!http_receive->responded) {
		cerver_log_with_date (
			LOG_TYPE_WARNING, LOG_TYPE_HTTP,
			"http_receive_handle () - the request was not answered - fd %d - %s %s",
			http_receive->cr->connection->socket->sock_fd,
			http_request_method_str (http_receive->request->method),
			http_receive_url (http_receive)
		);

		(void) http_response_send (server_error, http_receive);

		CERVER_STATS_ADD (http_receive->http_cerver->n_unanswered_requests, 1);
	}

}

// the endpoint and its authentication were resolved in on_headers_complete,
// now that the body is complete the request is handled
static int http_receive_handle_message_completed (llhttp_t *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_COMPLETED;
	http_receive->stage = HTTP_STAGE_HANDLER;

	// what the client asked (HTTP/1.1 without close, HTTP/1.0 with keep-alive),
	// the connection is still closed after the response
	http_receive->keep_alive_requested = llhttp_should_keep_alive (parser);

	switch (http_receive->type) {
		case HTTP_RECEIVE_TYPE_FILE:
			http_receive_handle_serve_file (http_receive);
			break;

		default:
			http_receive_handle_dispatch (http_receive);
			break;
	}

	http_receive_handle_unanswered (http_receive);

	// the client left while its response was sent
	if (http_receive->response_failed) {
		http_receive->receive_status = HTTP_RECEIVE_STATUS_RESPONSE_FAILED;
	}

	// only one request is handled by connection,
	// the parser pauses here, so a pipelined request in the same buffer is never parsed
	// (a callback can not call llhttp_pause (), it returns HPE_PAUSED)
	return HPE_PAUSED;

}

// reads the data that the client already sent, up to HTTP_RECEIVE_DRAIN_SIZE bytes
// waiting tells if recv () waits for more data, with the socket's timeout
static void http_receive_drain (const int sock_fd, const bool waiting) {

	char buffer[HTTP_RECEIVE_DRAIN_BUFFER_SIZE];
	const int flags = waiting ? 0 : MSG_DONTWAIT;

	size_t drained = 0;
	while (drained < HTTP_RECEIVE_DRAIN_SIZE) {
		const ssize_t received = recv (sock_fd, buffer, sizeof (buffer), flags);
		if (received <= 0) break;

		drained += (size_t) received;
	}

}

// reads, without waiting, the data that the client already sent
// up to HTTP_RECEIVE_DRAIN_SIZE bytes
// returns 0 on success, 1 on error
u8 http_receive_drain_available (const int sock_fd) {

	u8 retval = 1;

	if (sock_fd > -1) {
		http_receive_drain (sock_fd, false);
		retval = 0;
	}

	return retval;

}

// closes the connection after its response was sent
// the data that the client already sent is read first,
// because a close () with unread data answers with a RST that can destroy the response
// a rejected request waits a bit for the rest of its body
void http_receive_close (
	HttpReceive *http_receive, const bool rejected
) {

	Connection *connection = http_receive->cr->connection;
	const int sock_fd = connection->socket->sock_fd;

	if (!http_receive->keep_alive && (sock_fd > -1)) {
		(void) shutdown (sock_fd, SHUT_WR);

		if (rejected) {
			(void) sock_set_timeout (sock_fd, HTTP_RECEIVE_DRAIN_TIMEOUT);
		}

		http_receive_drain (sock_fd, rejected);

		connection_end (connection);
	}

}

// a request that the parser could not parse is answered with 400,
// unless a callback already answered it (a limit, a failed authentication, ...)
// or it stopped the parser because the client is gone
static void http_receive_handle_parser_error (
	HttpReceive *http_receive, const llhttp_errno_t parser_errno,
	const size_t n_parsed, const ssize_t received
) {

	if (!http_receive->responded && (http_receive->receive_status != HTTP_RECEIVE_STATUS_CLOSED)) {
		const char *reason = llhttp_get_error_reason (&http_receive->parser);

		cerver_log_with_date (
			LOG_TYPE_ERROR, LOG_TYPE_HTTP,
			"http_receive_handle () - llhttp_execute () failed - n parsed %zu / received %zd"
			" - fd %d - %s: %s - status %s",
			n_parsed, received,
			http_receive->cr->connection->socket->sock_fd,
			llhttp_errno_name (parser_errno),
			reason ? reason : http_receive_parser_no_reason,
			http_receive_status_str (http_receive->receive_status)
		);

		(void) http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	http_receive_close (http_receive, true);

}

// returns true while the request's headers have not been completed
// (HTTP_RECEIVE_STATUS_HEADERS is set when they are complete)
bool http_receive_is_receiving_headers (const HttpReceive *http_receive) {

	return (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE);

}

// a request that did not arrive in time is answered with 408
// if the client did not send anything, the connection is only closed
void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
) {

	if (received && !http_receive->responded) {
		(void) pthread_mutex_lock (http_receive->http_cerver->mutex);
		http_receive->http_cerver->n_timeout_requests += 1;
		(void) pthread_mutex_unlock (http_receive->http_cerver->mutex);

		(void) http_receive_reject (
			http_receive, request_timeout_error, HTTP_RECEIVE_STATUS_TIMEOUT
		);
	}

	else if (!http_receive->responded) {
		http_receive->receive_status = HTTP_RECEIVE_STATUS_TIMEOUT;
	}

	http_receive_close (http_receive, false);

}

// the bytes of the buffer that the parser consumed,
// after an error or a pause llhttp tells where it stopped
static size_t http_receive_parsed_bytes (
	const HttpReceive *http_receive, const llhttp_errno_t parser_errno,
	const char *buffer, const size_t length
) {

	size_t parsed = length;

	const char *position = llhttp_get_error_pos (&http_receive->parser);
	if (
		(parser_errno != HPE_OK) && position
		&& (position >= buffer) && (position <= (buffer + length))
	) {
		parsed = (size_t) (position - buffer);
	}

	return parsed;

}

void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
) {

	// the first bytes start the request line
	if (http_receive->stage == HTTP_STAGE_QUEUED) {
		http_receive->stage = HTTP_STAGE_REQUEST_LINE;
	}

	const llhttp_errno_t parser_errno = llhttp_execute (
		&http_receive->parser, packet_buffer, (size_t) rc
	);

	const size_t n_parsed = http_receive_parsed_bytes (
		http_receive, parser_errno, packet_buffer, (size_t) rc
	);

	switch (parser_errno) {
		// all the data was parsed, the request is not complete yet
		case HPE_OK: break;

		// the request was completed and handled,
		// the bytes after it (a pipelined request) were not parsed
		case HPE_PAUSED:
		case HPE_PAUSED_UPGRADE:
			http_receive->unparsed = (size_t) rc - n_parsed;

			// the connection belongs to its WebSocket now, with the bytes that followed the request
			if (http_receive->upgraded) http_receive_web_socket_pending (http_receive, packet_buffer + n_parsed);
			else http_receive_close (http_receive, false);
			break;

		default:
			http_receive_handle_parser_error (http_receive, parser_errno, n_parsed, rc);
			break;
	}

}

#pragma endregion
