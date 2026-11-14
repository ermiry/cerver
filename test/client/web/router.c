#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include <cerver/http/responses/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define ROUTER_CLIENT_SCHEME			"http://"
#define ROUTER_CLIENT_URL_SIZE			256
#define ROUTER_CLIENT_BUFFER_SIZE		4096
#define ROUTER_CLIENT_HEADERS			4

// the values of the requests table must be constant expressions
#define router_client_request_id			"X-Request-Id: client-1"
#define router_client_authorization			"Authorization: router-token"
#define router_client_bad_authorization		"Authorization: wrong"

#define router_client_large_body			"0123456789abcdef0123456789abcdef"

// what a response sent back
typedef struct RouterClientBuffer {

	size_t len;
	char data[ROUTER_CLIENT_BUFFER_SIZE];

} RouterClientBuffer;

static RouterClientBuffer router_client_body;
static RouterClientBuffer router_client_headers;

// a request and what is expected from it
typedef struct RouterClientRequest {

	const char *method;
	const char *path;
	const char *headers[ROUTER_CLIENT_HEADERS];
	const char *body;

	http_status status;
	const char *expected_body;
	const char *expected_header;

} RouterClientRequest;

static size_t router_client_write (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	RouterClientBuffer *buffer = (RouterClientBuffer *) storage;

	const size_t length = size * nmemb;
	const size_t available = ROUTER_CLIENT_BUFFER_SIZE - 1 - buffer->len;
	const size_t copied = (length < available) ? length : available;

	(void) memcpy (buffer->data + buffer->len, contents, copied);
	buffer->len += copied;
	buffer->data[buffer->len] = '\0';

	return length;

}

static struct curl_slist *router_client_headers_list (const RouterClientRequest *request) {

	struct curl_slist *headers = NULL;

	for (unsigned int idx = 0; (idx < ROUTER_CLIENT_HEADERS) && request->headers[idx]; idx++) {
		headers = curl_slist_append (headers, request->headers[idx]);
	}

	return headers;

}

// the host and the port of the cerver, set in main ()
static char router_client_address[WEB_TEST_ADDRESS_SIZE] = { 0 };

static void router_client_setup (
	CURL *curl, const RouterClientRequest *request, struct curl_slist *headers
) {

	char url[ROUTER_CLIENT_URL_SIZE] = { 0 };
	(void) snprintf (
		url, ROUTER_CLIENT_URL_SIZE, "%s%s%s",
		ROUTER_CLIENT_SCHEME, router_client_address, request->path
	);

	(void) memset (&router_client_body, 0, sizeof (RouterClientBuffer));
	(void) memset (&router_client_headers, 0, sizeof (RouterClientBuffer));

	curl_easy_reset (curl);
	(void) curl_easy_setopt (curl, CURLOPT_URL, url);
	(void) curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, request->method);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, router_client_write);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEDATA, &router_client_body);
	(void) curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, router_client_write);
	(void) curl_easy_setopt (curl, CURLOPT_HEADERDATA, &router_client_headers);

	if (headers) (void) curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

	// a HEAD response has no body
	if (!strcmp (request->method, "HEAD")) (void) curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);

	if (request->body) {
		(void) curl_easy_setopt (curl, CURLOPT_POSTFIELDS, request->body);
		(void) curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (long) strlen (request->body));
	}

}

static unsigned int router_client_check (const RouterClientRequest *request) {

	unsigned int errors = 0;

	if (request->expected_body && !strstr (router_client_body.data, request->expected_body)) {
		cerver_log_error ("%s %s - the body has no %s: %s", request->method, request->path, request->expected_body, router_client_body.data);
		errors = 1;
	}

	if (request->expected_header && !strstr (router_client_headers.data, request->expected_header)) {
		cerver_log_error ("%s %s - the headers have no %s: %s", request->method, request->path, request->expected_header, router_client_headers.data);
		errors = 1;
	}

	return errors;

}

// returns 0 on success, 1 on error
static unsigned int router_client_request (
	CURL *curl, const RouterClientRequest *request
) {

	struct curl_slist *headers = router_client_headers_list (request);

	router_client_setup (curl, request, headers);

	unsigned int errors = (curl_perform_request (curl, request->status) != CURL_RESULT_NONE);
	if (errors) cerver_log_error ("%s %s - unexpected status", request->method, request->path);

	errors |= router_client_check (request);

	curl_slist_free_all (headers);

	return errors;

}

static const RouterClientRequest router_client_requests[] = {
	// a param with a validator before a param without it, a static segment before both
	{ "GET", "/api/items/5f9b1c2d3e4f5a6b7c8d9e0f", { router_client_request_id }, NULL, HTTP_STATUS_OK, "\"id\": \"5f9b1c2d3e4f5a6b7c8d9e0f\"", NULL },
	{ "GET", "/api/items/abc%20def", { router_client_request_id }, NULL, HTTP_STATUS_OK, "\"name\": \"abc def\"", NULL },
	{ "GET", "/api/items/me", { router_client_request_id }, NULL, HTTP_STATUS_OK, "doki", NULL },

	// the middleware of the group answers without the request id
	{ "GET", "/api/items/abc", { NULL }, NULL, HTTP_STATUS_BAD_REQUEST, NULL, NULL },

	// HEAD, PATCH, OPTIONS and 405
	{ "HEAD", "/api/items/me", { router_client_request_id }, NULL, HTTP_STATUS_OK, NULL, "Content-Length: 15" },
	{ "PATCH", "/api/items/abc", { router_client_request_id }, NULL, HTTP_STATUS_OK, "doki", NULL },
	{ "OPTIONS", "/api/items/abc", { NULL }, NULL, HTTP_STATUS_NO_CONTENT, NULL, "Allow: GET, HEAD, OPTIONS, PATCH" },
	{ "DELETE", "/api/items/abc", { NULL }, NULL, HTTP_STATUS_METHOD_NOT_ALLOWED, NULL, "Allow: GET, HEAD, OPTIONS, PATCH" },

	// a tolerated trailing slash, an empty segment and an unknown path
	{ "GET", "/api/items/abc/", { router_client_request_id }, NULL, HTTP_STATUS_OK, "\"name\": \"abc\"", NULL },
	{ "GET", "/api//items/abc", { router_client_request_id }, NULL, HTTP_STATUS_NOT_FOUND, NULL, NULL },
	{ "GET", "/api/missing", { NULL }, NULL, HTTP_STATUS_NOT_FOUND, NULL, NULL },

	// the body limit of an endpoint
	{ "POST", "/api/limited", { NULL }, router_client_large_body, HTTP_STATUS_PAYLOAD_TOO_LARGE, NULL, NULL },

	// the authentication and the custom data of a group
	{ "GET", "/api/private", { router_client_bad_authorization }, NULL, HTTP_STATUS_UNAUTHORIZED, NULL, NULL },
	{ "GET", "/api/private", { router_client_authorization }, NULL, HTTP_STATUS_OK, "\"data\": \"items\"", NULL },

	// the first routes API, the static route is no longer shadowed by the param
	{ "GET", "/legacy/debug/status", { NULL }, NULL, HTTP_STATUS_OK, "doki", NULL },
	{ "GET", "/legacy/42/status", { NULL }, NULL, HTTP_STATUS_OK, "\"status\": \"42\"", NULL },
};

static const size_t router_client_requests_count = (
	sizeof (router_client_requests) / sizeof (RouterClientRequest)
);

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	unsigned int errors = 0;

	web_test_address (router_client_address, NULL);

	(void) curl_global_init (CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init ();
	if (curl) {
		for (size_t idx = 0; idx < router_client_requests_count; idx++) {
			errors |= router_client_request (curl, &router_client_requests[idx]);
		}

		curl_easy_cleanup (curl);
	}

	else {
		errors = 1;
	}

	curl_global_cleanup ();

	if (!errors) cerver_log_success ("Router integration test passed!");

	return (int) errors;

}
