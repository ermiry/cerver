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

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/header.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/http_parser.h"
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

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the optional whitespace after the value of a header
static const char http_header_value_space = ' ';
static const char http_header_value_tab = '\t';

// the parts of the target of a request
static const char http_target_scheme_symbols[] = { "+-." };
static const char http_target_authority_start[] = { "://" };
static const size_t http_target_authority_start_len = 3;
static const char http_target_authority_end[] = { "/?#" };
static const char http_target_query_start = '?';
static const char http_target_fragment_start = '#';
static const char http_target_root[] = { "/" };
static const size_t http_target_root_len = 1;

#pragma region parser

// sends a response before the request was completed
// and marks it, so the rest of the request is not handled
// returns 1 to be used as the return value of a parser callback
int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
) {

	if (!http_receive->responded) {
		(void) http_response_send (response, http_receive);

		http_receive->responded = true;
		http_receive->receive_status = status;

		// its readers (the stats, the metrics) load it without the mutex of the cerver, like every counter
		if (status == HTTP_RECEIVE_STATUS_LIMIT) {
			CERVER_STATS_ADD (http_receive->http_cerver->n_limited_requests, 1);
		}
	}

	return 1;

}

// keeps the string's memory but empties its contents
void http_receive_buffer_reset (String *buffer) {

	if (buffer) {
		buffer->len = 0;
		if (buffer->str) buffer->str[0] = '\0';
	}

}

// adds a piece of a token to its buffer
// returns 0 on success, 1 on error
static inline int http_receive_buffer_append (
	String **buffer, const char *at, const size_t length
) {

	if (!*buffer) *buffer = str_new (NULL);

	return *buffer ? str_append_bytes (*buffer, at, length) : 1;

}

// splits the url that was received into the request's path and query
// the url, the query and the headers of a request in an arena are created in it
static String *http_receive_string_create (
	const HttpReceive *http_receive, const char *bytes, const size_t length
) {

	return http_receive->request->arena ?
		str_create_from_bytes_arena (http_receive->request->arena, bytes, length) :
		str_create_from_bytes (bytes, length);

}

// the length of the scheme and of the authority of a target in absolute form
// (http://host:port/path?query, RFC 9112 section 3.2.2), that a proxy sends:
// what follows them is the path and the query of the request
// returns 0 for a target in any other form
static size_t http_receive_target_origin_length (const char *target, const size_t target_len) {

	size_t length = 0;

	// the scheme has letters, digits and a few symbols before its separator
	size_t scheme_len = 0;
	while (
		(scheme_len < target_len)
		&& (isalnum ((unsigned char) target[scheme_len]) || strchr (http_target_scheme_symbols, target[scheme_len]))
		&& target[scheme_len]
	) {
		scheme_len += 1;
	}

	if (
		scheme_len
		&& ((target_len - scheme_len) >= http_target_authority_start_len)
		&& !memcmp (target + scheme_len, http_target_authority_start, http_target_authority_start_len)
	) {
		length = scheme_len + http_target_authority_start_len;

		// the authority ends where the path or the query start
		while ((length < target_len) && !strchr (http_target_authority_end, target[length])) {
			length += 1;
		}
	}

	return length;

}

u8 http_receive_close_url (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;

	bool closed = true;

	if (!request->url) {
		const String *url = http_receive->url_buffer;
		const char *target = (url && url->str) ? url->str : "";
		const size_t target_len = url ? url->len : 0;

		const size_t origin_len = http_receive_target_origin_length (target, target_len);
		const char *start = target + origin_len;
		size_t url_len = target_len - origin_len;

		// a fragment is never part of the path nor of the query
		const char *fragment = (const char *) memchr (start, http_target_fragment_start, url_len);
		if (fragment) url_len = (size_t) (fragment - start);

		const char *question = (const char *) memchr (start, http_target_query_start, url_len);
		const size_t path_len = question ? (size_t) (question - start) : url_len;

		// an absolute target without a path is the root
		request->url = (origin_len && !path_len) ?
			http_receive_string_create (http_receive, http_target_root, http_target_root_len) :
			http_receive_string_create (http_receive, start, path_len);

		// the query does not include the '?'
		if (question) {
			request->query = http_receive_string_create (
				http_receive, question + 1, url_len - path_len - 1
			);
		}

		// the url and the query are used without a check after the headers
		closed = request->url && (!question || request->query);
	}

	return closed ? 0 : 1;

}

// these headers must appear only once (RFC 9110 section 5.3)
static inline bool http_header_is_unique (const http_header type) {

	return (type == HTTP_HEADER_HOST)
		|| (type == HTTP_HEADER_CONTENT_TYPE)
		|| (type == HTTP_HEADER_CONTENT_LENGTH)
		|| (type == HTTP_HEADER_AUTHORIZATION);

}

static int http_header_values_join_arena (
	Arena *arena, String **header, const char *value, const size_t value_len
);

// the value of a custom header fits below the limit of its cerver, 0 is no limit
static inline bool http_custom_header_value_fits (
	const size_t value_len, const size_t max_value_size
) {

	return !max_value_size || (value_len < max_value_size);

}

// joins a new value to a custom header that was already received
// returns 0 on success, 1 if the joined value does not fit
static int http_receive_join_custom_header (
	HttpRequest *request,
	HttpCustomHeader *existing, const char *value, const size_t value_len,
	const size_t max_value_size
) {

	int retval = 1;

	const size_t new_len = existing->value->len
		+ HTTP_HEADER_VALUES_SEPARATOR_LEN + value_len;

	Arena *arena = http_request_get_arena (request);
	if (arena && http_custom_header_value_fits (new_len, max_value_size)) {
		retval = http_header_values_join_arena (arena, &existing->value, value, value_len);
	}

	return retval;

}

// adds a new custom header to the request
// returns 0 on success, 1 if the value does not fit
static int http_receive_add_custom_header (
	HttpRequest *request,
	const String *field, const char *value, const size_t value_len,
	const size_t max_value_size
) {

	int retval = 1;

	Arena *arena = http_request_get_arena (request);
	if (arena && http_custom_header_value_fits (value_len, max_value_size)) {
		HttpCustomHeader *previous = request->current_custom_header;
		http_request_set_current_custom_header (request, field->str);

		HttpCustomHeader *custom_header = request->current_custom_header;
		if (custom_header && (custom_header != previous)) {
			retval = (int) http_custom_header_set_value (arena, custom_header, value, value_len);
		}
	}

	return retval;

}

// saves a custom header, a repeated one is joined with HTTP_HEADER_VALUES_SEPARATOR
// returns 0 on success, 1 if the header does not fit
static int http_receive_close_custom_header (
	HttpRequest *request,
	const String *field, const char *value, const size_t value_len,
	const size_t max_value_size
) {

	if (field->len >= HTTP_CUSTOM_HEADER_SIZE) return 1;

	HttpCustomHeader *existing = http_request_custom_header_find (request, field->str);

	return existing ?
		http_receive_join_custom_header (request, existing, value, value_len, max_value_size) :
		http_receive_add_custom_header (request, field, value, value_len, max_value_size);

}

// joins a new value to a header that was already received
// returns 0 on success, 1 on error
static inline int http_header_values_join (
	String *header, const char *value, const size_t value_len
) {

	return str_append_bytes (header, HTTP_HEADER_VALUES_SEPARATOR, HTTP_HEADER_VALUES_SEPARATOR_LEN)
		|| str_append_bytes (header, value, value_len);

}

// writes the separator and the value after the header, that has room for them
static void http_header_values_write (
	String *header, const char *value, const size_t value_len
) {

	char *end = header->str + header->len;

	(void) memcpy (end, HTTP_HEADER_VALUES_SEPARATOR, HTTP_HEADER_VALUES_SEPARATOR_LEN);
	(void) memcpy (end + HTTP_HEADER_VALUES_SEPARATOR_LEN, value, value_len);

	header->len += HTTP_HEADER_VALUES_SEPARATOR_LEN + value_len;
	header->str[header->len] = '\0';

}

// a string in an arena can not grow, so the joined value is a new string in it
// with room for as many bytes again: a header that is repeated many times is copied
// a few times, not once for every repetition
static int http_header_values_join_arena (
	Arena *arena, String **header, const char *value, const size_t value_len
) {

	int retval = 1;

	String *current = *header;
	const size_t needed = current->len + HTTP_HEADER_VALUES_SEPARATOR_LEN + value_len + 1;

	// the header that was joined before keeps its capacity in max_len
	if (current->max_len >= needed) {
		http_header_values_write (current, value, value_len);
		retval = 0;
	}

	else {
		const size_t capacity = needed * HTTP_HEADER_VALUES_GROWTH;

		// the same layout as str_create_from_bytes_arena ()
		String *joined = (String *) arena_alloc (arena, sizeof (String) + capacity);
		if (joined) {
			joined->max_len = capacity;
			joined->len = current->len;
			joined->str = ((char *) joined) + sizeof (String);

			(void) memcpy (joined->str, current->str, current->len);
			http_header_values_write (joined, value, value_len);

			*header = joined;
			retval = 0;
		}
	}

	return retval;

}

static int http_receive_header_join (
	HttpRequest *request, const http_header type,
	const char *value, const size_t value_len
) {

	return request->arena ?
		http_header_values_join_arena (request->arena, &request->headers[type], value, value_len) :
		http_header_values_join (request->headers[type], value, value_len);

}

// saves a header of the HTTP headers map
// a repeated header is joined with HTTP_HEADER_VALUES_SEPARATOR, unless it must be unique
// returns 0 on success, 1 if the request was rejected
static int http_receive_close_known_header (
	HttpReceive *http_receive, const http_header type,
	const char *value, const size_t value_len
) {

	int retval = 0;

	HttpRequest *request = http_receive->request;

	if (!request->headers[type]) {
		request->headers[type] = http_receive_string_create (http_receive, value, value_len);
	}

	else if (http_header_is_unique (type)) {
		retval = http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	else if (http_receive_header_join (request, type, value, value_len)) {
		retval = http_receive_reject (
			http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	return retval;

}

// the length of the value of a header without the spaces and tabs after it,
// that are not part of the value (RFC 9110 section 5.5): the parser gives them
static size_t http_header_value_length (const char *value, const size_t value_len) {

	size_t length = value_len;

	while (
		length
		&& ((value[length - 1] == http_header_value_space) || (value[length - 1] == http_header_value_tab))
	) {
		length -= 1;
	}

	return length;

}

// counts a header of the request, or a trailer of its chunked body
// returns 0 on success, 1 if the request was rejected
static int http_receive_count_header (HttpReceive *http_receive) {

	http_receive->n_headers += 1;

	const unsigned int max_headers = http_receive->http_cerver->max_headers;

	return (max_headers && (http_receive->n_headers > max_headers)) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// saves a complete header in the request
// returns 0 on success, 1 if the request was rejected
static int http_receive_save_header (
	HttpReceive *http_receive, const String *field
) {

	const String *value_buffer = http_receive->value_buffer;
	const char *value = (value_buffer && value_buffer->str) ? value_buffer->str : "";
	const size_t value_len = http_header_value_length (value, value_buffer ? value_buffer->len : 0);

	if (http_receive_count_header (http_receive)) return 1;

	const http_header type = http_header_type_with_string (field->str);
	if (type != HTTP_HEADER_UNDEFINED) {
		return http_receive_close_known_header (http_receive, type, value, value_len);
	}

	return http_receive_close_custom_header (
		http_receive->request, field, value, value_len,
		http_receive->http_cerver->max_custom_header_size
	) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// closes the header that was received in pieces
// returns 0 on success, 1 if the request was rejected
int http_receive_close_header (HttpReceive *http_receive) {

	int retval = 0;

	const String *field = http_receive->field_buffer;
	if (field && field->len) {
		retval = http_receive_save_header (http_receive, field);
	}

	http_receive_buffer_reset (http_receive->field_buffer);
	http_receive_buffer_reset (http_receive->value_buffer);

	return retval;

}

// marks that the body is being received and checks its size
// returns 0 on success, 1 if the request was rejected
int http_receive_check_body_size (
	HttpReceive *http_receive, const size_t length
) {

	if (http_receive->receive_status == HTTP_RECEIVE_STATUS_HEADERS)
		http_receive->receive_status = HTTP_RECEIVE_STATUS_BODY;

	http_receive->body_size += length;

	const size_t max_body_size = http_receive->max_body_size;

	return (max_body_size && (http_receive->body_size > max_body_size)) ?
		http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// the bytes to reserve for the body when its first part arrives,
// the parser has already discounted the part from its content length
// a chunked body or one without Content-Length is not reserved
static size_t http_request_body_reserve_size (
	const HttpReceive *http_receive, const size_t length
) {

	const http_parser *parser = http_receive->parser;

	if ((parser->flags & F_CHUNKED) || (parser->content_length == (uint64_t) -1)) return 0;

	uint64_t reserve = (uint64_t) length + parser->content_length;

	if (reserve > HTTP_REQUEST_BODY_RESERVE_LIMIT) reserve = HTTP_REQUEST_BODY_RESERVE_LIMIT;

	const size_t max_body_size = http_receive->max_body_size;
	if (max_body_size && (reserve > max_body_size)) reserve = max_body_size;

	// the '\0' of the body
	return (size_t) reserve + 1;

}

// creates the body with the bytes of its Content-Length reserved,
// so its next parts do not reallocate it
// returns 0 on success, 1 on error
static int http_request_body_create (
	HttpReceive *http_receive, const char *at, const size_t length
) {

	HttpRequest *request = http_receive->request;

	// the string of the body lives in the arena of the request when it has one,
	// and its buffer is taken from the ones that the thread reuses
	String *body = request->arena ?
		(String *) arena_alloc (request->arena, sizeof (String)) :
		(String *) malloc (sizeof (String));

	if (!body) return 1;

	const size_t reserve = http_request_body_reserve_size (http_receive, length);

	size_t capacity = 0;
	body->str = (char *) http_recycle_buffer_take (reserve, &capacity);
	if (!body->str) {
		if (!request->arena) free (body);
		return 1;
	}

	body->str[0] = '\0';
	body->len = 0;
	body->max_len = capacity;

	request->body = body;

	return str_append_bytes (request->body, at, length);

}

// the body is binary, it can contain '\0', and it is always terminated with '\0'
// returns 0 on success, 1 on error
static int http_request_body_append (
	HttpReceive *http_receive, const char *at, const size_t length
) {

	HttpRequest *request = http_receive->request;

	return request->body ?
		str_append_bytes (request->body, at, length) :
		http_request_body_create (http_receive, at, length);

}

// checks the size of all the headers that have been received
static inline int http_receive_check_header_size (
	HttpReceive *http_receive, const size_t length
) {

	http_receive->header_size += length;

	const size_t max_header_size = http_receive->http_cerver->max_header_size;

	return (max_header_size && (http_receive->header_size > max_header_size)) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}


// the url can arrive in many pieces, it is closed in on_headers_complete
int http_receive_handle_url (
	http_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->token = HTTP_RECEIVE_TOKEN_URL;

	const size_t current_len = http_receive->url_buffer ? http_receive->url_buffer->len : 0;
	const size_t max_url_size = http_receive->http_cerver->max_url_size;
	if (max_url_size && ((current_len + length) > max_url_size)) {
		return http_receive_reject (
			http_receive, uri_too_long_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	return http_receive_buffer_append (&http_receive->url_buffer, at, length) ?
		http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0;

}

// the fields that the parser finds after on_headers_complete are the trailers of a chunked body
static inline bool http_receive_headers_are_complete (const HttpReceive *http_receive) {

	return http_receive->receive_status != HTTP_RECEIVE_STATUS_NONE;

}

// the trailers of a chunked body are not kept, but they are limited as the headers are:
// their bytes count for the size of the headers and each one of them for their number,
// so a body can not be followed by trailers without an end
// returns 0 on success, 1 if the request was rejected
static int http_receive_handle_trailer (
	HttpReceive *http_receive, const HttpReceiveToken token, const size_t length
) {

	// a field after a value, or the first one, starts a trailer
	const bool starts = (token == HTTP_RECEIVE_TOKEN_FIELD)
		&& (http_receive->token != HTTP_RECEIVE_TOKEN_FIELD);

	http_receive->token = token;

	if (starts && http_receive_count_header (http_receive)) return 1;

	return http_receive_check_header_size (http_receive, length);

}

// a header field can arrive in many pieces,
// a new field after a value closes the previous header
int http_receive_handle_header_field (
	http_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_headers_are_complete (http_receive)) {
		return http_receive_handle_trailer (http_receive, HTTP_RECEIVE_TOKEN_FIELD, length);
	}

	// the first field ends the request line
	http_receive->stage = HTTP_STAGE_HEADERS;

	if (http_receive->token == HTTP_RECEIVE_TOKEN_VALUE) {
		if (http_receive_close_header (http_receive)) return 1;
	}

	http_receive->token = HTTP_RECEIVE_TOKEN_FIELD;

	if (http_receive_check_header_size (http_receive, length)) return 1;

	return http_receive_buffer_append (&http_receive->field_buffer, at, length) ?
		http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0;

}

// a header value can arrive in many pieces,
// it is closed by the next field or by on_headers_complete
int http_receive_handle_header_value (
	http_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_headers_are_complete (http_receive)) {
		return http_receive_handle_trailer (http_receive, HTTP_RECEIVE_TOKEN_VALUE, length);
	}

	http_receive->token = HTTP_RECEIVE_TOKEN_VALUE;

	if (http_receive_check_header_size (http_receive, length)) return 1;

	return http_receive_buffer_append (&http_receive->value_buffer, at, length) ?
		http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0;

}

int http_receive_handle_body (
	http_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_check_body_size (http_receive, length)) return 1;

	return http_request_body_append (http_receive, at, length) ?
		http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0;

}

#pragma endregion
