#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/events/backend.h"
#include "cerver/events/internal.h"

#include "cerver/http/clients/client.h"
#include "cerver/http/clients/internal.h"
#include "cerver/http/core/types.h"
#include "cerver/http/parser/body.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/parser/parser.h"

#include "cerver/threads/thpool.h"

// an exchange is a request and its response over a connection, and only the thread of its event loop runs it:
// the request is given to the loop, takes a connection that was kept or opens one (with the address of its host,
// that a thread of a pool looks for when it is a name), sends its bytes and reads its response with the parser
// of cerver. The connection of a response that says so is kept for the next request of its destination

// the bytes of a port as text, with its end
#define HTTP_CLIENT_PORT_SIZE					8
#define HTTP_CLIENT_PORT_FORMAT					"%u"

// the informational responses come before the response of a request, except the one that switches protocols
#define HTTP_CLIENT_STATUS_INFORMATIONAL		100
#define HTTP_CLIENT_STATUS_SWITCHING			101
#define HTTP_CLIENT_STATUS_SUCCESS				200

// what the bytes of a response did to it: the values of HTTP_CLIENT_READ_*
typedef enum HttpClientFeed {

	HTTP_CLIENT_FEED_MORE			= HTTP_CLIENT_READ_MORE,
	HTTP_CLIENT_FEED_COMPLETE		= HTTP_CLIENT_READ_COMPLETE,
	HTTP_CLIENT_FEED_FAILED			= HTTP_CLIENT_READ_FAILED

} HttpClientFeed;

static void http_client_exchange_connect (CerverHttpClientRequest *request);
static void http_client_exchange_open (CerverHttpClientRequest *request);
static void http_client_exchange_fail (
	CerverHttpClientRequest *request, const CerverHttpClientError error, const int code
);

#pragma region pool

// the pool of a loop of a client: the one of its own loop, or the one in the position of a loop of its cerver.
// A cerver that started again with more loops than the first time keeps no connections in the new ones
static HttpClientPool *http_client_pool_of (CerverHttpClient *client, EventLoop *loop) {

	const unsigned int position = client->loop ? 0 : loop->id;

	HttpClientPool *pool = (position < __atomic_load_n (&client->n_pools, __ATOMIC_SEQ_CST)) ?
		&client->pools[position] : NULL;

	if (pool) __atomic_store_n (&pool->loop, loop, __ATOMIC_SEQ_CST);

	return pool;

}

static void http_client_pool_add (HttpClientPool *pool, HttpClientConnection *connection) {

	connection->previous = NULL;
	connection->next = pool->idle;
	if (pool->idle) pool->idle->previous = connection;
	pool->idle = connection;

	pool->n_idle += 1;

	connection->idle = true;

}

static void http_client_pool_remove (HttpClientPool *pool, HttpClientConnection *connection) {

	if (connection->previous) connection->previous->next = connection->next;
	else pool->idle = connection->next;

	if (connection->next) connection->next->previous = connection->previous;

	connection->next = NULL;
	connection->previous = NULL;

	pool->n_idle -= 1;

	connection->idle = false;

}

// the connection that was kept last for a destination: the one that its server is least likely to have closed
static HttpClientConnection *http_client_pool_find (const HttpClientPool *pool, const char *destination) {

	HttpClientConnection *connection = pool ? pool->idle : NULL;

	while (connection && strcmp (connection->destination, destination)) connection = connection->next;

	return connection;

}

static unsigned int http_client_pool_count (const HttpClientPool *pool, const char *destination) {

	unsigned int count = 0;

	for (const HttpClientConnection *connection = pool->idle; connection; connection = connection->next) {
		if (!strcmp (connection->destination, destination)) count += 1;
	}

	return count;

}

#pragma endregion

#pragma region connection

static EventBackend *http_client_connection_backend (const HttpClientConnection *connection) {

	return connection->source.loop->backend;

}

// the socket of a connection has no operations: it leaves its loop
static void http_client_connection_free (HttpClientConnection *connection) {

	CerverHttpClient *client = connection->client;

	const int fd = connection->source.handle.fd;

	event_loop_source_remove (&connection->source);

	if (fd > -1) (void) close (fd);

	free (connection);

	(void) __atomic_sub_fetch (&client->n_alive, 1, __ATOMIC_SEQ_CST);

}

// a connection is closed once: it leaves the ones that are kept, and it is released when its backend ended
// the operations of its socket
static void http_client_connection_close (HttpClientConnection *connection) {

	if (!connection->closing) {
		connection->closing = true;

		if (connection->idle) http_client_pool_remove (connection->pool, connection);

		connection->request = NULL;

		event_loop_source_timer_clear (&connection->source);

		if (!event_backend_stop (http_client_connection_backend (connection), &connection->source.handle)) {
			http_client_connection_free (connection);
		}
	}

}

// a connection that is kept waits with a read: its server closes it, or sends what nobody asked for
static bool http_client_connection_listens (HttpClientConnection *connection) {

	return (connection->source.handle.operations & EVENT_OPERATION_RECEIVE)
		|| !event_backend_receive (http_client_connection_backend (connection), &connection->source.handle);

}

// a pool keeps the connections of each destination up to the limit of its client, while the client and the
// loop go on
static bool http_client_connection_can_stay (const HttpClientConnection *connection) {

	const CerverHttpClient *client = connection->client;
	const HttpClientPool *pool = connection->pool;

	return pool && client->max_idle && !connection->source.stopped
		&& !__atomic_load_n (&client->deleting, __ATOMIC_SEQ_CST)
		&& (http_client_pool_count (pool, connection->destination) < client->max_idle);

}

// the connection of a request that ended: it is kept for the next one of its destination, or closed
static void http_client_connection_release (HttpClientConnection *connection, const bool reusable) {

	connection->request = NULL;

	if (reusable && http_client_connection_can_stay (connection) && http_client_connection_listens (connection)) {
		connection->reused = true;

		http_client_pool_add (connection->pool, connection);

		event_loop_source_timer_set (&connection->source, connection->client->idle_timeout);
	}

	else {
		http_client_connection_close (connection);
	}

}

static HttpClientConnection *http_client_connection_new (CerverHttpClientRequest *request, const int fd) {

	CerverHttpClient *client = request->client;
	EventLoop *loop = request->source.loop;

	HttpClientConnection *connection = (HttpClientConnection *) calloc (1, sizeof (HttpClientConnection));
	if (connection) {
		event_source_init (&connection->source, &http_client_connection_methods);

		// a loop that ends takes no more connections
		if (!event_loop_source_add (loop, &connection->source)) {
			event_loop_source_attach (&connection->source);

			event_handle_init (&connection->source.handle, EVENT_HANDLE_TYPE_SOURCE, fd, &connection->source);

			connection->client = client;
			connection->pool = http_client_pool_of (client, loop);

			(void) memcpy (connection->destination, request->destination, HTTP_CLIENT_DESTINATION_SIZE);

			(void) __atomic_add_fetch (&client->n_alive, 1, __ATOMIC_SEQ_CST);
			(void) __atomic_add_fetch (&client->n_connections, 1, __ATOMIC_SEQ_CST);
		}

		else {
			free (connection);
			connection = NULL;
		}
	}

	return connection;

}

#pragma endregion

#pragma region request

// tells who sent the request, once: its handler takes the response. The caller that waits is woken up when the
// loop leaves the request
static void http_client_request_complete (CerverHttpClientRequest *request) {

	if (!request->completed) {
		request->completed = true;

		if (!request->blocking) request->handler (&request->response, request->data);
	}

}

static void http_client_request_wake (CerverHttpClientRequest *request) {

	(void) pthread_mutex_lock (&request->mutex);

	request->finished = true;

	(void) pthread_cond_signal (&request->condition);

	(void) pthread_mutex_unlock (&request->mutex);

}

// the loop leaves a request: the one of a handler is released, the one of a caller that waits is its again
static void http_client_request_release (CerverHttpClientRequest *request) {

	CerverHttpClient *client = request->client;

	request->state = HTTP_CLIENT_STATE_DONE;

	event_loop_source_remove (&request->source);

	if (request->blocking) http_client_request_wake (request);
	else http_client_request_free (request);

	(void) __atomic_sub_fetch (&client->n_requests, 1, __ATOMIC_SEQ_CST);

}

static void http_client_request_finish (CerverHttpClientRequest *request) {

	http_client_request_complete (request);

	http_client_request_release (request);

}

// takes its connection from a request, NULL without one
static HttpClientConnection *http_client_request_detach (CerverHttpClientRequest *request) {

	HttpClientConnection *connection = request->connection;

	request->connection = NULL;

	return connection;

}

static void http_client_request_error (
	CerverHttpClientRequest *request, const CerverHttpClientError error, const int code
) {

	request->response.error = error;
	request->response.error_code = code;

}

#pragma endregion

#pragma region response

// makes room for more bytes in a buffer of a response, with their end; false without memory
static bool http_client_buffer_reserve (char **buffer, size_t *capacity, const size_t needed) {

	bool reserved = true;

	if (needed > *capacity) {
		size_t size = *capacity ? *capacity : HTTP_CLIENT_BUFFER_SIZE;
		while (size < needed) size *= 2;

		char *bigger = (char *) realloc (*buffer, size);
		if (bigger) {
			*buffer = bigger;
			*capacity = size;
		}

		else {
			reserved = false;
		}
	}

	return reserved;

}

static bool http_client_response_body_add (
	CerverHttpClientRequest *request, const char *piece, const size_t length
) {

	CerverHttpClientResponse *response = &request->response;

	bool added = false;

	if ((response->body_length + length) > request->client->max_response_size) {
		http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_SIZE, 0);
	}

	else if (!http_client_buffer_reserve (&response->body, &response->body_capacity, response->body_length + length + 1)) {
		http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_MEMORY, 0);
	}

	else {
		(void) memcpy (response->body + response->body_length, piece, length);
		response->body_length += length;
		response->body[response->body_length] = '\0';

		added = true;
	}

	return added;

}

// the bytes of the body of a response: each piece that the parser gives is added to the body
static HttpClientFeed http_client_response_feed_body (
	CerverHttpClientRequest *request, const char *data, const size_t length
) {

	CerverHttpClientResponse *response = &request->response;

	HttpClientFeed feed = HTTP_CLIENT_FEED_MORE;

	size_t offset = 0;
	bool reading = true;
	while (reading) {
		size_t consumed = 0;
		const char *piece = NULL;
		size_t piece_length = 0;

		const CerverHttpParserResult result = cerver_http_parser_body (
			&response->parser, data + offset, length - offset, &consumed, &piece, &piece_length
		);

		offset += consumed;

		switch (result) {
			case CERVER_HTTP_PARSER_RESULT_BODY:
				if (!http_client_response_body_add (request, piece, piece_length)) {
					feed = HTTP_CLIENT_FEED_FAILED;
					reading = false;
				}
				break;

			case CERVER_HTTP_PARSER_RESULT_MORE:
				reading = false;
				break;

			case CERVER_HTTP_PARSER_RESULT_COMPLETE:
				response->complete = true;
				// what follows a response was not asked for: its connection is not kept
				response->exceeded = (offset < length);

				feed = HTTP_CLIENT_FEED_COMPLETE;
				reading = false;
				break;

			default:
				http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_RESPONSE, 0);

				feed = HTTP_CLIENT_FEED_FAILED;
				reading = false;
				break;
		}
	}

	return feed;

}

// the values of the fields of a head end in its buffer with a '\0', over the byte that follows each one: the
// views of the response are strings
static void http_client_response_head_terminate (CerverHttpClientResponse *response) {

	const CerverHttpHead *head = &response->parser.head;

	for (unsigned int idx = 0; idx < head->fields_count; idx++) {
		const CerverHttpSpan value = head->fields[idx].value;

		response->head[value.offset + value.length] = '\0';
	}

}

// the status of a response has three digits and the first one is its class, from 1: the parser takes any three
static bool http_client_response_has_status (const CerverHttpClientResponse *response) {

	return response->parser.head.status >= HTTP_CLIENT_STATUS_INFORMATIONAL;

}

static HttpClientFeed http_client_response_head_rejected (CerverHttpClientRequest *request) {

	http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_RESPONSE, 0);

	return HTTP_CLIENT_FEED_FAILED;

}

static bool http_client_response_is_informational (const CerverHttpClientResponse *response) {

	const unsigned int status = response->parser.head.status;

	return (status >= HTTP_CLIENT_STATUS_INFORMATIONAL) && (status < HTTP_CLIENT_STATUS_SUCCESS)
		&& (status != HTTP_CLIENT_STATUS_SWITCHING);

}

// an informational response has no body: its message is complete with its head, and its bytes leave the buffer
// for the head of the response that follows it
static HttpClientFeed http_client_response_head_skip (
	CerverHttpClientRequest *request, const size_t consumed
) {

	CerverHttpClientResponse *response = &request->response;

	HttpClientFeed feed = HTTP_CLIENT_FEED_FAILED;

	size_t none = 0;
	const char *piece = NULL;
	size_t piece_length = 0;

	const CerverHttpParserResult result = cerver_http_parser_body (
		&response->parser, response->head + consumed, 0, &none, &piece, &piece_length
	);

	if (result == CERVER_HTTP_PARSER_RESULT_COMPLETE) {
		response->head_length -= consumed;

		(void) memmove (response->head, response->head + consumed, response->head_length);

		// the parser starts the next message here: the head that follows a complete message resets it by
		// itself, and with it what it was told about the body of that message
		cerver_http_parser_reset (&response->parser);

		feed = HTTP_CLIENT_FEED_MORE;
	}

	else {
		http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_RESPONSE, 0);
	}

	return feed;

}

// the head of the response is complete: what its buffer has after it is the start of its body
static HttpClientFeed http_client_response_head_done (
	CerverHttpClientRequest *request, const size_t consumed
) {

	CerverHttpClientResponse *response = &request->response;

	response->head_complete = true;

	http_client_response_head_terminate (response);

	return http_client_response_feed_body (request, response->head + consumed, response->head_length - consumed);

}

// reads the heads that the buffer of the response has: the informational ones, and the one of the response
static HttpClientFeed http_client_response_head_parse (CerverHttpClientRequest *request) {

	CerverHttpClientResponse *response = &request->response;

	HttpClientFeed feed = HTTP_CLIENT_FEED_MORE;

	bool parsing = true;
	while (parsing && response->head_length) {
		// the response of a HEAD has no body, whatever its head says: the parser is told before each head
		response->parser.skip_body = (request->method == REQUEST_METHOD_HEAD);

		size_t consumed = 0;
		const CerverHttpParserResult result = cerver_http_parser_head (
			&response->parser, response->head, response->head_length, &consumed
		);

		switch (result) {
			case CERVER_HTTP_PARSER_RESULT_MORE:
				parsing = false;
				break;

			case CERVER_HTTP_PARSER_RESULT_HEAD:
				if (!http_client_response_has_status (response)) {
					feed = http_client_response_head_rejected (request);
					parsing = false;
				}

				else if (http_client_response_is_informational (response)) {
					feed = http_client_response_head_skip (request, consumed);
					parsing = (feed == HTTP_CLIENT_FEED_MORE);
				}

				else {
					feed = http_client_response_head_done (request, consumed);
					parsing = false;
				}
				break;

			case CERVER_HTTP_PARSER_RESULT_FIELDS:
				http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_SIZE, 0);

				feed = HTTP_CLIENT_FEED_FAILED;
				parsing = false;
				break;

			default:
				http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_RESPONSE, 0);

				feed = HTTP_CLIENT_FEED_FAILED;
				parsing = false;
				break;
		}
	}

	return feed;

}

// the bytes of the head of a response: the parser reads a head from its first byte, so they are kept together
static HttpClientFeed http_client_response_feed_head (
	CerverHttpClientRequest *request, const char *data, const size_t length
) {

	CerverHttpClientResponse *response = &request->response;

	HttpClientFeed feed = HTTP_CLIENT_FEED_FAILED;

	if ((response->head_length + length) > HTTP_CLIENT_HEAD_SIZE) {
		http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_SIZE, 0);
	}

	else if (!http_client_buffer_reserve (&response->head, &response->head_capacity, response->head_length + length + 1)) {
		http_client_request_error (request, CERVER_HTTP_CLIENT_ERROR_MEMORY, 0);
	}

	else {
		(void) memcpy (response->head + response->head_length, data, length);
		response->head_length += length;

		feed = http_client_response_head_parse (request);
	}

	return feed;

}

static HttpClientFeed http_client_response_feed (
	CerverHttpClientRequest *request, const char *data, const size_t length
) {

	request->response.started = true;

	return request->response.head_complete ?
		http_client_response_feed_body (request, data, length) :
		http_client_response_feed_head (request, data, length);

}

unsigned int http_client_response_read (
	CerverHttpClientRequest *request, const char *data, const size_t length
) {

	return (unsigned int) http_client_response_feed (request, data, length);

}

#pragma endregion

#pragma region exchange

// the response of a request is complete: its connection stays for the next request when the response says so,
// it has nothing after it, and the whole request was sent
static void http_client_exchange_done (CerverHttpClientRequest *request) {

	HttpClientConnection *connection = http_client_request_detach (request);

	if (connection) {
		const bool reusable = cerver_http_parser_keep_alive (&request->response.parser)
			&& !request->response.exceeded && !request->response.ended
			&& (request->sent == request->output_length)
			&& !(connection->source.handle.operations & EVENT_OPERATION_SEND);

		http_client_connection_release (connection, reusable);
	}

	http_client_request_finish (request);

}

// a request ends with an error. The one whose address a thread is looking for stays with that thread: who sent it
// is told now, and the loop leaves the request when the thread gives it back
static void http_client_exchange_fail (
	CerverHttpClientRequest *request, const CerverHttpClientError error, const int code
) {

	http_client_request_error (request, error, code);

	HttpClientConnection *connection = http_client_request_detach (request);
	if (connection) http_client_connection_close (connection);

	if (request->state == HTTP_CLIENT_STATE_RESOLVE) {
		event_loop_source_timer_clear (&request->source);

		http_client_request_complete (request);
	}

	else {
		http_client_request_finish (request);
	}

}

// a connection that was kept can have been closed by its server at any moment: a request that found it like
// that, before any byte of its response, is sent again in a new connection, once
static void http_client_exchange_broken (
	CerverHttpClientRequest *request, const CerverHttpClientError error, const int code
) {

	const HttpClientConnection *connection = request->connection;

	if (connection && connection->reused && !request->response.started && !request->retried) {
		request->retried = true;
		request->sent = 0;

		http_client_connection_close (http_client_request_detach (request));

		http_client_exchange_connect (request);
	}

	else {
		http_client_exchange_fail (request, error, code);
	}

}

static void http_client_exchange_send (CerverHttpClientRequest *request) {

	HttpClientConnection *connection = request->connection;

	if (event_backend_send (
		http_client_connection_backend (connection), &connection->source.handle,
		request->output + request->sent, request->output_length - request->sent
	)) {
		http_client_exchange_broken (request, CERVER_HTTP_CLIENT_ERROR_SEND, errno);
	}

}

// the request has a connection that is connected: its response is read from now on, and its bytes are sent
static void http_client_exchange_begin (CerverHttpClientRequest *request) {

	HttpClientConnection *connection = request->connection;

	request->state = HTTP_CLIENT_STATE_EXCHANGE;

	event_loop_source_timer_clear (&connection->source);

	if (!http_client_connection_listens (connection)) {
		http_client_exchange_broken (request, CERVER_HTTP_CLIENT_ERROR_RECEIVE, errno);
	}

	else {
		http_client_exchange_send (request);
	}

}

static void http_client_exchange_sent (CerverHttpClientRequest *request, const int result) {

	if (result < 0) {
		http_client_exchange_broken (request, CERVER_HTTP_CLIENT_ERROR_SEND, -result);
	}

	else {
		request->sent += (size_t) result;

		if (request->sent < request->output_length) http_client_exchange_send (request);
	}

}

// the server ended the connection: the response whose body ends with it is complete, any other one is not
static void http_client_exchange_ended (CerverHttpClientRequest *request) {

	CerverHttpClientResponse *response = &request->response;

	response->ended = true;

	if (
		response->head_complete
		&& (cerver_http_parser_finish (&response->parser) == CERVER_HTTP_PARSER_RESULT_COMPLETE)
	) {
		response->complete = true;

		http_client_exchange_done (request);
	}

	else {
		http_client_exchange_broken (request, CERVER_HTTP_CLIENT_ERROR_RECEIVE, 0);
	}

}

static void http_client_exchange_received (
	CerverHttpClientRequest *request, const char *data, const size_t length
) {

	HttpClientConnection *connection = request->connection;

	switch (http_client_response_feed (request, data, length)) {
		case HTTP_CLIENT_FEED_MORE:
			if (event_backend_receive (http_client_connection_backend (connection), &connection->source.handle)) {
				http_client_exchange_fail (request, CERVER_HTTP_CLIENT_ERROR_RECEIVE, errno);
			}
			break;

		case HTTP_CLIENT_FEED_COMPLETE:
			http_client_exchange_done (request);
			break;

		default:
			http_client_exchange_fail (request, request->response.error, request->response.error_code);
			break;
	}

}

#pragma endregion

#pragma region connect

static int http_client_socket_error (const int fd) {

	int error = 0;
	socklen_t length = sizeof (error);

	if (getsockopt (fd, SOL_SOCKET, SO_ERROR, &error, &length)) error = errno;

	return error;

}

// a connection could not connect to an address of the host of its request: the request tries the next one,
// and fails when none is left
static void http_client_exchange_unconnected (CerverHttpClientRequest *request, const int error) {

	request->next_address += 1;

	if (request->next_address < request->n_addresses) {
		HttpClientConnection *connection = http_client_request_detach (request);
		if (connection) http_client_connection_close (connection);

		http_client_exchange_open (request);
	}

	else {
		http_client_exchange_fail (request, CERVER_HTTP_CLIENT_ERROR_CONNECT, error);
	}

}

// the socket of a connection that was connecting can be written, or it failed
static void http_client_connection_writable (HttpClientConnection *connection, const int result) {

	CerverHttpClientRequest *request = connection->request;

	if (request && !connection->connected) {
		const int error = (result < 0) ? -result : http_client_socket_error (connection->source.handle.fd);

		if (error) {
			http_client_exchange_unconnected (request, error);
		}

		else {
			connection->connected = true;

			http_client_exchange_begin (request);
		}
	}

}

// a connection does not wait to join small writes: a request is sent whole
static void http_client_socket_no_delay (const int fd) {

	const int enabled = 1;

	(void) setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (enabled));

}

// the connect of a socket that does not block: it is connected at once, or its loop is told when it can be
// written, before the time that a connection has to connect
static void http_client_connection_connect (
	CerverHttpClientRequest *request, HttpClientConnection *connection
) {

	const int fd = connection->source.handle.fd;

	const HttpClientAddress *address = &request->addresses[request->next_address];

	if (!connect (fd, (const struct sockaddr *) &address->address, address->length)) {
		connection->connected = true;

		http_client_exchange_begin (request);
	}

	else if (
		(errno == EINPROGRESS)
		&& !event_backend_writable (http_client_connection_backend (connection), &connection->source.handle)
	) {
		event_loop_source_timer_set (&connection->source, request->client->connect_timeout);
	}

	else {
		http_client_exchange_unconnected (request, errno);
	}

}

// a new connection to the address of the host of a request that is tried next
static void http_client_exchange_open (CerverHttpClientRequest *request) {

	const int fd = socket (
		request->addresses[request->next_address].address.ss_family,
		SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0
	);

	HttpClientConnection *connection = (fd > -1) ? http_client_connection_new (request, fd) : NULL;

	if (connection) {
		http_client_socket_no_delay (fd);

		request->connection = connection;
		connection->request = request;

		request->state = HTTP_CLIENT_STATE_CONNECT;

		http_client_connection_connect (request, connection);
	}

	else {
		const int error = errno;

		if (fd > -1) (void) close (fd);

		// without a socket the system said why; with one, its loop ends or there is no memory
		http_client_exchange_fail (
			request, (fd > -1) ? CERVER_HTTP_CLIENT_ERROR_STOPPED : CERVER_HTTP_CLIENT_ERROR_CONNECT, error
		);
	}

}

#pragma endregion

#pragma region resolve

// the first addresses that were found for the host of a request, in their order: a name can have an address of
// each family, and its server can listen in only one of them
static void http_client_resolve_take (CerverHttpClientRequest *request, const struct addrinfo *found) {

	unsigned int count = 0;

	for (const struct addrinfo *info = found; info && (count < HTTP_CLIENT_ADDRESSES); info = info->ai_next) {
		if (info->ai_addrlen <= sizeof (struct sockaddr_storage)) {
			(void) memcpy (&request->addresses[count].address, info->ai_addr, info->ai_addrlen);
			request->addresses[count].length = info->ai_addrlen;

			count += 1;
		}
	}

	request->n_addresses = count;

}

// looks for the address of the host of a request and gives the request back to its loop; in a thread of the
// pool of the cerver of its client, or in its own thread for a client alone. The client is not released while
// one of these runs: the thread still uses the loop after it gave the request
static void http_client_resolve_work (void *request_ptr) {

	CerverHttpClientRequest *request = (CerverHttpClientRequest *) request_ptr;
	CerverHttpClient *client = request->client;

	char port[HTTP_CLIENT_PORT_SIZE] = { 0 };
	(void) snprintf (port, HTTP_CLIENT_PORT_SIZE, HTTP_CLIENT_PORT_FORMAT, (unsigned int) request->port);

	struct addrinfo hints;
	(void) memset (&hints, 0, sizeof (hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *found = NULL;
	request->resolve_code = getaddrinfo (request->host, port, &hints, &found);

	if (!request->resolve_code) http_client_resolve_take (request, found);

	if (found) freeaddrinfo (found);

	event_loop_source_give (&request->source);

	(void) __atomic_sub_fetch (&client->n_resolving, 1, __ATOMIC_SEQ_CST);

}

static void *http_client_resolve_thread (void *request_ptr) {

	http_client_resolve_work (request_ptr);

	return NULL;

}

// a client alone has no pool of threads: a thread of its own looks for each address
static bool http_client_resolve_alone (CerverHttpClientRequest *request) {

	pthread_t thread = 0;

	const bool started = !pthread_create (&thread, NULL, http_client_resolve_thread, request);

	if (started) (void) pthread_detach (thread);

	return started;

}

static bool http_client_resolve_submit (CerverHttpClientRequest *request) {

	const CerverHttpClient *client = request->client;

	return client->http_cerver ?
		!thpool_add_work (client->http_cerver->cerver->thpool, http_client_resolve_work, request) :
		http_client_resolve_alone (request);

}

// the address of a host that is a name is looked for outside of the loop: getaddrinfo () waits
static void http_client_exchange_resolve (CerverHttpClientRequest *request) {

	CerverHttpClient *client = request->client;

	request->state = HTTP_CLIENT_STATE_RESOLVE;

	(void) __atomic_add_fetch (&client->n_resolving, 1, __ATOMIC_SEQ_CST);

	if (!http_client_resolve_submit (request)) {
		(void) __atomic_sub_fetch (&client->n_resolving, 1, __ATOMIC_SEQ_CST);

		// no thread has the request
		request->state = HTTP_CLIENT_STATE_CONNECT;

		http_client_exchange_fail (request, CERVER_HTTP_CLIENT_ERROR_RESOLVE, EAGAIN);
	}

}

// the thread that looked for the address of a request gave it back; a request that ended while it was there
// (its time ended, or its loop does) only waited to be left
static void http_client_exchange_resolved (CerverHttpClientRequest *request) {

	if (request->completed) {
		http_client_request_release (request);
	}

	else if (request->n_addresses) {
		http_client_exchange_open (request);
	}

	else {
		request->state = HTTP_CLIENT_STATE_CONNECT;

		http_client_exchange_fail (request, CERVER_HTTP_CLIENT_ERROR_RESOLVE, request->resolve_code);
	}

}

static void http_client_exchange_connect (CerverHttpClientRequest *request) {

	if (request->n_addresses) http_client_exchange_open (request);
	else http_client_exchange_resolve (request);

}

#pragma endregion

#pragma region start

// the time of a request counts from the moment that its loop takes it
static void http_client_exchange_deadline (CerverHttpClientRequest *request) {

	const u64 timeout = request->timeout ? request->timeout : request->client->request_timeout;

	if (timeout) {
		request->deadline = request->source.loop->now + timeout;

		event_loop_source_timer_set (&request->source, timeout);
	}

}

// takes a connection that its loop kept for the destination of a request, NULL without one
static HttpClientConnection *http_client_exchange_take (CerverHttpClientRequest *request) {

	CerverHttpClient *client = request->client;

	HttpClientPool *pool = http_client_pool_of (client, request->source.loop);

	HttpClientConnection *connection = http_client_pool_find (pool, request->destination);
	if (connection) {
		http_client_pool_remove (pool, connection);

		event_loop_source_timer_clear (&connection->source);

		request->connection = connection;
		connection->request = request;

		(void) __atomic_add_fetch (&client->n_reused, 1, __ATOMIC_SEQ_CST);
	}

	return connection;

}

// the loop of a request took it: it goes out in a connection that was kept for its destination, or in a new one
static void http_client_exchange_start (CerverHttpClientRequest *request) {

	event_loop_source_attach (&request->source);

	http_client_exchange_deadline (request);

	if (http_client_exchange_take (request)) http_client_exchange_begin (request);
	else http_client_exchange_connect (request);

}

#pragma endregion

#pragma region sources

static void http_client_request_given (EventSource *source) {

	CerverHttpClientRequest *request = (CerverHttpClientRequest *) source;

	switch (request->state) {
		case HTTP_CLIENT_STATE_GIVEN: http_client_exchange_start (request); break;
		case HTTP_CLIENT_STATE_RESOLVE: http_client_exchange_resolved (request); break;

		default: break;
	}

}

static void http_client_request_expired (EventSource *source) {

	http_client_exchange_fail ((CerverHttpClientRequest *) source, CERVER_HTTP_CLIENT_ERROR_TIMEOUT, ETIMEDOUT);

}

// the loop of a request ends before its response: a request that already ended only waits for the thread that
// looks for its address
static void http_client_request_stopped (EventSource *source) {

	CerverHttpClientRequest *request = (CerverHttpClientRequest *) source;

	if (!request->completed) http_client_exchange_fail (request, CERVER_HTTP_CLIENT_ERROR_STOPPED, 0);

}

const EventSourceMethods http_client_request_methods = {
	.completed = NULL,
	.expired = http_client_request_expired,
	.given = http_client_request_given,
	.stopped = http_client_request_stopped
};

// the bytes of a connection, or its end: the ones of the response of its request. A connection that is kept
// waits for nothing: whatever arrives closes it
static void http_client_connection_received (
	HttpClientConnection *connection, const EventCompletion *completion
) {

	CerverHttpClientRequest *request = connection->request;

	if (!request) http_client_connection_close (connection);
	else if (completion->result < 0) http_client_exchange_broken (request, CERVER_HTTP_CLIENT_ERROR_RECEIVE, -completion->result);
	else if (!completion->result) http_client_exchange_ended (request);
	else http_client_exchange_received (request, completion->data, (size_t) completion->result);

}

static void http_client_connection_sent (HttpClientConnection *connection, const int result) {

	if (connection->request) http_client_exchange_sent (connection->request, result);

}

static void http_client_connection_completed (EventSource *source, const EventCompletion *completion) {

	HttpClientConnection *connection = (HttpClientConnection *) source;

	switch (completion->operation) {
		case EVENT_OPERATION_STOP: http_client_connection_free (connection); break;
		case EVENT_OPERATION_WRITABLE: http_client_connection_writable (connection, completion->result); break;
		case EVENT_OPERATION_SEND: http_client_connection_sent (connection, completion->result); break;
		case EVENT_OPERATION_RECEIVE: http_client_connection_received (connection, completion); break;

		default: break;
	}

}

// the time of a connection ended: the one that it had to connect, or the one that it is kept without a request
static void http_client_connection_expired (EventSource *source) {

	HttpClientConnection *connection = (HttpClientConnection *) source;

	if (connection->request) {
		http_client_exchange_unconnected (connection->request, ETIMEDOUT);
	}

	else {
		http_client_connection_close (connection);
	}

}

// the loop of a connection ends: one that is kept is closed, the one of a request ends with its request
static void http_client_connection_stopped (EventSource *source) {

	HttpClientConnection *connection = (HttpClientConnection *) source;

	if (!connection->request) http_client_connection_close (connection);

}

const EventSourceMethods http_client_connection_methods = {
	.completed = http_client_connection_completed,
	.expired = http_client_connection_expired,
	.given = NULL,
	.stopped = http_client_connection_stopped
};

// the client of a pool is being deleted: its loop closes the connections that it keeps. The thread that deletes
// the client releases the pool once it is closed: that is the last thing that the loop does with it
static void http_client_pool_close (EventSource *source) {

	HttpClientPool *pool = (HttpClientPool *) source;

	// a connection that is closed leaves the list, and it can be released at once: the next one is read first
	HttpClientConnection *connection = pool->idle;
	while (connection) {
		HttpClientConnection *next = connection->next;

		http_client_connection_close (connection);

		connection = next;
	}

	event_loop_source_remove (source);

	__atomic_store_n (&pool->state, HTTP_CLIENT_POOL_STATE_CLOSED, __ATOMIC_SEQ_CST);

}

const EventSourceMethods http_client_pool_methods = {
	.completed = NULL,
	.expired = NULL,
	.given = http_client_pool_close,
	.stopped = http_client_pool_close
};

#pragma endregion
