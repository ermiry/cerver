#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/config.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/json/internal.h>
#include <cerver/http/json/json.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/security/auth.h>

#include "../test.h"
#include "faults.h"
#include "requests.h"
#include "sweep.h"

// the requests of HTTP with each one of their allocations failing, one in each run, fed to the receive of a
// cerver that is never started, like the fuzz targets do: each run answers or ends its connection,
// and it leaves alive only what a run without a failure leaves

#define REQUESTS_NAME					"faults-web"
#define REQUESTS_PORT					8090
#define REQUESTS_CONNECTION_QUEUE		2

#define REQUESTS_UPLOADS_PATH			"/tmp/cerver-faults-uploads"
#define REQUESTS_DIRECTORY_MODE			0755

#define REQUESTS_PRIVATE_KEY			"./test/http/keys/key.key"
#define REQUESTS_PUBLIC_KEY				"./test/http/keys/key.pub"

#define REQUESTS_SERVER					0
#define REQUESTS_CLIENT					1

#define REQUESTS_SEND_TIMEOUT_SECONDS	1
#define REQUESTS_RESPONSE_SIZE			(1 << 12)
#define REQUESTS_REQUEST_SIZE			(1 << 12)

#define REQUESTS_ADDRESS				"127.0.0.1"

static const char *requests_get = {
	"GET /api/ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *requests_json = {
	"POST /api/json HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 31\r\n"
	"\r\n"
	"{\"name\": \"cerver\", \"value\": 64}"
};

static const char *requests_multipart = {
	"POST /api/upload HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=sweep\r\n"
	"Content-Length: 182\r\n"
	"\r\n"
	"--sweep\r\n"
	"Content-Disposition: form-data; name=\"key\"\r\n"
	"\r\n"
	"value\r\n"
	"--sweep\r\n"
	"Content-Disposition: form-data; name=\"file\"; filename=\"sweep.txt\"\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"cerver\r\n"
	"--sweep--\r\n"
};

#define REQUESTS_PRIVATE_FORMAT			\
	"GET /api/private HTTP/1.1\r\n"		\
	"Host: localhost\r\n"				\
	"Authorization: %s\r\n"				\
	"\r\n"

// a bearer whose signature does not verify: OpenSSL keeps the queue of its errors in each thread,
// so the first failure that it reports is not counted as a leak of a run
static const char *requests_forged = {
	"GET /api/private HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpZCI6ImZhdWx0cyJ9.c2lnbmF0dXJl\r\n"
	"\r\n"
};

static Cerver *requests_cerver = NULL;

// the request with the bearer of the cerver
static char requests_private[REQUESTS_REQUEST_SIZE] = { 0 };

#pragma region cerver

static void requests_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// the body is parsed and its value is answered
static void requests_json_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	json_error_t error;
	json_t *json = body ? json_loadb (body->str, body->len, 0, &error) : NULL;

	if (json) {
		(void) http_response_json_key_value_send (
			http_receive, HTTP_STATUS_OK, "name", json_string_value (json_object_get (json, "name"))
		);

		json_decref (json);
	}

	else {
		(void) http_response_send (bad_request_error, http_receive);
	}

}

static void requests_routes (HttpCerver *http_cerver) {

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");

	(void) http_group_get (api, "ok", requests_ok_handler);
	(void) http_group_post (api, "json", requests_json_handler);

	HttpEndpoint *upload = http_group_post (api, "upload", requests_ok_handler);
	http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpEndpoint *private_endpoint = http_group_get (api, "private", requests_ok_handler);
	http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_BEARER);

}

// the bearer of the private request is generated once, before the sweep
static void requests_bearer (HttpCerver *http_cerver) {

	HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
	test_check_ptr (http_jwt);

	http_cerver_auth_jwt_add_value (http_jwt, "id", "faults");
	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt (http_cerver, http_jwt), 0, NULL);

	(void) snprintf (requests_private, REQUESTS_REQUEST_SIZE, REQUESTS_PRIVATE_FORMAT, http_jwt_get_bearer (http_jwt));

	http_cerver_auth_jwt_delete (http_jwt);

}

static void requests_cerver_create (void) {

	requests_cerver = cerver_create (
		CERVER_TYPE_WEB, REQUESTS_NAME, REQUESTS_PORT, PROTOCOL_TCP, false, REQUESTS_CONNECTION_QUEUE
	);

	test_check_ptr (requests_cerver);

	HttpCerver *http_cerver = (HttpCerver *) requests_cerver->cerver_data;

	requests_routes (http_cerver);

	(void) mkdir (REQUESTS_UPLOADS_PATH, REQUESTS_DIRECTORY_MODE);
	http_cerver_set_uploads_path (http_cerver, REQUESTS_UPLOADS_PATH);
	http_cerver_set_default_uploads_filename_generator (http_cerver);
	http_cerver_set_uploads_delete_when_done (http_cerver, true);

	test_check_unsigned_eq (http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256), 0, NULL);
	http_cerver_auth_set_jwt_priv_key_filename (http_cerver, REQUESTS_PRIVATE_KEY);
	http_cerver_auth_set_jwt_pub_key_filename (http_cerver, REQUESTS_PUBLIC_KEY);

	test_check_unsigned_eq (http_cerver_init (http_cerver), 0, NULL);

	requests_bearer (http_cerver);

}

#pragma endregion

#pragma region feed

static void requests_drain (const int fd) {

	char response[REQUESTS_RESPONSE_SIZE];
	while (read (fd, response, REQUESTS_RESPONSE_SIZE) > 0);

}

// the connection of the receive, a failure to create it ends the run
static Connection *requests_connection_create (const int sock_fd) {

	Connection *connection = connection_create_empty ();
	if (connection) {
		connection->socket->sock_fd = sock_fd;
		connection->active = true;

		(void) snprintf (connection->ip, CONNECTION_IP_SIZE, "%s", REQUESTS_ADDRESS);

		// a response that does not fit in the socket fails instead of blocking
		const struct timeval timeout = { REQUESTS_SEND_TIMEOUT_SECONDS, 0 };
		(void) setsockopt (sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (struct timeval));
	}

	return connection;

}

// feeds the request whole to the receive, through a socket pair whose other end collects the response
static void requests_feed_connection (Connection *connection, const char *request) {

	CerverReceive cerver_receive = CERVER_ZERO_INITIALIZER;
	cerver_receive.cerver = requests_cerver;
	cerver_receive.socket = connection->socket;
	cerver_receive.connection = connection;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	if (http_receive) {
		// the receive reads the request from its own buffer, like the receive loop does
		const size_t length = strlen (request);
		char *buffer = (char *) malloc (length);
		if (buffer) {
			(void) memcpy (buffer, request, length);
			http_receive->handler (http_receive, (ssize_t) length, buffer);
			free (buffer);
		}

		http_receive_delete (http_receive);
	}

}

static void requests_feed (void *request_ptr) {

	const char *request = (const char *) request_ptr;

	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	(void) shutdown (fds[REQUESTS_CLIENT], SHUT_WR);

	Connection *connection = requests_connection_create (fds[REQUESTS_SERVER]);
	if (connection) {
		requests_feed_connection (connection, request);

		if (connection->socket->sock_fd != -1) connection_end (connection);
		connection_delete (connection);
	}

	else {
		(void) close (fds[REQUESTS_SERVER]);
	}

	requests_drain (fds[REQUESTS_CLIENT]);
	(void) close (fds[REQUESTS_CLIENT]);

}

#pragma endregion

void faults_tests_requests (void) {

	(void) printf ("Testing FAULTS requests...\n");

	requests_cerver_create ();

	faults_sweep_path ("request get", requests_feed, (void *) requests_get);
	faults_sweep_path ("request json", requests_feed, (void *) requests_json);
	faults_sweep_path ("request multipart", requests_feed, (void *) requests_multipart);

	requests_feed ((void *) requests_forged);
	faults_sweep_path ("request jwt", requests_feed, (void *) requests_private);

	test_check_unsigned_eq (cerver_teardown (requests_cerver), 0, NULL);
	requests_cerver = NULL;

	(void) printf ("Done!\n");

}
