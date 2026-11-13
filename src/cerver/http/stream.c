#include <stdio.h>
#include <string.h>

#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/response.h"
#include "cerver/http/stream.h"

#include "cerver/utils/log.h"

static const char http_stream_chunk_end[] = { "\r\n" };
static const size_t http_stream_chunk_end_length = 2;

static const char http_stream_last_chunk[] = { HTTP_STREAM_LAST_CHUNK };
static const size_t http_stream_last_chunk_length = sizeof (HTTP_STREAM_LAST_CHUNK) - 1;

#define HTTP_STREAM_CHUNK_HEADER_FORMAT		"%zx\r\n"

// the first version that has chunked transfer coding
#define HTTP_STREAM_CHUNKED_MAJOR			1
#define HTTP_STREAM_CHUNKED_MINOR			1

// the version of the request, a receive whose parser did not parse a request has 0.0
static bool http_stream_is_chunked (const HttpReceive *http_receive) {

	const uint8_t major = llhttp_get_http_major (&http_receive->parser);
	const uint8_t minor = llhttp_get_http_minor (&http_receive->parser);

	return (major > HTTP_STREAM_CHUNKED_MAJOR)
		|| ((major == HTTP_STREAM_CHUNKED_MAJOR) && (minor >= HTTP_STREAM_CHUNKED_MINOR));

}

static bool http_stream_is_head (const HttpReceive *http_receive) {

	return http_receive->head
		|| (http_receive->request && (http_receive->request->method == REQUEST_METHOD_HEAD));

}

u8 http_response_stream_begin (
	const HttpReceive *http_receive, HttpResponse *response
) {

	u8 retval = 1;

	HttpReceive *receive = (HttpReceive *) http_receive;
	if (receive && response && receive->cr->connection) {
		if (receive->stream.state != HTTP_STREAM_STATE_NONE) {
			cerver_log_error ("http_response_stream_begin () - The stream was already started");
		}

		else if (http_response_header_count (response, HTTP_HEADER_CONTENT_LENGTH)) {
			cerver_log_error ("http_response_stream_begin () - A stream can not have a Content-Length");
		}

		else {
			if (!response->header) http_response_compile_header (response);

			if (response->header) {
				receive->stream.chunked = http_stream_is_chunked (receive);
				receive->stream.state = HTTP_STREAM_STATE_STARTED;

				retval = http_response_send_message (
					receive, response->status,
					response->header_is_raw ? NULL : &response->headers,
					response->header, response->header_len, NULL, 0,
					HTTP_RESPONSE_SEND_OPTION_STREAM
				);

				// a stream whose header was not sent can not continue
				if (retval) receive->stream.state = HTTP_STREAM_STATE_FAILED;
			}
		}
	}

	return retval;

}

static u8 http_stream_write_chunk (
	const HttpReceive *http_receive,
	const void *data, const size_t length
) {

	char chunk_header[HTTP_STREAM_CHUNK_HEADER_SIZE] = { 0 };
	const int written = snprintf (
		chunk_header, HTTP_STREAM_CHUNK_HEADER_SIZE, HTTP_STREAM_CHUNK_HEADER_FORMAT, length
	);

	struct iovec parts[HTTP_STREAM_CHUNK_PARTS];
	parts[0].iov_base = chunk_header;
	parts[0].iov_len = (written > 0) ? (size_t) written : 0;
	parts[1].iov_base = (void *) data;
	parts[1].iov_len = length;
	parts[2].iov_base = (void *) http_stream_chunk_end;
	parts[2].iov_len = http_stream_chunk_end_length;

	return http_response_send_pieces (http_receive, parts, HTTP_STREAM_CHUNK_PARTS);

}

u8 http_response_stream_write (
	const HttpReceive *http_receive,
	const void *data, const size_t length
) {

	u8 retval = 1;

	if (http_receive && (http_receive->stream.state == HTTP_STREAM_STATE_STARTED)) {
		if (!data || !length || http_stream_is_head (http_receive)) {
			retval = 0;
		}

		else if (http_receive->stream.chunked) {
			retval = http_stream_write_chunk (http_receive, data, length);
		}

		else {
			struct iovec part;
			part.iov_base = (void *) data;
			part.iov_len = length;

			retval = http_response_send_pieces (http_receive, &part, 1);
		}

		if (retval) ((HttpReceive *) http_receive)->stream.state = HTTP_STREAM_STATE_FAILED;
	}

	// the client of a failed stream is gone, it is not an error of the handler
	else if (!http_receive || (http_receive->stream.state != HTTP_STREAM_STATE_FAILED)) {
		cerver_log_error ("http_response_stream_write () - The stream was not started");
	}

	return retval;

}

u8 http_response_stream_end (
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	HttpReceive *receive = (HttpReceive *) http_receive;
	if (receive) {
		if (receive->stream.state == HTTP_STREAM_STATE_STARTED) {
			receive->stream.state = HTTP_STREAM_STATE_ENDED;

			retval = 0;
			if (receive->stream.chunked && !http_stream_is_head (receive)) {
				struct iovec part;
				part.iov_base = (void *) http_stream_last_chunk;
				part.iov_len = http_stream_last_chunk_length;

				retval = http_response_send_pieces (receive, &part, 1);

				if (retval) receive->stream.state = HTTP_STREAM_STATE_FAILED;

				// the last chunk is the end of the response
				else receive->framed = true;
			}
		}

		// ending a stream twice is not an error
		else if (receive->stream.state == HTTP_STREAM_STATE_ENDED) {
			retval = 0;
		}
	}

	return retval;

}

bool http_response_stream_is_active (
	const HttpReceive *http_receive
) {

	return http_receive && (http_receive->stream.state == HTTP_STREAM_STATE_STARTED);

}
