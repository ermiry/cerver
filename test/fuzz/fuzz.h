#ifndef _CERVER_TESTS_FUZZ_H_
#define _CERVER_TESTS_FUZZ_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
#include <cerver/core/cerver.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

// feeds bytes to http_receive_handle () like the receive loop does,
// through a socket pair whose other end only collects the responses

#define FUZZ_NAME						"fuzz-cerver"
#define FUZZ_PORT						8080
#define FUZZ_CONNECTION_QUEUE			2

#define FUZZ_UPLOADS_PATH				"/tmp/cerver-fuzz-uploads"
#define FUZZ_DIRECTORY_MODE				0755

#define FUZZ_SERVER						0
#define FUZZ_CLIENT						1

#define FUZZ_SEND_TIMEOUT_SECONDS		1

#define FUZZ_PROXY_ADDRESS				"10.0.0.2"
#define FUZZ_ORIGIN						"http://allowed.com"
#define FUZZ_RESPONSE_BUFFER_SIZE		(1 << 12)

static Cerver *fuzz_cerver = NULL;

static void fuzz_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

static const HttpWebSocketHandlers fuzz_web_socket_handlers = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static void fuzz_routes (HttpCerver *http_cerver) {

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");

	// the conditional requests, the compression and the CORS of every response
	http_group_set_cors (api, true);
	http_group_set_etag (api, true);
	http_group_set_compression (api, true);

	(void) http_group_get (api, "users/:id<int>", fuzz_ok_handler);
	(void) http_group_get (api, "users/:name", fuzz_ok_handler);
	(void) http_group_post (api, "items/:id<uuid>/update", fuzz_ok_handler);
	(void) http_group_patch (api, "files/*", fuzz_ok_handler);

	HttpEndpoint *upload = http_group_post (api, "upload", fuzz_ok_handler);
	http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpEndpoint *private_endpoint = http_group_get (api, "private", fuzz_ok_handler);
	http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_BEARER);

	// the upgrades to WebSocket, a socket pair never starts their connections
	HttpEndpoint *socket = http_group_web_socket_add (api, "socket", &fuzz_web_socket_handlers);
	const char *protocols[] = { "chat", "superchat" };
	(void) http_endpoint_set_web_socket_protocols (socket, protocols, 2);
	http_endpoint_set_web_socket_compression (socket, true);
	http_endpoint_set_web_socket_origin_check (socket, true);

}

// the cerver is created once and never started, like in the receive unit tests
static void fuzz_cerver_create (void) {

	if (fuzz_cerver) return;

	cerver_log_init ();

	fuzz_cerver = cerver_create (
		CERVER_TYPE_WEB, FUZZ_NAME, FUZZ_PORT, PROTOCOL_TCP, false, FUZZ_CONNECTION_QUEUE
	);

	if (!fuzz_cerver) abort ();

	HttpCerver *http_cerver = (HttpCerver *) fuzz_cerver->cerver_data;

	fuzz_routes (http_cerver);

	// the request id, and the client's address from X-Forwarded-For and X-Real-IP
	http_cerver_set_request_id (http_cerver, true);
	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);
	(void) http_cerver_add_origin_to_whitelist (http_cerver, FUZZ_ORIGIN);

	(void) mkdir (FUZZ_UPLOADS_PATH, FUZZ_DIRECTORY_MODE);
	http_cerver_set_uploads_path (http_cerver, FUZZ_UPLOADS_PATH);
	http_cerver_set_default_uploads_filename_generator (http_cerver);
	http_cerver_set_uploads_delete_when_done (http_cerver, true);

	if (http_cerver_init (http_cerver)) abort ();

}

static void fuzz_feed_piece (
	HttpReceive *http_receive, const uint8_t *piece, const size_t piece_len
) {

	// a buffer of the exact size, so a read past it is found
	char *buffer = (char *) malloc (piece_len);
	if (!buffer) return;

	(void) memcpy (buffer, piece, piece_len);

	http_receive->handler (http_receive, (ssize_t) piece_len, buffer);

	free (buffer);

}

static void fuzz_drain (const int fd) {

	char response[FUZZ_RESPONSE_BUFFER_SIZE];
	while (read (fd, response, FUZZ_RESPONSE_BUFFER_SIZE) > 0);

}

static Connection *fuzz_connection_create (const int sock_fd) {

	Connection *connection = connection_create_empty ();
	if (!connection) abort ();

	connection->socket->sock_fd = sock_fd;
	connection->active = true;

	// a socket pair has no address, the one of a proxy uses the forwarded headers
	(void) snprintf (connection->ip, CONNECTION_IP_SIZE, "%s", FUZZ_PROXY_ADDRESS);

	// a response that does not fit in the socket fails instead of blocking
	const struct timeval timeout = { FUZZ_SEND_TIMEOUT_SECONDS, 0 };
	(void) setsockopt (sock_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (struct timeval));

	return connection;

}

// feeds the request in pieces of piece_len bytes, 0 feeds it whole
// a target that feeds its connection its own way does not use it
static inline void fuzz_feed (
	const uint8_t *request, const size_t request_len, const size_t piece_len
) {

	int fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds)) return;

	(void) shutdown (fds[FUZZ_CLIENT], SHUT_WR);

	Connection *connection = fuzz_connection_create (fds[FUZZ_SERVER]);

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = fuzz_cerver;
	cerver_receive.socket = connection->socket;
	cerver_receive.connection = connection;

	HttpReceive *http_receive = http_receive_create (&cerver_receive);
	if (!http_receive) abort ();

	// an upgraded connection is read by its WebSocket, never by the parser again
	const size_t step = piece_len ? piece_len : request_len;
	for (
		size_t start = 0;
		(start < request_len) && (connection->socket->sock_fd != -1) && !http_receive->upgraded;
		start += step
	) {
		const size_t left = request_len - start;
		fuzz_feed_piece (http_receive, request + start, (left < step) ? left : step);
	}

	if (connection->socket->sock_fd != -1) connection_end (connection);

	fuzz_drain (fds[FUZZ_CLIENT]);

	http_receive_delete (http_receive);
	connection_delete (connection);

	(void) close (fds[FUZZ_CLIENT]);

}

#endif
