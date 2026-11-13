#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <strings.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/cerver.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/version.h"

#include "cerver/http/compression.h"
#include "cerver/http/conditional.h"
#include "cerver/http/content.h"
#include "cerver/http/date.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/origin.h"
#include "cerver/http/recycle.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/status.h"
#include "cerver/http/stream.h"
#include "cerver/http/tree.h"
#include "cerver/http/utils.h"

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "cerver/utils/utils.h"

// the separators ", " and ": " of the json bodies, as they were written before
#define HTTP_RESPONSE_JSON_DUMP_FLAGS		0

typedef struct HttpResponseProducer {

	Pool *pool;
	const HttpCerver *http_cerver;

} HttpResponseProducer;

static HttpResponseProducer *producer = NULL;

static HttpResponseProducer *http_response_producer_new (
	const HttpCerver *http_cerver
) {

	HttpResponseProducer *producer = (HttpResponseProducer *) malloc (
		sizeof (HttpResponseProducer)
	);

	if (producer) {
		producer->pool = NULL;
		producer->http_cerver = http_cerver;
	}

	return producer;

}

static void http_response_producer_delete (
	HttpResponseProducer *producer
) {

	if (producer) {
		pool_delete (producer->pool);
		producer->pool = NULL;

		producer->http_cerver = NULL;

		free (producer);
	}

}

// the cerver can change when its own one ends while another HTTP cerver is alive,
// so each function loads it once
static const HttpCerver *http_response_producer_cerver (void) {

	return __atomic_load_n (&producer->http_cerver, __ATOMIC_ACQUIRE);

}

// the headers that the cerver adds to every response that does not have them,
// NULL when no HTTP cerver has been created
static const HttpHeaders *http_response_default_headers (void) {

	const HttpCerver *http_cerver = producer ? http_response_producer_cerver () : NULL;

	return http_cerver ? &http_cerver->response_headers : NULL;

}

#pragma region responses

// the header of the response was allocated, it is not its buffer
static bool http_response_header_is_allocated (const HttpResponse *response) {

	return response->header && (response->header != (void *) response->header_buffer);

}

static void http_response_header_release (HttpResponse *response) {

	if (http_response_header_is_allocated (response)) free (response->header);

	response->header = NULL;
	response->header_len = 0;
	response->header_is_raw = false;

}

static void http_response_init (HttpResponse *response) {

	response->status = HTTP_STATUS_OK;

	http_headers_init (&response->headers);

	response->header = NULL;
	response->header_len = 0;
	response->header_is_raw = false;
	response->header_buffer[0] = '\0';

	response->data = NULL;
	response->data_len = 0;
	response->data_ref = false;

	response->res = NULL;
	response->res_len = 0;

	response->recycle_next = NULL;

}

void *http_response_new (void) {

	HttpResponse *response = http_recycle_response_take ();

	if (!response) response = (HttpResponse *) malloc (sizeof (HttpResponse));

	if (response) http_response_init (response);

	return response;

}

void http_response_reset (HttpResponse *response) {

	response->status = HTTP_STATUS_OK;

	http_headers_reset (&response->headers);

	http_response_header_release (response);

	if (!response->data_ref) {
		if (response->data) free (response->data);
	}

	response->data = NULL;
	response->data_len = 0;
	response->data_ref = false;

	response->res_len = 0;
	if (response->res) {
		free (response->res);
		response->res = NULL;
	}

}

// the response is reset and kept to be used again
void http_response_delete (void *res_ptr) {

	if (res_ptr) {
		HttpResponse *response = (HttpResponse *) res_ptr;

		http_response_reset (response);

		http_recycle_response_give_back (response);
	}

}

// get a new HTTP response ready to be used
HttpResponse *http_response_get (void) {

	return (HttpResponse *) pool_pop (producer->pool);

}

// correctly disposes a HTTP response
void http_response_return (HttpResponse *response) {

	if (response) {
		http_response_reset (response);

		(void) pool_push (producer->pool, response);
	}

}

static void http_response_headers_changed (HttpResponse *response);

// sets the HTTP response's status code to be used in the header
void http_response_set_status (
	HttpResponse *response, const http_status status
) {

	if (response && (response->status != status)) {
		response->status = status;

		// the status is part of the header that was compiled
		http_response_headers_changed (response);
	}

}

// sets the response's header as the bytes that are sent, it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_header (
	HttpResponse *response, const void *header, const size_t header_len
) {

	if (response) {
		http_response_header_release (response);

		if (header) {
			response->header = (void *) header;
			response->header_len = header_len;
			response->header_is_raw = true;
		}
	}

}

// the header that was compiled does not have the change, so it is compiled again
// when the response is sent, a header that was set as bytes is kept
static void http_response_headers_changed (HttpResponse *response) {

	if (!response->header_is_raw) {
		http_response_header_release (response);

		// the compiled response starts with the header that changed
		if (response->res) {
			free (response->res);
			response->res = NULL;
			response->res_len = 0;
		}
	}

}

u8 http_response_header_set (
	HttpResponse *response, const http_header type, const char *value
) {

	u8 retval = 1;

	if (response && value) {
		retval = http_headers_set (&response->headers, type, value, strlen (value));

		if (!retval) http_response_headers_changed (response);
	}

	return retval;

}

u8 http_response_header_add (
	HttpResponse *response, const http_header type, const char *value
) {

	u8 retval = 1;

	if (response && value) {
		retval = http_headers_add (&response->headers, type, value, strlen (value));

		if (!retval) http_response_headers_changed (response);
	}

	return retval;

}

unsigned int http_response_header_remove (
	HttpResponse *response, const http_header type
) {

	unsigned int removed = 0;

	if (response) {
		removed = http_headers_remove (&response->headers, type);

		if (removed) http_response_headers_changed (response);
	}

	return removed;

}

unsigned int http_response_header_count (
	const HttpResponse *response, const http_header type
) {

	return response ? http_headers_count (&response->headers, type) : 0;

}

static const char *http_response_header_field_value (
	const HttpResponse *response, const HttpHeaderField *field, size_t *value_length
) {

	if (!field && value_length) *value_length = 0;

	return field ? http_headers_field_value (&response->headers, field, value_length) : NULL;

}

const char *http_response_header_get (
	const HttpResponse *response, const http_header type,
	const unsigned int position, size_t *value_length
) {

	return http_response_header_field_value (
		response,
		response ? http_headers_find (&response->headers, type, position) : NULL,
		value_length
	);

}

u8 http_response_header_set_by_name (
	HttpResponse *response, const char *name, const char *value
) {

	u8 retval = 1;

	if (response && name && value) {
		retval = http_headers_set_by_name (
			&response->headers, name, strlen (name), value, strlen (value)
		);

		if (!retval) http_response_headers_changed (response);
	}

	return retval;

}

u8 http_response_header_add_by_name (
	HttpResponse *response, const char *name, const char *value
) {

	u8 retval = 1;

	if (response && name && value) {
		retval = http_headers_add_by_name (
			&response->headers, name, strlen (name), value, strlen (value)
		);

		if (!retval) http_response_headers_changed (response);
	}

	return retval;

}

unsigned int http_response_header_remove_by_name (
	HttpResponse *response, const char *name
) {

	unsigned int removed = 0;

	if (response && name) {
		removed = http_headers_remove_by_name (&response->headers, name, strlen (name));

		if (removed) http_response_headers_changed (response);
	}

	return removed;

}

unsigned int http_response_header_count_by_name (
	const HttpResponse *response, const char *name
) {

	return (response && name) ?
		http_headers_count_by_name (&response->headers, name, strlen (name)) : 0;

}

const char *http_response_header_get_by_name (
	const HttpResponse *response, const char *name,
	const unsigned int position, size_t *value_length
) {

	return http_response_header_field_value (
		response,
		(response && name) ? http_headers_find_by_name (
			&response->headers, name, strlen (name), position
		) : NULL,
		value_length
	);

}

// sets a header of the response, like http_response_header_set ()
// returns 0 on success, 1 on error
u8 http_response_add_header (
	HttpResponse *response,
	const http_header type, const char *actual_header
) {

	return http_response_header_set (response, type, actual_header);

}

// works like http_response_add_header ()
// but generates the header values in the fly
u8 http_response_add_custom_header (
	HttpResponse *response,
	const http_header type, const char *format, ...
) {

	u8 retval = 1;

	if (response && format) {
		va_list args;
		va_start (args, format);

		char value[HTTP_RESPONSE_HEADER_FORMAT_SIZE];
		const int written = vsnprintf (value, HTTP_RESPONSE_HEADER_FORMAT_SIZE, format, args);

		va_end (args);

		// a value that does not fit is not added cut
		if ((written >= 0) && (written < HTTP_RESPONSE_HEADER_FORMAT_SIZE)) {
			retval = http_response_header_set (response, type, value);
		}
	}

	return retval;

}

// adds a "Content-Type" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_type_header (
	HttpResponse *response, const ContentType type
) {

	return http_response_header_set (
		response, HTTP_HEADER_CONTENT_TYPE, http_content_type_mime (type)
	);

}

// the length of a body as the value of its header
// returns the length of the value
static size_t http_response_content_length_value (
	const size_t length, char *value
) {

	const int written = snprintf (value, HTTP_RESPONSE_CONTENT_LENGTH_SIZE, "%zu", length);

	return (written > 0) ? (size_t) written : 0;

}

// adds a "Content-Length" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_length_header (
	HttpResponse *response, const size_t length
) {

	u8 retval = 1;

	if (response) {
		char value[HTTP_RESPONSE_CONTENT_LENGTH_SIZE];
		(void) http_response_content_length_value (length, value);

		retval = http_response_header_set (response, HTTP_HEADER_CONTENT_LENGTH, value);
	}

	return retval;

}

// adds a "Content-Type" with value "application/json"
// adds a "Content-Length" header to the response
void http_response_add_json_headers (
	HttpResponse *response, const size_t json_len
) {

	(void) http_response_add_content_type_header (
		response, HTTP_CONTENT_TYPE_JSON
	);

	(void) http_response_add_content_length_header (
		response, json_len
	);

}

// adds an "Access-Control-Allow-Origin" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_cors_header (
	HttpResponse *response, const char *origin
) {

	return http_response_add_header (
		response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, origin
	);

}

// works like http_response_add_cors_header ()
// but takes a HttpOrigin instead of a c string
u8 http_response_add_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
) {

	return http_response_add_header (
		response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, origin->value
	);

}

// works like http_response_add_cors_header () but first
// checks if the domain matches any entry in the whitelist
// returns 0 on success, 1 on error
u8 http_response_add_whitelist_cors_header (
	HttpResponse *response, const char *domain
) {

	u8 retval = 1;

	const HttpCerver *http_cerver = http_response_producer_cerver ();

	for (u8 idx = 0; idx < http_cerver->n_origins; idx++) {
		if (!strcmp (
			http_cerver->origins_whitelist[idx].value,
			domain
		)) {
			retval = http_response_add_header (
				response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, domain
			);

			break;
		}
	}

	return retval;

}

// works like http_response_add_whitelist_cors_header ()
// but takes a HttpOrigin instead of a c string
u8 http_response_add_whitelist_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
) {

	return http_response_add_whitelist_cors_header (
		response, origin->value
	);

}

// checks if the HTTP request's "Origin" header value
// matches any domain in the whitelist
// then adds an "Access-Control-Allow-Origin" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_whitelist_cors_header_from_request (
	const HttpReceive *http_receive,
	HttpResponse *response
) {

	u8 retval = 1;

	if (http_receive->request->headers[HTTP_HEADER_ORIGIN]) {
		retval = http_response_add_whitelist_cors_header (
			response, http_receive->request->headers[
				HTTP_HEADER_ORIGIN
			]->str
		);
	}

	return retval;

}

// sets CORS related header "Access-Control-Allow-Credentials"
// this header is needed when a CORS request has an "Authorization" header
// returns 0 on success, 1 on error
u8 http_response_add_cors_allow_credentials_header (
	HttpResponse *response
) {

	return http_response_add_header (
		response,
		HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS,
		"true"
	);

}

// sets CORS related header "Access-Control-Allow-Methods"
// to be a list of available methods like "GET, HEAD, OPTIONS"
// this header is needed in preflight OPTIONS request's responses
// returns 0 on success, 1 on error
u8 http_response_add_cors_allow_methods_header (
	HttpResponse *response, const char *methods
) {

	return http_response_add_header (
		response,
		HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS,
		methods
	);

}

// adds a "Content-Range: bytes ${start}-${end}/${file_size}"
// header to the response
u8 http_response_add_content_range_header (
	HttpResponse *response, const BytesRange *bytes_range
) {

	return http_response_add_custom_header (
		response, HTTP_HEADER_CONTENT_RANGE,
		"bytes %ld-%ld/%ld",
		bytes_range->start, bytes_range->end, bytes_range->file_size
	);

}

// adds an "Accept-Ranges" with value "bytes"
// adds a "Content-Type" with content type value
// adds a "Content-Length" with value of chunk_size
// adds a "Content-Range: bytes ${start}-${end}/${file_size}"
void http_response_add_video_headers (
	HttpResponse *response,
	const ContentType content_type, const BytesRange *bytes_range
) {

	(void) http_response_add_header (
		response, HTTP_HEADER_ACCEPT_RANGES, "bytes"
	);

	(void) http_response_add_content_type_header (
		response, content_type
	);

	(void) http_response_add_content_length_header (
		response, (const size_t) bytes_range->chunk_size
	);

	(void) http_response_add_custom_header (
		response, HTTP_HEADER_CONTENT_RANGE,
		"bytes %ld-%ld/%ld",
		bytes_range->start, bytes_range->end, bytes_range->file_size
	);

}

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_data (
	HttpResponse *response, const void *data, const size_t data_len
) {

	if (response) {
		if (response->data) {
			free (response->data);
			response->data = NULL;
		}

		if (data) {
			response->data = (void *) data;
			response->data_len = data_len;
		}
	}

}

// sets a reference to a data buffer to send
// data will not be copied into the response and will not be freed after use
// this method is similar to packet_set_data_ref ()
// returns 0 on success, 1 on error
u8 http_response_set_data_ref (
	HttpResponse *response, const void *data, const size_t data_size
) {

	u8 retval = 1;

	if (response && data) {
		if (!response->data_ref) {
			if (response->data) free (response->data);
		}

		response->data = (void *) data;
		response->data_len = data_size;
		response->data_ref = true;

		retval = 0;
	}

	return retval;

}

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted
HttpResponse *http_response_create (
	const http_status status, const void *data, size_t data_len
) {

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (response) {
		response->status = status;

		if (data) {
			response->data = malloc (data_len);
			if (response->data) {
				(void) memcpy (response->data, data, data_len);
				response->data_len = data_len;
			}
		}
	}

	return response;

}

static const char http_response_header_end[] = { "\r\n" };
static const size_t http_response_header_end_length = 2;

// the status line and the Server header, the start of the header that HTTP/1.1 sends
// returns the length that was written
static size_t http_response_main_header (
	const http_status status, char *main_header
) {

	const int written = snprintf (
		main_header, HTTP_RESPONSE_MAIN_HEADER_SIZE,
		"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n",
		(unsigned int) status, http_status_string (status),
		CERVER_VERSION
	);

	// a truncated main header is never used beyond its buffer
	return (written < 0) ? 0 : (
		((size_t) written < HTTP_RESPONSE_MAIN_HEADER_SIZE) ?
			(size_t) written : HTTP_RESPONSE_MAIN_HEADER_SIZE - 1
	);

}

// a header of the cerver is added to a response that does not have one with its name
static bool http_response_default_applies (
	const HttpHeaders *headers,
	const HttpHeaders *defaults, const HttpHeaderField *field
) {

	size_t name_length = 0;
	const char *name = http_headers_field_name (defaults, field, &name_length);

	return (field->type != HTTP_HEADER_UNDEFINED) ?
		!http_headers_count (headers, field->type) :
		!http_headers_count_by_name (headers, name, name_length);

}

// the length of the lines of the headers of the cerver that the response does not have
static size_t http_response_defaults_length (
	const HttpHeaders *headers, const HttpHeaders *defaults
) {

	size_t length = 0;

	const HttpHeaderField *field = NULL;
	for (unsigned int idx = 0; (field = http_headers_get (defaults, idx)) != NULL; idx++) {
		if (http_response_default_applies (headers, defaults, field)) {
			size_t line_length = 0;
			(void) http_headers_field_line (defaults, field, &line_length);

			length += line_length;
		}
	}

	return length;

}

// writes the lines of the headers of the cerver that the response does not have
// returns the end of what was written
static char *http_response_defaults_write (
	char *end, const HttpHeaders *headers, const HttpHeaders *defaults
) {

	const HttpHeaderField *field = NULL;
	for (unsigned int idx = 0; (field = http_headers_get (defaults, idx)) != NULL; idx++) {
		if (http_response_default_applies (headers, defaults, field)) {
			size_t line_length = 0;
			const char *line = http_headers_field_line (defaults, field, &line_length);

			(void) memcpy (end, line, line_length);
			end += line_length;
		}
	}

	return end;

}

// writes the header as HTTP/1.1 sends it: the main header, the headers of the response
// in the order they were added, the ones of the cerver that it does not have and the empty line
static void http_response_header_write (
	char *end,
	const char *main_header, const size_t main_length,
	const HttpHeaders *headers, const HttpHeaders *defaults
) {

	(void) memcpy (end, main_header, main_length);
	end += main_length;

	size_t lines_length = 0;
	const char *lines = http_headers_lines (headers, &lines_length);

	(void) memcpy (end, lines, lines_length);
	end += lines_length;

	end = http_response_defaults_write (end, headers, defaults);

	(void) memcpy (end, http_response_header_end, http_response_header_end_length);
	end += http_response_header_end_length;

	*end = '\0';

}

// uses the status and the headers of the response to create its header
// as HTTP/1.1 sends it, in a continuos buffer ready to be sent by the response,
// it is the only place where a header is serialized, in the buffer of the response when it fits
void http_response_compile_header (HttpResponse *response) {

	const HttpHeaders *defaults = http_response_default_headers ();

	char main_header[HTTP_RESPONSE_MAIN_HEADER_SIZE];
	const size_t main_length = http_response_main_header (response->status, main_header);

	const size_t header_len = main_length + response->headers.length
		+ http_response_defaults_length (&response->headers, defaults)
		+ http_response_header_end_length;

	http_response_header_release (response);

	// one more byte to always terminate the header with '\0'
	const size_t header_size = header_len + 1;
	char *header = (header_size <= HTTP_RESPONSE_HEADER_BUFFER_SIZE) ?
		response->header_buffer : (char *) malloc (header_size);

	if (header) {
		http_response_header_write (
			header, main_header, main_length, &response->headers, defaults
		);

		response->header = header;
		response->header_len = header_len;
	}

}

// the type and the length of a json body are headers of the response
static void http_response_json_header (HttpResponse *response) {

	const char *type = http_content_type_mime (HTTP_CONTENT_TYPE_JSON);

	(void) http_headers_set (
		&response->headers, HTTP_HEADER_CONTENT_TYPE, type, strlen (type)
	);

	char length[HTTP_RESPONSE_CONTENT_LENGTH_SIZE];
	const size_t length_length = http_response_content_length_value (response->data_len, length);

	(void) http_headers_set (
		&response->headers, HTTP_HEADER_CONTENT_LENGTH, length, length_length
	);

	http_response_compile_header (response);

}

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
u8 http_response_compile (HttpResponse *response) {

	u8 retval = 1;

	if (response) {
		if (!response->header) {
			http_response_compile_header (response);
		}

		// compile into a continous buffer
		if (response->res) {
			free (response->res);
			response->res = NULL;
			response->res_len = 0;
		}

		response->res_len = response->header_len + response->data_len;
		response->res = malloc (response->res_len);
		if (response->res) {
			char *end = (char *) response->res;
			(void) memcpy (end, response->header, response->header_len);

			if (response->data) {
				end += response->header_len;
				(void) memcpy (end, response->data, response->data_len);
			}

			retval = 0;
		}
	}

	return retval;

}

void http_response_print (const HttpResponse *response) {

	if (response) {
		if (response->res) {
			(void) printf (
				"\n%.*s\n\n",
				(int) response->res_len, (char *) response->res
			);
		}
	}

}

#pragma endregion

#pragma region send

// every response that cerver tries to send marks the request as answered,
// even if the client is already gone, so it is never answered twice
static void http_response_mark_responded (const HttpReceive *http_receive) {

	((HttpReceive *) http_receive)->responded = true;

}

// the milliseconds that all the sends of a response take together:
// the send timeout of the HTTP cerver, or the one of its cerver (secs),
// or CERVER_DEFAULT_SEND_TIMEOUT, so a client that never reads can not keep a thread forever
static u32 http_response_send_timeout (const HttpReceive *http_receive) {

	const Cerver *cerver = http_receive->cr->cerver;

	u32 timeout = (u32) CERVER_DEFAULT_SEND_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND;
	if (http_receive->http_cerver && http_receive->http_cerver->send_timeout) {
		timeout = http_receive->http_cerver->send_timeout;
	}

	else if (cerver && cerver->send_timeout) {
		timeout = cerver->send_timeout * SOCKET_MILLISECONDS_PER_SECOND;
	}

	return timeout;

}

// the deadline of the response is set by its first send and shared by the others,
// a stream can last longer than a response, so each one of its pieces sets its own
static const struct timespec *http_response_send_deadline (const HttpReceive *http_receive) {

	HttpReceive *receive = (HttpReceive *) http_receive;

	if (!receive->send_deadline_set || (receive->stream.state == HTTP_STREAM_STATE_STARTED)) {
		socket_deadline_create_milliseconds (&receive->send_deadline, http_response_send_timeout (http_receive));
		receive->send_deadline_set = true;
	}

	return &receive->send_deadline;

}

static void http_response_sent_bytes (
	const HttpReceive *http_receive, const size_t bytes
);

// a response that could not be sent completely marks its request,
// it is counted once as a failed response and where its client left
static void http_response_send_failed (
	const HttpReceive *http_receive, const char *operation,
	const int error, const size_t pending
) {

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_WARNING, LOG_TYPE_HTTP,
		"http_response_send () - %s () failed - fd %d - errno %d (%s) - pending %zu",
		operation, http_receive->cr->connection->socket->sock_fd, error,
		strerror_r (error, error_description, sizeof (error_description)),
		pending
	);

	// the client can not find the end of a response that was cut
	((HttpReceive *) http_receive)->framed = false;

	http_receive_response_failed (http_receive, error);

}

// sends all the parts with as few system calls as the socket takes,
// with the send of the core (MSG_NOSIGNAL, EINTR, and poll () on a full socket
// until the deadline of the response)
// returns 0 on success, 1 on error
static u8 http_response_send_vector (
	const HttpReceive *http_receive, struct iovec *parts, int n_parts
) {

	const Socket *socket = http_receive->cr->connection->socket;

	http_response_mark_responded (http_receive);

	size_t total = 0;
	for (int i = 0; i < n_parts; i++) total += parts[i].iov_len;

	size_t sent = 0;
	int error = 0;
	const u8 retval = socket_send_vector_until (
		socket, parts, n_parts, 0, http_response_send_deadline (http_receive),
		&sent, &error
	);

	if (retval) {
		// the bytes that left before the failure are part of the response
		http_response_sent_bytes (http_receive, sent);

		http_response_send_failed (http_receive, "sendmsg", error ? error : EPIPE, total - sent);
	}

	return retval;

}

// the response to a HEAD request is sent without its body,
// it is handled by its own handler or by the GET handler of its path
static bool http_response_is_head (const HttpReceive *http_receive) {

	return http_receive->head
		|| (http_receive->request && (http_receive->request->method == REQUEST_METHOD_HEAD));

}

static void http_response_sent_bytes (
	const HttpReceive *http_receive, const size_t bytes
) {

	if (http_receive->cr->cerver)
		CERVER_STATS_ADD (http_receive->cr->cerver->stats->total_bytes_sent, bytes);

	http_receive->cr->connection->stats->total_bytes_sent += bytes;

	HttpReceive *receive = (HttpReceive *) http_receive;
	receive->sent += bytes;

	// the first bytes of the response that its handler sends start the response stage
	if (bytes && (receive->stage == HTTP_STAGE_HANDLER)) receive->stage = HTTP_STAGE_RESPONSE;

}

static void http_response_sent (
	const HttpReceive *http_receive,
	const http_status status, const size_t total_size
) {

	((HttpReceive *) http_receive)->status = status;
	((HttpReceive *) http_receive)->sent = 0;

	http_response_sent_bytes (http_receive, total_size);

	if (http_receive->http_cerver && ((size_t) status < HTTP_CERVER_RESPONSES_STATUS_SIZE)) {
		CERVER_STATS_ADD (http_receive->http_cerver->n_responses_by_status[status], 1);
	}

}

u8 http_response_send_pieces (
	const HttpReceive *http_receive, struct iovec *parts, const int n_parts
) {

	size_t total = 0;
	for (int i = 0; i < n_parts; i++) total += parts[i].iov_len;

	const u8 retval = http_response_send_vector (http_receive, parts, n_parts);

	if (!retval) http_response_sent_bytes (http_receive, total);

	return retval;

}

u8 http_response_send_pieces_status (
	const HttpReceive *http_receive, const http_status status,
	struct iovec *parts, const int n_parts
) {

	size_t total = 0;
	for (int i = 0; i < n_parts; i++) total += parts[i].iov_len;

	const u8 retval = http_response_send_vector (http_receive, parts, n_parts);

	if (!retval) http_response_sent (http_receive, status, total);

	return retval;

}

#pragma endregion

#pragma region message

// the empty line that ends a compiled header
static const char http_response_headers_end[] = { "\r\n\r\n" };
static const size_t http_response_headers_end_length = 4;

static const char http_response_header_name_separator = ':';

static const char http_response_vary_origin[] = { "Origin" };
static const char http_response_vary_encoding[] = { "Accept-Encoding" };
static const char http_response_vary_origin_and_encoding[] = { "Origin, Accept-Encoding" };

static const char http_response_vary_name[] = { "Vary" };

// the headers of a response that a 304 keeps (RFC 9110 section 15.4.5)
static const http_header http_response_not_modified_headers[] = {
	HTTP_HEADER_CACHE_CONTROL,
	HTTP_HEADER_CONTENT_LOCATION,
	HTTP_HEADER_ETAG,
	HTTP_HEADER_EXPIRES
};

static const size_t http_response_n_not_modified_headers = 4;

// a response that is being sent: its status, its headers and its body,
// with the headers that cerver adds for the request (Connection, Date, X-Request-Id,
// Access-Control-Allow-Origin, Vary, ETag, Content-Encoding) in their own list,
// so each protocol writes them its way: HTTP/1.1 sends the compiled header
// (without the empty line that ends it), the lines of the added ones and the empty line
typedef struct HttpResponseMessage {

	http_status status;

	// the headers of the response and the ones of its cerver,
	// NULL when only its compiled header is known (it was set as bytes, or it was filtered)
	const HttpHeaders *headers;
	const HttpHeaders *defaults;

	const char *header;
	size_t header_length;

	const void *body;
	size_t body_length;

	char etag[HTTP_ETAG_SIZE];
	size_t etag_length;
	bool etag_in_header;

	bool compress;
	bool compressed;

	HttpHeaders extra;

} HttpResponseMessage;

static void http_response_extra_add_by_name (
	HttpHeaders *extra,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	if (http_headers_add_by_name (extra, name, name_length, value, value_length)) {
		cerver_log_warning (
			"http_response_send () - The %.*s header could not be added to the response",
			(int) name_length, name
		);
	}

}

static void http_response_extra_add_header (
	HttpHeaders *extra, const http_header type,
	const char *value, const size_t value_length
) {

	if (http_headers_add (extra, type, value, value_length)) {
		cerver_log_warning (
			"http_response_send () - The %s header could not be added to the response",
			http_header_string (type)
		);
	}

}

// the value of a header of the list or of the ones of the cerver
static bool http_response_message_find_in_lists (
	const HttpResponseMessage *message,
	const http_header type, const char *name, const size_t name_length,
	const char **value, size_t *value_length
) {

	const bool known = (type != HTTP_HEADER_UNDEFINED);

	const HttpHeaders *owner = message->headers;
	const HttpHeaderField *field = known ?
		http_headers_find (owner, type, 0) :
		http_headers_find_by_name (owner, name, name_length, 0);

	if (!field) {
		owner = message->defaults;
		field = known ?
			http_headers_find (owner, type, 0) :
			http_headers_find_by_name (owner, name, name_length, 0);
	}

	if (field) {
		size_t field_length = 0;
		const char *field_value = http_headers_field_value (owner, field, &field_length);

		if (value) *value = field_value;
		if (value_length) *value_length = field_length;
	}

	return field != NULL;

}

// finds a header of the response that is being sent, in its headers when it has them
// or in its compiled header, a header that has no type is found by its name
static bool http_response_message_find (
	const HttpResponseMessage *message,
	const http_header type, const char *name, const size_t name_length,
	const char **value, size_t *value_length
) {

	return message->headers ?
		http_response_message_find_in_lists (message, type, name, name_length, value, value_length) :
		http_headers_buffer_find (
			message->header, message->header_length, name, name_length, value, value_length
		);

}

static bool http_response_message_find_type (
	const HttpResponseMessage *message, const http_header type,
	const char **value, size_t *value_length
) {

	const char *name = http_header_string (type);

	return http_response_message_find (message, type, name, strlen (name), value, value_length);

}

static bool http_response_message_has (
	const HttpResponseMessage *message, const http_header type
) {

	return http_response_message_find_type (message, type, NULL, NULL);

}

static void http_response_extra_date (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	if (
		http_receive->http_cerver->date_header
		&& !http_response_message_has (message, HTTP_HEADER_DATE)
	) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_DATE, http_date_current (), HTTP_DATE_LENGTH
		);
	}

}

static void http_response_extra_request_id (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	const char *id = http_receive->request ? http_receive->request->id : NULL;

	if (
		id && id[0]
		&& !http_response_message_has (message, HTTP_HEADER_REQUEST_ID)
	) {
		http_response_extra_add_header (&message->extra, HTTP_HEADER_REQUEST_ID, id, strlen (id));
	}

}

static bool http_response_origin_is_whitelisted (
	const HttpCerver *http_cerver, const String *origin
) {

	bool whitelisted = false;

	for (u8 idx = 0; !whitelisted && (idx < http_cerver->n_origins); idx++) {
		whitelisted = !strcmp (http_cerver->origins_whitelist[idx].value, origin->str);
	}

	return whitelisted;

}

// a whitelisted origin of a request to an endpoint with CORS can read the response
static void http_response_extra_cors (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	const String *origin = http_receive->request ?
		http_receive->request->headers[HTTP_HEADER_ORIGIN] : NULL;

	if (
		origin
		&& http_response_origin_is_whitelisted (http_receive->http_cerver, origin)
		&& !http_response_message_has (message, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN)
	) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, origin->str, origin->len
		);
	}

}

static void http_response_extra_vary (
	HttpResponseMessage *message, const bool origin, const bool encoding
) {

	const char *vary = (origin && encoding) ? http_response_vary_origin_and_encoding :
		(origin ? http_response_vary_origin : http_response_vary_encoding);

	if (
		(origin || encoding)
		&& !http_response_message_find (
			message, HTTP_HEADER_UNDEFINED,
			http_response_vary_name, sizeof (http_response_vary_name) - 1, NULL, NULL
		)
	) {
		http_response_extra_add_by_name (
			&message->extra,
			http_response_vary_name, sizeof (http_response_vary_name) - 1,
			vary, strlen (vary)
		);
	}

}

// a connection that is closed after the response says so, as it is known when it is sent,
// a response with its own Connection header keeps it
static void http_response_extra_connection (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	if (
		!http_receive->keep_alive
		&& !http_response_message_has (message, HTTP_HEADER_CONNECTION)
	) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_CONNECTION,
			HTTP_RESPONSE_CONNECTION_CLOSE, sizeof (HTTP_RESPONSE_CONNECTION_CLOSE) - 1
		);
	}

}

// the headers that every response of the request has
static void http_response_extra_common (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	http_response_extra_connection (http_receive, message);

	http_response_extra_date (http_receive, message);

	http_response_extra_request_id (http_receive, message);

	const HttpEndpoint *endpoint = http_receive->endpoint;
	const bool cors = endpoint && endpoint->resolved.cors;
	const bool compression = endpoint && endpoint->resolved.compression
		&& (http_receive->dispatch == HTTP_RECEIVE_DISPATCH_ENDPOINT);

	if (cors) http_response_extra_cors (http_receive, message);

	http_response_extra_vary (message, cors, compression);

}

// the length of the line that starts at start, without its "\r\n"
static size_t http_response_header_line (const char *start, const char *end) {

	const char *line_end = start;
	while (
		((size_t) (end - line_end) >= http_response_header_end_length)
		&& memcmp (line_end, http_response_header_end, http_response_header_end_length)
	) {
		line_end += 1;
	}

	return ((size_t) (end - line_end) >= http_response_header_end_length) ?
		(size_t) (line_end - start) : (size_t) (end - start);

}

static bool http_response_header_line_is_name (
	const char *line, const size_t line_length,
	const char *name, const size_t name_length
) {

	return (line_length > name_length)
		&& (line[name_length] == http_response_header_name_separator)
		&& !strncasecmp (line, name, name_length);

}

static bool http_response_header_line_is (
	const char *line, const size_t line_length, const http_header type
) {

	const char *name = http_header_string (type);

	return http_response_header_line_is_name (line, line_length, name, strlen (name));

}

// copies the lines of the header (without its empty line) into the arena,
// the line of the header type is not copied
// returns NULL on error
static char *http_response_header_copy_without (
	Arena *arena, const char *header, const size_t header_length,
	const http_header type, size_t *copy_length
) {

	char *copy = arena ? (char *) arena_alloc (arena, header_length + 1) : NULL;
	if (copy) {
		char *end = copy;
		const char *last = header + header_length;
		for (const char *line = header; line < last; ) {
			const size_t line_length = http_response_header_line (line, last);
			const size_t with_end = ((size_t) (last - line) > line_length) ?
				line_length + http_response_header_end_length : line_length;

			// the status line is always copied
			if ((line == header) || !http_response_header_line_is (line, line_length, type)) {
				(void) memcpy (end, line, with_end);
				end += with_end;
			}

			line += with_end;
		}

		*copy_length = (size_t) (end - copy);
	}

	return copy;

}

static bool http_response_is_compressible (
	const HttpReceive *http_receive, const HttpResponseMessage *message
) {

	const String *accept_encoding = http_receive->request->headers[HTTP_HEADER_ACCEPT_ENCODING];

	const char *content_type = NULL;
	size_t content_type_length = 0;

	return accept_encoding
		&& http_compression_accepts_gzip (accept_encoding->str, accept_encoding->len)
		&& !http_response_message_has (message, HTTP_HEADER_CONTENT_ENCODING)
		&& http_response_message_find_type (
			message, HTTP_HEADER_CONTENT_TYPE, &content_type, &content_type_length
		)
		&& http_compression_is_compressible (content_type, content_type_length);

}

static bool http_response_should_compress (
	const HttpReceive *http_receive, const HttpResponseMessage *message
) {

	const http_status status = message->status;

	return http_compression_available ()
		&& message->body && message->body_length
		&& (message->body_length >= http_receive->http_cerver->compression_min_size)
		&& (status >= HTTP_STATUS_OK)
		&& (status != HTTP_STATUS_NO_CONTENT)
		&& (status != HTTP_STATUS_PARTIAL_CONTENT)
		&& (status != HTTP_STATUS_NOT_MODIFIED)
		&& http_response_is_compressible (http_receive, message);

}

// the entity tag of the response, the one of its header or the one of its body
static void http_response_message_etag (HttpResponseMessage *message) {

	const char *value = NULL;
	size_t value_length = 0;

	message->etag_in_header = http_response_message_find_type (
		message, HTTP_HEADER_ETAG, &value, &value_length
	);

	if (message->etag_in_header) {
		message->etag_length = (value_length < HTTP_ETAG_SIZE) ? value_length : 0;
		if (message->etag_length) (void) memcpy (message->etag, value, value_length);
	}

	else {
		message->etag_length = http_etag_from_data (
			message->body, message->body_length, message->compress, message->etag
		);
	}

}

static bool http_response_message_uses_etag (
	const HttpReceive *http_receive, const HttpResponseMessage *message
) {

	const RequestMethod method = http_receive->request->method;

	return (message->status == HTTP_STATUS_OK)
		&& ((method == REQUEST_METHOD_GET) || (method == REQUEST_METHOD_HEAD));

}

static void http_response_message_compress (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	Arena *arena = http_request_get_arena (http_receive->request);

	void *compressed = NULL;
	size_t compressed_length = 0;
	if (
		http_compression_gzip (
			arena, message->body, message->body_length,
			http_receive->http_cerver->compression_level,
			&compressed, &compressed_length
		)
		&& (compressed_length < message->body_length)
	) {
		size_t header_length = 0;
		const char *header = http_response_header_copy_without (
			arena, message->header, message->header_length,
			HTTP_HEADER_CONTENT_LENGTH, &header_length
		);

		if (header) {
			// the header without the length of the plain body is the one that is sent
			message->headers = NULL;
			message->header = header;
			message->header_length = header_length;
			message->body = compressed;
			message->body_length = compressed_length;
			message->compressed = true;
		}
	}

}

static void http_response_extra_compression (HttpResponseMessage *message) {

	char length[HTTP_RESPONSE_CONTENT_LENGTH_SIZE];
	const size_t length_length = http_response_content_length_value (message->body_length, length);

	http_response_extra_add_header (
		&message->extra, HTTP_HEADER_CONTENT_LENGTH, length, length_length
	);

	http_response_extra_add_header (
		&message->extra, HTTP_HEADER_CONTENT_ENCODING,
		HTTP_COMPRESSION_ENCODING, sizeof (HTTP_COMPRESSION_ENCODING) - 1
	);

}

// the status of a response that never has a body (RFC 9110 section 6.4.1)
static bool http_response_status_has_no_body (const http_status status) {

	return (status < HTTP_STATUS_OK)
		|| (status == HTTP_STATUS_NO_CONTENT)
		|| (status == HTTP_STATUS_NOT_MODIFIED);

}

// the client finds the end of the response that was sent without the close of the connection,
// a response whose body is sent after its header (a file) is not complete yet
static bool http_response_message_is_framed (
	const HttpReceive *http_receive, const HttpResponseMessage *message,
	const bool with_body
) {

	return http_response_is_head (http_receive)
		|| http_response_status_has_no_body (message->status)
		|| (with_body && (
			http_response_message_has (message, HTTP_HEADER_CONTENT_LENGTH)
			|| http_headers_count (&message->extra, HTTP_HEADER_CONTENT_LENGTH)
		));

}

// the bytes of the body that was announced in the header were sent after it
static void http_response_body_sent (
	const HttpReceive *http_receive, const size_t sent, const size_t expected
) {

	if (sent == expected) ((HttpReceive *) http_receive)->framed = true;

}

static u8 http_response_message_send (
	const HttpReceive *http_receive, const HttpResponseMessage *message,
	const bool with_body
) {

	const size_t body_length = (with_body && !http_response_is_head (http_receive) && message->body) ?
		message->body_length : 0;

	size_t extra_length = 0;
	const char *extra = http_headers_lines (&message->extra, &extra_length);

	struct iovec parts[HTTP_RESPONSE_MESSAGE_PARTS];
	parts[0].iov_base = (void *) message->header;
	parts[0].iov_len = message->header_length;
	parts[1].iov_base = (void *) extra;
	parts[1].iov_len = extra_length;
	parts[2].iov_base = (void *) http_response_header_end;
	parts[2].iov_len = http_response_header_end_length;
	parts[3].iov_base = (void *) message->body;
	parts[3].iov_len = body_length;

	const int n_parts = body_length ? HTTP_RESPONSE_MESSAGE_PARTS : HTTP_RESPONSE_MESSAGE_PARTS - 1;

	const u8 retval = http_response_send_vector (http_receive, parts, n_parts);

	if (!retval) {
		http_response_sent (
			http_receive, message->status,
			message->header_length + extra_length + http_response_header_end_length + body_length
		);

		((HttpReceive *) http_receive)->framed = http_response_message_is_framed (
			http_receive, message, with_body
		);
	}

	// the request is logged with the status that it tried to send
	else {
		((HttpReceive *) http_receive)->status = message->status;
	}

	return retval;

}

static bool http_response_not_modified_keeps (
	const char *line, const size_t line_length
) {

	bool keeps = http_response_header_line_is_name (
		line, line_length, http_response_vary_name, sizeof (http_response_vary_name) - 1
	);

	for (size_t i = 0; !keeps && (i < http_response_n_not_modified_headers); i++) {
		keeps = http_response_header_line_is (line, line_length, http_response_not_modified_headers[i]);
	}

	return keeps;

}

// copies the status line and the headers that a 304 keeps into the arena
// returns NULL on error
static char *http_response_not_modified_header (
	Arena *arena, const char *header, const size_t header_length, size_t *copy_length
) {

	char main_header[HTTP_RESPONSE_MAIN_HEADER_SIZE];
	const size_t main_length = http_response_main_header (HTTP_STATUS_NOT_MODIFIED, main_header);

	char *copy = arena ? (char *) arena_alloc (arena, main_length + header_length + 1) : NULL;
	if (copy) {
		char *end = copy;
		(void) memcpy (end, main_header, main_length);
		end += main_length;

		const char *last = header + header_length;
		const char *line = header + http_response_header_line (header, last) + http_response_header_end_length;
		while (line < last) {
			const size_t line_length = http_response_header_line (line, last);
			const size_t with_end = ((size_t) (last - line) > line_length) ?
				line_length + http_response_header_end_length : line_length;

			if (http_response_not_modified_keeps (line, line_length)) {
				(void) memcpy (end, line, with_end);
				end += with_end;
			}

			line += with_end;
		}

		*copy_length = (size_t) (end - copy);
	}

	return copy;

}

// answers a conditional request whose representation was not modified
// returns 0 on success, 1 on error
static u8 http_response_send_not_modified (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	u8 retval = 1;

	size_t header_length = 0;
	const char *header = http_response_not_modified_header (
		http_request_get_arena (http_receive->request),
		message->header, message->header_length, &header_length
	);

	if (header) {
		message->status = HTTP_STATUS_NOT_MODIFIED;

		// only the headers that a 304 keeps are sent
		message->headers = NULL;
		message->header = header;
		message->header_length = header_length;
		message->body = NULL;
		message->body_length = 0;

		http_headers_reset (&message->extra);

		if (!message->etag_in_header) {
			http_response_extra_add_header (
				&message->extra, HTTP_HEADER_ETAG, message->etag, message->etag_length
			);
		}

		// a 304 has the Vary of the response that it replaces
		http_response_extra_common (http_receive, message);

		retval = http_response_message_send (http_receive, message, false);
	}

	return retval;

}

// applies the entity tag and the compression of the endpoint to the response
// returns true if the request is answered with 304 instead of the response
static bool http_response_message_transform (
	const HttpReceive *http_receive, HttpResponseMessage *message,
	u8 *retval
) {

	bool answered = false;

	const HttpEndpoint *endpoint = http_receive->endpoint;

	message->compress = endpoint->resolved.compression
		&& http_response_should_compress (http_receive, message);

	if (endpoint->resolved.etag && message->body && http_response_message_uses_etag (http_receive, message)) {
		http_response_message_etag (message);

		if (
			message->etag_length
			&& http_conditional_not_modified (http_receive->request, message->etag, message->etag_length, 0)
		) {
			*retval = http_response_send_not_modified (http_receive, message);
			answered = true;
		}
	}

	if (!answered && message->compress) {
		http_response_message_compress (http_receive, message);

		// a body that was not compressed has the entity tag of its plain bytes
		if (!message->compressed && message->etag_length && !message->etag_in_header) {
			message->etag_length = http_etag_from_data (
				message->body, message->body_length, false, message->etag
			);
		}
	}

	return answered;

}

static bool http_response_header_is_complete (
	const void *header, const size_t header_len
) {

	return header && (header_len >= http_response_headers_end_length)
		&& !memcmp (
			(const char *) header + header_len - http_response_headers_end_length,
			http_response_headers_end, http_response_headers_end_length
		);

}

// a header that cerver can not complete is sent as it is, with its data
static u8 http_response_send_raw (
	const HttpReceive *http_receive, const http_status status,
	const void *header, const size_t header_len,
	const void *data, const size_t data_len
) {

	const size_t body_len = (http_response_is_head (http_receive) || !data) ? 0 : data_len;

	struct iovec parts[HTTP_RESPONSE_PARTS];
	parts[0].iov_base = (void *) header;
	parts[0].iov_len = header_len;
	parts[1].iov_base = (void *) data;
	parts[1].iov_len = body_len;

	const int n_parts = body_len ? HTTP_RESPONSE_PARTS : 1;

	const u8 retval = http_response_send_vector (http_receive, parts, n_parts);

	if (!retval) http_response_sent (http_receive, status, header_len + body_len);
	else ((HttpReceive *) http_receive)->status = status;

	return retval;

}

static void http_response_message_init (
	HttpResponseMessage *message, const http_status status,
	const HttpHeaders *headers,
	const void *header, const size_t header_len,
	const void *data, const size_t data_len
) {

	message->status = status;

	message->headers = headers;
	message->defaults = headers ? http_response_default_headers () : NULL;

	// the empty line is sent after the headers that cerver adds
	message->header = (const char *) header;
	message->header_length = header_len - http_response_header_end_length;

	message->body = data;
	message->body_length = data ? data_len : 0;

	message->etag[0] = '\0';
	message->etag_length = 0;
	message->etag_in_header = false;

	message->compress = false;
	message->compressed = false;

	http_headers_init (&message->extra);

}

static void http_response_message_extra (
	const HttpReceive *http_receive, HttpResponseMessage *message,
	const unsigned int options
) {

	if (message->compressed) http_response_extra_compression (message);

	if (message->etag_length && !message->etag_in_header) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_ETAG, message->etag, message->etag_length
		);
	}

	http_response_extra_common (http_receive, message);

	if ((options & HTTP_RESPONSE_SEND_OPTION_STREAM) && http_receive->stream.chunked) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_TRANSFER_ENCODING,
			HTTP_STREAM_CHUNKED_ENCODING, sizeof (HTTP_STREAM_CHUNKED_ENCODING) - 1
		);
	}

}

u8 http_response_send_message (
	const HttpReceive *http_receive, const http_status status,
	const HttpHeaders *headers,
	const void *header, const size_t header_len,
	const void *data, const size_t data_len,
	const unsigned int options
) {

	u8 retval = 1;

	if (
		!(options & HTTP_RESPONSE_SEND_OPTION_STREAM)
		&& (http_receive->stream.state != HTTP_STREAM_STATE_NONE)
	) {
		cerver_log_error ("http_response_send () - The response is being sent with a stream");
	}

	else if (!http_receive->http_cerver || !http_response_header_is_complete (header, header_len)) {
		retval = http_response_send_raw (http_receive, status, header, header_len, data, data_len);
	}

	else {
		HttpResponseMessage message;
		http_response_message_init (&message, status, headers, header, header_len, data, data_len);

		bool answered = false;
		if (
			(options & HTTP_RESPONSE_SEND_OPTION_TRANSFORM)
			&& http_receive->endpoint && http_receive->request
			&& (http_receive->dispatch == HTTP_RECEIVE_DISPATCH_ENDPOINT)
		) {
			answered = http_response_message_transform (http_receive, &message, &retval);
		}

		if (!answered) {
			http_response_message_extra (http_receive, &message, options);

			retval = http_response_message_send (
				http_receive, &message, !(options & HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY)
			);
		}

		http_headers_reset (&message.extra);
	}

	return retval;

}

u8 http_response_send_not_modified_header (
	const HttpReceive *http_receive,
	const void *header, const size_t header_len
) {

	u8 retval = 1;

	if (http_receive->request && http_response_header_is_complete (header, header_len)) {
		HttpResponseMessage message;
		http_response_message_init (&message, HTTP_STATUS_OK, NULL, header, header_len, NULL, 0);

		// the entity tag is copied from the header
		message.etag_in_header = true;

		retval = http_response_send_not_modified (http_receive, &message);

		http_headers_reset (&message.extra);
	}

	return retval;

}

#pragma endregion

#pragma region send

// sends the header and the data without copying them into a single buffer,
// with the headers of the request and the entity tag and the compression of its endpoint
// the response to a HEAD request is sent without its data
// returns 0 on success, 1 on error
static u8 http_response_send_parts (
	const HttpReceive *http_receive, const http_status status,
	const HttpHeaders *headers,
	const void *header, const size_t header_len,
	const void *data, const size_t data_len
) {

	return http_response_send_message (
		http_receive, status, headers, header, header_len, data, data_len,
		HTTP_RESPONSE_SEND_OPTION_TRANSFORM
	);

}

// the headers of a response are sent with its header,
// but the ones of a header that was set as bytes are only in those bytes
static const HttpHeaders *http_response_sent_headers (const HttpResponse *response) {

	return response->header_is_raw ? NULL : &response->headers;

}

// sends a response through the connection's socket,
// a compiled response is sent from its single buffer (its header part and its data part),
// any other is sent from its header and its data (the header is compiled if needed)
// returns 0 on success, 1 on error
u8 http_response_send (
	HttpResponse *response,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	if (response && http_receive->cr->connection) {
		if (response->res) {
			// the compiled buffer starts with the header and continues with the data
			const bool split = response->header_len && (response->header_len <= response->res_len);
			const size_t header_len = split ? response->header_len : response->res_len;

			retval = http_response_send_parts (
				http_receive, response->status,
				http_response_sent_headers (response),
				response->res, header_len,
				(const char *) response->res + header_len, response->res_len - header_len
			);
		}

		else {
			if (!response->header) http_response_compile_header (response);

			if (response->header) {
				retval = http_response_send_parts (
					http_receive, response->status,
					http_response_sent_headers (response),
					response->header, response->header_len,
					response->data, response->data_len
				);
			}
		}
	}

	return retval;

}

// expects a response with an already created header and data
// as this method will send both parts without the need of a continuos response buffer
// use this for maximun efficiency
// returns 0 on success, 1 on error
u8 http_response_send_split (
	HttpResponse *response,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	if (response && http_receive->cr->connection) {
		if (response->header && response->data) {
			retval = http_response_send_parts (
				http_receive, response->status,
				http_response_sent_headers (response),
				response->header, response->header_len,
				response->data, response->data_len
			);
		}
	}

	return retval;

}

// creates & sends a response through the connection's socket
// returns 0 on success, 1 on error
u8 http_response_create_and_send (
	const http_status status, const void *data, size_t data_len,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	HttpResponse *response = http_response_create (status, data, data_len);
	if (response) {
		if (!http_response_compile (response)) {
			retval = http_response_send (response, http_receive);
		}

		http_response_delete (response);
	}

	return retval;

}

// the bytes that a pipe copies until the end of the file
static const size_t http_response_pipe_whole_file = SIZE_MAX;

// the bytes from the current offset of the file until its end
static size_t http_response_pipe_file_remaining (const int file_fd) {

	size_t remaining = 0;

	struct stat filestatus = CERVER_ZERO_INITIALIZER;
	const off_t current = lseek (file_fd, 0, SEEK_CUR);
	if (!fstat (file_fd, &filestatus) && (current > -1) && (filestatus.st_size > current)) {
		remaining = (size_t) (filestatus.st_size - current);
	}

	return remaining;

}

// copies the bytes of the file to the socket, from its offset (NULL for the file's current offset),
// until remaining is sent or the file ends, and adds the bytes that were sent to total_sent,
// with the deadline of the response
// sendfile () does not accept MSG_NOSIGNAL, so SIGPIPE is ignored by cerver_start ()
// returns 0 on success, 1 if the file was not sent completely
static u8 http_response_send_file_pipe (
	const HttpReceive *http_receive, const int file_fd,
	off_t *offset, size_t remaining, size_t *total_sent
) {

	if (remaining == http_response_pipe_whole_file) {
		remaining = http_response_pipe_file_remaining (file_fd);
	}

	size_t sent = 0;
	int error = 0;
	u8 retval = socket_send_file_until (
		http_receive->cr->connection->socket, file_fd, offset, remaining,
		http_response_send_deadline (http_receive), &sent, &error
	);

	*total_sent += sent;

	// a file that ended before its range is sent as it is
	if (retval && !error) retval = 0;

	if (retval) {
		http_response_send_failed (http_receive, "sendfile", error, remaining - sent);
	}

	return retval;

}

// adds the entity tag and the last modification of the file to the response
// returns the length of the entity tag that was written in etag
static size_t http_response_file_validators (
	HttpResponse *response, const struct stat *filestatus, char *etag
) {

	const size_t etag_length = http_etag_from_file (filestatus, etag);
	if (etag_length) (void) http_response_add_header (response, HTTP_HEADER_ETAG, etag);

	char last_modified[HTTP_DATE_SIZE] = { 0 };
	if (http_date_format (filestatus->st_mtime, last_modified)) {
		(void) http_response_add_header (response, HTTP_HEADER_LAST_MODIFIED, last_modified);
	}

	return etag_length;

}

// the header was already sent, so only the bytes of the file are added to the stats,
// the response ends where its header said when the file still has the expected bytes
static u8 http_response_send_file_actual (
	const HttpReceive *http_receive,
	const char *filename, const size_t expected
) {

	u8 retval = 1;

	int file_fd = open (filename, O_RDONLY);
	if (file_fd > -1) {
		size_t total_sent = 0;

		retval = http_response_send_file_pipe (
			http_receive, file_fd, NULL, http_response_pipe_whole_file, &total_sent
		);

		http_response_sent_bytes (http_receive, total_sent);

		if (!retval) http_response_body_sent (http_receive, total_sent, expected);

		(void) close (file_fd);
	}

	else {
		cerver_log_error (
			"http_response_send_file () - Failed to open %s",
			filename
		);
	}

	return retval;

}

u8 http_response_send_file_internal (
	const HttpReceive *http_receive,
	const http_status status,
	const char *filename,
	struct stat *filestatus
) {

	u8 retval = 1;

	// create response
	HttpResponse *response = http_response_get ();
	if (!response) return retval;

	// generate header
	response->status = status;
	http_response_add_content_type_header (
		response, http_content_type_from_filename (filename)
	);

	http_response_add_content_length_header (response, filestatus->st_size);

	char etag[HTTP_ETAG_SIZE] = { 0 };
	const size_t etag_length = http_response_file_validators (response, filestatus, etag);

	http_response_compile_header (response);

	#ifdef HTTP_RESPONSE_DEBUG
	cerver_log_msg ("\n%s\n", (const char *) response->header);
	#endif

	if (
		(status == HTTP_STATUS_OK)
		&& http_conditional_not_modified (
			http_receive->request, etag, etag_length, filestatus->st_mtime
		)
	) {
		retval = http_response_send_not_modified_header (
			http_receive, response->header, response->header_len
		);
	}

	// a HEAD request only receives the header
	else if (http_response_is_head (http_receive)) {
		retval = http_response_send_message (
			http_receive, status, &response->headers,
			response->header, response->header_len, NULL, 0, 0
		);
	}

	// pipe header
	else if (!http_response_send_message (
		http_receive, status, &response->headers,
		response->header, response->header_len, NULL, 0,
		HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY
	)) {
		retval = http_response_send_file_actual (
			http_receive, filename, (size_t) filestatus->st_size
		);
	}

	http_response_return (response);

	return retval;

}

// opens the selected file and sends it back to the client
// takes care of generating the header based on the file values
// returns 0 on success, 1 on error
u8 http_response_send_file (
	const HttpReceive *http_receive,
	const http_status status,
	const char *filename
) {

	u8 retval = 1;

	// check if file exists
	struct stat filestatus = CERVER_ZERO_INITIALIZER;
	if (!stat (filename, &filestatus)) {
		retval = http_response_send_file_internal (
			http_receive, status,
			filename, &filestatus
		);
	}

	else {
		cerver_log_error (
			"http_response_send_file () - File %s not found!",
			filename
		);
	}

	return retval;

}

// works like http_response_send_file ()
// but generates filename on the fly
u8 http_response_send_file_generate (
	const HttpReceive *http_receive,
	const http_status status,
	const char *format, ...
) {

	u8 retval = 1;

	va_list args;
	va_start (args, format);

	char filename[FILENAME_DEFAULT_SIZE] = { 0 };
	(void) vsnprintf (
		filename, FILENAME_DEFAULT_SIZE,
		format, args
	);

	retval = http_response_send_file (
		http_receive, status, filename
	);

	va_end (args);

	return retval;

}

#pragma endregion

#pragma region render

// sends the data with its type and its length as the headers of a response,
// the data is not copied and the response is used again
static u8 http_response_render (
	const HttpReceive *http_receive,
	const http_status status, const ContentType type,
	const char *data, const size_t data_len
) {

	u8 retval = 1;

	HttpResponse *response = http_receive->cr->connection ?
		(HttpResponse *) http_response_new () : NULL;

	if (response) {
		response->status = status;

		(void) http_response_add_content_type_header (response, type);
		(void) http_response_add_content_length_header (response, data_len);

		http_response_compile_header (response);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", (const char *) response->header);
		#endif

		if (response->header) {
			retval = http_response_send_parts (
				http_receive, status, &response->headers,
				response->header, response->header_len,
				data, data_len
			);
		}

		http_response_delete (response);
	}

	return retval;

}

// sends the selected text back to the user
// this methods takes care of generating a repsonse with text/html content type
// returns 0 on success, 1 on error
u8 http_response_render_text (
	const HttpReceive *http_receive,
	const http_status status,
	const char *text, const size_t text_len
) {

	u8 retval = 1;

	if (http_receive && text) {
		retval = http_response_render (
			http_receive, status, HTTP_CONTENT_TYPE_HTML, text, text_len
		);
	}

	return retval;

}

// sends the selected json back to the user
// this methods takes care of generating a repsonse with application/json content type
// returns 0 on success, 1 on error
u8 http_response_render_json (
	const HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
) {

	u8 retval = 1;

	if (http_receive && json) {
		retval = http_response_render (
			http_receive, status, HTTP_CONTENT_TYPE_JSON, json, json_len
		);
	}

	return retval;

}

#pragma endregion

#pragma region videos

static ContentType http_response_handle_video_content (
	const char *filename
) {

	ContentType content_type = HTTP_CONTENT_TYPE_MP4;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		content_type = http_content_type_by_extension (ext);
	}

	return content_type;

}

static u8 http_response_handle_video_internal (
	const HttpReceive *http_receive,
	const char *filename,
	BytesRange *bytes_range
) {

	u8 retval = 1;

	// create response
	HttpResponse *response = http_response_get ();

	// generate header
	response->status = HTTP_STATUS_PARTIAL_CONTENT;
	const ContentType content_type = http_response_handle_video_content (filename);
	http_response_add_video_headers (response, content_type, bytes_range);
	http_response_compile_header (response);

	// (void) printf ("\n\n%s\n\n", response->header);

	// pipe header
	if (!http_response_send_message (
		http_receive, response->status, &response->headers,
		response->header, response->header_len, NULL, 0,
		HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY
	)) {
		size_t total_sent = 0;

		// open video
		int video_fd = open (filename, O_RDONLY);
		if (video_fd > -1) {
			retval = http_response_send_file_pipe (
				http_receive, video_fd,
				&bytes_range->start, (size_t) bytes_range->chunk_size,
				&total_sent
			);

			// the header was already added to the stats
			http_response_sent_bytes (http_receive, total_sent);

			if (!retval) {
				http_response_body_sent (
					http_receive, total_sent, (size_t) bytes_range->chunk_size
				);
			}

			(void) close (video_fd);
		}

		else {
			cerver_log_error (
				"http_response_handle_video () - Failed to open %s",
				filename
			);
		}
	}

	http_response_return (response);

	return retval;

}

// handles the transmission of a video to the client
// returns 0 on success, 1 on error
u8 http_response_handle_video (
	const HttpReceive *http_receive,
	const char *filename
) {

	u8 retval = 1;

	// check that video exists
	struct stat filestats = CERVER_ZERO_INITIALIZER;
	if (!stat (filename, &filestats)) {
		// get range
		BytesRange bytes_range = {
			.file_size = filestats.st_size,
			.type = HTTP_RANGE_TYPE_NONE,
			.start = 0, .end = 0,
			.chunk_size = HTTP_RESPONSE_VIDEO_CHUNK_SIZE
		};

		http_request_get_bytes_range (
			http_receive->request, &bytes_range
		);

		switch (bytes_range.type) {
			case HTTP_RANGE_TYPE_EMPTY: {
				bytes_range.end = bytes_range.start + bytes_range.chunk_size;
			} break;

			case HTTP_RANGE_TYPE_FIRST: {
				long bytes_left = bytes_range.file_size - bytes_range.start;
				if (bytes_left < bytes_range.chunk_size) {
					bytes_range.chunk_size = bytes_left;
				}

				bytes_range.end = bytes_range.start + bytes_range.chunk_size;
			} break;

			case HTTP_RANGE_TYPE_FULL: {
				bytes_range.chunk_size = bytes_range.end - bytes_range.start;
			} break;

			default: break;
		}

		bytes_range.end -= 1;

		// (void) printf ("chunk size: %ld\n", bytes_range.chunk_size);

		retval = http_response_handle_video_internal (
			http_receive, filename, &bytes_range
		);
	}

	else {
		cerver_log_error (
			"http_response_handle_video () - Video %s not found!",
			filename
		);
	}

	return retval;

}

// works like http_response_handle_video ()
// but generates filename on the fly
u8 http_response_handle_video_generate (
	const HttpReceive *http_receive,
	const char *format, ...
) {

	u8 retval = 1;

	va_list args;
	va_start (args, format);

	char filename[FILENAME_DEFAULT_SIZE] = { 0 };
	(void) vsnprintf (
		filename, FILENAME_DEFAULT_SIZE,
		format, args
	);

	retval = http_response_handle_video (
		http_receive, filename
	);

	va_end (args);

	return retval;

}

#pragma endregion

#pragma region json

static HttpResponse *http_response_create_json_common (
	const http_status status,
	const char *json, const size_t json_len
) {

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (response) {
		response->status = status;

		// add headers
		(void) http_response_add_content_type_header (
			response, HTTP_CONTENT_TYPE_JSON
		);

		(void) http_response_add_content_length_header (
			response, json_len
		);

		// add body
		response->data = (char *) json;
		response->data_len = json_len;
		response->data_ref = true;

		// generate response
		(void) http_response_compile (response);
	}

	return response;

}

// creates a HTTP response with the defined status code
// with a custom json message body
// returns a new HTTP response instance ready to be sent
HttpResponse *http_response_create_json (
	const http_status status, const char *json, const size_t json_len
) {

	return json ? http_response_create_json_common (
		status, json, json_len
	) : NULL;

}

// the body { "key": value } of the json responses,
// the key and a string value are escaped by the json library,
// a real is written without the locale, and a NaN or an infinite value is an error
// it takes the value, as json_object_set_new () does
// returns the body allocated with malloc (), or NULL on error
static char *http_response_json_body (const char *key, json_t *value) {

	char *body = NULL;

	json_t *json = json_object ();
	if (json && value) {
		// the value is released by the object, also when it fails to set it
		if (!json_object_set_new (json, key, value)) {
			body = json_dumps (json, HTTP_RESPONSE_JSON_DUMP_FLAGS);
		}
	}

	else {
		json_decref (value);
	}

	json_decref (json);

	return body;

}

// the response owns the json, so its data is never left dangling,
// a response that is compiled again or sent in parts still has its body
static HttpResponse *http_response_create_json_owned (
	const http_status status, char *json
) {

	HttpResponse *response = NULL;

	if (json) {
		response = http_response_create_json_common (
			status, json, strlen (json)
		);

		if (response) response->data_ref = false;
		else free (json);
	}

	return response;

}

HttpResponse *http_response_create_json_key_value (
	const http_status status, const char *key, const char *value
) {

	return (key && value) ? http_response_create_json_owned (
		status, http_response_json_body (key, json_string (value))
	) : NULL;

}

// creates a HTTP response with the defined status code
// with a json body of type { "key": int_value }
// returns a new HTTP response instance ready to be sent
HttpResponse *http_response_json_int_value (
	const http_status status, const char *key, const int value
) {

	return key ? http_response_create_json_owned (
		status, http_response_json_body (key, json_integer ((json_int_t) value))
	) : NULL;

}

// sends a HTTP response with custom status code
// with a json body of type { "key": int_value }
// returns 0 on success, 1 on error
u8 http_response_json_int_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const int value
) {

	u8 retval = 1;

	HttpResponse *response = http_response_json_int_value (
		status, key, value
	);

	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// creates a HTTP response with the defined status code
// with a json body of type { "key": large_int_value }
// returns a new HTTP response instance ready to be sent
HttpResponse *http_response_json_large_int_value (
	const http_status status, const char *key, const long value
) {

	return key ? http_response_create_json_owned (
		status, http_response_json_body (key, json_integer ((json_int_t) value))
	) : NULL;

}

// sends a HTTP response with custom status code
// with a json body of type { "key": large_int_value }
// returns 0 on success, 1 on error
u8 http_response_json_large_int_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const long value
) {

	u8 retval = 1;

	HttpResponse *response = http_response_json_large_int_value (
		status, key, value
	);

	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// creates a HTTP response with the defined status code
// with a json body of type { "key": double_value }
// returns a new HTTP response instance ready to be sent
HttpResponse *http_response_json_real_value (
	const http_status status, const char *key, const double value
) {

	return key ? http_response_create_json_owned (
		status, http_response_json_body (key, json_real (value))
	) : NULL;

}

// sends a HTTP response with custom status code
// with a json body of type { "key": double_value }
// returns 0 on success, 1 on error
u8 http_response_json_real_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const double value
) {

	u8 retval = 1;

	HttpResponse *response = http_response_json_real_value (
		status, key, value
	);

	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// creates a HTTP response with the defined status code
// with a json body of type { "key": bool_value }
// returns a new HTTP response instance ready to be sent
HttpResponse *http_response_json_bool_value (
	const http_status status, const char *key, const bool value
) {

	return key ? http_response_create_json_owned (
		status, http_response_json_body (key, json_boolean (value))
	) : NULL;

}

// sends a HTTP response with custom status code
// with a json body of type { "key": bool_value }
// returns 0 on success, 1 on error
u8 http_response_json_bool_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const bool value
) {

	u8 retval = 1;

	HttpResponse *response = http_response_json_bool_value (
		status, key, value
	);

	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// a message that is not valid UTF-8 can not be json, there is no response
static HttpResponse *http_response_json_internal (
	const http_status status, const char *key, const char *value
) {

	char *json = http_response_json_body (key, json_string (value));
	if (!json) return NULL;

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (!response) {
		free (json);
	}

	else {
		response->status = status;

		// body
		response->data = json;
		response->data_len = strlen (json);

		http_response_json_header (response);
	}

	return response;

}

// the responses that are returned to be reused are compiled into a single buffer
static HttpResponse *http_response_json_internal_compiled (
	const http_status status, const char *key, const char *value
) {

	HttpResponse *response = http_response_json_internal (status, key, value);
	if (response) (void) http_response_compile (response);

	return response;

}

// creates a http response with the defined status code ready to be sent
// and a data (body) with a json message of type { msg: "your message" }
HttpResponse *http_response_json_msg (
	const http_status status, const char *msg
) {

	return msg ? http_response_json_internal_compiled (status, "msg", msg) : NULL;

}

// creates and sends a http json message response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_msg_send (
	const HttpReceive *http_receive,
	const http_status status, const char *msg
) {

	u8 retval = 1;

	HttpResponse *response = msg ? http_response_json_internal (status, "msg", msg) : NULL;
	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// creates a http response with the defined status code ready to be sent
// and a data (body) with a json message of type { error: "your error message" }
HttpResponse *http_response_json_error (
	const http_status status, const char *error_msg
) {

	return error_msg ? http_response_json_internal_compiled (status, "error", error_msg) : NULL;

}

// creates and sends a http json error response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_error_send (
	const HttpReceive *http_receive,
	const http_status status, const char *error_msg
) {

	u8 retval = 1;

	HttpResponse *response = error_msg ? http_response_json_internal (status, "error", error_msg) : NULL;
	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// creates a http response with the defined status code ready to be sent
// and a data (body) with a json meesage of type { key: value }
HttpResponse *http_response_json_key_value (
	const http_status status, const char *key, const char *value
) {

	return (key && value) ? http_response_json_internal_compiled (status, key, value) : NULL;

}

// creates and sends a http custom json response with the defined status code & key-value
// returns 0 on success, 1 on error
u8 http_response_json_key_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const char *value
) {

	u8 retval = 1;

	HttpResponse *response = (key && value) ? http_response_json_internal (status, key, value) : NULL;
	if (response) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (response);
		#endif
		retval = http_response_send (response, http_receive);
		http_response_delete (response);
	}

	return retval;

}

// a body that can not be copied is an error, not a response with uninitialized bytes
static HttpResponse *http_response_json_custom_internal (
	const http_status status, const char *json
) {

	char *body = strdup (json);
	if (!body) return NULL;

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (!response) {
		free (body);
	}

	else {
		response->status = status;

		// body
		response->data = body;
		response->data_len = strlen (body);

		// header
		http_response_json_header (response);
	}

	return response;

}

// creates a http response with the defined status code and the body with the custom json
HttpResponse *http_response_json_custom (
	const http_status status, const char *json
) {

	HttpResponse *response = NULL;
	if (json) {
		response = http_response_json_custom_internal (status, json);

		if (response) (void) http_response_compile (response);
	}

	return response;

}

// creates and sends a http custom json response with the defined status code
// returns 0 on success, 1 on error
u8 http_response_json_custom_send (
	const HttpReceive *http_receive,
	const http_status status, const char *json
) {

	u8 retval = 1;

	if (http_receive && json) {
		HttpResponse *response = http_response_json_custom_internal (status, json);
		if (response) {
			#ifdef HTTP_RESPONSE_DEBUG
			(void) printf ("\n%.*s", (int) response->header_len, (char *) response->header);
			(void) printf ("\n%.*s\n\n", (int) response->data_len, (char *) response->data);
			#endif
			retval = http_response_send_split (response, http_receive);
			http_response_delete (response);
		}
	}

	return retval;

}

static HttpResponse *http_response_json_custom_reference_internal (
	const http_status status,
	const char *json, const size_t json_len
) {

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (response) {
		response->status = status;

		// body
		response->data_ref = true;
		response->data = (char *) json;
		response->data_len = json_len;

		// header
		http_response_json_header (response);
	}

	return response;

}

// creates a http response with the defined status code
// and the body with a reference to a custom json
HttpResponse *http_response_json_custom_reference (
	const http_status status,
	const char *json, const size_t json_len
) {

	HttpResponse *response = NULL;
	if (json) {
		response = http_response_json_custom_reference_internal (
			status,
			json, json_len
		);

		(void) http_response_compile (response);
	}

	return response;

}

// creates and sends a http custom json reference response with the defined status code
// returns 0 on success, 1 on error
u8 http_response_json_custom_reference_send (
	const HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
) {

	u8 retval = 1;

	if (http_receive && json) {
		HttpResponse *response = http_response_json_custom_reference_internal (
			status,
			json, json_len
		);

		if (response) {
			#ifdef HTTP_RESPONSE_DEBUG
			(void) printf ("\n%.*s", (int) response->header_len, (char *) response->header);
			(void) printf ("\n%.*s\n\n", (int) response->data_len, (char *) response->data);
			#endif
			retval = http_response_send_split (response, http_receive);
			http_response_delete (response);
		}
	}

	return retval;

}

#pragma endregion

#pragma region main

static unsigned int http_responses_init_pool (void) {

	unsigned int retval = 1;

	producer->pool = pool_create (http_response_delete);
	if (producer->pool) {
		pool_set_create (producer->pool, http_response_new);
		pool_set_produce_if_empty (producer->pool, true);
		if (!pool_init (
			producer->pool, http_response_new, HTTP_RESPONSE_POOL_INIT
		)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init HTTP responses pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create HTTP responses pool!");
	}

	return retval;

}

unsigned int http_responses_init (
	const HttpCerver *http_cerver
) {

	unsigned int retval = 1;

	producer = http_response_producer_new (http_cerver);
	if (producer) {
		retval = http_responses_init_pool ();
	}

	return retval;

}

void http_responses_follow (
	const HttpCerver *ended, const HttpCerver *alive
) {

	if (producer && (http_response_producer_cerver () == ended)) {
		__atomic_store_n (&producer->http_cerver, alive, __ATOMIC_RELEASE);
	}

}

void http_responses_end (void) {

	http_response_producer_delete (producer);
	producer = NULL;

}

static const char http_response_service_unavailable[] = HTTP_RESPONSE_SERVICE_UNAVAILABLE;

// sends HTTP_RESPONSE_SERVICE_UNAVAILABLE without blocking
// returns 0 on success, 1 on error
u8 http_response_send_service_unavailable (const int sock_fd) {

	u8 retval = 1;

	if (sock_fd >= 0) {
		const size_t length = sizeof (http_response_service_unavailable) - 1;

		const ssize_t sent = send (
			sock_fd,
			http_response_service_unavailable, length,
			MSG_NOSIGNAL | MSG_DONTWAIT
		);

		retval = (sent == (ssize_t) length) ? 0 : 1;
	}

	return retval;

}

#pragma endregion
