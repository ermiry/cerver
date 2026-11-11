#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <strings.h>

#include <curl/curl.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#include "../../address.h"

// the client of test/web/features: standalone, against the cerver,
// and behind nginx (test/nginx), where the request id is the one of nginx,
// the peer is nginx and the client's address is forwarded by it

// standalone the client connects to the cerver of the pair (web_test_address ())
#define FEATURES_CLIENT_SCHEME				"http://"

// the address of the cerver or of nginx
#define FEATURES_CLIENT_ADDRESS_VARIABLE	"FEATURES_ADDRESS"

// 1 when the requests go through nginx
#define FEATURES_CLIENT_PROXY_VARIABLE		"FEATURES_PROXY"

// the prefix of nginx's location without buffering, "" when there is no proxy
#define FEATURES_CLIENT_UNBUFFERED_VARIABLE	"FEATURES_UNBUFFERED_PREFIX"

#define FEATURES_CLIENT_URL_SIZE			512
#define FEATURES_CLIENT_BUFFER_SIZE			(1 << 15)
#define FEATURES_CLIENT_HEADERS				4
#define FEATURES_CLIENT_VALUE_SIZE			256

// a value of FEATURES_CLIENT_VALUE_SIZE inside its JSON key
#define FEATURES_CLIENT_EXPECTED_SIZE		(FEATURES_CLIENT_VALUE_SIZE * 2)

// nginx's $request_id has 32 hex characters
#define FEATURES_CLIENT_NGINX_ID_LENGTH		32

#define FEATURES_CLIENT_GZIP_FIRST			0x1f
#define FEATURES_CLIENT_GZIP_SECOND			0x8b

#define FEATURES_CLIENT_DECIMAL_BASE		10

static const char *features_client_upload = { "a chunked body that arrives in pieces through the proxy" };
static const size_t features_client_upload_piece = 7;

// the address that the client connects from, the one of the pair (CERVER_TEST_ADDRESS)
static const char *features_client_loopback = NULL;
static const char *features_client_spoofed = { "203.0.113.50" };

typedef struct FeaturesClientBuffer {

	size_t len;
	char data[FEATURES_CLIENT_BUFFER_SIZE];

} FeaturesClientBuffer;

static FeaturesClientBuffer features_body;
static FeaturesClientBuffer features_headers;

static char features_default_address[FEATURES_CLIENT_URL_SIZE] = { 0 };
static const char *features_address = features_default_address;
static const char *features_unbuffered = "";
static bool features_proxy = false;

// the body that is uploaded in pieces
typedef struct FeaturesClientUpload {

	const char *data;
	size_t sent;

} FeaturesClientUpload;

static size_t features_client_write (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	FeaturesClientBuffer *buffer = (FeaturesClientBuffer *) storage;

	const size_t length = size * nmemb;
	const size_t available = FEATURES_CLIENT_BUFFER_SIZE - 1 - buffer->len;
	const size_t copied = (length < available) ? length : available;

	(void) memcpy (buffer->data + buffer->len, contents, copied);
	buffer->len += copied;
	buffer->data[buffer->len] = '\0';

	return length;

}

// sends the body a few bytes at a time, so curl sends many chunks
static size_t features_client_read (
	char *destination, size_t size, size_t nmemb, void *storage
) {

	FeaturesClientUpload *upload = (FeaturesClientUpload *) storage;

	const size_t left = strlen (upload->data) - upload->sent;
	size_t length = (left < features_client_upload_piece) ? left : features_client_upload_piece;
	if (length > (size * nmemb)) length = size * nmemb;

	(void) memcpy (destination, upload->data + upload->sent, length);
	upload->sent += length;

	return length;

}

static void features_client_setup (
	CURL *curl, const char *method, const char *prefix, const char *path,
	struct curl_slist *headers
) {

	char url[FEATURES_CLIENT_URL_SIZE] = { 0 };
	(void) snprintf (url, FEATURES_CLIENT_URL_SIZE, "%s%s%s", features_address, prefix, path);

	(void) memset (&features_body, 0, sizeof (FeaturesClientBuffer));
	(void) memset (&features_headers, 0, sizeof (FeaturesClientBuffer));

	curl_easy_reset (curl);
	(void) curl_easy_setopt (curl, CURLOPT_URL, url);
	(void) curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, method);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, features_client_write);
	(void) curl_easy_setopt (curl, CURLOPT_WRITEDATA, &features_body);
	(void) curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, features_client_write);
	(void) curl_easy_setopt (curl, CURLOPT_HEADERDATA, &features_headers);

	if (headers) (void) curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

	if (!strcmp (method, "HEAD")) (void) curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);

}

static struct curl_slist *features_client_headers (const char *const *values) {

	struct curl_slist *headers = NULL;

	for (unsigned int i = 0; (i < FEATURES_CLIENT_HEADERS) && values && values[i]; i++) {
		headers = curl_slist_append (headers, values[i]);
	}

	return headers;

}

// returns 0 on success, 1 on error
static unsigned int features_client_request (
	CURL *curl, const char *method, const char *prefix, const char *path,
	const char *const *values, const http_status status
) {

	struct curl_slist *headers = features_client_headers (values);

	features_client_setup (curl, method, prefix, path, headers);

	const unsigned int errors = (curl_perform_request (curl, status) != CURL_RESULT_NONE);
	if (errors) cerver_log_error ("%s %s%s - unexpected status", method, prefix, path);

	curl_slist_free_all (headers);

	return errors;

}

// copies the value of the header of the last response, the name is compared without case
static bool features_client_header (const char *name, char *value) {

	bool found = false;

	const size_t name_length = strlen (name);
	for (const char *line = features_headers.data; line && *line; ) {
		if (!strncasecmp (line, name, name_length) && (line[name_length] == ':')) {
			const char *start = line + name_length + 1;
			while (*start == ' ') start += 1;

			const char *end = strstr (start, "\r\n");
			const size_t length = end ? (size_t) (end - start) : strlen (start);
			(void) snprintf (value, FEATURES_CLIENT_VALUE_SIZE, "%.*s", (int) length, start);

			found = true;
			break;
		}

		line = strstr (line, "\r\n");
		if (line) line += 2;
	}

	return found;

}

static unsigned int features_client_expect_header (const char *context, const char *name, const char *expected) {

	unsigned int errors = 0;

	char value[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	if (!features_client_header (name, value)) {
		cerver_log_error ("%s - the response has no %s: %s", context, name, features_headers.data);
		errors = 1;
	}

	else if (expected && !strstr (value, expected)) {
		cerver_log_error ("%s - %s is %s instead of %s", context, name, value, expected);
		errors = 1;
	}

	return errors;

}

static unsigned int features_client_expect_no_header (const char *context, const char *name) {

	char value[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	const bool found = features_client_header (name, value);
	if (found) cerver_log_error ("%s - %s was not expected: %s", context, name, value);

	return found ? 1 : 0;

}

static unsigned int features_client_expect_body (const char *context, const char *expected) {

	const bool found = strstr (features_body.data, expected) != NULL;
	if (!found) cerver_log_error ("%s - the body has no %s: %s", context, expected, features_body.data);

	return found ? 0 : 1;

}

// every response has a Date and Connection: close, also the ones of an unknown path
static unsigned int features_client_date (CURL *curl) {

	unsigned int errors = features_client_request (curl, "GET", "", "/features/small", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("date", "Date", " GMT");
	errors |= features_client_expect_header ("date", "Connection", "close");
	errors |= features_client_expect_body ("date", "doki");

	errors |= features_client_request (curl, "GET", "", "/plain/missing", NULL, HTTP_STATUS_NOT_FOUND);
	errors |= features_client_expect_header ("not found", "Date", " GMT");
	errors |= features_client_expect_header ("not found", "X-Request-Id", NULL);

	errors |= features_client_request (curl, "HEAD", "", "/features/small", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("head", "Content-Length", "15");

	return errors;

}

// standalone the request keeps its id, behind nginx it is the one of nginx
static unsigned int features_client_request_id (CURL *curl) {

	static const char *values[] = { "X-Request-Id: client-request-1", NULL };

	unsigned int errors = features_client_request (curl, "GET", "", "/features/client", values, HTTP_STATUS_OK);

	char id[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	if (!features_client_header ("X-Request-Id", id)) {
		cerver_log_error ("request id - the response has no X-Request-Id");
		errors = 1;
	}

	else if (features_proxy) {
		if ((strlen (id) != FEATURES_CLIENT_NGINX_ID_LENGTH) || !strcmp (id, "client-request-1")) {
			cerver_log_error ("request id - %s is not the id of nginx", id);
			errors = 1;
		}
	}

	else if (strcmp (id, "client-request-1")) {
		cerver_log_error ("request id - %s is not the id of the request", id);
		errors = 1;
	}

	char expected[FEATURES_CLIENT_EXPECTED_SIZE] = { 0 };
	(void) snprintf (expected, FEATURES_CLIENT_EXPECTED_SIZE, "\"id\": \"%s\"", id);
	errors |= features_client_expect_body ("request id", expected);

	return errors;

}

// standalone the client is the peer, behind nginx the peer is nginx and the client is forwarded
static unsigned int features_client_address (CURL *curl) {

	unsigned int errors = features_client_request (curl, "GET", "", "/features/client", NULL, HTTP_STATUS_OK);

	char expected[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	(void) snprintf (expected, FEATURES_CLIENT_VALUE_SIZE, "\"ip\": \"%s\"", features_client_loopback);
	errors |= features_client_expect_body ("client address", expected);

	(void) snprintf (expected, FEATURES_CLIENT_VALUE_SIZE, "\"peer\": \"%s\"", features_client_loopback);
	const bool peer_is_loopback = strstr (features_body.data, expected) != NULL;
	if (peer_is_loopback == features_proxy) {
		cerver_log_error ("client address - unexpected peer: %s", features_body.data);
		errors = 1;
	}

	// the loopback is a trusted proxy too, so the address that it forwards is used
	static const char *values[] = { "X-Forwarded-For: 203.0.113.50", NULL };
	errors |= features_client_request (curl, "GET", "", "/features/client", values, HTTP_STATUS_OK);
	(void) snprintf (expected, FEATURES_CLIENT_VALUE_SIZE, "\"ip\": \"%s\"", features_client_spoofed);
	errors |= features_client_expect_body ("forwarded address", expected);

	return errors;

}

static unsigned int features_client_cors (CURL *curl) {

	static const char *allowed[] = { "Origin: http://allowed.com", NULL };
	unsigned int errors = features_client_request (curl, "GET", "", "/features/small", allowed, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("cors", "Access-Control-Allow-Origin", "http://allowed.com");
	errors |= features_client_expect_header ("cors", "Vary", "Origin");

	static const char *other[] = { "Origin: http://other.com", NULL };
	errors |= features_client_request (curl, "GET", "", "/features/small", other, HTTP_STATUS_OK);
	errors |= features_client_expect_no_header ("cors", "Access-Control-Allow-Origin");

	errors |= features_client_request (curl, "GET", "", "/plain/small", allowed, HTTP_STATUS_OK);
	errors |= features_client_expect_no_header ("cors", "Access-Control-Allow-Origin");

	return errors;

}

static unsigned int features_client_not_modified (
	CURL *curl, const char *path, const char *validator
) {

	unsigned int errors = features_client_request (curl, "GET", "", path, NULL, HTTP_STATUS_OK);

	char value[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	if (!features_client_header (validator, value)) {
		cerver_log_error ("%s - the response has no %s", path, validator);
		errors = 1;
	}

	else {
		char header[FEATURES_CLIENT_VALUE_SIZE * 2] = { 0 };
		(void) snprintf (
			header, sizeof (header), "%s: %s",
			strcasecmp (validator, "ETag") ? "If-Modified-Since" : "If-None-Match", value
		);

		const char *values[] = { header, NULL };
		errors |= features_client_request (curl, "GET", "", path, values, HTTP_STATUS_NOT_MODIFIED);
		if (features_body.len) {
			cerver_log_error ("%s - a 304 has a body", path);
			errors = 1;
		}
	}

	return errors;

}

static unsigned int features_client_conditional (CURL *curl) {

	unsigned int errors = features_client_not_modified (curl, "/features/small", "ETag");

	errors |= features_client_not_modified (curl, "/index.html", "ETag");
	errors |= features_client_not_modified (curl, "/index.html", "Last-Modified");

	static const char *other[] = { "If-None-Match: \"other\"", NULL };
	errors |= features_client_request (curl, "GET", "", "/features/small", other, HTTP_STATUS_OK);

	return errors;

}

// the body is compressed by cerver and arrives as it was, nginx does not compress it again
static unsigned int features_client_compression (CURL *curl) {

	static const char *values[] = { "Accept-Encoding: gzip", NULL };
	unsigned int errors = features_client_request (curl, "GET", "", "/features/items", values, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("compression", "Content-Encoding", "gzip");
	errors |= features_client_expect_header ("compression", "Vary", "Accept-Encoding");

	const unsigned char *bytes = (const unsigned char *) features_body.data;
	if ((features_body.len < 2) || (bytes[0] != FEATURES_CLIENT_GZIP_FIRST) || (bytes[1] != FEATURES_CLIENT_GZIP_SECOND)) {
		cerver_log_error ("compression - the body is not gzip");
		errors = 1;
	}

	char length[FEATURES_CLIENT_VALUE_SIZE] = { 0 };
	if (features_client_header ("Content-Length", length)) {
		if (strtoul (length, NULL, FEATURES_CLIENT_DECIMAL_BASE) != features_body.len) {
			cerver_log_error ("compression - Content-Length %s but %zu bytes", length, features_body.len);
			errors = 1;
		}
	}

	// a client without gzip
	errors |= features_client_request (curl, "GET", "", "/features/items", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_no_header ("plain body", "Content-Encoding");
	errors |= features_client_expect_body ("plain body", "{\"index\": 127, \"name\": \"item\"}]");

	return errors;

}

// the stream arrives complete, with or without nginx's buffering and with HTTP/1.0
static unsigned int features_client_stream (CURL *curl) {

	unsigned int errors = features_client_request (curl, "GET", "", "/features/stream", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("stream", "Transfer-Encoding", "chunked");
	errors |= features_client_expect_body ("stream", "firstsecond");

	errors |= features_client_request (curl, "GET", features_unbuffered, "/features/stream", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_body ("unbuffered stream", "firstsecond");

	features_client_setup (curl, "GET", "", "/features/stream", NULL);
	(void) curl_easy_setopt (curl, CURLOPT_HTTP_VERSION, (long) CURL_HTTP_VERSION_1_0);
	if (curl_perform_request (curl, HTTP_STATUS_OK) != CURL_RESULT_NONE) {
		cerver_log_error ("HTTP/1.0 stream - unexpected status");
		errors = 1;
	}

	errors |= features_client_expect_no_header ("HTTP/1.0 stream", "Transfer-Encoding");
	errors |= features_client_expect_body ("HTTP/1.0 stream", "firstsecond");

	return errors;

}

static unsigned int features_client_upload_with (CURL *curl, const char *prefix) {

	static const char *values[] = { "Transfer-Encoding: chunked", "Content-Type: text/plain", NULL };
	struct curl_slist *headers = features_client_headers (values);

	FeaturesClientUpload upload = { features_client_upload, 0 };

	features_client_setup (curl, "POST", prefix, "/features/echo", headers);
	(void) curl_easy_setopt (curl, CURLOPT_POST, 1L);
	(void) curl_easy_setopt (curl, CURLOPT_READFUNCTION, features_client_read);
	(void) curl_easy_setopt (curl, CURLOPT_READDATA, &upload);

	unsigned int errors = (curl_perform_request (curl, HTTP_STATUS_OK) != CURL_RESULT_NONE);
	if (errors) cerver_log_error ("chunked upload %s - unexpected status", prefix);

	if (strcmp (features_body.data, features_client_upload)) {
		cerver_log_error ("chunked upload %s - the echo is %s", prefix, features_body.data);
		errors = 1;
	}

	curl_slist_free_all (headers);

	return errors;

}

// nginx buffers the chunked body and sends it with Content-Length,
// without buffering it sends the chunks as they arrive
static unsigned int features_client_chunked_upload (CURL *curl) {

	unsigned int errors = features_client_upload_with (curl, "");

	errors |= features_client_upload_with (curl, features_unbuffered);

	return errors;

}

// a handler without a response is answered with 500, also behind a proxy (instead of a 502)
static unsigned int features_client_unanswered (CURL *curl) {

	unsigned int errors = features_client_request (
		curl, "GET", "", "/features/unanswered", NULL, HTTP_STATUS_INTERNAL_SERVER_ERROR
	);

	errors |= features_client_expect_body ("unanswered", "Internal error!");

	return errors;

}

static unsigned int features_client_metrics (CURL *curl) {

	unsigned int errors = features_client_request (curl, "GET", "", "/features/cerver/metrics", NULL, HTTP_STATUS_OK);
	errors |= features_client_expect_header ("metrics", "Content-Type", "text/plain; version=0.0.4");
	errors |= features_client_expect_body ("metrics", "cerver_info{version=\"");
	errors |= features_client_expect_body ("metrics", "cerver_http_responses_total{code=\"304\"} ");
	errors |= features_client_expect_body ("metrics", "cerver_http_request_duration_seconds_count{method=\"GET\",path=\"/features/small\"} ");
	errors |= features_client_expect_body ("metrics", "cerver_http_requests_unanswered_total ");

	return errors;

}

static void features_client_environment (void) {

	char host[WEB_TEST_ADDRESS_SIZE] = { 0 };
	web_test_address (host, NULL);
	(void) snprintf (
		features_default_address, FEATURES_CLIENT_URL_SIZE, "%s%s", FEATURES_CLIENT_SCHEME, host
	);

	features_client_loopback = test_address ();

	const char *address = getenv (FEATURES_CLIENT_ADDRESS_VARIABLE);
	if (address && address[0]) features_address = address;

	const char *proxy = getenv (FEATURES_CLIENT_PROXY_VARIABLE);
	features_proxy = proxy && !strcmp (proxy, "1");

	const char *unbuffered = getenv (FEATURES_CLIENT_UNBUFFERED_VARIABLE);
	if (unbuffered) features_unbuffered = unbuffered;

	cerver_log_msg (
		"Features client - %s - %s", features_address, features_proxy ? "behind a proxy" : "standalone"
	);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	unsigned int errors = 0;

	cerver_log_init ();

	features_client_environment ();

	(void) curl_global_init (CURL_GLOBAL_ALL);

	CURL *curl = curl_easy_init ();
	if (curl) {
		errors |= features_client_date (curl);
		errors |= features_client_request_id (curl);
		errors |= features_client_address (curl);
		errors |= features_client_cors (curl);
		errors |= features_client_conditional (curl);
		errors |= features_client_compression (curl);
		errors |= features_client_stream (curl);
		errors |= features_client_chunked_upload (curl);
		errors |= features_client_unanswered (curl);
		errors |= features_client_metrics (curl);

		curl_easy_cleanup (curl);
	}

	else {
		errors = 1;
	}

	curl_global_cleanup ();

	if (!errors) cerver_log_success ("Features integration test passed!");

	cerver_log_end ();

	return (int) errors;

}
