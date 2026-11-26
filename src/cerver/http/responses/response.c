#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/core/cerver.h"
#include "cerver/core/start.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/network/socket.h"

#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/utils.h"
#include "cerver/http/headers/buffer.h"
#include "cerver/http/headers/header.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/conditional.h"
#include "cerver/http/responses/date.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/status.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/security/origin.h"

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

// the readers of the producer hold it while they read the origins of its cerver,
// so the cerver that ends is not released while they read it (http_responses_follow ())
static pthread_rwlock_t producer_lock = PTHREAD_RWLOCK_INITIALIZER;

// the HTTP cerver whose origins a response without a request takes, NULL without an HTTP cerver;
// it can change when its own one ends while another HTTP cerver is alive, so it is read with producer_lock
static const HttpCerver *http_response_producer_cerver (void) {

	return producer ? __atomic_load_n (&producer->http_cerver, __ATOMIC_ACQUIRE) : NULL;

}

// the HTTP cerver of a request, none without a request: a response compiled without one
// gets the default headers of the cerver that sends it (http_response_extra_defaults ())
static const HttpCerver *http_response_receive_cerver (const HttpReceive *http_receive) {

	return http_receive ? http_receive->http_cerver : NULL;

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

}

// the headers of a response are all empty when it has none,
// a recycled response was reset, so only a new one clears them
static void http_response_init (HttpResponse *response, const bool clear_headers) {

	response->status = HTTP_STATUS_OK;

	response->n_headers = 0;
	if (clear_headers) {
		(void) memset (
			response->headers, 0,
			HTTP_HEADERS_SIZE * sizeof (HttpHeader)
		);
	}

	response->header = NULL;
	response->header_len = 0;
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
	const bool recycled = (response != NULL);

	if (!response) response = (HttpResponse *) malloc (sizeof (HttpResponse));

	if (response) http_response_init (response, !recycled);

	return response;

}

void http_response_reset (HttpResponse *response) {

	response->status = HTTP_STATUS_OK;

	// only the headers that were added have values
	if (response->n_headers) {
		(void) memset (
			response->headers, 0,
			HTTP_HEADERS_SIZE * sizeof (HttpHeader)
		);
	}

	response->n_headers = 0;

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

// get a new HTTP response ready to be used,
// without an HTTP cerver there is no producer and no pool to take it from (it crashed)
HttpResponse *http_response_get (void) {

	return producer ? (HttpResponse *) pool_pop (producer->pool) : NULL;

}

// correctly disposes a HTTP response,
// a response that no pool takes is released (it was lost)
void http_response_return (HttpResponse *response) {

	if (response) {
		http_response_reset (response);

		if (!producer || pool_push (producer->pool, response)) {
			http_response_delete (response);
		}
	}

}

// sets the HTTP response's status code to be used in the header
void http_response_set_status (
	HttpResponse *response, const http_status status
) {

	if (response) response->status = status;

}

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_header (
	HttpResponse *response, const void *header, const size_t header_len
) {

	if (response) {
		http_response_header_release (response);

		if (header) {
			response->header = (void *) header;
			response->header_len = header_len;
		}
	}

}

// a header that was set is counted once,
// the one that does not fit is not a header of the response, that keeps the one it had
static u8 http_response_header_added (
	HttpResponse *response, const http_header type,
	const bool had, const u8 result
) {

	if (result) {
		cerver_log_warning (
			"The %s header does not fit in a header of a response, it was not added",
			http_header_string (type)
		);
	}

	else if (!had) response->n_headers += 1;

	return result;

}

// adds a new header to the response
// the headers will be handled when calling
// http_response_compile () to generate a continuos header buffer
// a header that does not fit in HTTP_HEADER_VALUE_SIZE is not added
// returns 0 on success, 1 on error
u8 http_response_add_header (
	HttpResponse *response,
	const http_header type, const char *actual_header
) {

	u8 retval = 1;

	if (response && actual_header && (type < HTTP_HEADERS_SIZE)) {
		const bool had = (response->headers[type].len > 0);

		retval = http_response_header_added (
			response, type, had,
			http_response_header_set_with_type (
				&response->headers[type], type, actual_header
			)
		);
	}

	return retval;

}

// works like http_response_add_header ()
// but generates the header values in the fly
u8 http_response_add_custom_header (
	HttpResponse *response,
	const http_header type, const char *format, ...
) {

	u8 retval = 1;

	if (response && format && (type < HTTP_HEADERS_SIZE)) {
		va_list args;
		va_start (args, format);

		const bool had = (response->headers[type].len > 0);

		retval = http_response_header_added (
			response, type, had,
			http_response_header_set_with_type_and_args (
				&response->headers[type], type,
				format, args
			)
		);

		va_end (args);
	}

	return retval;

}

// adds a "Content-Type" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_type_header (
	HttpResponse *response, const ContentType type
) {

	u8 retval = 1;

	if (response) {
		HttpHeader *header = &response->headers[HTTP_HEADER_CONTENT_TYPE];

		if (header->len <= 0) {
			response->n_headers += 1;
		}

		header->len = snprintf (
			header->value, HTTP_HEADER_VALUE_SIZE,
			"%s: %s\r\n",
			http_header_string (HTTP_HEADER_CONTENT_TYPE),
			http_content_type_mime (type)
		);

		retval = 0;
	}

	return retval;

}

// adds a "Content-Length" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_length_header (
	HttpResponse *response, const size_t length
) {

	u8 retval = 1;

	if (response) {
		HttpHeader *header = &response->headers[HTTP_HEADER_CONTENT_LENGTH];

		if (header->len <= 0) {
			response->n_headers += 1;
		}

		header->len = snprintf (
			header->value, HTTP_HEADER_VALUE_SIZE,
			"%s: %zu\r\n",
			http_header_string (HTTP_HEADER_CONTENT_LENGTH), length
		);

		retval = 0;
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

	return origin ? http_response_add_header (
		response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, origin->value
	) : 1;

}

// adds the domain as the allowed origin when it is in the whitelist of the cerver,
// without an HTTP cerver there is no whitelist
// returns 0 on success, 1 on error
static u8 http_response_add_whitelisted_origin (
	HttpResponse *response, const HttpCerver *http_cerver, const char *domain
) {

	u8 retval = 1;

	for (u8 idx = 0; domain && http_cerver && (idx < http_cerver->n_origins); idx++) {
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

// works like http_response_add_cors_header () but first
// checks if the domain matches any entry in the whitelist of the producer cerver
// returns 0 on success, 1 on error
u8 http_response_add_whitelist_cors_header (
	HttpResponse *response, const char *domain
) {

	(void) pthread_rwlock_rdlock (&producer_lock);

	const u8 retval = http_response_add_whitelisted_origin (response, http_response_producer_cerver (), domain);

	(void) pthread_rwlock_unlock (&producer_lock);

	return retval;

}

// works like http_response_add_whitelist_cors_header ()
// but takes a HttpOrigin instead of a c string
u8 http_response_add_whitelist_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
) {

	return origin ? http_response_add_whitelist_cors_header (
		response, origin->value
	) : 1;

}

// checks if the HTTP request's "Origin" header value
// matches any domain in the whitelist of the cerver of the request
// then adds an "Access-Control-Allow-Origin" header to the response
// returns 0 on success, 1 on error
u8 http_response_add_whitelist_cors_header_from_request (
	const HttpReceive *http_receive,
	HttpResponse *response
) {

	u8 retval = 1;

	if (http_receive && http_receive->request && http_receive->request->headers[HTTP_HEADER_ORIGIN]) {
		retval = http_response_add_whitelisted_origin (
			response, http_receive->http_cerver,
			http_receive->request->headers[HTTP_HEADER_ORIGIN]->str
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

	return bytes_range ? http_response_add_custom_header (
		response, HTTP_HEADER_CONTENT_RANGE,
		"bytes %ld-%ld/%ld",
		bytes_range->start, bytes_range->end, bytes_range->file_size
	) : 1;

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
// the data will be deleted when the response gets deleted,
// a reference that the response had is replaced without freeing it
void http_response_set_data (
	HttpResponse *response, const void *data, const size_t data_len
) {

	if (response) {
		if (response->data && !response->data_ref) free (response->data);

		response->data = NULL;
		response->data_len = 0;
		response->data_ref = false;

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

static void http_response_compile_single_header (
	HttpResponse *response
) {

	response->header = response->header_buffer;

	const int written = snprintf (
		response->header_buffer, HTTP_RESPONSE_MAIN_HEADER_SIZE,
		"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n"
		HTTP_RESPONSE_CONNECTION_CLOSE_HEADER
		"\r\n",
		(unsigned int) response->status, http_status_string (response->status),
		CERVER_VERSION
	);

	// a truncated header is never sent beyond its buffer
	response->header_len = (written < 0) ? 0 : (
		((size_t) written < HTTP_RESPONSE_MAIN_HEADER_SIZE) ?
			(size_t) written : HTTP_RESPONSE_MAIN_HEADER_SIZE - 1
	);

}

#define HTTP_RESPONSE_JSON_HEADER_FORMAT									\
	"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n"								\
	HTTP_RESPONSE_CONNECTION_CLOSE_HEADER									\
	"Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n"

// the header of a json body, in the buffer of the response when it fits
static void http_response_json_header (HttpResponse *response) {

	const unsigned int status = (unsigned int) response->status;
	const char *status_string = http_status_string (response->status);

	const int written = snprintf (
		response->header_buffer, HTTP_RESPONSE_HEADER_BUFFER_SIZE,
		HTTP_RESPONSE_JSON_HEADER_FORMAT,
		status, status_string, CERVER_VERSION, response->data_len
	);

	if ((written >= 0) && ((size_t) written < HTTP_RESPONSE_HEADER_BUFFER_SIZE)) {
		response->header = response->header_buffer;
		response->header_len = (size_t) written;
	}

	else {
		response->header = c_string_create (
			HTTP_RESPONSE_JSON_HEADER_FORMAT,
			status, status_string, CERVER_VERSION, response->data_len
		);

		response->header_len = response->header ? strlen ((const char *) response->header) : 0;
	}

}

// the default header of the cerver for a header that the response does not have, NULL when it has neither
static const HttpHeader *http_response_default_header (
	const HttpCerver *http_cerver, const u8 type
) {

	return (http_cerver && (http_cerver->response_headers[type].len > 0)) ?
		&http_cerver->response_headers[type] : NULL;

}

// the headers of the response, and the default headers of the cerver for the ones that it does not have
static void http_response_compile_multiple_headers (
	HttpResponse *response, const HttpCerver *http_cerver
) {

	char main_header[HTTP_RESPONSE_MAIN_HEADER_SIZE] = { 0 };
	(void) snprintf (
		main_header, HTTP_RESPONSE_MAIN_HEADER_SIZE,
		"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n"
		HTTP_RESPONSE_CONNECTION_CLOSE_HEADER,
		(unsigned int) response->status, http_status_string (response->status),
		CERVER_VERSION
	);

	size_t main_header_len = strlen (main_header);
	response->header_len = main_header_len;

	u8 i = 0;
	for (; i < HTTP_HEADERS_SIZE; i++) {
		const HttpHeader *default_header = http_response_default_header (http_cerver, i);

		if (response->headers[i].len > 0) {
			response->header_len += response->headers[i].len;
		}

		else if (default_header) {
			response->header_len += default_header->len;
		}
	}

	response->header_len += 2;	// \r\n

	// one more byte to always terminate the header with '\0',
	// in the buffer of the response when it fits
	const size_t header_size = response->header_len + 1;
	response->header = (header_size <= HTTP_RESPONSE_HEADER_BUFFER_SIZE) ?
		response->header_buffer : calloc (header_size, sizeof (char));

	if (!response->header) {
		response->header_len = 0;
		return;
	}

	char *end = (char *) response->header;
	(void) memcpy (end, main_header, main_header_len);
	end += main_header_len;
	for (i = 0; i < HTTP_HEADERS_SIZE; i++) {
		const HttpHeader *default_header = http_response_default_header (http_cerver, i);

		if (response->headers[i].len > 0) {
			(void) memcpy (end, response->headers[i].value, response->headers[i].len);
			end += response->headers[i].len;
		}

		else if (default_header) {
			(void) memcpy (end, default_header->value, default_header->len);
			end += default_header->len;
		}
	}

	// append header end
	*end = '\r';
	end += 1;
	*end = '\n';
	end += 1;
	*end = '\0';

}

// uses the exiting response's values to correctly
// create a HTTP header in a continuos buffer
// ready to be sent by the response,
// with the default headers of the cerver, none for a NULL cerver
void http_response_compile_header_with_cerver (
	HttpResponse *response, const HttpCerver *http_cerver
) {

	if (response->n_headers || (http_cerver && http_cerver->n_response_headers)) {
		http_response_compile_multiple_headers (response, http_cerver);
	}

	// create the default header
	else {
		http_response_compile_single_header (response);
	}

}

// the header without a request has no default headers: the cerver that sends it adds its own ones
void http_response_compile_header (HttpResponse *response) {

	http_response_compile_header_with_cerver (response, NULL);

}

// merge the response header and the data into the final response,
// a header that is not compiled takes the default headers of the cerver
// returns 0 on success, 1 on error
u8 http_response_compile_with_cerver (
	HttpResponse *response, const HttpCerver *http_cerver
) {

	u8 retval = 1;

	if (response) {
		if (!response->header) {
			http_response_compile_header_with_cerver (response, http_cerver);
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

			// a response without its header yet has only its data (memcpy () does not take a NULL source)
			if (response->header) (void) memcpy (end, response->header, response->header_len);

			if (response->data) {
				end += response->header_len;
				(void) memcpy (end, response->data, response->data_len);
			}

			retval = 0;
		}
	}

	return retval;

}

// a response compiled without a request has no default headers: the cerver that sends it adds its own ones
// returns 0 on success, 1 on error
u8 http_response_compile (HttpResponse *response) {

	return http_response_compile_with_cerver (response, NULL);

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

	// a client that did not read in all the time of the response is taken as gone
	(void) socket_end_on_send_timeout (http_receive->cr->connection->socket, error);

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

#pragma endregion

#pragma region message

static const char http_response_header_end[] = { "\r\n" };
static const size_t http_response_header_end_length = 2;

// the empty line that ends a compiled header
static const char http_response_headers_end[] = { "\r\n\r\n" };
static const size_t http_response_headers_end_length = 4;

static const char http_response_header_separator[] = { ": " };
static const size_t http_response_header_separator_length = 2;

static const char http_response_header_name_separator = ':';

static const char http_response_vary_origin[] = { "Origin" };
static const char http_response_vary_encoding[] = { "Accept-Encoding" };
static const char http_response_vary_origin_and_encoding[] = { "Origin, Accept-Encoding" };

static const char http_response_vary_name[] = { "Vary" };

#define HTTP_RESPONSE_NOT_MODIFIED_MAIN_HEADER							\
	"HTTP/1.1 304 Not Modified\r\nServer: Cerver/" CERVER_VERSION "\r\n"	\
	HTTP_RESPONSE_CONNECTION_CLOSE_HEADER

static const char http_response_not_modified_main_header[] = { HTTP_RESPONSE_NOT_MODIFIED_MAIN_HEADER };

// the headers of a response that a 304 keeps (RFC 9110 section 15.4.5)
static const http_header http_response_not_modified_headers[] = {
	HTTP_HEADER_CACHE_CONTROL,
	HTTP_HEADER_CONTENT_LOCATION,
	HTTP_HEADER_ETAG,
	HTTP_HEADER_EXPIRES
};

static const size_t http_response_n_not_modified_headers = 4;

// the headers that cerver adds to a response when it sends it
typedef struct HttpResponseExtra {

	size_t length;
	char headers[HTTP_RESPONSE_EXTRA_HEADERS_SIZE];

} HttpResponseExtra;

// a response that is being sent,
// its header does not have the empty line that ends it
typedef struct HttpResponseMessage {

	http_status status;

	const char *header;
	size_t header_length;

	const void *body;
	size_t body_length;

	char etag[HTTP_ETAG_SIZE];
	size_t etag_length;
	bool etag_in_header;

	bool compress;
	bool compressed;

	// the default headers of the cerver of the request that the header does not have,
	// sent from the cerver as they are
	const HttpHeader *defaults[HTTP_HEADERS_SIZE];
	u8 n_defaults;

	HttpResponseExtra extra;

} HttpResponseMessage;

static void http_response_extra_add (
	HttpResponseExtra *extra,
	const char *name, const size_t name_length,
	const char *value, const size_t value_length
) {

	const size_t needed = name_length + http_response_header_separator_length
		+ value_length + http_response_header_end_length;

	if ((extra->length + needed) <= HTTP_RESPONSE_EXTRA_HEADERS_SIZE) {
		char *end = extra->headers + extra->length;
		(void) memcpy (end, name, name_length);
		end += name_length;
		(void) memcpy (end, http_response_header_separator, http_response_header_separator_length);
		end += http_response_header_separator_length;
		(void) memcpy (end, value, value_length);
		end += value_length;
		(void) memcpy (end, http_response_header_end, http_response_header_end_length);

		extra->length += needed;
	}

	else {
		cerver_log_warning (
			"http_response_send () - The %.*s header does not fit in the response",
			(int) name_length, name
		);
	}

}

static void http_response_extra_add_header (
	HttpResponseExtra *extra, const http_header type,
	const char *value, const size_t value_length
) {

	const char *name = http_header_string (type);

	http_response_extra_add (extra, name, strlen (name), value, value_length);

}

static bool http_response_header_has (
	const char *header, const size_t header_length, const http_header type
) {

	const char *name = http_header_string (type);

	return http_headers_buffer_find (header, header_length, name, strlen (name), NULL, NULL);

}

static void http_response_extra_date (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	if (
		http_receive->http_cerver->date_header
		&& !http_response_header_has (message->header, message->header_length, HTTP_HEADER_DATE)
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
		&& !http_response_header_has (message->header, message->header_length, HTTP_HEADER_REQUEST_ID)
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
		&& !http_response_header_has (
			message->header, message->header_length, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN
		)
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
		&& !http_headers_buffer_find (
			message->header, message->header_length,
			http_response_vary_name, sizeof (http_response_vary_name) - 1, NULL, NULL
		)
	) {
		http_response_extra_add (
			&message->extra,
			http_response_vary_name, sizeof (http_response_vary_name) - 1,
			vary, strlen (vary)
		);
	}

}

// the headers that every response of the request has
static void http_response_extra_common (
	const HttpReceive *http_receive, HttpResponseMessage *message
) {

	http_response_extra_date (http_receive, message);

	http_response_extra_request_id (http_receive, message);

	const HttpEndpoint *endpoint = http_receive->endpoint;
	const bool cors = endpoint && endpoint->resolved.cors;
	const bool compression = endpoint && endpoint->resolved.compression
		&& (http_receive->dispatch == HTTP_RECEIVE_DISPATCH_ENDPOINT);

	if (cors) http_response_extra_cors (http_receive, message);

	http_response_extra_vary (message, cors, compression);

}

// a 304 keeps only some of the headers of the response that it replaces
static bool http_response_not_modified_keeps_type (const http_header type) {

	bool keeps = false;
	for (size_t i = 0; !keeps && (i < http_response_n_not_modified_headers); i++) {
		keeps = (http_response_not_modified_headers[i] == type);
	}

	return keeps;

}

// the default headers of the cerver of the request that the header does not have, so a response that was
// compiled without a request (http_response_compile ()) has the ones of the cerver that sends it
static void http_response_extra_defaults (
	const HttpReceive *http_receive, HttpResponseMessage *message, const bool not_modified
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	message->n_defaults = 0;
	for (u8 type = 0; http_cerver->n_response_headers && (type < HTTP_HEADERS_SIZE); type++) {
		const bool added = (http_cerver->response_headers[type].len > 0)
			&& (!not_modified || http_response_not_modified_keeps_type ((http_header) type))
			&& !http_response_header_has (message->header, message->header_length, (http_header) type);

		if (added) {
			message->defaults[message->n_defaults] = &http_cerver->response_headers[type];
			message->n_defaults += 1;
		}
	}

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
	const char *content_type_name = http_header_string (HTTP_HEADER_CONTENT_TYPE);

	return accept_encoding
		&& http_compression_accepts_gzip (accept_encoding->str, accept_encoding->len)
		&& !http_response_header_has (message->header, message->header_length, HTTP_HEADER_CONTENT_ENCODING)
		&& http_headers_buffer_find (
			message->header, message->header_length,
			content_type_name, strlen (content_type_name),
			&content_type, &content_type_length
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
	const char *name = http_header_string (HTTP_HEADER_ETAG);

	message->etag_in_header = http_headers_buffer_find (
		message->header, message->header_length, name, strlen (name), &value, &value_length
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
			message->header = header;
			message->header_length = header_length;
			message->body = compressed;
			message->body_length = compressed_length;
			message->compressed = true;
		}
	}

}

static void http_response_extra_compression (HttpResponseMessage *message) {

	char length[HTTP_RESPONSE_CONTENT_LENGTH_SIZE] = { 0 };
	const int written = snprintf (length, HTTP_RESPONSE_CONTENT_LENGTH_SIZE, "%zu", message->body_length);

	http_response_extra_add_header (
		&message->extra, HTTP_HEADER_CONTENT_LENGTH,
		length, (written > 0) ? (size_t) written : 0
	);

	http_response_extra_add_header (
		&message->extra, HTTP_HEADER_CONTENT_ENCODING,
		HTTP_COMPRESSION_ENCODING, sizeof (HTTP_COMPRESSION_ENCODING) - 1
	);

}

// a part of the message, after the ones that it already has
static void http_response_message_part (
	struct iovec *parts, int *n_parts, const void *base, const size_t length
) {

	parts[*n_parts].iov_base = (void *) base;
	parts[*n_parts].iov_len = length;
	*n_parts += 1;

}

// the default headers of the cerver go between the header and the headers that cerver adds
static size_t http_response_message_defaults (
	const HttpResponseMessage *message, struct iovec *parts, int *n_parts
) {

	size_t length = 0;
	for (u8 i = 0; i < message->n_defaults; i++) {
		const size_t default_length = (size_t) message->defaults[i]->len;

		http_response_message_part (parts, n_parts, message->defaults[i]->value, default_length);
		length += default_length;
	}

	return length;

}

static u8 http_response_message_send (
	const HttpReceive *http_receive, const HttpResponseMessage *message,
	const bool with_body
) {

	const size_t body_length = (with_body && !http_response_is_head (http_receive) && message->body) ?
		message->body_length : 0;

	struct iovec parts[HTTP_RESPONSE_MESSAGE_PARTS + HTTP_HEADERS_SIZE];
	int n_parts = 0;

	http_response_message_part (parts, &n_parts, message->header, message->header_length);
	const size_t defaults_length = http_response_message_defaults (message, parts, &n_parts);
	http_response_message_part (parts, &n_parts, message->extra.headers, message->extra.length);
	http_response_message_part (parts, &n_parts, http_response_header_end, http_response_header_end_length);
	if (body_length) http_response_message_part (parts, &n_parts, message->body, body_length);

	const u8 retval = http_response_send_vector (http_receive, parts, n_parts);

	if (!retval) {
		http_response_sent (
			http_receive, message->status,
			message->header_length + defaults_length + message->extra.length
				+ http_response_header_end_length + body_length
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

	const size_t main_length = sizeof (http_response_not_modified_main_header) - 1;

	char *copy = arena ? (char *) arena_alloc (arena, main_length + header_length + 1) : NULL;
	if (copy) {
		char *end = copy;
		(void) memcpy (end, http_response_not_modified_main_header, main_length);
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
		message->header = header;
		message->header_length = header_length;
		message->body = NULL;
		message->body_length = 0;
		message->extra.length = 0;

		if (!message->etag_in_header) {
			http_response_extra_add_header (
				&message->extra, HTTP_HEADER_ETAG, message->etag, message->etag_length
			);
		}

		// a 304 has the Vary of the response that it replaces, and the defaults that it keeps
		http_response_extra_common (http_receive, message);
		http_response_extra_defaults (http_receive, message, true);

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
	const void *header, const size_t header_len,
	const void *data, const size_t data_len
) {

	message->status = status;

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

	message->n_defaults = 0;

	message->extra.length = 0;

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
	http_response_extra_defaults (http_receive, message, false);

	if ((options & HTTP_RESPONSE_SEND_OPTION_STREAM) && http_receive->stream.chunked) {
		http_response_extra_add_header (
			&message->extra, HTTP_HEADER_TRANSFER_ENCODING,
			HTTP_STREAM_CHUNKED_ENCODING, sizeof (HTTP_STREAM_CHUNKED_ENCODING) - 1
		);
	}

}

u8 http_response_send_message (
	const HttpReceive *http_receive, const http_status status,
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
		http_response_message_init (&message, status, header, header_len, data, data_len);

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
		http_response_message_init (&message, HTTP_STATUS_OK, header, header_len, NULL, 0);

		// the entity tag is copied from the header
		message.etag_in_header = true;

		retval = http_response_send_not_modified (http_receive, &message);
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
	const void *header, const size_t header_len,
	const void *data, const size_t data_len
) {

	return http_response_send_message (
		http_receive, status, header, header_len, data, data_len,
		HTTP_RESPONSE_SEND_OPTION_TRANSFORM
	);

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

	if (response && http_receive && http_receive->cr->connection) {
		if (response->res) {
			// the compiled buffer starts with the header and continues with the data
			const bool split = response->header_len && (response->header_len <= response->res_len);
			const size_t header_len = split ? response->header_len : response->res_len;

			retval = http_response_send_parts (
				http_receive, response->status,
				response->res, header_len,
				(const char *) response->res + header_len, response->res_len - header_len
			);
		}

		else {
			// with the default headers of the cerver of the request
			if (!response->header) http_response_compile_header_with_cerver (response, http_receive->http_cerver);

			if (response->header) {
				retval = http_response_send_parts (
					http_receive, response->status,
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

	if (response && http_receive && http_receive->cr->connection) {
		if (response->header && response->data) {
			retval = http_response_send_parts (
				http_receive, response->status,
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
		if (!http_response_compile_with_cerver (response, http_response_receive_cerver (http_receive))) {
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

// the header was already sent, so only the bytes of the file are added to the stats
static u8 http_response_send_file_actual (
	const HttpReceive *http_receive,
	const char *filename
) {

	u8 retval = 1;

	int file_fd = open (filename, O_RDONLY);
	if (file_fd > -1) {
		size_t total_sent = 0;

		retval = http_response_send_file_pipe (
			http_receive, file_fd, NULL, http_response_pipe_whole_file, &total_sent
		);

		http_response_sent_bytes (http_receive, total_sent);

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

	http_response_compile_multiple_headers (response, http_receive->http_cerver);

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
			http_receive, status, response->header, response->header_len, NULL, 0, 0
		);
	}

	// pipe header
	else if (!http_response_send_message (
		http_receive, status, response->header, response->header_len, NULL, 0,
		HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY
	)) {
		retval = http_response_send_file_actual (http_receive, filename);
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

static u8 http_response_render_send (
	const HttpReceive *http_receive,
	const http_status status,
	const char *header, const size_t header_len,
	const char *data, const size_t data_len
) {

	return http_receive->cr->connection ? http_response_send_parts (
		http_receive, status, header, header_len, data, data_len
	) : 1;

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
		char header[HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE] = { 0 };
		size_t header_len = (size_t) snprintf (
			header, HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE - 1,
			"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n"
			HTTP_RESPONSE_CONNECTION_CLOSE_HEADER
			"Content-Type: text/html; charset=UTF-8\r\nContent-Length: %zu\r\n\r\n",
			(unsigned int) status,
			http_status_string (status),
			CERVER_VERSION,
			text_len
		);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", header);
		#endif

		retval = http_response_render_send (
			http_receive,
			status,
			header, header_len,
			text, text_len
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
		char header[HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE] = { 0 };
		size_t header_len = (size_t) snprintf (
			header, HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE - 1,
			"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\n"
			HTTP_RESPONSE_CONNECTION_CLOSE_HEADER
			"Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
			(unsigned int) status,
			http_status_string (status),
			CERVER_VERSION,
			json_len
		);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", header);
		#endif

		retval = http_response_render_send (
			http_receive,
			status,
			header, header_len,
			json, json_len
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
		content_type = http_content_type_with_extension (ext);
	}

	return content_type;

}

// the headers of the whole video, a 200 has no Content-Range
static void http_response_add_video_whole_headers (
	HttpResponse *response,
	const ContentType content_type, const BytesRange *bytes_range
) {

	(void) http_response_add_header (response, HTTP_HEADER_ACCEPT_RANGES, "bytes");

	(void) http_response_add_content_type_header (response, content_type);

	(void) http_response_add_content_length_header (response, (const size_t) bytes_range->chunk_size);

}

// pipes the bytes of the range after the header was sent
static u8 http_response_handle_video_pipe (
	const HttpReceive *http_receive,
	const char *filename,
	BytesRange *bytes_range
) {

	u8 retval = 1;

	int video_fd = open (filename, O_RDONLY);
	if (video_fd > -1) {
		size_t total_sent = 0;
		retval = http_response_send_file_pipe (
			http_receive, video_fd,
			&bytes_range->start, (size_t) bytes_range->chunk_size,
			&total_sent
		);

		// the header was already added to the stats
		http_response_sent_bytes (http_receive, total_sent);

		(void) close (video_fd);
	}

	else {
		cerver_log_error (
			"http_response_handle_video () - Failed to open %s",
			filename
		);
	}

	return retval;

}

// a 206 with the range, or a 200 with the whole video
static u8 http_response_handle_video_internal (
	const HttpReceive *http_receive,
	const char *filename,
	BytesRange *bytes_range,
	const http_status status
) {

	u8 retval = 1;

	HttpResponse *response = http_response_get ();
	if (response) {
		response->status = status;

		const ContentType content_type = http_response_handle_video_content (filename);
		if (status == HTTP_STATUS_PARTIAL_CONTENT) http_response_add_video_headers (response, content_type, bytes_range);
		else http_response_add_video_whole_headers (response, content_type, bytes_range);

		http_response_compile_multiple_headers (response, http_receive->http_cerver);

		if (!http_response_send_message (
			http_receive, response->status, response->header, response->header_len, NULL, 0,
			HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY
		)) {
			// an empty video has no bytes to pipe
			retval = bytes_range->chunk_size ?
				http_response_handle_video_pipe (http_receive, filename, bytes_range) : 0;
		}

		http_response_return (response);
	}

	return retval;

}

// a range that starts after the end of the video gets a 416 with the size of the video
static u8 http_response_handle_video_unsatisfiable (
	const HttpReceive *http_receive, const BytesRange *bytes_range
) {

	u8 retval = 1;

	HttpResponse *response = http_response_get ();
	if (response) {
		response->status = HTTP_STATUS_RANGE_NOT_SATISFIABLE;

		(void) http_response_add_custom_header (
			response, HTTP_HEADER_CONTENT_RANGE, "bytes */%ld", bytes_range->file_size
		);

		(void) http_response_add_content_length_header (response, 0);

		http_response_compile_multiple_headers (response, http_receive->http_cerver);

		retval = http_response_send_message (
			http_receive, response->status, response->header, response->header_len, NULL, 0,
			HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY
		);

		http_response_return (response);
	}

	return retval;

}

// bytes=N- sends at most HTTP_RESPONSE_VIDEO_CHUNK_SIZE bytes, and never after the last one of the video
static void http_response_video_range_open (BytesRange *bytes_range) {

	const long last = bytes_range->file_size - 1;
	const long end = bytes_range->start + HTTP_RESPONSE_VIDEO_CHUNK_SIZE - 1;

	bytes_range->end = (end < last) ? end : last;

}

// bytes=N-M includes M, and ends with the video (it sent one byte less and read after its end)
static void http_response_video_range_full (BytesRange *bytes_range) {

	const long last = bytes_range->file_size - 1;
	if (bytes_range->end > last) bytes_range->end = last;

}

// the last bytes of the file, all of it when the file is smaller than the suffix:
// the end of the range was the number of bytes, it becomes the last byte of the video
static void http_response_video_range_suffix (BytesRange *bytes_range) {

	const long suffix = (bytes_range->end < bytes_range->file_size) ?
		bytes_range->end : bytes_range->file_size;

	bytes_range->start = bytes_range->file_size - suffix;
	bytes_range->end = bytes_range->file_size - 1;

}

// the range of the video with its last byte, its size is what is sent
static void http_response_video_range_resolve (BytesRange *bytes_range) {

	switch (bytes_range->type) {
		case HTTP_RANGE_TYPE_EMPTY:
		case HTTP_RANGE_TYPE_FIRST:
			http_response_video_range_open (bytes_range);
			break;

		case HTTP_RANGE_TYPE_FULL:
			http_response_video_range_full (bytes_range);
			break;

		case HTTP_RANGE_TYPE_SUFFIX:
			http_response_video_range_suffix (bytes_range);
			break;

		// without a range (or with an invalid one) the whole video
		default:
			bytes_range->start = 0;
			bytes_range->end = bytes_range->file_size - 1;
			break;
	}

	bytes_range->chunk_size = bytes_range->end - bytes_range->start + 1;

}

// RFC 9110: without a valid range the whole video with 200, a range inside the video with 206,
// and one that starts after its end with 416 (it sent a 206 for everything)
static u8 http_response_handle_video_answer (
	const HttpReceive *http_receive,
	const char *filename,
	BytesRange *bytes_range
) {

	u8 retval = 1;

	if (bytes_range->type == HTTP_RANGE_TYPE_NONE) {
		retval = http_response_handle_video_internal (http_receive, filename, bytes_range, HTTP_STATUS_OK);
	}

	else if (bytes_range->start < bytes_range->file_size) {
		retval = http_response_handle_video_internal (
			http_receive, filename, bytes_range, HTTP_STATUS_PARTIAL_CONTENT
		);
	}

	else {
		retval = http_response_handle_video_unsatisfiable (http_receive, bytes_range);
	}

	return retval;

}

// bytes=N-M with M before N is not a range, the request gets the whole video
static void http_response_video_range_check (BytesRange *bytes_range) {

	if ((bytes_range->type == HTTP_RANGE_TYPE_FULL) && (bytes_range->end < bytes_range->start)) {
		bytes_range->type = HTTP_RANGE_TYPE_NONE;
	}

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
		BytesRange bytes_range = {
			.file_size = filestats.st_size,
			.type = HTTP_RANGE_TYPE_NONE,
			.start = 0, .end = 0,
			.chunk_size = 0
		};

		(void) http_request_get_bytes_range (http_receive->request, &bytes_range);

		http_response_video_range_check (&bytes_range);
		http_response_video_range_resolve (&bytes_range);

		retval = http_response_handle_video_answer (http_receive, filename, &bytes_range);
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

// a reader of the origins of the cerver that ends finishes before it is replaced (and released)
void http_responses_follow (
	const HttpCerver *ended, const HttpCerver *alive
) {

	(void) pthread_rwlock_wrlock (&producer_lock);

	if (producer && (http_response_producer_cerver () == ended)) {
		__atomic_store_n (&producer->http_cerver, alive, __ATOMIC_RELEASE);
	}

	(void) pthread_rwlock_unlock (&producer_lock);

}

void http_responses_end (void) {

	(void) pthread_rwlock_wrlock (&producer_lock);

	http_response_producer_delete (producer);
	producer = NULL;

	(void) pthread_rwlock_unlock (&producer_lock);

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
