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
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"
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
// a request that is rejected closes its connection, and its response has to say it: its status is set before
// its response is sent, because a complete request that is rejected later (an authentication that runs with
// its handler) was already going to keep its connection, and a connection that is closed after a response
// that did not say it is a 502 behind a proxy
// returns 1, a part of the receive answers it with HTTP_RECEIVE_PARSER_STOP
int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
) {

	if (!http_receive->responded) {
		http_receive->receive_status = status;

		(void) http_response_send (response, http_receive);

		http_receive->responded = true;

		// its readers (the stats, the metrics) load it without the mutex of the cerver, like every counter
		if (status == HTTP_RECEIVE_STATUS_LIMIT) {
			CERVER_STATS_ADD (http_receive->http_cerver->n_limited_requests, 1);
		}
	}

	return 1;

}

// the limits answer with their own status, a version that cerver does not serve with 505 (RFC 9110 section
// 15.6.6), a method that it does not implement with 501 (RFC 9110 section 9.1), and anything else that the parser
// rejects with 400
void http_receive_parser_rejected (HttpReceive *http_receive) {

	switch (cerver_http_parser_get_error (&http_receive->parser)) {
		case CERVER_HTTP_PARSER_ERROR_TARGET_SIZE:
			(void) http_receive_reject (http_receive, uri_too_long_error, HTTP_RECEIVE_STATUS_LIMIT);
			break;

		case CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE:
			(void) http_receive_reject (http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT);
			break;

		case CERVER_HTTP_PARSER_ERROR_VERSION_UNSUPPORTED:
			(void) http_receive_reject (http_receive, http_version_not_supported_error, HTTP_RECEIVE_STATUS_REJECTED);
			break;

		case CERVER_HTTP_PARSER_ERROR_METHOD_UNKNOWN:
			(void) http_receive_reject (http_receive, not_implemented_error, HTTP_RECEIVE_STATUS_REJECTED);
			break;

		default:
			(void) http_receive_reject (http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);
			break;
	}

}

#pragma endregion

#pragma region target

// a string of the request over the copy of its head: its bytes stay where they are, with a '\0' written after them
// (the byte after a token of the head is never part of another one: a '?', a '#', a space, a colon or a CR)
static String *http_receive_string_view (
	HttpReceive *http_receive, char *bytes, const size_t length
) {

	Arena *arena = http_request_get_arena (http_receive->request);
	String *string = arena ? (String *) arena_alloc (arena, sizeof (String)) : NULL;

	if (string) {
		bytes[length] = '\0';

		string->str = bytes;
		string->len = length;
		string->max_len = length;
	}

	return string;

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

// splits the target of the request into its path and its query, views of the copy of its head
// returns 0 on success, 1 when their strings could not be created
static int http_receive_close_url (HttpReceive *http_receive, char *target, const size_t target_len) {

	HttpRequest *request = http_receive->request;

	const size_t origin_len = http_receive_target_origin_length (target, target_len);
	char *start = target + origin_len;
	size_t url_len = target_len - origin_len;

	// a fragment is never part of the path nor of the query
	const char *fragment = (const char *) memchr (start, http_target_fragment_start, url_len);
	if (fragment) url_len = (size_t) (fragment - start);

	char *question = (char *) memchr (start, http_target_query_start, url_len);
	const size_t path_len = question ? (size_t) (question - start) : url_len;

	// the query does not include the '?', its view is made first: the one of the path ends where it starts
	if (question) {
		request->query = http_receive_string_view (http_receive, question + 1, url_len - path_len - 1);
		if (!request->query) return 1;
	}

	// an absolute target without a path is the root
	request->url = (origin_len && !path_len) ?
		str_create_from_bytes_arena (http_request_get_arena (request), http_target_root, http_target_root_len) :
		http_receive_string_view (http_receive, start, path_len);

	return request->url ? 0 : 1;

}

#pragma endregion

#pragma region headers

// these headers must appear only once (RFC 9110 section 5.3, RFC 6455 section 11.3)
static inline bool http_header_is_unique (const CerverHttpHeader type) {

	return (type == HTTP_HEADER_HOST)
		|| (type == HTTP_HEADER_CONTENT_TYPE)
		|| (type == HTTP_HEADER_CONTENT_LENGTH)
		|| (type == HTTP_HEADER_AUTHORIZATION)
		|| (type == HTTP_HEADER_SEC_WEBSOCKET_KEY)
		|| (type == HTTP_HEADER_SEC_WEBSOCKET_VERSION);

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

// saves a header of the HTTP headers map
// a repeated header is joined with HTTP_HEADER_VALUES_SEPARATOR, unless it must be unique
// returns 0 on success, 1 if the request was rejected
static int http_receive_close_known_header (
	HttpReceive *http_receive, const CerverHttpHeader type,
	char *value, const size_t value_len
) {

	int retval = 0;

	HttpRequest *request = http_receive->request;

	if (!request->headers[type]) {
		request->headers[type] = http_receive_string_view (http_receive, value, value_len);
		if (!request->headers[type]) {
			retval = http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);
		}
	}

	else if (http_header_is_unique (type)) {
		retval = http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	else {
		Arena *arena = http_request_get_arena (request);
		if (!arena || http_header_values_join_arena (arena, &request->headers[type], value, value_len)) {
			retval = http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);
		}
	}

	return retval;

}

// the value of a folded field (only with the lenient flag of the headers) is joined in its place,
// without its line breaks, like the pieces that the parser of before gave
static size_t http_receive_value_unfold (char *value, const size_t length) {

	size_t written = 0;
	for (size_t i = 0; i < length; i++) {
		if ((value[i] != '\r') && (value[i] != '\n')) value[written++] = value[i];
	}

	return written;

}

// the lenient flag of the headers takes a NUL in a value, but the values are C strings for whoever reads them:
// a value of the whitelist of origins followed by a NUL and anything else was the value of the whitelist,
// and a proxy saw another one
static bool http_receive_value_has_nul (
	const HttpReceive *http_receive, const char *value, const size_t length
) {

	return (http_receive->http_cerver->parser_lenient & HTTP_PARSER_LENIENT_HEADERS)
		&& (memchr (value, '\0', length) != NULL);

}

// saves a field of the head in the request, its name and its value are views of the copy of the head
// returns 0 on success, 1 if the request was rejected
static int http_receive_save_header (
	HttpReceive *http_receive, char *head, const CerverHttpField *field
) {

	char *value = head + field->value.offset;
	size_t value_len = field->value.length;

	if (field->flags & CERVER_HTTP_FIELD_FOLDED) value_len = http_receive_value_unfold (value, value_len);

	if (http_receive_value_has_nul (http_receive, value, value_len)) {
		return http_receive_reject (http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);
	}

	if (field->type != HTTP_HEADER_UNDEFINED) {
		return http_receive_close_known_header (http_receive, field->type, value, value_len);
	}

	// the value ends with its own '\0', so the name is made after it
	value[value_len] = '\0';

	const String *name = http_receive_string_view (http_receive, head + field->name.offset, field->name.length);
	if (!name) return http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);

	return http_receive_close_custom_header (
		http_receive->request, name, value, value_len,
		http_receive->http_cerver->max_custom_header_size
	) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// the head is copied once to the memory of the request, where its views live as long as it
int http_receive_head_complete (HttpReceive *http_receive) {

	CerverHttpParser *parser = &http_receive->parser;
	const CerverHttpHead *head = &parser->head;

	Arena *arena = http_request_get_arena (http_receive->request);

	// a '\0' after the last byte, the one of a token at the end of a head of HTTP/0.9
	char *copy = arena ? (char *) arena_alloc (arena, head->length + 1) : NULL;
	if (!copy) return http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);

	(void) memcpy (copy, head->base, head->length);
	copy[head->length] = '\0';
	cerver_http_parser_rebase (parser, copy);

	http_receive->request->strings_in_arena = true;

	if (http_receive_close_url (http_receive, copy + head->target.offset, head->target.length)) {
		return http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR);
	}

	int rejected = 0;
	for (unsigned int i = 0; !rejected && (i < head->fields_count); i++) {
		rejected = http_receive_save_header (http_receive, copy, &head->fields[i]);
	}

	return rejected;

}

#pragma endregion

#pragma region body

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
// the parser has already discounted the part from what is left of its content length
// a chunked body or one without Content-Length is not reserved
static size_t http_request_body_reserve_size (
	const HttpReceive *http_receive, const size_t length
) {

	const CerverHttpParser *parser = &http_receive->parser;
	const unsigned int flags = parser->head.flags;

	if ((flags & CERVER_HTTP_HEAD_CHUNKED) || !(flags & CERVER_HTTP_HEAD_CONTENT_LENGTH)) return 0;

	uint64_t reserve = (uint64_t) length + parser->remaining;

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

int http_receive_handle_body (
	HttpReceive *http_receive, const char *at, size_t length
) {

	if (http_receive_check_body_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	return http_receive_parser_result (
		http_request_body_append (http_receive, at, length) ?
			http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

#pragma endregion
