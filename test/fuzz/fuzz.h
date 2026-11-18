#ifndef _CERVER_TESTS_FUZZ_H_
#define _CERVER_TESTS_FUZZ_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
#include <cerver/core/cerver.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/context.h>
#include "cerver/http/core/handler.h"
#include <cerver/http/core/http.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/headers/fields.h>
#include <cerver/http/requests/request.h>
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

#define FUZZ_PUBLIC_PATH				"/tmp/cerver-fuzz-public"
#define FUZZ_PUBLIC_FILE				FUZZ_PUBLIC_PATH "/index.html"
#define FUZZ_PUBLIC_CONTENT				"<html>fuzz</html>"

#define FUZZ_SERVER						0
#define FUZZ_CLIENT						1

#define FUZZ_SEND_TIMEOUT_SECONDS		1

#define FUZZ_PROXY_ADDRESS				"10.0.0.2"
#define FUZZ_ORIGIN						"http://allowed.com"
#define FUZZ_RESPONSE_BUFFER_SIZE		(1 << 12)

static Cerver *fuzz_cerver = NULL;

static void fuzz_ok_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

static const HttpWebSocketHandlers fuzz_web_socket_handlers = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

// a target whose connections would start their WebSocket (the ones of an event loop)
// clears it before the cerver is created, so no endpoint takes upgrades
static bool fuzz_web_sockets = true;

// the custom authentication accepts the requests whose Authorization starts with this,
// so it depends only on the bytes of the request
#define FUZZ_AUTHORIZATION_ACCEPTED		'a'

static unsigned int fuzz_authentication (CerverHttpContext *context) {

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	return (
		!cerver_http_view_is_missing (authorization) && authorization.length
		&& (authorization.data[0] == FUZZ_AUTHORIZATION_ACCEPTED)
	) ? 0 : 1;

}

// the requests with a custom authentication: before their body, with it, and their uploads
static void fuzz_routes_secure (HttpCerver *http_cerver) {

	HttpGroup *secure = cerver_http_group_create (http_cerver, "secure");
	cerver_http_group_set_auth (secure, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (secure, fuzz_authentication);

	(void) cerver_http_group_get (secure, "users/:id", fuzz_ok_handler);
	(void) cerver_http_group_post (secure, "items", fuzz_ok_handler);

	HttpEndpoint *upload = cerver_http_group_post (secure, "upload", fuzz_ok_handler);
	cerver_http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpEndpoint *with_body = cerver_http_group_post (secure, "body", fuzz_ok_handler);
	cerver_http_endpoint_set_authentication_with_body (with_body, true);

}

// the upgrades to WebSocket, a socket pair never starts their connections
static void fuzz_routes_web_socket (HttpGroup *api) {

	HttpEndpoint *socket = cerver_http_group_web_socket_add (api, "socket", &fuzz_web_socket_handlers);
	const char *protocols[] = { "chat", "superchat" };
	(void) cerver_http_endpoint_set_web_socket_protocols (socket, protocols, 2);
	cerver_http_endpoint_set_web_socket_compression (socket, true);
	cerver_http_endpoint_set_web_socket_origin_check (socket, true);

}

static void fuzz_routes (HttpCerver *http_cerver) {

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");

	// the conditional requests, the compression and the CORS of every response
	cerver_http_group_set_cors (api, true);
	cerver_http_group_set_etag (api, true);
	cerver_http_group_set_compression (api, true);

	(void) cerver_http_group_get (api, "users/:id<int>", fuzz_ok_handler);
	(void) cerver_http_group_get (api, "users/:name", fuzz_ok_handler);
	(void) cerver_http_group_post (api, "items/:id<uuid>/update", fuzz_ok_handler);
	(void) cerver_http_group_patch (api, "files/*", fuzz_ok_handler);

	HttpEndpoint *upload = cerver_http_group_post (api, "upload", fuzz_ok_handler);
	cerver_http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpEndpoint *private_endpoint = cerver_http_group_get (api, "private", fuzz_ok_handler);
	cerver_http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_BEARER);

	if (fuzz_web_sockets) fuzz_routes_web_socket (api);

	fuzz_routes_secure (http_cerver);

}

// a file of the static path of the cerver
static void fuzz_public_create (void) {

	(void) mkdir (FUZZ_PUBLIC_PATH, FUZZ_DIRECTORY_MODE);

	const int fd = open (FUZZ_PUBLIC_FILE, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
	if (fd > -1) {
		if (write (fd, FUZZ_PUBLIC_CONTENT, strlen (FUZZ_PUBLIC_CONTENT)) < 0) abort ();

		(void) close (fd);
	}

}

// the cerver is created once and never started, like in the receive unit tests
static void fuzz_cerver_create (void) {

	if (fuzz_cerver) return;

	cerver_log_init ();

	fuzz_cerver = cerver_create (
		CERVER_TYPE_WEB, FUZZ_NAME, FUZZ_PORT, PROTOCOL_TCP, false, FUZZ_CONNECTION_QUEUE
	);

	if (!fuzz_cerver) abort ();

	HttpCerver *http_cerver = cerver_http_get (fuzz_cerver);

	fuzz_routes (http_cerver);

	// the request id, and the client's address from X-Forwarded-For and X-Real-IP
	cerver_http_set_request_id (http_cerver, true);
	cerver_http_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);
	(void) cerver_http_add_origin_to_whitelist (http_cerver, FUZZ_ORIGIN);

	(void) mkdir (FUZZ_UPLOADS_PATH, FUZZ_DIRECTORY_MODE);
	cerver_http_set_uploads_path (http_cerver, FUZZ_UPLOADS_PATH);
	cerver_http_set_default_uploads_filename_generator (http_cerver);
	cerver_http_set_uploads_delete_when_done (http_cerver, true);

	fuzz_public_create ();
	(void) cerver_http_static_path_add (http_cerver, FUZZ_PUBLIC_PATH);

	if (cerver_http_init (http_cerver)) abort ();

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
