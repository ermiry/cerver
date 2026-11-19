#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/types.h>

#include <cerver/events/events.h>

#include <cerver/http/clients/client.h>
#include "cerver/http/clients/internal.h"
#include <cerver/http/core/context.h>
#include <cerver/http/requests/method.h>

#include "../../handler.h"
#include "../../test.h"
#include "../http.h"

// the client of the tests is alone, with its own event loop, and talks to a server of the test that answers
// each path with the bytes of a script: the test decides every byte and every close that the client sees

// the bytes that the server keeps of a request, and the ones of a url or of a body of the tests
#define CLIENT_TEST_REQUEST_SIZE			4096
#define CLIENT_TEST_URL_SIZE				256
#define CLIENT_TEST_BODY_SIZE				256

// the connections that the server has at the same time at most
#define CLIENT_TEST_CONNECTIONS				64

#define CLIENT_TEST_BACKLOG					64

// the milliseconds between two looks of a thread of the server at its end
#define CLIENT_TEST_SERVER_POLL				20

// secs that a test waits for a response or for the server
#define CLIENT_TEST_WAIT					16

// the milliseconds of the request that is never answered, and the ones that a connection is kept in the test
// of that time
#define CLIENT_TEST_SHORT_TIMEOUT			200
#define CLIENT_TEST_IDLE_TIMEOUT			100

// the bytes of the body that the client does not take, and its limit
#define CLIENT_TEST_BIG_LIMIT				16

// the requests that are sent together
#define CLIENT_TEST_MANY					32

// a port that nobody listens to: the one of a socket that was bound and closed
#define CLIENT_TEST_STATUS_OK				200
#define CLIENT_TEST_STATUS_NOT_FOUND		404

static const char *client_test_address = { "127.0.0.1" };

static const char *client_head_end = { "\r\n\r\n" };
#define CLIENT_HEAD_END_LENGTH				4

static const char *client_content_length_name = { "Content-Length:" };
static const char *client_content_type_name = { "Content-Type:" };
static const char *client_custom_name = { "X-Custom:" };

static const char *client_text_body = { "hello" };

static const char *client_text_response = {
	"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhello"
};

static const char *client_chunked_response = {
	"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nhel\r\n2\r\nlo\r\n0\r\n\r\n"
};

// its body ends with its connection
static const char *client_close_response = {
	"HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nhello"
};

static const char *client_continue_response = { "HTTP/1.1 100 Continue\r\n\r\n" };

// the response to a HEAD says the length of the body that it does not have
static const char *client_head_response = { "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n" };

// an informational response before the one to a HEAD, that has no length and whose connection sent more bytes
static const char *client_head_after_continue_response = {
	"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nhello"
};

static const char *client_not_found_response = { "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n" };

static const char *client_invalid_response = { "THIS IS NOT HTTP\r\n\r\n" };

// a status has three digits and the first one is its class, from 1: there is no status under 100
static const char *client_no_status_response = { "HTTP/1.1 000 OK\r\nContent-Length: 0\r\n\r\n" };

static const char *client_big_response = {
	"HTTP/1.1 200 OK\r\nContent-Length: 32\r\n\r\n0123456789abcdef0123456789abcdef"
};

#define CLIENT_ECHO_RESPONSE_FORMAT			\
	"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nX-Custom: %s\r\nContent-Length: %zu\r\n\r\n"

static const char *client_json_type = { "application/json" };
static const char *client_json_body = { "{\"value\": 7}" };
static const char *client_custom_value = { "custom value" };

#pragma region server

typedef struct ClientServer {

	int listen_fd;
	u16 port;
	pthread_t thread;

	unsigned int stopping;

	// the connections that it accepted and the ones whose thread ended, and the requests that it read
	unsigned int accepted;
	unsigned int closed;
	unsigned int requests;

	pthread_mutex_t mutex;
	char last_request[CLIENT_TEST_REQUEST_SIZE];

} ClientServer;

// a connection of the server, of its own thread
typedef struct ClientServerConnection {

	ClientServer *server;
	int fd;
	// the requests that it answered
	unsigned int served;

} ClientServerConnection;

static bool client_server_is_stopping (const ClientServer *server) {

	return __atomic_load_n (&server->stopping, __ATOMIC_SEQ_CST) != 0;

}

// waits until the socket can be read, false when the server stops first
static bool client_server_wait (const ClientServer *server, const int fd) {

	bool readable = false;

	while (!readable && !client_server_is_stopping (server)) {
		struct pollfd waited = { fd, POLLIN, 0 };

		readable = poll (&waited, 1, CLIENT_TEST_SERVER_POLL) > 0;
	}

	return readable;

}

static void client_server_write (const int fd, const char *data, const size_t length) {

	size_t written = 0;
	bool failed = false;

	while ((written < length) && !failed) {
		const ssize_t result = send (fd, data + written, length - written, MSG_NOSIGNAL);

		if (result > 0) written += (size_t) result;
		else failed = true;
	}

}

static void client_server_send (const int fd, const char *response) {

	client_server_write (fd, response, strlen (response));

}

// the value of a header of a request, copied until the end of its line; empty without it
static void client_server_header (const char *request, const char *name, char *value, const size_t size) {

	value[0] = '\0';

	const char *found = strcasestr (request, name);
	if (found) {
		found += strlen (name);
		while (*found == ' ') found += 1;

		size_t length = strcspn (found, "\r");
		if (length >= size) length = size - 1;

		(void) memcpy (value, found, length);
		value[length] = '\0';
	}

}

static size_t client_server_content_length (const char *request) {

	char value[CLIENT_TEST_BODY_SIZE] = { 0 };
	client_server_header (request, client_content_length_name, value, sizeof (value));

	return (size_t) strtoul (value, NULL, 10);

}

// reads a request, its head and the body that its Content-Length says; returns its bytes, 0 when the
// connection ended or the server stops
static size_t client_server_read (ClientServerConnection *connection, char *request) {

	size_t length = 0;
	size_t expected = 0;
	bool complete = false;
	bool ended = false;

	while (!complete && !ended) {
		if (!client_server_wait (connection->server, connection->fd)) {
			ended = true;
		}

		else {
			const ssize_t result = recv (
				connection->fd, request + length, CLIENT_TEST_REQUEST_SIZE - length - 1, 0
			);

			if (result <= 0) {
				ended = true;
			}

			else {
				length += (size_t) result;
				request[length] = '\0';

				const char *head_end = strstr (request, client_head_end);
				if (head_end && !expected) {
					expected = (size_t) (head_end - request) + CLIENT_HEAD_END_LENGTH
						+ client_server_content_length (request);
				}

				complete = expected && (length >= expected);
			}
		}
	}

	return complete ? length : 0;

}

static bool client_server_path_is (const char *request, const char *path) {

	const char *target = strchr (request, ' ');

	const size_t length = strlen (path);

	return target && !strncmp (target + 1, path, length) && (target[1 + length] == ' ');

}

// answers with the content type, the custom header and the body of the request
static void client_server_echo (const int fd, const char *request) {

	char type[CLIENT_TEST_BODY_SIZE] = { 0 };
	char custom[CLIENT_TEST_BODY_SIZE] = { 0 };
	client_server_header (request, client_content_type_name, type, sizeof (type));
	client_server_header (request, client_custom_name, custom, sizeof (custom));

	const char *body = strstr (request, client_head_end) + CLIENT_HEAD_END_LENGTH;
	const size_t body_length = strlen (body);

	char head[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	(void) snprintf (head, sizeof (head), CLIENT_ECHO_RESPONSE_FORMAT, type, custom, body_length);

	client_server_send (fd, head);
	client_server_write (fd, body, body_length);

}

// the request that is never answered: its connection is read until its client leaves, or the server stops
static void client_server_hold (ClientServerConnection *connection) {

	char discarded[CLIENT_TEST_BODY_SIZE];

	while (
		client_server_wait (connection->server, connection->fd)
		&& (recv (connection->fd, discarded, sizeof (discarded), 0) > 0)
	);

}

// answers a request with the script of its path; returns whether the connection goes on
static bool client_server_answer (ClientServerConnection *connection, const char *request) {

	const int fd = connection->fd;

	bool keep = true;

	if (client_server_path_is (request, "/text")) client_server_send (fd, client_text_response);

	else if (client_server_path_is (request, "/chunked")) client_server_send (fd, client_chunked_response);

	else if (client_server_path_is (request, "/close")) {
		client_server_send (fd, client_close_response);
		keep = false;
	}

	else if (client_server_path_is (request, "/continue")) {
		client_server_send (fd, client_continue_response);
		client_server_send (fd, client_text_response);
	}

	else if (client_server_path_is (request, "/head")) client_server_send (fd, client_head_response);

	else if (client_server_path_is (request, "/echo")) client_server_echo (fd, request);

	else if (client_server_path_is (request, "/slow")) {
		client_server_hold (connection);
		keep = false;
	}

	else if (client_server_path_is (request, "/invalid")) client_server_send (fd, client_invalid_response);

	else if (client_server_path_is (request, "/big")) client_server_send (fd, client_big_response);

	else if (client_server_path_is (request, "/drop")) keep = false;

	// the first request of a connection is answered, the next one finds it closed
	else if (client_server_path_is (request, "/retry")) {
		if (connection->served) keep = false;
		else client_server_send (fd, client_text_response);
	}

	// answered, and closed right after
	else if (client_server_path_is (request, "/gone")) {
		client_server_send (fd, client_text_response);
		keep = false;
	}

	else client_server_send (fd, client_not_found_response);

	connection->served += 1;

	return keep;

}

static void *client_server_connection_thread (void *connection_ptr) {

	ClientServerConnection *connection = (ClientServerConnection *) connection_ptr;
	ClientServer *server = connection->server;

	char *request = (char *) calloc (1, CLIENT_TEST_REQUEST_SIZE);

	bool keep = (request != NULL);
	while (keep && client_server_read (connection, request)) {
		(void) pthread_mutex_lock (&server->mutex);
		(void) strncpy (server->last_request, request, CLIENT_TEST_REQUEST_SIZE - 1);
		(void) pthread_mutex_unlock (&server->mutex);

		test_counter_add (&server->requests);

		keep = client_server_answer (connection, request);
	}

	(void) close (connection->fd);

	free (request);
	free (connection);

	// the last thing that the thread does with its server, that waits for this count before it is released
	test_counter_add (&server->closed);

	return NULL;

}

static void client_server_accept (ClientServer *server) {

	const int fd = accept (server->listen_fd, NULL, NULL);
	if (fd > -1) {
		ClientServerConnection *connection = (ClientServerConnection *) calloc (1, sizeof (ClientServerConnection));
		test_check_ptr (connection);

		connection->server = server;
		connection->fd = fd;

		test_counter_add (&server->accepted);

		pthread_t thread = 0;
		test_check_int_eq (pthread_create (&thread, NULL, client_server_connection_thread, connection), 0, NULL);
		(void) pthread_detach (thread);
	}

}

static void *client_server_thread (void *server_ptr) {

	ClientServer *server = (ClientServer *) server_ptr;

	while (!client_server_is_stopping (server)) {
		if (client_server_wait (server, server->listen_fd)) client_server_accept (server);
	}

	return NULL;

}

static ClientServer *client_server_start (void) {

	ClientServer *server = (ClientServer *) calloc (1, sizeof (ClientServer));
	test_check_ptr (server);

	(void) pthread_mutex_init (&server->mutex, NULL);

	server->listen_fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check_true (server->listen_fd > -1);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	test_check_int_eq (bind (server->listen_fd, (const struct sockaddr *) &address, sizeof (address)), 0, NULL);
	test_check_int_eq (listen (server->listen_fd, CLIENT_TEST_BACKLOG), 0, NULL);

	socklen_t length = sizeof (address);
	test_check_int_eq (getsockname (server->listen_fd, (struct sockaddr *) &address, &length), 0, NULL);
	server->port = ntohs (address.sin_port);

	test_check_int_eq (pthread_create (&server->thread, NULL, client_server_thread, server), 0, NULL);

	return server;

}

static void client_server_stop (ClientServer *server) {

	__atomic_store_n (&server->stopping, 1, __ATOMIC_SEQ_CST);

	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);

	// the threads of its connections see that it stops: it accepts no more, and each one counts when it ends
	const unsigned int accepted = __atomic_load_n (&server->accepted, __ATOMIC_SEQ_CST);
	test_check_unsigned_eq (
		test_wait_counter (&server->closed, accepted, CLIENT_TEST_WAIT * test_time_scale ()), accepted, NULL
	);

	(void) close (server->listen_fd);

	(void) pthread_mutex_destroy (&server->mutex);

	free (server);

}

static void client_server_last_request (ClientServer *server, char *request) {

	(void) pthread_mutex_lock (&server->mutex);
	(void) memcpy (request, server->last_request, CLIENT_TEST_REQUEST_SIZE);
	(void) pthread_mutex_unlock (&server->mutex);

}

// a port of the loopback that nobody listens to: the one of a socket that was bound and closed
static u16 client_test_closed_port (void) {

	const int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check_true (fd > -1);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	test_check_int_eq (bind (fd, (const struct sockaddr *) &address, sizeof (address)), 0, NULL);

	socklen_t length = sizeof (address);
	test_check_int_eq (getsockname (fd, (struct sockaddr *) &address, &length), 0, NULL);

	(void) close (fd);

	return ntohs (address.sin_port);

}

#pragma endregion

#pragma region client

// the backend of the event loop of the clients of the tests that run
static CerverEventsBackend client_test_backend = CERVER_EVENTS_BACKEND_EPOLL;

static CerverHttpClient *client_test_create (void) {

	CerverHttpClient *client = cerver_http_client_create_with_backend (client_test_backend);
	test_check_ptr (client);

	return client;

}

static const char *client_test_url (const ClientServer *server, const char *path) {

	static char url[CLIENT_TEST_URL_SIZE];

	(void) snprintf (url, CLIENT_TEST_URL_SIZE, "http://%s:%u%s", client_test_address, (unsigned int) server->port, path);

	return url;

}

// what a handler kept of a response
typedef struct ClientResult {

	unsigned int done;
	CerverHttpClientError error;
	unsigned int status;
	char body[CLIENT_TEST_BODY_SIZE];

} ClientResult;

static void client_test_handler (const CerverHttpClientResponse *response, void *result_ptr) {

	ClientResult *result = (ClientResult *) result_ptr;

	result->error = cerver_http_client_response_error (response);
	result->status = cerver_http_client_response_status (response);

	const CerverHttpView body = cerver_http_client_response_body (response);
	if (body.data && (body.length < CLIENT_TEST_BODY_SIZE)) (void) memcpy (result->body, body.data, body.length + 1);

	test_counter_add (&result->done);

}

static unsigned int client_test_wait (const unsigned int *counter, const unsigned int expected) {

	return test_wait_counter (counter, expected, CLIENT_TEST_WAIT * test_time_scale ());

}

// sends a request and waits for it: its response has the expected error, status and body (NULL for none)
static CerverHttpClientRequest *client_test_perform (
	CerverHttpClient *client, const CerverHttpMethod method, const char *url,
	const CerverHttpClientError error, const unsigned int status, const char *body
) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (client, method, url);
	test_check_ptr (request);

	test_check_null_ptr (cerver_http_client_request_response (request));

	test_check_unsigned_eq (cerver_http_client_request_perform (request), 0, NULL);

	const CerverHttpClientResponse *response = cerver_http_client_request_response (request);
	test_check_ptr (response);

	test_check_unsigned_eq (cerver_http_client_response_error (response), error, NULL);
	test_check_unsigned_eq (cerver_http_client_response_status (response), status, NULL);

	const CerverHttpView view = cerver_http_client_response_body (response);
	if (body) {
		test_check_ptr (view.data);
		test_check_unsigned_eq (view.length, strlen (body), NULL);
		test_check_str_eq (view.data, body, NULL);
	}

	else {
		test_check_null_ptr (view.data);
		test_check_unsigned_eq (view.length, 0, NULL);
	}

	return request;

}

static void client_test_get (
	CerverHttpClient *client, const char *url,
	const CerverHttpClientError error, const unsigned int status, const char *body
) {

	cerver_http_client_request_delete (client_test_perform (client, REQUEST_METHOD_GET, url, error, status, body));

}

#pragma endregion

#pragma region requests

// what a request takes and what it refuses, without sending anything
static void test_client_request (void) {

	CerverHttpClient *client = client_test_create ();

	test_check_null_ptr (cerver_http_client_request_create (NULL, REQUEST_METHOD_GET, "http://localhost/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, NULL));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "https://localhost/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "localhost/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost:0/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost:65536/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost:port/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost/a b"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost/a#b"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://[::1/"));
	test_check_null_ptr (cerver_http_client_request_create (client, REQUEST_METHOD_GET, "http://localhost/\r\nX: y"));

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_POST, "http://localhost:8080/path?query=1"
	);
	test_check_ptr (request);

	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "X-Custom", "value"), 0, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (NULL, "X-Custom", "value"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, NULL, "value"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "X-Custom", NULL), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "X Custom", "value"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "X-Custom", "a\r\nb: c"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "Host", "other"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "content-length", "1"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "Content-Type", "text/plain"), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "Transfer-Encoding", "chunked"), 1, NULL);

	test_check_unsigned_eq (cerver_http_client_request_set_body (request, "text/plain", "body", 4), 0, NULL);
	test_check_unsigned_eq (cerver_http_client_request_set_body (request, NULL, NULL, 0), 0, NULL);
	test_check_unsigned_eq (cerver_http_client_request_set_body (NULL, NULL, "body", 4), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_set_body (request, NULL, NULL, 4), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_set_body (request, "text\r\nX: y", "body", 4), 1, NULL);

	// a request is not sent without a handler, and it is deleted by the call
	test_check_unsigned_eq (cerver_http_client_request_send (request, NULL, NULL), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_send (NULL, client_test_handler, NULL), 1, NULL);

	test_check_unsigned_eq (cerver_http_client_request_perform (NULL), 1, NULL);

	test_check_null_ptr (cerver_http_client_request_response (NULL));

	cerver_http_client_request_delete (NULL);

	// the getters of a response that is not there
	test_check_unsigned_eq (cerver_http_client_response_error (NULL), CERVER_HTTP_CLIENT_ERROR_MEMORY, NULL);
	test_check_unsigned_eq (cerver_http_client_response_status (NULL), 0, NULL);
	test_check_null_ptr (cerver_http_client_response_header (NULL, "Content-Type").data);
	test_check_null_ptr (cerver_http_client_response_body (NULL).data);

	test_check_str_eq (cerver_http_client_error_to_string (CERVER_HTTP_CLIENT_ERROR_TIMEOUT), "Timeout", NULL);
	test_check_str_eq (cerver_http_client_error_to_string (CERVER_HTTP_CLIENT_ERROR_MAX), "None", NULL);
	test_check_str_eq (
		cerver_http_client_error_description (CERVER_HTTP_CLIENT_ERROR_NONE), "The response is complete", NULL
	);

	cerver_http_client_delete (client);
	cerver_http_client_delete (NULL);

}

// reads a response one byte at a time, like the slowest connection: what the last byte did to it
static unsigned int client_test_read_bytes (CerverHttpClientRequest *request, const char *response) {

	unsigned int result = HTTP_CLIENT_READ_MORE;

	http_client_response_start (request);

	const size_t length = strlen (response);
	for (size_t idx = 0; (idx < length) && (result == HTTP_CLIENT_READ_MORE); idx++) {
		result = http_client_response_read (request, response + idx, 1);
	}

	return result;

}

// reads a response in one piece, like a connection that got all of it at once
static unsigned int client_test_read_piece (CerverHttpClientRequest *request, const char *response) {

	http_client_response_start (request);

	return http_client_response_read (request, response, strlen (response));

}

// a response that was read is complete, with its status and its body (NULL for none)
static void client_test_read_expect (
	CerverHttpClientRequest *request, const unsigned int result, const char *body
) {

	test_check_unsigned_eq (result, HTTP_CLIENT_READ_COMPLETE, NULL);

	const CerverHttpClientResponse *response = &request->response;
	test_check_unsigned_eq (cerver_http_client_response_error (response), CERVER_HTTP_CLIENT_ERROR_NONE, NULL);
	test_check_unsigned_eq (cerver_http_client_response_status (response), CLIENT_TEST_STATUS_OK, NULL);

	const CerverHttpView view = cerver_http_client_response_body (response);
	if (body) test_check_str_eq (view.data, body, NULL);
	else test_check_null_ptr (view.data);

}

// a response that arrives one byte at a time is complete with its last byte, with its status and its body
static void client_test_read_complete (
	CerverHttpClient *client, const CerverHttpMethod method, const char *bytes, const char *body
) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (client, method, "http://localhost/");
	test_check_ptr (request);

	client_test_read_expect (request, client_test_read_bytes (request, bytes), body);

	cerver_http_client_request_delete (request);

}

// the same response when all its bytes arrive in one read
static void client_test_read_whole (
	CerverHttpClient *client, const CerverHttpMethod method, const char *bytes, const char *body
) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (client, method, "http://localhost/");
	test_check_ptr (request);

	client_test_read_expect (request, client_test_read_piece (request, bytes), body);

	cerver_http_client_request_delete (request);

}

// a response that fails while it is read has its error
static void client_test_read_failed (
	CerverHttpClient *client, const char *bytes, const CerverHttpClientError error
) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_GET, "http://localhost/"
	);
	test_check_ptr (request);

	test_check_unsigned_eq (client_test_read_bytes (request, bytes), HTTP_CLIENT_READ_FAILED, NULL);
	test_check_unsigned_eq (cerver_http_client_response_error (&request->response), error, NULL);

	cerver_http_client_request_delete (request);

}

// the bytes of a response in the smallest pieces, without a connection: every kind of body, and the errors
static void test_client_read (void) {

	CerverHttpClient *client = client_test_create ();

	client_test_read_complete (client, REQUEST_METHOD_GET, client_text_response, client_text_body);
	client_test_read_complete (client, REQUEST_METHOD_GET, client_chunked_response, client_text_body);
	client_test_read_complete (client, REQUEST_METHOD_HEAD, client_head_response, NULL);

	char informational[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		informational, sizeof (informational), "%s%s%s",
		client_continue_response, client_continue_response, client_text_response
	);
	client_test_read_complete (client, REQUEST_METHOD_GET, informational, client_text_body);
	client_test_read_whole (client, REQUEST_METHOD_GET, informational, client_text_body);

	// the response of a HEAD has no body after an informational one either, with the bytes that follow its head
	client_test_read_complete (client, REQUEST_METHOD_HEAD, client_head_after_continue_response, NULL);
	client_test_read_whole (client, REQUEST_METHOD_HEAD, client_head_after_continue_response, NULL);

	client_test_read_failed (client, client_invalid_response, CERVER_HTTP_CLIENT_ERROR_RESPONSE);
	client_test_read_failed (client, client_no_status_response, CERVER_HTTP_CLIENT_ERROR_RESPONSE);

	cerver_http_client_set_max_response_size (client, CLIENT_TEST_BIG_LIMIT);
	client_test_read_failed (client, client_big_response, CERVER_HTTP_CLIENT_ERROR_SIZE);

	cerver_http_client_delete (client);

}

// a request that its caller waits for: its status, its headers and its body; and the bytes that it sent
static void test_client_perform (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	CerverHttpClientRequest *request = client_test_perform (
		client, REQUEST_METHOD_GET, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	const CerverHttpClientResponse *response = cerver_http_client_request_response (request);

	const CerverHttpView type = cerver_http_client_response_header (response, "content-type");
	test_check_ptr (type.data);
	test_check_str_eq (type.data, "text/plain", NULL);
	test_check_unsigned_eq (type.length, strlen ("text/plain"), NULL);

	test_check_str_eq (cerver_http_client_response_header (response, "Content-Length").data, "5", NULL);
	test_check_null_ptr (cerver_http_client_response_header (response, "X-Missing").data);
	test_check_null_ptr (cerver_http_client_response_header (response, NULL).data);

	// a request is sent once
	test_check_unsigned_eq (cerver_http_client_request_perform (request), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_request_send (request, client_test_handler, NULL), 1, NULL);

	cerver_http_client_request_delete (request);

	char expected[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		expected, sizeof (expected), "GET /text HTTP/1.1\r\nHost: %s:%u\r\n\r\n",
		client_test_address, (unsigned int) server->port
	);

	char sent[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	client_server_last_request (server, sent);
	test_check_str_eq (sent, expected, NULL);

	// a status that is not a success is a complete response too
	client_test_get (
		client, client_test_url (server, "/missing"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_NOT_FOUND, NULL
	);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// a request with a handler: the loop of the client calls it with the response
static void test_client_send (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	ClientResult result;
	(void) memset (&result, 0, sizeof (result));

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_GET, client_test_url (server, "/text")
	);
	test_check_ptr (request);

	test_check_unsigned_eq (cerver_http_client_request_send (request, client_test_handler, &result), 0, NULL);

	test_check_unsigned_eq (client_test_wait (&result.done, 1), 1, NULL);
	test_check_unsigned_eq (result.error, CERVER_HTTP_CLIENT_ERROR_NONE, NULL);
	test_check_unsigned_eq (result.status, CLIENT_TEST_STATUS_OK, NULL);
	test_check_str_eq (result.body, client_text_body, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// a request with headers and a body: its server gets them, and its Content-Length
static void test_client_body (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_POST, client_test_url (server, "/echo")
	);
	test_check_ptr (request);

	test_check_unsigned_eq (cerver_http_client_request_set_body (
		request, client_json_type, client_json_body, strlen (client_json_body)
	), 0, NULL);

	test_check_unsigned_eq (cerver_http_client_request_add_header (request, "X-Custom", client_custom_value), 0, NULL);

	test_check_unsigned_eq (cerver_http_client_request_perform (request), 0, NULL);

	const CerverHttpClientResponse *response = cerver_http_client_request_response (request);
	test_check_unsigned_eq (cerver_http_client_response_error (response), CERVER_HTTP_CLIENT_ERROR_NONE, NULL);
	test_check_unsigned_eq (cerver_http_client_response_status (response), CLIENT_TEST_STATUS_OK, NULL);
	test_check_str_eq (cerver_http_client_response_body (response).data, client_json_body, NULL);
	test_check_str_eq (cerver_http_client_response_header (response, "Content-Type").data, client_json_type, NULL);
	test_check_str_eq (cerver_http_client_response_header (response, "X-Custom").data, client_custom_value, NULL);

	cerver_http_client_request_delete (request);

	char expected[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		expected, sizeof (expected),
		"POST /echo HTTP/1.1\r\nHost: %s:%u\r\nX-Custom: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s",
		client_test_address, (unsigned int) server->port,
		client_custom_value, client_json_type, strlen (client_json_body), client_json_body
	);

	char sent[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	client_server_last_request (server, sent);
	test_check_str_eq (sent, expected, NULL);

	// a POST without a body says that it has none
	request = cerver_http_client_request_create (client, REQUEST_METHOD_POST, client_test_url (server, "/echo"));
	test_check_ptr (request);
	test_check_unsigned_eq (cerver_http_client_request_perform (request), 0, NULL);
	cerver_http_client_request_delete (request);

	client_server_last_request (server, sent);
	test_check_ptr (strstr (sent, "Content-Length: 0\r\n"));

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// the bodies that a response can have: chunked, until the end of its connection, after an informational
// response, and none for a HEAD
static void test_client_bodies (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	client_test_get (
		client, client_test_url (server, "/chunked"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	client_test_get (
		client, client_test_url (server, "/continue"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	CerverHttpClientRequest *request = client_test_perform (
		client, REQUEST_METHOD_HEAD, client_test_url (server, "/head"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, NULL
	);

	test_check_str_eq (
		cerver_http_client_response_header (cerver_http_client_request_response (request), "Content-Length").data,
		"5", NULL
	);

	cerver_http_client_request_delete (request);

	// the three used the same connection
	test_check_unsigned_eq (cerver_http_client_get_connections (client), 1, NULL);

	client_test_get (
		client, client_test_url (server, "/close"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// the connection of a response is kept for the next request of its destination; the one of a response that
// ends with its connection is not
static void test_client_keep (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	for (unsigned int idx = 0; idx < CLIENT_TEST_MANY; idx++) {
		client_test_get (
			client, client_test_url (server, "/text"),
			CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
		);
	}

	test_check_unsigned_eq (cerver_http_client_get_connections (client), 1, NULL);
	test_check_unsigned_eq (cerver_http_client_get_reused (client), CLIENT_TEST_MANY - 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->accepted, __ATOMIC_SEQ_CST), 1, NULL);

	client_test_get (
		client, client_test_url (server, "/close"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	test_check_unsigned_eq (cerver_http_client_get_connections (client), 2, NULL);

	cerver_http_client_delete (client);

	// the client closed its connections before it was deleted
	test_check_unsigned_eq (client_test_wait (&server->closed, 2), 2, NULL);

	client_server_stop (server);

}

// a client that keeps no connections opens one for each request
static void test_client_no_keep (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();
	cerver_http_client_set_max_idle_connections (client, 0);

	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	test_check_unsigned_eq (cerver_http_client_get_connections (client), 2, NULL);
	test_check_unsigned_eq (cerver_http_client_get_reused (client), 0, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// a connection that was kept is closed when its time ends, and when its server closes it
static void test_client_idle (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();
	cerver_http_client_set_idle_timeout (client, CLIENT_TEST_IDLE_TIMEOUT);

	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	// the time of the connection ends: the client closes it
	test_check_unsigned_eq (client_test_wait (&server->closed, 1), 1, NULL);

	// the server closes the connection after its response: the client finds out, and the next request has a new one
	client_test_get (
		client, client_test_url (server, "/gone"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	test_check_unsigned_eq (client_test_wait (&server->closed, 2), 2, NULL);

	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	test_check_unsigned_eq (cerver_http_client_get_connections (client), 3, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// a request that finds the connection that it took closed by its server is sent again in a new one, once
static void test_client_retry (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	client_test_get (
		client, client_test_url (server, "/retry"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	// the server closes the connection when it reads the second request: the client sends it again
	client_test_get (
		client, client_test_url (server, "/retry"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	test_check_unsigned_eq (cerver_http_client_get_connections (client), 2, NULL);
	test_check_unsigned_eq (cerver_http_client_get_reused (client), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&server->requests, __ATOMIC_SEQ_CST), 3, NULL);

	// a request in a new connection that is closed without a response is not sent again
	client_test_get (client, client_test_url (server, "/drop"), CERVER_HTTP_CLIENT_ERROR_RECEIVE, 0, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// the errors of a request: nobody listens, the response does not arrive in time, it is not HTTP, or it is
// bigger than the limit
static void test_client_errors (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();
	cerver_http_client_set_max_response_size (client, CLIENT_TEST_BIG_LIMIT);

	char url[CLIENT_TEST_URL_SIZE] = { 0 };
	(void) snprintf (url, sizeof (url), "http://%s:%u/", client_test_address, (unsigned int) client_test_closed_port ());

	client_test_get (client, url, CERVER_HTTP_CLIENT_ERROR_CONNECT, 0, NULL);

	client_test_get (client, client_test_url (server, "/invalid"), CERVER_HTTP_CLIENT_ERROR_RESPONSE, 0, NULL);

	// the head of the response arrived: its status can be read, and the part of its body that fit
	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_GET, client_test_url (server, "/big")
	);
	test_check_ptr (request);
	test_check_unsigned_eq (cerver_http_client_request_perform (request), 0, NULL);
	test_check_unsigned_eq (
		cerver_http_client_response_error (cerver_http_client_request_response (request)),
		CERVER_HTTP_CLIENT_ERROR_SIZE, NULL
	);
	test_check_unsigned_eq (
		cerver_http_client_response_status (cerver_http_client_request_response (request)),
		CLIENT_TEST_STATUS_OK, NULL
	);
	cerver_http_client_request_delete (request);

	// the time of one request, shorter than the one of its client
	request = cerver_http_client_request_create (client, REQUEST_METHOD_GET, client_test_url (server, "/slow"));
	test_check_ptr (request);
	cerver_http_client_request_set_timeout (request, CLIENT_TEST_SHORT_TIMEOUT);
	test_check_unsigned_eq (cerver_http_client_request_perform (request), 0, NULL);
	test_check_unsigned_eq (
		cerver_http_client_response_error (cerver_http_client_request_response (request)),
		CERVER_HTTP_CLIENT_ERROR_TIMEOUT, NULL
	);
	cerver_http_client_request_delete (request);

	// the client still works
	cerver_http_client_set_max_response_size (client, CERVER_HTTP_CLIENT_DEFAULT_MAX_RESPONSE_SIZE);
	client_test_get (
		client, client_test_url (server, "/text"),
		CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body
	);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// the host of a url can be a name: a thread looks for its address; one that does not exist is an error
static void test_client_resolve (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	char url[CLIENT_TEST_URL_SIZE] = { 0 };
	(void) snprintf (url, sizeof (url), "http://localhost:%u/text", (unsigned int) server->port);

	client_test_get (client, url, CERVER_HTTP_CLIENT_ERROR_NONE, CLIENT_TEST_STATUS_OK, client_text_body);

	char expected[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		expected, sizeof (expected), "GET /text HTTP/1.1\r\nHost: localhost:%u\r\n\r\n", (unsigned int) server->port
	);

	char sent[CLIENT_TEST_REQUEST_SIZE] = { 0 };
	client_server_last_request (server, sent);
	test_check_str_eq (sent, expected, NULL);

	client_test_get (client, "http://name.that.does.not.exist.invalid/", CERVER_HTTP_CLIENT_ERROR_RESOLVE, 0, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// many requests that are sent together: each one gets its response, in the connections that they need
static void test_client_many (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	ClientResult results[CLIENT_TEST_MANY];
	(void) memset (results, 0, sizeof (results));

	for (unsigned int idx = 0; idx < CLIENT_TEST_MANY; idx++) {
		CerverHttpClientRequest *request = cerver_http_client_request_create (
			client, REQUEST_METHOD_GET, client_test_url (server, (idx % 2) ? "/text" : "/chunked")
		);
		test_check_ptr (request);

		test_check_unsigned_eq (
			cerver_http_client_request_send (request, client_test_handler, &results[idx]), 0, NULL
		);
	}

	for (unsigned int idx = 0; idx < CLIENT_TEST_MANY; idx++) {
		test_check_unsigned_eq (client_test_wait (&results[idx].done, 1), 1, NULL);
		test_check_unsigned_eq (results[idx].error, CERVER_HTTP_CLIENT_ERROR_NONE, NULL);
		test_check_unsigned_eq (results[idx].status, CLIENT_TEST_STATUS_OK, NULL);
		test_check_str_eq (results[idx].body, client_text_body, NULL);
	}

	test_check_unsigned_eq (__atomic_load_n (&server->requests, __ATOMIC_SEQ_CST), CLIENT_TEST_MANY, NULL);

	cerver_http_client_delete (client);

	client_server_stop (server);

}

// a client that is deleted with requests that have not ended: each one fails, and its handler was called
// when the delete returns
static void test_client_delete (void) {

	ClientServer *server = client_server_start ();

	CerverHttpClient *client = client_test_create ();

	ClientResult results[CLIENT_TEST_MANY];
	(void) memset (results, 0, sizeof (results));

	for (unsigned int idx = 0; idx < CLIENT_TEST_MANY; idx++) {
		CerverHttpClientRequest *request = cerver_http_client_request_create (
			client, REQUEST_METHOD_GET, client_test_url (server, "/slow")
		);
		test_check_ptr (request);

		test_check_unsigned_eq (
			cerver_http_client_request_send (request, client_test_handler, &results[idx]), 0, NULL
		);
	}

	// the server has their requests
	test_check_unsigned_eq (client_test_wait (&server->requests, CLIENT_TEST_MANY), CLIENT_TEST_MANY, NULL);

	cerver_http_client_delete (client);

	for (unsigned int idx = 0; idx < CLIENT_TEST_MANY; idx++) {
		test_check_unsigned_eq (__atomic_load_n (&results[idx].done, __ATOMIC_SEQ_CST), 1, NULL);
		test_check_unsigned_eq (results[idx].error, CERVER_HTTP_CLIENT_ERROR_STOPPED, NULL);
	}

	client_server_stop (server);

}

#pragma endregion

static void client_tests_run (void) {

	test_run (test_client_request);
	test_run (test_client_read);
	test_run (test_client_perform);
	test_run (test_client_send);
	test_run (test_client_body);
	test_run (test_client_bodies);
	test_run (test_client_keep);
	test_run (test_client_no_keep);
	test_run (test_client_idle);
	test_run (test_client_retry);
	test_run (test_client_errors);
	test_run (test_client_resolve);
	test_run (test_client_many);
	test_run (test_client_delete);

}

// io_uring is not available where its system calls are not allowed: its tests are skipped there, unless the
// backend of the tests was asked to be that one
static bool client_test_uring_available (void) {

	CerverHttpClient *client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_URING);

	const bool available = (client != NULL);

	if (available) cerver_http_client_delete (client);
	else (void) printf ("\nio_uring is not available, the tests of the HTTP client with it are skipped\n");

	return available;

}

void http_tests_client (void) {

	(void) printf ("\nTesting the HTTP client...\n");

	if (!test_backend_is (TEST_BACKEND_URING)) {
		client_test_backend = CERVER_EVENTS_BACKEND_EPOLL;

		client_tests_run ();
	}

	if (!test_backend_is (TEST_BACKEND_EPOLL) && (test_backend_is (TEST_BACKEND_URING) || client_test_uring_available ())) {
		client_test_backend = CERVER_EVENTS_BACKEND_URING;

		client_tests_run ();
	}

	(void) printf ("Done!\n");

}
