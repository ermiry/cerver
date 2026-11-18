#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <cerver/core/types.h>

#include <cerver/http/responses/status.h>

#include <cerver/utils/log.h>

#include "../../address.h"
#include "../../handler.h"
#include "curl.h"

// the client of test/web/pools.c: each route answers with the thread that handled it, a thread of the pool
// that the route chose when the cerver has event loops, and a thread of its thpool when it does not

#define POOLS_CLIENT_SCHEME				"http://"
#define POOLS_CLIENT_URL_SIZE			256
#define POOLS_CLIENT_BUFFER_SIZE		(1 << 15)

// the values of the requests table must be constant expressions
#define pools_client_default_thread		"\"thread\": \"thpool-main-"
#define pools_client_slow_thread		"\"thread\": \"thpool-slow-"

#define pools_client_default_metric		"cerver_http_pool_threads{pool=\"default\"} 4\n"
#define pools_client_slow_metric		"cerver_http_pool_threads{pool=\"slow\"} 2\n"
#define pools_client_health_metric		"cerver_http_health_requests_total 2\n"
#define pools_client_ready_metric		"cerver_http_ready_requests_total 1\n"

// what a response sent back
typedef struct PoolsClientBuffer {

	size_t len;
	char data[POOLS_CLIENT_BUFFER_SIZE];

} PoolsClientBuffer;

static PoolsClientBuffer pools_client_body;

// a request and what is expected from it
typedef struct PoolsClientRequest {

	const char *method;
	const char *path;

	CerverHttpStatus status;

	// what its body has, NULL when it is not checked
	const char *expected;

} PoolsClientRequest;

static size_t pools_client_write (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	PoolsClientBuffer *buffer = (PoolsClientBuffer *) storage;

	const size_t length = size * nmemb;
	const size_t available = POOLS_CLIENT_BUFFER_SIZE - 1 - buffer->len;
	const size_t copied = (length < available) ? length : available;

	(void) memcpy (buffer->data + buffer->len, contents, copied);
	buffer->len += copied;
	buffer->data[buffer->len] = '\0';

	return length;

}

// the host and the port of the cerver, set in main ()
static char pools_client_address[WEB_TEST_ADDRESS_SIZE] = { 0 };

static void pools_client_setup (CURL *curl, const PoolsClientRequest *request) {

	char url[POOLS_CLIENT_URL_SIZE] = { 0 };
	(void) snprintf (
		url, POOLS_CLIENT_URL_SIZE, "%s%s%s",
		POOLS_CLIENT_SCHEME, pools_client_address, request->path
	);

	(void) memset (&pools_client_body, 0, sizeof (PoolsClientBuffer));

	curl_easy_reset (curl);
	(void) curl_easy_setopt (curl, CURLOPT_URL, url);
	(void) curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, request->method);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, pools_client_write);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEDATA, &pools_client_body);

	// a HEAD response has no body
	if (!strcmp (request->method, "HEAD")) (void) curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);

}

static unsigned int pools_client_check (const PoolsClientRequest *request) {

	unsigned int errors = 0;

	const char *expected = request->expected;

	if (expected && !strstr (pools_client_body.data, expected)) {
		cerver_log_error (
			"%s %s - the body has no %s: %s", request->method, request->path, expected, pools_client_body.data
		);

		errors = 1;
	}

	return errors;

}

// returns 0 on success, 1 on error
static unsigned int pools_client_request (
	CURL *curl, const PoolsClientRequest *request
) {

	pools_client_setup (curl, request);

	unsigned int errors = (curl_perform_request (curl, request->status) != CURL_RESULT_NONE);
	if (errors) cerver_log_error ("%s %s - unexpected status", request->method, request->path);

	errors |= pools_client_check (request);

	return errors;

}

static const PoolsClientRequest pools_client_requests[] = {
	// a route without a pool runs in the one that its cerver set: its thpool
	{ "GET", "/api/fast", HTTP_STATUS_OK, pools_client_default_thread },

	// the routes of a group run in its pool, the one that chose another one runs there
	{ "GET", "/api/slow/job", HTTP_STATUS_OK, pools_client_slow_thread },
	{ "GET", "/api/slow/fast", HTTP_STATUS_OK, pools_client_default_thread },

	// a group with one endpoint in its root, that chose its pool
	{ "GET", "/old", HTTP_STATUS_OK, pools_client_slow_thread },

	// the routes that cerver answers: a GET or a HEAD of their path
	{ "GET", "/health", HTTP_STATUS_OK, "doki" },
	{ "HEAD", "/health", HTTP_STATUS_OK, NULL },
	{ "GET", "/ready", HTTP_STATUS_OK, "doki" },
	{ "POST", "/health", HTTP_STATUS_NOT_FOUND, NULL },
	{ "GET", "/health/more", HTTP_STATUS_NOT_FOUND, NULL },

	// the metrics have the pools with threads
	{ "GET", "/api/metrics", HTTP_STATUS_OK, pools_client_default_metric },
	{ "GET", "/api/metrics", HTTP_STATUS_OK, pools_client_slow_metric },
	{ "GET", "/api/metrics", HTTP_STATUS_OK, pools_client_health_metric },
	{ "GET", "/api/metrics", HTTP_STATUS_OK, pools_client_ready_metric },
};

static const size_t pools_client_requests_count = (
	sizeof (pools_client_requests) / sizeof (PoolsClientRequest)
);

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	unsigned int errors = 0;

	web_test_address (pools_client_address, NULL);

	(void) curl_global_init (CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init ();
	if (curl) {
		for (size_t idx = 0; idx < pools_client_requests_count; idx++) {
			errors |= pools_client_request (curl, &pools_client_requests[idx]);
		}

		curl_easy_cleanup (curl);
	}

	else {
		errors = 1;
	}

	curl_global_cleanup ();

	if (!errors) cerver_log_success ("Pools integration test passed!");

	return (int) errors;

}
