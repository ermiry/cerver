#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/internal.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region websocket

// the bytes of a 101 with its accept, its protocol and its extensions
#define HTTP_WEB_SOCKET_RESPONSE_SIZE			(1 << 10)

static const char *http_web_socket_response_start = {
	"HTTP/1.1 101 Switching Protocols\r\n"
	"Server: Cerver/" CERVER_VERSION "\r\n"
	"Upgrade: websocket\r\n"
	"Connection: Upgrade\r\n"
	"Sec-WebSocket-Accept: "
};

static const char *http_web_socket_response_protocol = { "\r\nSec-WebSocket-Protocol: " };
static const char *http_web_socket_response_extensions = { "\r\nSec-WebSocket-Extensions: " };

// the end of the last header of the handshake, and the empty line of the response
static const char http_web_socket_response_line_end[] = { "\r\n" };

#define HTTP_WEB_SOCKET_RESPONSE_LINE_END_LENGTH	(sizeof (http_web_socket_response_line_end) - 1)

// the headers of the handshake, the ones that on_upgrade () added and the empty line
#define HTTP_WEB_SOCKET_RESPONSE_PARTS			3

static const char *http_web_socket_rejected_key = { "error" };
static const char *http_web_socket_rejected_value = { "WebSocket rejected!" };

// the endpoint takes WebSocket connections, a route of the first API with the modifier does not
bool http_endpoint_is_web_socket (const HttpEndpoint *endpoint) {

	return (endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_WEB_SOCKET)
		&& (endpoint->resolved.web_socket.set & HTTP_WEB_SOCKET_SETTING_HANDLERS);

}

// gives back the place of an upgrade that never started its connection
void http_receive_web_socket_release (HttpReceive *http_receive) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;
	if (handshake) {
		if (handshake->reserved && http_receive->http_cerver) {
			http_web_socket_registry_unreserve (http_receive->http_cerver->web_sockets);
		}

		http_web_socket_handshake_delete (handshake);
		http_receive->web_socket = NULL;
	}

}

// the request asks for WebSocket: its Upgrade has websocket and its Connection has upgrade
static bool http_request_web_socket_requested (const HttpRequest *request) {

	const String *upgrade = request->headers[HTTP_HEADER_UPGRADE];
	const String *connection = request->headers[HTTP_HEADER_CONNECTION];

	return upgrade && connection
		&& http_web_socket_header_has_token (upgrade->str, upgrade->len, HTTP_WEB_SOCKET_UPGRADE_TOKEN)
		&& http_web_socket_header_has_token (connection->str, connection->len, HTTP_WEB_SOCKET_CONNECTION_TOKEN);

}

// the request of an upgrade (RFC 6455 section 4.2.1): HTTP/1.1 or newer,
// a key of 16 bytes, the version 13 and no body
// returns true when it is valid, and sets the rejection when it is not
static bool http_receive_web_socket_validate (
	const llhttp_t *parser, const HttpRequest *request,
	HttpWebSocketRejection *rejection
) {

	const String *key = request->headers[HTTP_HEADER_SEC_WEBSOCKET_KEY];
	const String *version = request->headers[HTTP_HEADER_SEC_WEBSOCKET_VERSION];

	const u8 major = llhttp_get_http_major (parser);
	const u8 minor = llhttp_get_http_minor (parser);

	bool valid = false;

	if (!major || ((major == 1) && !minor)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else if (!key || !http_web_socket_key_is_valid (key->str, key->len)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else if (!version || strcmp (version->str, HTTP_WEB_SOCKET_VERSION)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_VERSION;
	}

	else if (((parser->flags & F_CONTENT_LENGTH) && parser->content_length) || (parser->flags & F_CHUNKED)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else {
		valid = true;
	}

	return valid;

}

// the Origin of the request is in the origins whitelist of the cerver
static bool http_receive_web_socket_origin_allowed (
	const HttpCerver *http_cerver, const HttpRequest *request
) {

	const String *origin = request->headers[HTTP_HEADER_ORIGIN];

	bool allowed = false;
	for (u8 idx = 0; origin && !allowed && (idx < http_cerver->n_origins); idx++) {
		allowed = !strcmp (http_cerver->origins_whitelist[idx].value, origin->str);
	}

	return allowed;

}

static int http_receive_web_socket_reject (
	HttpReceive *http_receive, HttpResponse *response,
	const HttpWebSocketRejection rejection
) {

	http_web_socket_registry_count_rejection (http_receive->http_cerver->web_sockets, rejection);

	return http_receive_reject (http_receive, response, HTTP_RECEIVE_STATUS_REJECTED);

}

// what the 101 says: its accept, the protocol of the server that the client offered and the compression
static HttpWebSocketHandshake *http_receive_web_socket_handshake (
	const HttpRequest *request, const HttpWebSocketSettings *settings
) {

	const String *key = request->headers[HTTP_HEADER_SEC_WEBSOCKET_KEY];

	HttpWebSocketHandshake *handshake = (HttpWebSocketHandshake *) calloc (1, sizeof (HttpWebSocketHandshake));

	// without its accept there is no 101: the request is answered with a 500
	if (handshake && !http_web_socket_accept_compute (key->str, key->len, handshake->accept)) {
		free (handshake);
		handshake = NULL;
	}

	if (handshake) {
		handshake->reserved = true;

		const String *offered = request->headers[HTTP_HEADER_SEC_WEBSOCKET_PROTOCOL];
		const char *protocol = offered ? http_web_socket_protocol_select (
			offered->str, offered->len,
			(const char * const *) settings->protocols, settings->n_protocols
		) : NULL;

		if (protocol) handshake->protocol = strdup (protocol);

		const String *extensions = request->headers[HTTP_HEADER_SEC_WEBSOCKET_EXTENSIONS];
		handshake->compression = extensions && http_web_socket_deflate_negotiate (
			extensions->str, extensions->len, &settings->compression, &handshake->deflate
		);
	}

	return handshake;

}

// the checks of an upgrade before its body, a place for its connection is taken
static int http_receive_web_socket_prepare (
	HttpReceive *http_receive, const llhttp_t *parser,
	const HttpEndpoint *endpoint
) {

	int retval = 0;

	HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;
	HttpWebSocketRejection rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;

	if (!http_receive_web_socket_validate (parser, request, &rejection)) {
		retval = http_receive_web_socket_reject (
			http_receive,
			(rejection == HTTP_WEB_SOCKET_REJECTION_VERSION) ? upgrade_required_error : bad_request_error,
			rejection
		);
	}

	else if (endpoint->resolved.web_socket.origin_check && !http_receive_web_socket_origin_allowed (http_cerver, request)) {
		retval = http_receive_web_socket_reject (http_receive, forbidden_error, HTTP_WEB_SOCKET_REJECTION_ORIGIN);
	}

	else if (!http_web_socket_registry_reserve (http_cerver->web_sockets)) {
		retval = http_receive_web_socket_reject (http_receive, web_socket_unavailable_error, HTTP_WEB_SOCKET_REJECTION_LIMIT);
	}

	else {
		http_receive->web_socket = http_receive_web_socket_handshake (request, &endpoint->resolved.web_socket);
		if (!http_receive->web_socket) {
			http_web_socket_registry_unreserve (http_cerver->web_sockets);
			retval = http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_REJECTED);
		}
	}

	return retval;

}

// a request to a WebSocket endpoint, after its authentication and before its body:
// an upgrade is checked, a request without one goes to the handler of the endpoint (426 without it)
// returns 0 to continue, 1 when the request was rejected
int http_receive_handle_headers_web_socket (
	HttpReceive *http_receive, const llhttp_t *parser
) {

	int retval = 0;

	const HttpEndpoint *endpoint = http_receive->endpoint;
	if (endpoint && http_endpoint_is_web_socket (endpoint)) {
		if (llhttp_get_upgrade (parser) && http_request_web_socket_requested (http_receive->request)) {
			retval = http_receive_web_socket_prepare (http_receive, parser, endpoint);
		}

		else if (!endpoint->handler) {
			retval = http_receive_web_socket_reject (
				http_receive, upgrade_required_error, HTTP_WEB_SOCKET_REJECTION_UPGRADE
			);
		}
	}

	return retval;

}

// appends text to the response, returns false when it does not fit
static bool http_web_socket_response_append (
	char *response, size_t *length, const char *text, const size_t text_length
) {

	bool appended = false;

	if ((*length + text_length) < HTTP_WEB_SOCKET_RESPONSE_SIZE) {
		(void) memcpy (response + *length, text, text_length);
		*length += text_length;
		appended = true;
	}

	return appended;

}

// a part of the 101 that has bytes
static void http_web_socket_response_part (
	struct iovec *parts, int *n_parts, const char *text, const size_t length
) {

	if (length) {
		parts[*n_parts].iov_base = (void *) text;
		parts[*n_parts].iov_len = length;
		*n_parts += 1;
	}

}

// sends the 101, it never has a Content-Length nor a Connection: close:
// the headers of the handshake, the ones that on_upgrade () added (each one is already its line)
// and the empty line
// returns 0 on success, 1 on error
static u8 http_receive_web_socket_switch (
	HttpReceive *http_receive, const HttpWebSocketHandshake *handshake,
	const HttpHeaders *headers
) {

	char response[HTTP_WEB_SOCKET_RESPONSE_SIZE];
	size_t length = 0;

	bool fits = http_web_socket_response_append (
		response, &length, http_web_socket_response_start, strlen (http_web_socket_response_start)
	) && http_web_socket_response_append (response, &length, handshake->accept, HTTP_WEB_SOCKET_ACCEPT_LENGTH);

	if (fits && handshake->protocol) {
		fits = http_web_socket_response_append (
			response, &length, http_web_socket_response_protocol, strlen (http_web_socket_response_protocol)
		) && http_web_socket_response_append (response, &length, handshake->protocol, strlen (handshake->protocol));
	}

	if (fits && handshake->compression) {
		char extensions[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
		const size_t extensions_length = http_web_socket_deflate_response (
			&handshake->deflate, extensions, HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE
		);

		fits = extensions_length && http_web_socket_response_append (
			response, &length, http_web_socket_response_extensions, strlen (http_web_socket_response_extensions)
		) && http_web_socket_response_append (response, &length, extensions, extensions_length);
	}

	fits = fits && http_web_socket_response_append (
		response, &length, http_web_socket_response_line_end, HTTP_WEB_SOCKET_RESPONSE_LINE_END_LENGTH
	);

	size_t lines_length = 0;
	const char *lines = http_headers_lines (headers, &lines_length);

	struct iovec parts[HTTP_WEB_SOCKET_RESPONSE_PARTS];
	int n_parts = 0;

	http_web_socket_response_part (parts, &n_parts, response, length);
	http_web_socket_response_part (parts, &n_parts, lines, lines_length);
	http_web_socket_response_part (
		parts, &n_parts, http_web_socket_response_line_end, HTTP_WEB_SOCKET_RESPONSE_LINE_END_LENGTH
	);

	return fits ?
		http_response_send_pieces_status (http_receive, HTTP_STATUS_SWITCHING_PROTOCOLS, parts, n_parts) : 1;

}

// the handshake takes what on_upgrade () decided:
// a protocol that the client offered, the data of the connection and the decoded data of the request
static void http_receive_web_socket_decided (
	HttpWebSocketHandshake *handshake, HttpRequest *request,
	const HttpWebSocketUpgrade *upgrade
) {

	const String *offered = request->headers[HTTP_HEADER_SEC_WEBSOCKET_PROTOCOL];

	if (upgrade->protocol != handshake->protocol) {
		const char *protocol = (upgrade->protocol && offered) ? http_web_socket_protocol_select (
			offered->str, offered->len, &upgrade->protocol, 1
		) : NULL;

		free (handshake->protocol);
		handshake->protocol = protocol ? strdup (protocol) : NULL;
	}

	handshake->data = upgrade->data;
	handshake->delete_data = upgrade->delete_data;

	if (upgrade->adopt_decoded_data) {
		handshake->decoded_data = request->decoded_data;
		handshake->delete_decoded_data = request->delete_decoded_data;
		request->decoded_data = NULL;
		request->delete_decoded_data = NULL;
	}

}

// runs on_upgrade (), that accepts the connection or rejects it with its status,
// and adds its headers to the ones of the 101
static bool http_receive_web_socket_accept (
	HttpReceive *http_receive, HttpRequest *request,
	const HttpEndpoint *endpoint, HttpWebSocketHandshake *handshake,
	HttpHeaders *headers
) {

	bool accepted = true;

	const HttpWebSocketHandlers *handlers = &endpoint->resolved.web_socket.handlers;
	if (handlers->on_upgrade) {
		HttpWebSocketUpgrade upgrade = { handshake->protocol, NULL, NULL, false, HTTP_STATUS_FORBIDDEN, headers };

		accepted = handlers->on_upgrade (http_receive, request, &upgrade);
		if (accepted) {
			http_receive_web_socket_decided (handshake, request, &upgrade);
		}

		else {
			if (upgrade.data && upgrade.delete_data) upgrade.delete_data (upgrade.data);

			http_web_socket_registry_count_rejection (
				http_receive->http_cerver->web_sockets, HTTP_WEB_SOCKET_REJECTION_APPLICATION
			);

			// on_upgrade () can answer the request itself
			if (!http_receive->responded) {
				(void) http_response_json_key_value_send (
					http_receive, upgrade.status, http_web_socket_rejected_key, http_web_socket_rejected_value
				);
			}
		}
	}

	return accepted;

}

// the frames that the client sent with its request, before the 101,
// are copied out of the buffer of the thread for its connection
void http_receive_web_socket_pending (
	HttpReceive *http_receive, const char *unparsed
) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;
	if (handshake && http_receive->unparsed && !handshake->pending) {
		handshake->pending = (u8 *) malloc (http_receive->unparsed);
		if (handshake->pending) {
			(void) memcpy (handshake->pending, unparsed, http_receive->unparsed);
			handshake->pending_size = http_receive->unparsed;
		}

		// the connection can not start without its first frames
		else {
			http_receive->upgraded = false;
			http_receive_close (http_receive, false);
		}
	}

}

// the upgrade of a request that passed its checks: the before middlewares, on_upgrade () and the 101
void http_receive_web_socket_upgrade (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;

	// the headers that on_upgrade () adds to the 101
	HttpHeaders headers;
	http_headers_init (&headers);

	// a before middleware that stops answers the request itself
	if (
		http_middlewares_run_before (&endpoint->resolved.middlewares, http_receive, request)
		&& http_receive_web_socket_accept (http_receive, request, endpoint, handshake, &headers)
		&& !http_receive_web_socket_switch (http_receive, handshake, &headers)
	) {
		http_receive->upgraded = true;
	}

	http_headers_reset (&headers);

}

#pragma endregion
