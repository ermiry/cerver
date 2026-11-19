#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/events/events.h>

#include <cerver/http/clients/client.h>
#include "cerver/http/clients/internal.h"
#include <cerver/http/core/context.h>
#include <cerver/http/requests/method.h>

#include "../test.h"
#include "clients.h"
#include "faults.h"

// the allocations of the HTTP client that fail, with a fault injected in one of its calls: the client and its
// requests are created without a server, and the responses are read without a connection

// the target of the url is the only malloc () of its size, its characters and the null byte
#define FAULTS_CLIENTS_TARGET				"/faults/target"
#define FAULTS_CLIENTS_TARGET_URL			"http://localhost" FAULTS_CLIENTS_TARGET

// a query without a path is copied after a '/', with the null byte
#define FAULTS_CLIENTS_QUERY				"?name=cerver"
#define FAULTS_CLIENTS_QUERY_URL			"http://localhost" FAULTS_CLIENTS_QUERY
#define FAULTS_CLIENTS_QUERY_TARGET			"/" FAULTS_CLIENTS_QUERY

// a url without a path has the root as its target
#define FAULTS_CLIENTS_ROOT					"/"
#define FAULTS_CLIENTS_ROOT_URL				"http://localhost"

#define FAULTS_CLIENTS_URL					"http://localhost/"

// the first header takes the first buffer of the headers, the long one makes it grow to twice its size
#define FAULTS_CLIENTS_HEADER_NAME			"X-Faults"
#define FAULTS_CLIENTS_HEADER_VALUE			"first"
#define FAULTS_CLIENTS_HEADER_LINE			FAULTS_CLIENTS_HEADER_NAME ": " FAULTS_CLIENTS_HEADER_VALUE "\r\n"
#define FAULTS_CLIENTS_LONG_NAME			"X-Long"
#define FAULTS_CLIENTS_LONG_VALUE_SIZE		(HTTP_CLIENT_BUFFER_SIZE + 64)
#define FAULTS_CLIENTS_GROWN_SIZE			(HTTP_CLIENT_BUFFER_SIZE * 2)

static const char faults_clients_long_character = 'v';

// the body that a request has, and the one that fails to replace it: their copies have sizes that no other
// allocation of the path has
#define FAULTS_CLIENTS_TYPE					"text/plain"
#define FAULTS_CLIENTS_BODY					"hello"
#define FAULTS_CLIENTS_NEW_TYPE				"application/json"
#define FAULTS_CLIENTS_NEW_BODY				"{\"value\": 7}"

// a response whose head and body arrive apart: the buffers of both take HTTP_CLIENT_BUFFER_SIZE first
#define FAULTS_CLIENTS_RESPONSE_HEAD		"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"
#define FAULTS_CLIENTS_RESPONSE_BODY		"hello"
#define FAULTS_CLIENTS_RESPONSE				FAULTS_CLIENTS_RESPONSE_HEAD FAULTS_CLIENTS_RESPONSE_BODY

#define FAULTS_CLIENTS_STATUS_OK			200

#pragma region client

// creates a client alone with a fault armed in one of its calls: there is no client
static void faults_clients_create_failed (const FaultsFunction function, const size_t size) {

	const unsigned int triggered = faults_triggered (function);

	faults_arm (function, true, size);
	CerverHttpClient *client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_EPOLL);
	faults_disarm (function);

	test_check_unsigned_eq (faults_triggered (function), triggered + 1, "the client did not fail");
	test_check_null_ptr (client);

}

// the client itself (the first allocation of the path), the pool of its loop (created after the loop, whose
// allocations have other sizes) and the thread of its loop
static void faults_clients_create (void) {

	faults_clients_create_failed (FAULTS_FUNCTION_CALLOC, sizeof (CerverHttpClient));
	faults_clients_create_failed (FAULTS_FUNCTION_CALLOC, sizeof (HttpClientPool));
	faults_clients_create_failed (FAULTS_FUNCTION_THREAD, FAULTS_ANY_SIZE);

	CerverHttpClient *client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_EPOLL);
	test_check_ptr (client);
	cerver_http_client_delete (client);

}

#pragma endregion

#pragma region request

// creates a request with a fault armed in one of its allocations: there is no request
static void faults_clients_request_create_failed (
	CerverHttpClient *client, const char *url, const FaultsFunction function, const size_t size
) {

	const unsigned int triggered = faults_triggered (function);

	faults_arm (function, true, size);
	CerverHttpClientRequest *request = cerver_http_client_request_create (client, REQUEST_METHOD_GET, url);
	faults_disarm (function);

	test_check_unsigned_eq (faults_triggered (function), triggered + 1, "the request did not fail");
	test_check_null_ptr (request);

}

// the same request with memory, and its target
static void faults_clients_request_create_target (
	CerverHttpClient *client, const char *url, const char *target
) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (client, REQUEST_METHOD_GET, url);
	test_check_ptr (request);
	test_check_str_eq (request->target, target, NULL);

	cerver_http_client_request_delete (request);

}

// the request itself and the copy of its target: of its path, of its query after a '/', or of the root
static void faults_clients_request_create (CerverHttpClient *client) {

	faults_clients_request_create_failed (
		client, FAULTS_CLIENTS_URL, FAULTS_FUNCTION_CALLOC, sizeof (CerverHttpClientRequest)
	);

	faults_clients_request_create_failed (
		client, FAULTS_CLIENTS_TARGET_URL, FAULTS_FUNCTION_MALLOC, sizeof (FAULTS_CLIENTS_TARGET)
	);
	faults_clients_request_create_target (client, FAULTS_CLIENTS_TARGET_URL, FAULTS_CLIENTS_TARGET);

	faults_clients_request_create_failed (
		client, FAULTS_CLIENTS_QUERY_URL, FAULTS_FUNCTION_MALLOC, sizeof (FAULTS_CLIENTS_QUERY_TARGET)
	);
	faults_clients_request_create_target (client, FAULTS_CLIENTS_QUERY_URL, FAULTS_CLIENTS_QUERY_TARGET);

	faults_clients_request_create_failed (
		client, FAULTS_CLIENTS_ROOT_URL, FAULTS_FUNCTION_MALLOC, sizeof (FAULTS_CLIENTS_ROOT)
	);
	faults_clients_request_create_target (client, FAULTS_CLIENTS_ROOT_URL, FAULTS_CLIENTS_ROOT);

}

// adds a header with its buffer armed to fail: the headers stay as they were
static void faults_clients_add_header_failed (
	CerverHttpClientRequest *request, const char *name, const char *value, const size_t size
) {

	const size_t length = request->headers_length;
	const size_t capacity = request->headers_capacity;

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_REALLOC);

	faults_arm (FAULTS_FUNCTION_REALLOC, true, size);
	const u8 result = cerver_http_client_request_add_header (request, name, value);
	faults_disarm (FAULTS_FUNCTION_REALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_REALLOC), triggered + 1, "the headers did not grow");
	test_check_unsigned_eq (result, 1, "a header without memory was added");
	test_check_unsigned_eq ((unsigned int) request->headers_length, (unsigned int) length, NULL);
	test_check_unsigned_eq ((unsigned int) request->headers_capacity, (unsigned int) capacity, NULL);

}

// the first buffer of the headers, and the one that it grows to
static void faults_clients_add_header (CerverHttpClient *client) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_GET, FAULTS_CLIENTS_URL
	);
	test_check_ptr (request);

	faults_clients_add_header_failed (
		request, FAULTS_CLIENTS_HEADER_NAME, FAULTS_CLIENTS_HEADER_VALUE, HTTP_CLIENT_BUFFER_SIZE
	);
	test_check_null_ptr (request->headers);

	test_check_unsigned_eq (
		cerver_http_client_request_add_header (request, FAULTS_CLIENTS_HEADER_NAME, FAULTS_CLIENTS_HEADER_VALUE),
		0, NULL
	);

	char value[FAULTS_CLIENTS_LONG_VALUE_SIZE] = { 0 };
	(void) memset (value, faults_clients_long_character, FAULTS_CLIENTS_LONG_VALUE_SIZE - 1);

	faults_clients_add_header_failed (request, FAULTS_CLIENTS_LONG_NAME, value, FAULTS_CLIENTS_GROWN_SIZE);

	// the header that was added before is still there
	test_check (
		!strncmp (request->headers, FAULTS_CLIENTS_HEADER_LINE, request->headers_length),
		"the headers changed without memory"
	);

	test_check_unsigned_eq (cerver_http_client_request_add_header (request, FAULTS_CLIENTS_LONG_NAME, value), 0, NULL);
	test_check_unsigned_eq ((unsigned int) request->headers_capacity, FAULTS_CLIENTS_GROWN_SIZE, NULL);

	cerver_http_client_request_delete (request);

}

// the request has the body and the content type that it was given first
static void faults_clients_body_check (const CerverHttpClientRequest *request) {

	test_check_str_eq (request->content_type, FAULTS_CLIENTS_TYPE, NULL);
	test_check_unsigned_eq ((unsigned int) request->body_length, (unsigned int) strlen (FAULTS_CLIENTS_BODY), NULL);
	test_check (!memcmp (request->body, FAULTS_CLIENTS_BODY, request->body_length), "the body changed without memory");

}

// sets a body with one of its copies armed to fail: the request keeps the body that it had
static void faults_clients_set_body_failed (CerverHttpClientRequest *request, const size_t size) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, size);
	const u8 result = cerver_http_client_request_set_body (
		request, FAULTS_CLIENTS_NEW_TYPE, FAULTS_CLIENTS_NEW_BODY, strlen (FAULTS_CLIENTS_NEW_BODY)
	);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_MALLOC), triggered + 1, "the body was not copied");
	test_check_unsigned_eq (result, 1, "a body without memory was set");

	faults_clients_body_check (request);

}

// the copy of the content type, and the one of the body
static void faults_clients_set_body (CerverHttpClient *client) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_POST, FAULTS_CLIENTS_URL
	);
	test_check_ptr (request);

	test_check_unsigned_eq (
		cerver_http_client_request_set_body (request, FAULTS_CLIENTS_TYPE, FAULTS_CLIENTS_BODY, strlen (FAULTS_CLIENTS_BODY)),
		0, NULL
	);

	faults_clients_set_body_failed (request, sizeof (FAULTS_CLIENTS_NEW_TYPE));
	faults_clients_set_body_failed (request, strlen (FAULTS_CLIENTS_NEW_BODY));

	test_check_unsigned_eq (
		cerver_http_client_request_set_body (
			request, FAULTS_CLIENTS_NEW_TYPE, FAULTS_CLIENTS_NEW_BODY, strlen (FAULTS_CLIENTS_NEW_BODY)
		),
		0, NULL
	);
	test_check_str_eq (request->content_type, FAULTS_CLIENTS_NEW_TYPE, NULL);

	cerver_http_client_request_delete (request);

}

#pragma endregion

#pragma region response

static unsigned int faults_clients_read (CerverHttpClientRequest *request, const char *bytes) {

	return http_client_response_read (request, bytes, strlen (bytes));

}

// reads bytes of a response with the first buffer armed to fail: the response fails without memory
static void faults_clients_read_failed (CerverHttpClientRequest *request, const char *bytes) {

	const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_REALLOC);

	faults_arm (FAULTS_FUNCTION_REALLOC, true, HTTP_CLIENT_BUFFER_SIZE);
	const unsigned int result = faults_clients_read (request, bytes);
	faults_disarm (FAULTS_FUNCTION_REALLOC);

	test_check_unsigned_eq (faults_triggered (FAULTS_FUNCTION_REALLOC), triggered + 1, "the buffer was not taken");
	test_check_unsigned_eq (result, HTTP_CLIENT_READ_FAILED, NULL);

	const CerverHttpClientResponse *response = &request->response;
	test_check_unsigned_eq (cerver_http_client_response_error (response), CERVER_HTTP_CLIENT_ERROR_MEMORY, NULL);
	test_check_null_ptr (cerver_http_client_response_body (response).data);

}

static CerverHttpClientRequest *faults_clients_response_request (CerverHttpClient *client) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		client, REQUEST_METHOD_GET, FAULTS_CLIENTS_URL
	);
	test_check_ptr (request);

	http_client_response_start (request);

	return request;

}

// the buffer of the head: the response has no head
static void faults_clients_response_head (CerverHttpClient *client) {

	CerverHttpClientRequest *request = faults_clients_response_request (client);

	faults_clients_read_failed (request, FAULTS_CLIENTS_RESPONSE);
	test_check_unsigned_eq (cerver_http_client_response_status (&request->response), 0, NULL);

	cerver_http_client_request_delete (request);

}

// the buffer of the body, after its head arrived alone: the response has its head and no body
static void faults_clients_response_body (CerverHttpClient *client) {

	CerverHttpClientRequest *request = faults_clients_response_request (client);

	test_check_unsigned_eq (faults_clients_read (request, FAULTS_CLIENTS_RESPONSE_HEAD), HTTP_CLIENT_READ_MORE, NULL);

	faults_clients_read_failed (request, FAULTS_CLIENTS_RESPONSE_BODY);
	test_check_unsigned_eq (
		cerver_http_client_response_status (&request->response), FAULTS_CLIENTS_STATUS_OK, NULL
	);

	cerver_http_client_request_delete (request);

	// the same response with memory
	request = faults_clients_response_request (client);

	test_check_unsigned_eq (faults_clients_read (request, FAULTS_CLIENTS_RESPONSE), HTTP_CLIENT_READ_COMPLETE, NULL);
	test_check_str_eq (cerver_http_client_response_body (&request->response).data, FAULTS_CLIENTS_RESPONSE_BODY, NULL);

	cerver_http_client_request_delete (request);

}

#pragma endregion

void faults_tests_clients (void) {

	(void) printf ("Testing FAULTS clients...\n");

	faults_clients_create ();

	CerverHttpClient *client = cerver_http_client_create_with_backend (CERVER_EVENTS_BACKEND_EPOLL);
	test_check_ptr (client);

	faults_clients_request_create (client);
	faults_clients_add_header (client);
	faults_clients_set_body (client);

	faults_clients_response_head (client);
	faults_clients_response_body (client);

	cerver_http_client_delete (client);

	(void) printf ("Done!\n");

}
