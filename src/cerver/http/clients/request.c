#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/clients/client.h"
#include "cerver/http/clients/internal.h"
#include "cerver/http/core/context.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/method.h"

#define HTTP_CLIENT_SCHEME_LENGTH				(sizeof (CERVER_HTTP_CLIENT_SCHEME) - 1)

#define HTTP_CLIENT_MAX_PORT					65535
#define HTTP_CLIENT_DECIMAL_BASE				10

#define HTTP_CLIENT_DESTINATION_FORMAT			"%s:%u"

// the Host header: the port is written only when it is not the one of the scheme, an IPv6 goes in brackets
#define HTTP_CLIENT_HOST_FORMAT					"%s"
#define HTTP_CLIENT_HOST_PORT_FORMAT			"%s:%u"
#define HTTP_CLIENT_IPV6_HOST_FORMAT			"[%s]"
#define HTTP_CLIENT_IPV6_HOST_PORT_FORMAT		"[%s]:%u"

// the Host header takes its host, its brackets, its port and its end
#define HTTP_CLIENT_HOST_HEADER_SIZE			(HTTP_CLIENT_HOST_SIZE + 16)

#define HTTP_CLIENT_REQUEST_START_FORMAT		"%s %s HTTP/1.1\r\nHost: %s\r\n"
#define HTTP_CLIENT_HEADER_FORMAT				"%s: %s\r\n"
#define HTTP_CLIENT_CONTENT_TYPE_FORMAT			"Content-Type: %s\r\n"
#define HTTP_CLIENT_CONTENT_LENGTH_FORMAT		"Content-Length: %zu\r\n"

// the longest Content-Type and Content-Length lines without their values, and the digits of a length
#define HTTP_CLIENT_CONTENT_LINES_SIZE			64

static const char *http_client_line_end = { "\r\n" };
#define HTTP_CLIENT_LINE_END_LENGTH				2

static const char http_client_ipv6_start = '[';
static const char http_client_ipv6_end = ']';
static const char http_client_port_separator = ':';
static const char http_client_path_start = '/';
static const char http_client_query_start = '?';
static const char http_client_fragment_start = '#';
static const char http_client_zero = '0';

// the characters of a url are above the space and are not the delete control
static const unsigned char http_client_url_space = ' ';
static const unsigned char http_client_url_delete = 0x7F;

static const char *http_client_root_target = { "/" };

// the headers that the client writes: they can not be added to a request
static const char *http_client_reserved_headers[] = {
	"Host", "Content-Length", "Content-Type", "Transfer-Encoding"
};

#define HTTP_CLIENT_RESERVED_HEADERS			(sizeof (http_client_reserved_headers) / sizeof (const char *))

#pragma region errors

const char *cerver_http_client_error_to_string (
	const CerverHttpClientError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_HTTP_CLIENT_ERROR_##name: return #string;
		CERVER_HTTP_CLIENT_ERROR_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_http_client_error_to_string (CERVER_HTTP_CLIENT_ERROR_NONE);

}

const char *cerver_http_client_error_description (
	const CerverHttpClientError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_HTTP_CLIENT_ERROR_##name: return #description;
		CERVER_HTTP_CLIENT_ERROR_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_http_client_error_description (CERVER_HTTP_CLIENT_ERROR_NONE);

}

#pragma endregion

#pragma region url

// the url is written in the request line and in the Host header:
// a space or a control character in it would end them and start other lines
static bool http_client_url_is_visible (const char *url) {

	bool visible = true;

	for (const unsigned char *ch = (const unsigned char *) url; visible && *ch; ch++) {
		visible = (*ch > http_client_url_space) && (*ch != http_client_url_delete);
	}

	return visible;

}

// where the host of a url ends: an IPv6 at its closing bracket, any other one at its port, its path or its query
static size_t http_client_url_host_end (const char *url, const size_t position, const size_t length) {

	size_t end = position;

	while (
		(end < length) && (url[end] != http_client_port_separator)
		&& (url[end] != http_client_path_start) && (url[end] != http_client_query_start)
	) {
		end += 1;
	}

	return end;

}

static bool http_client_url_host_set (
	CerverHttpClientRequest *request, const char *host, const size_t host_length
) {

	const bool fits = host_length && (host_length < HTTP_CLIENT_HOST_SIZE);

	if (fits) {
		(void) memcpy (request->host, host, host_length);
		request->host[host_length] = '\0';
	}

	return fits;

}

// the host, in brackets for an IPv6, until its port or its path
// returns the position after it, 0 on error
static size_t http_client_url_host (
	CerverHttpClientRequest *request, const char *url, const size_t position, const size_t length
) {

	size_t next = 0;

	if ((position < length) && (url[position] == http_client_ipv6_start)) {
		const char *closing = (const char *) memchr (url + position, http_client_ipv6_end, length - position);
		if (closing) {
			const size_t start = position + 1;
			const size_t end = (size_t) (closing - url);

			if (http_client_url_host_set (request, url + start, end - start)) {
				request->ipv6 = true;
				next = end + 1;
			}
		}
	}

	else {
		const size_t end = http_client_url_host_end (url, position, length);

		if (http_client_url_host_set (request, url + position, end - position)) next = end;
	}

	return next;

}

// the port after the ':', 1 to 65535
// returns the position after it, 0 on error
static size_t http_client_url_port (
	CerverHttpClientRequest *request, const char *url, const size_t position, const size_t length
) {

	size_t next = position;
	request->port = CERVER_HTTP_CLIENT_DEFAULT_PORT;

	if ((position < length) && (url[position] == http_client_port_separator)) {
		unsigned long port = 0;
		next = position + 1;
		while ((next < length) && isdigit ((unsigned char) url[next]) && (port <= HTTP_CLIENT_MAX_PORT)) {
			port = (port * HTTP_CLIENT_DECIMAL_BASE) + (unsigned long) (url[next] - http_client_zero);
			next += 1;
		}

		if ((next == (position + 1)) || !port || (port > HTTP_CLIENT_MAX_PORT)) next = 0;
		else request->port = (u16) port;
	}

	return next;

}

// a query without a path is a query of the root
static char *http_client_url_root_query (const char *query, const size_t query_length) {

	char *target = (char *) malloc (query_length + 2);
	if (target) {
		target[0] = http_client_path_start;
		(void) memcpy (target + 1, query, query_length);
		target[query_length + 1] = '\0';
	}

	return target;

}

// the path and the query, "/" when it has none; a url can not have a fragment, and what follows its port is
// a path or a query
static u8 http_client_url_target (
	CerverHttpClientRequest *request, const char *url, const size_t position, const size_t length
) {

	const char *target = url + position;
	const size_t target_length = length - position;

	if (!memchr (target, http_client_fragment_start, target_length)) {
		if (!target_length) request->target = strdup (http_client_root_target);
		else if (target[0] == http_client_query_start) request->target = http_client_url_root_query (target, target_length);
		else if (target[0] == http_client_path_start) request->target = strndup (target, target_length);
	}

	return request->target ? 0 : 1;

}

// the address of a host that is one, without asking anybody
static void http_client_request_address (CerverHttpClientRequest *request) {

	HttpClientAddress *address = &request->addresses[0];

	struct sockaddr_in *ipv4 = (struct sockaddr_in *) &address->address;
	struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) &address->address;

	if (!request->ipv6 && (inet_pton (AF_INET, request->host, &ipv4->sin_addr) == 1)) {
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons (request->port);

		address->length = sizeof (struct sockaddr_in);
		request->n_addresses = 1;
	}

	else if (inet_pton (AF_INET6, request->host, &ipv6->sin6_addr) == 1) {
		ipv6->sin6_family = AF_INET6;
		ipv6->sin6_port = htons (request->port);

		address->length = sizeof (struct sockaddr_in6);
		request->n_addresses = 1;
	}

}

static u8 http_client_request_url (CerverHttpClientRequest *request, const char *url) {

	u8 retval = 1;

	if (
		http_client_url_is_visible (url)
		&& !strncasecmp (url, CERVER_HTTP_CLIENT_SCHEME, HTTP_CLIENT_SCHEME_LENGTH)
	) {
		const size_t length = strlen (url);

		size_t position = http_client_url_host (request, url, HTTP_CLIENT_SCHEME_LENGTH, length);
		if (position) position = http_client_url_port (request, url, position, length);
		if (position) retval = http_client_url_target (request, url, position, length);
	}

	if (!retval) {
		(void) snprintf (
			request->destination, HTTP_CLIENT_DESTINATION_SIZE,
			HTTP_CLIENT_DESTINATION_FORMAT, request->host, (unsigned int) request->port
		);

		http_client_request_address (request);
	}

	return retval;

}

#pragma endregion

#pragma region request

static void http_client_response_end (CerverHttpClientResponse *response) {

	free (response->head);
	free (response->body);

}

void http_client_request_free (CerverHttpClientRequest *request) {

	if (request) {
		http_client_response_end (&request->response);

		free (request->target);
		free (request->headers);
		free (request->content_type);
		free (request->body);
		free (request->output);

		(void) pthread_cond_destroy (&request->condition);
		(void) pthread_mutex_destroy (&request->mutex);

		free (request);
	}

}

static CerverHttpClientRequest *http_client_request_new (
	CerverHttpClient *client, const CerverHttpMethod method
) {

	CerverHttpClientRequest *request = (CerverHttpClientRequest *) calloc (1, sizeof (CerverHttpClientRequest));
	if (request) {
		event_source_init (&request->source, &http_client_request_methods);

		request->client = client;
		request->method = method;

		(void) pthread_mutex_init (&request->mutex, NULL);
		(void) pthread_cond_init (&request->condition, NULL);
	}

	return request;

}

CerverHttpClientRequest *cerver_http_client_request_create (
	CerverHttpClient *client, const CerverHttpMethod method, const char *url
) {

	CerverHttpClientRequest *request = NULL;

	if (client && url && cerver_http_method_is_valid (method)) {
		request = http_client_request_new (client, method);

		if (request && http_client_request_url (request, url)) {
			http_client_request_free (request);
			request = NULL;
		}
	}

	return request;

}

// a request that was sent is of its loop until it is finished: only one that its caller waited for comes back
static bool http_client_request_is_of_caller (const CerverHttpClientRequest *request) {

	return (request->state == HTTP_CLIENT_STATE_NONE) || (request->blocking && request->finished);

}

void cerver_http_client_request_delete (
	CerverHttpClientRequest *request
) {

	if (request && http_client_request_is_of_caller (request)) http_client_request_free (request);

}

static bool http_client_header_is_reserved (const char *name) {

	bool reserved = false;

	for (size_t idx = 0; (idx < HTTP_CLIENT_RESERVED_HEADERS) && !reserved; idx++) {
		reserved = !strcasecmp (name, http_client_reserved_headers[idx]);
	}

	return reserved;

}

// makes room for more bytes of the headers of a request, with their end
static bool http_client_headers_reserve (CerverHttpClientRequest *request, const size_t length) {

	bool reserved = true;

	const size_t needed = request->headers_length + length + 1;
	if (needed > request->headers_capacity) {
		size_t capacity = request->headers_capacity ? request->headers_capacity : HTTP_CLIENT_BUFFER_SIZE;
		while (capacity < needed) capacity *= 2;

		char *headers = (char *) realloc (request->headers, capacity);
		if (headers) {
			request->headers = headers;
			request->headers_capacity = capacity;
		}

		else {
			reserved = false;
		}
	}

	return reserved;

}

static u8 http_client_request_header_append (
	CerverHttpClientRequest *request, const char *name, const char *value
) {

	u8 retval = 1;

	// the name, the separator and its space, the value and the line break
	const size_t length = strlen (name) + strlen (value) + HTTP_CLIENT_LINE_END_LENGTH + 2;

	if (http_client_headers_reserve (request, length)) {
		request->headers_length += (size_t) snprintf (
			request->headers + request->headers_length, length + 1, HTTP_CLIENT_HEADER_FORMAT, name, value
		);

		retval = 0;
	}

	return retval;

}

u8 cerver_http_client_request_add_header (
	CerverHttpClientRequest *request, const char *name, const char *value
) {

	u8 retval = 1;

	if (
		request && name && value && (request->state == HTTP_CLIENT_STATE_NONE)
		&& http_header_name_is_valid (name, strlen (name))
		&& http_header_value_is_valid (value, strlen (value))
		&& !http_client_header_is_reserved (name)
	) {
		retval = http_client_request_header_append (request, name, value);
	}

	return retval;

}

// the copies of a body and of its content type, that replace the ones of the request only when both were made
static u8 http_client_request_body_replace (
	CerverHttpClientRequest *request,
	const char *content_type, const char *body, const size_t length
) {

	u8 retval = 1;

	char *type_copy = content_type ? strdup (content_type) : NULL;
	char *body_copy = length ? (char *) malloc (length) : NULL;

	if ((!content_type || type_copy) && (!length || body_copy)) {
		if (length) (void) memcpy (body_copy, body, length);

		free (request->content_type);
		free (request->body);

		request->content_type = type_copy;
		request->body = body_copy;
		request->body_length = length;
		request->has_body = true;

		retval = 0;
	}

	else {
		free (type_copy);
		free (body_copy);
	}

	return retval;

}

u8 cerver_http_client_request_set_body (
	CerverHttpClientRequest *request,
	const char *content_type, const char *body, const size_t length
) {

	u8 retval = 1;

	if (
		request && (request->state == HTTP_CLIENT_STATE_NONE) && (body || !length)
		&& (!content_type || http_header_value_is_valid (content_type, strlen (content_type)))
	) {
		retval = http_client_request_body_replace (request, content_type, body, length);
	}

	return retval;

}

void cerver_http_client_request_set_timeout (
	CerverHttpClientRequest *request, const u32 milliseconds
) {

	if (request && (request->state == HTTP_CLIENT_STATE_NONE)) request->timeout = milliseconds;

}

#pragma endregion

#pragma region output

static void http_client_request_host_header (const CerverHttpClientRequest *request, char *host) {

	const bool with_port = request->port != CERVER_HTTP_CLIENT_DEFAULT_PORT;

	if (request->ipv6 && with_port) {
		(void) snprintf (
			host, HTTP_CLIENT_HOST_HEADER_SIZE, HTTP_CLIENT_IPV6_HOST_PORT_FORMAT,
			request->host, (unsigned int) request->port
		);
	}

	else if (request->ipv6) {
		(void) snprintf (host, HTTP_CLIENT_HOST_HEADER_SIZE, HTTP_CLIENT_IPV6_HOST_FORMAT, request->host);
	}

	else if (with_port) {
		(void) snprintf (
			host, HTTP_CLIENT_HOST_HEADER_SIZE, HTTP_CLIENT_HOST_PORT_FORMAT,
			request->host, (unsigned int) request->port
		);
	}

	else {
		(void) snprintf (host, HTTP_CLIENT_HOST_HEADER_SIZE, HTTP_CLIENT_HOST_FORMAT, request->host);
	}

}

// a request with a body says its length, also when it is empty; the methods that carry one say it without one
static bool http_client_request_sends_length (const CerverHttpClientRequest *request) {

	return request->has_body
		|| (request->method == REQUEST_METHOD_POST)
		|| (request->method == REQUEST_METHOD_PUT)
		|| (request->method == REQUEST_METHOD_PATCH);

}

// the lines of the Content-Type and the Content-Length of a request, returns their bytes
static size_t http_client_request_content_lines (
	const CerverHttpClientRequest *request, char *lines, const size_t size
) {

	size_t length = 0;

	if (request->content_type) {
		length += (size_t) snprintf (lines, size, HTTP_CLIENT_CONTENT_TYPE_FORMAT, request->content_type);
	}

	if (http_client_request_sends_length (request)) {
		length += (size_t) snprintf (
			lines + length, size - length, HTTP_CLIENT_CONTENT_LENGTH_FORMAT, request->body_length
		);
	}

	return length;

}

// the bytes that a request sends: its request line, its Host, the headers that were added, the ones of its
// body, the line that ends its head and its body
static u8 http_client_request_write (CerverHttpClientRequest *request) {

	u8 retval = 1;

	char host[HTTP_CLIENT_HOST_HEADER_SIZE] = { 0 };
	http_client_request_host_header (request, host);

	const char *method = cerver_http_method_to_string (request->method);

	const size_t content_size = HTTP_CLIENT_CONTENT_LINES_SIZE
		+ (request->content_type ? strlen (request->content_type) : 0);

	const size_t start_length = (size_t) snprintf (
		NULL, 0, HTTP_CLIENT_REQUEST_START_FORMAT, method, request->target, host
	);

	const size_t size = start_length + request->headers_length + content_size
		+ HTTP_CLIENT_LINE_END_LENGTH + request->body_length + 1;

	char *output = (char *) malloc (size);
	if (output) {
		size_t length = (size_t) snprintf (
			output, size, HTTP_CLIENT_REQUEST_START_FORMAT, method, request->target, host
		);

		if (request->headers_length) {
			(void) memcpy (output + length, request->headers, request->headers_length);
			length += request->headers_length;
		}

		length += http_client_request_content_lines (request, output + length, content_size);

		(void) memcpy (output + length, http_client_line_end, HTTP_CLIENT_LINE_END_LENGTH);
		length += HTTP_CLIENT_LINE_END_LENGTH;

		if (request->body_length) {
			(void) memcpy (output + length, request->body, request->body_length);
			length += request->body_length;
		}

		request->output = output;
		request->output_length = length;

		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region send

// the response of a request starts with its parser: one of a HEAD has no body, whatever its head says
void http_client_response_start (CerverHttpClientRequest *request) {

	CerverHttpClientResponse *response = &request->response;

	cerver_http_parser_init (
		&response->parser, CERVER_HTTP_PARSER_TYPE_RESPONSE, HTTP_PARSER_LENIENT_NONE, NULL,
		response->fields, HTTP_CLIENT_FIELDS
	);

	response->parser.skip_body = (request->method == REQUEST_METHOD_HEAD);

}

static u8 http_client_request_start (CerverHttpClientRequest *request) {

	u8 retval = 1;

	if (!http_client_request_write (request)) {
		http_client_response_start (request);

		retval = http_client_request_give (request);
	}

	return retval;

}

u8 cerver_http_client_request_send (
	CerverHttpClientRequest *request, CerverHttpClientHandler handler, void *data
) {

	u8 retval = 1;

	if (request && (request->state == HTTP_CLIENT_STATE_NONE)) {
		if (handler) {
			request->handler = handler;
			request->data = data;

			retval = http_client_request_start (request);
		}

		// the request is of its client from the call, also when it could not be sent
		if (retval) http_client_request_free (request);
	}

	return retval;

}

static void http_client_request_wait (CerverHttpClientRequest *request) {

	(void) pthread_mutex_lock (&request->mutex);

	while (!request->finished) (void) pthread_cond_wait (&request->condition, &request->mutex);

	(void) pthread_mutex_unlock (&request->mutex);

}

u8 cerver_http_client_request_perform (
	CerverHttpClientRequest *request
) {

	u8 retval = 1;

	if (request && (request->state == HTTP_CLIENT_STATE_NONE)) {
		request->blocking = true;

		if (!http_client_request_start (request)) {
			http_client_request_wait (request);

			retval = 0;
		}

		// it was not sent: it is of its caller again, that deletes it
		else {
			request->blocking = false;

			free (request->output);
			request->output = NULL;
		}
	}

	return retval;

}

const CerverHttpClientResponse *cerver_http_client_request_response (
	const CerverHttpClientRequest *request
) {

	return (request && request->blocking && request->finished) ? &request->response : NULL;

}

#pragma endregion

#pragma region response

CerverHttpClientError cerver_http_client_response_error (
	const CerverHttpClientResponse *response
) {

	return response ? response->error : CERVER_HTTP_CLIENT_ERROR_MEMORY;

}

unsigned int cerver_http_client_response_status (
	const CerverHttpClientResponse *response
) {

	return (response && response->head_complete) ? response->parser.head.status : 0;

}

CerverHttpView cerver_http_client_response_header (
	const CerverHttpClientResponse *response, const char *name
) {

	CerverHttpView view = { NULL, 0 };

	if (response && name && response->head_complete) {
		const CerverHttpHead *head = &response->parser.head;

		const CerverHttpField *field = cerver_http_head_field_with_name (head, name, strlen (name));
		if (field) {
			view.data = cerver_http_head_at (head, field->value);
			view.length = field->value.length;
		}
	}

	return view;

}

CerverHttpView cerver_http_client_response_body (
	const CerverHttpClientResponse *response
) {

	CerverHttpView view = { NULL, 0 };

	if (response && response->body) {
		view.data = response->body;
		view.length = response->body_length;
	}

	return view;

}

#pragma endregion
