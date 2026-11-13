#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <ctype.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <sys/eventfd.h>
#include <sys/socket.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/network.h"
#include "cerver/socket.h"

#include "cerver/utils/log.h"

#include "cerver/http/headers.h"

#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"

#include "cerver/http/websockets/client.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/settings.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/thread.h"

#define HTTP_WEB_SOCKET_CLIENT_SCHEME_LENGTH			(sizeof (HTTP_WEB_SOCKET_CLIENT_SCHEME) - 1)
#define HTTP_WEB_SOCKET_CLIENT_SECURE_SCHEME_LENGTH		(sizeof (HTTP_WEB_SOCKET_CLIENT_SECURE_SCHEME) - 1)

#define HTTP_WEB_SOCKET_CLIENT_MAX_PORT					65535
#define HTTP_WEB_SOCKET_CLIENT_DECIMAL_BASE				10

// the bytes of the name of a header of the response, a longer one is never one that is checked
#define HTTP_WEB_SOCKET_CLIENT_FIELD_SIZE				(1 << 6)

// the end of the headers of the response
static const char *http_web_socket_client_headers_end = { "\r\n\r\n" };
#define HTTP_WEB_SOCKET_CLIENT_HEADERS_END_LENGTH		4

static const char *http_web_socket_client_values_separator = { ", " };
#define HTTP_WEB_SOCKET_CLIENT_VALUES_SEPARATOR_LENGTH	2

static const char http_web_socket_client_ipv6_start = '[';
static const char http_web_socket_client_ipv6_end = ']';
static const char http_web_socket_client_port_separator = ':';
static const char http_web_socket_client_path_start = '/';
static const char http_web_socket_client_query_start = '?';
static const char http_web_socket_client_fragment_start = '#';

// the characters of a url are above the space and are not the delete control
static const unsigned char http_web_socket_client_url_space = ' ';
static const unsigned char http_web_socket_client_url_delete = 0x7F;

static const char *http_web_socket_client_root_path = { "/" };

#define HTTP_WEB_SOCKET_CLIENT_REQUEST_START			\
	"GET %s HTTP/1.1\r\n"								\
	"Host: %s\r\n"										\
	"Upgrade: websocket\r\n"							\
	"Connection: Upgrade\r\n"							\
	"Sec-WebSocket-Key: %s\r\n"							\
	"Sec-WebSocket-Version: " HTTP_WEB_SOCKET_VERSION "\r\n"

#define HTTP_WEB_SOCKET_CLIENT_HOST_PORT_FORMAT			"%s:%u"
#define HTTP_WEB_SOCKET_CLIENT_IPV6_HOST_FORMAT			"[%s]"
#define HTTP_WEB_SOCKET_CLIENT_IPV6_HOST_PORT_FORMAT	"[%s]:%u"
#define HTTP_WEB_SOCKET_CLIENT_HEADER_FORMAT			"%s: %s"

static const char *http_web_socket_client_protocols_header = { "Sec-WebSocket-Protocol: " };
static const char *http_web_socket_client_extensions_header = { "Sec-WebSocket-Extensions: " };
static const char *http_web_socket_client_line_end = { "\r\n" };

static const char *http_web_socket_client_upgrade_name = { "Upgrade" };
static const char *http_web_socket_client_connection_name = { "Connection" };
static const char *http_web_socket_client_accept_name = { "Sec-WebSocket-Accept" };
static const char *http_web_socket_client_protocol_name = { "Sec-WebSocket-Protocol" };
static const char *http_web_socket_client_extensions_name = { "Sec-WebSocket-Extensions" };

// a fraction of the wait between reconnections is random, so many clients do not come back together
#define HTTP_WEB_SOCKET_CLIENT_JITTER_DIVISOR			4

// an attempt waits for its socket and for the wake up of a client that is stopping
#define HTTP_WEB_SOCKET_CLIENT_WAIT_FDS					2
#define HTTP_WEB_SOCKET_CLIENT_WAIT_SOCKET				0
#define HTTP_WEB_SOCKET_CLIENT_WAIT_WAKE				1

// the client whose methods run in this thread: its own thread, or the one of its synchronous connect
// that client can not be waited for nor deleted from here
static __thread const HttpWebSocketClient *http_web_socket_client_running = NULL;

#define HTTP_WEB_SOCKET_CLIENT_WAKE_VALUE				1

#define HTTP_WEB_SOCKET_CLIENT_HOST_FORMAT				"%s"

static const char http_web_socket_client_zero = '0';

#pragma region response

// the headers that the client checks
typedef struct HttpWebSocketClientSeen {

	bool upgrade;
	bool connection;
	bool accept;

} HttpWebSocketClientSeen;

// the state of the parse of the response
typedef struct HttpWebSocketClientParse {

	HttpWebSocketClientResponse *response;

	HttpWebSocketClientSeen seen;

	char field[HTTP_WEB_SOCKET_CLIENT_FIELD_SIZE];
	size_t field_length;
	bool field_overflow;

	char value[HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	size_t value_length;
	bool value_overflow;

} HttpWebSocketClientParse;

// appends a piece to a buffer of size bytes, it keeps its '\0'
static void http_web_socket_client_append (
	char *buffer, const size_t size, size_t *length, bool *overflow,
	const char *at, const size_t at_length
) {

	if ((*length + at_length) < size) {
		(void) memcpy (buffer + *length, at, at_length);
		*length += at_length;
		buffer[*length] = '\0';
	}

	else {
		*overflow = true;
	}

}

static int http_web_socket_client_on_field (llhttp_t *parser, const char *at, size_t length) {

	HttpWebSocketClientParse *parse = (HttpWebSocketClientParse *) parser->data;

	http_web_socket_client_append (
		parse->field, HTTP_WEB_SOCKET_CLIENT_FIELD_SIZE,
		&parse->field_length, &parse->field_overflow, at, length
	);

	return 0;

}

static int http_web_socket_client_on_value (llhttp_t *parser, const char *at, size_t length) {

	HttpWebSocketClientParse *parse = (HttpWebSocketClientParse *) parser->data;

	http_web_socket_client_append (
		parse->value, HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE,
		&parse->value_length, &parse->value_overflow, at, length
	);

	return 0;

}

// a value that can only appear once
static void http_web_socket_client_unique (
	HttpWebSocketClientParse *parse, char *destination, bool *seen
) {

	if (*seen || parse->value_overflow) parse->response->invalid = true;
	else (void) strcpy (destination, parse->value);

	*seen = true;

}

// a value that can be repeated, its values are joined like a list
static void http_web_socket_client_list (
	HttpWebSocketClientParse *parse, char *destination, bool *seen
) {

	size_t length = strlen (destination);
	bool overflow = parse->value_overflow;

	if (*seen) {
		http_web_socket_client_append (
			destination, HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE, &length, &overflow,
			http_web_socket_client_values_separator, HTTP_WEB_SOCKET_CLIENT_VALUES_SEPARATOR_LENGTH
		);
	}

	http_web_socket_client_append (
		destination, HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE, &length, &overflow,
		parse->value, parse->value_length
	);

	if (overflow) parse->response->invalid = true;

	*seen = true;

}

static void http_web_socket_client_header (HttpWebSocketClientParse *parse) {

	HttpWebSocketClientResponse *response = parse->response;
	HttpWebSocketClientSeen *seen = &parse->seen;
	const char *name = parse->field;

	if (parse->field_overflow) return;

	if (!strcasecmp (name, http_web_socket_client_upgrade_name)) {
		http_web_socket_client_unique (parse, response->upgrade, &seen->upgrade);
	}

	else if (!strcasecmp (name, http_web_socket_client_connection_name)) {
		http_web_socket_client_list (parse, response->connection, &seen->connection);
	}

	else if (!strcasecmp (name, http_web_socket_client_accept_name)) {
		http_web_socket_client_unique (parse, response->accept, &seen->accept);
	}

	else if (!strcasecmp (name, http_web_socket_client_protocol_name)) {
		http_web_socket_client_unique (parse, response->protocol, &response->has_protocol);
	}

	else if (!strcasecmp (name, http_web_socket_client_extensions_name)) {
		http_web_socket_client_list (parse, response->extensions, &response->has_extensions);
	}

}

static int http_web_socket_client_on_value_complete (llhttp_t *parser) {

	HttpWebSocketClientParse *parse = (HttpWebSocketClientParse *) parser->data;

	http_web_socket_client_header (parse);

	parse->field_length = 0;
	parse->field[0] = '\0';
	parse->field_overflow = false;

	parse->value_length = 0;
	parse->value[0] = '\0';
	parse->value_overflow = false;

	return 0;

}

static int http_web_socket_client_on_headers_complete (llhttp_t *parser) {

	HttpWebSocketClientParse *parse = (HttpWebSocketClientParse *) parser->data;

	parse->response->status = llhttp_get_status_code (parser);
	parse->response->complete = true;

	// the body of a response that was not a 101 is never read
	return 1;

}

u8 http_web_socket_client_response_parse (
	const char *data, const size_t size,
	HttpWebSocketClientResponse *response
) {

	u8 retval = HTTP_WEB_SOCKET_CLIENT_RESPONSE_INVALID;

	if (data && response) {
		(void) memset (response, 0, sizeof (HttpWebSocketClientResponse));

		const char *end = (const char *) memmem (
			data, size, http_web_socket_client_headers_end, HTTP_WEB_SOCKET_CLIENT_HEADERS_END_LENGTH
		);

		if (!end) {
			// a response that has no end in the most bytes that are read is not valid either
			response->invalid = (size >= HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE);
			if (!response->invalid) retval = HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL;
		}

		else {
			response->size = (size_t) (end - data) + HTTP_WEB_SOCKET_CLIENT_HEADERS_END_LENGTH;

			llhttp_settings_t settings;
			llhttp_settings_init (&settings);
			settings.on_header_field = http_web_socket_client_on_field;
			settings.on_header_value = http_web_socket_client_on_value;
			settings.on_header_value_complete = http_web_socket_client_on_value_complete;
			settings.on_headers_complete = http_web_socket_client_on_headers_complete;

			HttpWebSocketClientParse parse;
			(void) memset (&parse, 0, sizeof (HttpWebSocketClientParse));
			parse.response = response;

			llhttp_t parser;
			llhttp_init (&parser, HTTP_RESPONSE, &settings);
			parser.data = &parse;

			const llhttp_errno_t result = llhttp_execute (&parser, data, response->size);

			const bool parsed = (result == HPE_OK) || (result == HPE_PAUSED_UPGRADE) || (result == HPE_PAUSED);
			response->invalid = !parsed || !response->complete || response->invalid;
			if (!response->invalid) retval = HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE;
		}
	}

	return retval;

}

// the protocol of the response is one of the client, the same pointer is returned
static bool http_web_socket_client_protocol_check (
	const HttpWebSocketClientResponse *response,
	char * const *protocols, const unsigned int n_protocols,
	const char **protocol
) {

	bool valid = !response->has_protocol;
	const char *selected = NULL;

	for (unsigned int i = 0; response->has_protocol && !valid && (i < n_protocols); i++) {
		if (protocols[i] && !strcmp (protocols[i], response->protocol)) {
			selected = protocols[i];
			valid = true;
		}
	}

	if (protocol) *protocol = selected;

	return valid;

}

bool http_web_socket_client_response_check (
	const HttpWebSocketClientResponse *response, const char *key,
	char * const *protocols, const unsigned int n_protocols,
	const HttpWebSocketDeflateOptions *options,
	const char **protocol, bool *compression, HttpWebSocketDeflateParameters *parameters
) {

	bool valid = false;

	char accept[HTTP_WEB_SOCKET_ACCEPT_SIZE];
	if (
		response && key && options && compression && parameters
		&& response->complete && !response->invalid
		&& (response->status == HTTP_STATUS_SWITCHING_PROTOCOLS)
		&& http_web_socket_header_has_token (response->upgrade, strlen (response->upgrade), HTTP_WEB_SOCKET_UPGRADE_TOKEN)
		&& http_web_socket_header_has_token (response->connection, strlen (response->connection), HTTP_WEB_SOCKET_CONNECTION_TOKEN)
		&& http_web_socket_accept_compute (key, strlen (key), accept)
		&& !strcmp (accept, response->accept)
		&& http_web_socket_client_protocol_check (response, protocols, n_protocols, protocol)
	) {
		// the server can only use an extension that the client offered
		*compression = response->has_extensions;
		valid = !response->has_extensions || http_web_socket_deflate_accept (
			response->extensions, strlen (response->extensions), options, parameters
		);
	}

	return valid;

}

#pragma endregion

#pragma region client

HttpWebSocketClient *http_web_socket_client_create (void) {

	HttpWebSocketClient *client = (HttpWebSocketClient *) calloc (1, sizeof (HttpWebSocketClient));
	if (client) {
		client->port = HTTP_WEB_SOCKET_CLIENT_DEFAULT_PORT;
		(void) strcpy (client->path, http_web_socket_client_root_path);

		client->headers = dlist_init (free, NULL);

		http_web_socket_settings_init (&client->settings);

		client->connect_timeout = HTTP_WEB_SOCKET_CLIENT_CONNECT_TIMEOUT;
		client->handshake_timeout = HTTP_WEB_SOCKET_CLIENT_HANDSHAKE_TIMEOUT;

		client->reconnect = false;
		client->reconnect_min = HTTP_WEB_SOCKET_CLIENT_RECONNECT_MIN;
		client->reconnect_max = HTTP_WEB_SOCKET_CLIENT_RECONNECT_MAX;

		(void) pthread_mutex_init (&client->mutex, NULL);
		(void) pthread_mutex_init (&client->join_mutex, NULL);

		client->wake_fd = eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);

		if (!client->headers || (client->wake_fd < 0)) {
			http_web_socket_client_delete (client);
			client = NULL;
		}
	}

	return client;

}

// releases the client, its thread already ended
static void http_web_socket_client_release (HttpWebSocketClient *client) {

	dlist_delete (client->headers);

	http_web_socket_settings_end (&client->settings);

	if (client->wake_fd >= 0) (void) close (client->wake_fd);

	(void) pthread_mutex_destroy (&client->join_mutex);
	(void) pthread_mutex_destroy (&client->mutex);

	free (client);

}

void http_web_socket_client_delete (HttpWebSocketClient *client) {

	if (client) {
		(void) http_web_socket_client_close (client, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);

		// a method of the client runs in the thread that still uses the client:
		// it is closed, and it is deleted from another thread
		if (http_web_socket_client_running == client) {
			cerver_log_error (
				"http_web_socket_client_delete () - A client can not be deleted by one of its methods, it was only closed"
			);
		}

		else {
			http_web_socket_client_wait (client);
			http_web_socket_client_release (client);
		}
	}

}

// the host, in brackets for an IPv6, until its port or its path
// returns the position after it, 0 on error
static size_t http_web_socket_client_url_host (
	HttpWebSocketClient *client, const char *url, size_t position, const size_t length
) {

	size_t start = position;
	size_t end = position;
	size_t next = 0;

	if ((position < length) && (url[position] == http_web_socket_client_ipv6_start)) {
		const char *closing = (const char *) memchr (url + position, http_web_socket_client_ipv6_end, length - position);
		if (closing) {
			start = position + 1;
			end = (size_t) (closing - url);
			next = end + 1;
		}
	}

	else {
		while (
			(end < length) && (url[end] != http_web_socket_client_port_separator)
			&& (url[end] != http_web_socket_client_path_start) && (url[end] != http_web_socket_client_query_start)
		) {
			end += 1;
		}

		next = end;
	}

	const size_t host_length = end - start;
	if (next && host_length && (host_length < HTTP_WEB_SOCKET_CLIENT_HOST_SIZE)) {
		(void) memcpy (client->host, url + start, host_length);
		client->host[host_length] = '\0';
	}

	else {
		next = 0;
	}

	return next;

}

// the port after the ':', 1 to 65535
// returns the position after it, 0 on error
static size_t http_web_socket_client_url_port (
	HttpWebSocketClient *client, const char *url, size_t position, const size_t length
) {

	size_t next = position;
	client->port = HTTP_WEB_SOCKET_CLIENT_DEFAULT_PORT;

	if ((position < length) && (url[position] == http_web_socket_client_port_separator)) {
		unsigned long port = 0;
		next = position + 1;
		while ((next < length) && isdigit ((unsigned char) url[next]) && (port <= HTTP_WEB_SOCKET_CLIENT_MAX_PORT)) {
			port = (port * HTTP_WEB_SOCKET_CLIENT_DECIMAL_BASE) + (unsigned long) (url[next] - http_web_socket_client_zero);
			next += 1;
		}

		if ((next == (position + 1)) || !port || (port > HTTP_WEB_SOCKET_CLIENT_MAX_PORT)) next = 0;
		else client->port = (u16) port;
	}

	return next;

}

// the path and the query, "/" when it has none, a URL can not have a fragment
static u8 http_web_socket_client_url_path (
	HttpWebSocketClient *client, const char *url, const size_t position, const size_t length
) {

	u8 retval = 1;

	const char *path = url + position;
	const size_t path_length = length - position;

	if (memchr (path, http_web_socket_client_fragment_start, path_length)) {
		retval = 1;
	}

	else if (!path_length) {
		(void) strcpy (client->path, http_web_socket_client_root_path);
		retval = 0;
	}

	// a query without a path is a query of the root
	else if ((path[0] == http_web_socket_client_query_start) && ((path_length + 1) < HTTP_WEB_SOCKET_CLIENT_PATH_SIZE)) {
		client->path[0] = http_web_socket_client_path_start;
		(void) memcpy (client->path + 1, path, path_length);
		client->path[path_length + 1] = '\0';
		retval = 0;
	}

	else if ((path[0] == http_web_socket_client_path_start) && (path_length < HTTP_WEB_SOCKET_CLIENT_PATH_SIZE)) {
		(void) memcpy (client->path, path, path_length);
		client->path[path_length] = '\0';
		retval = 0;
	}

	return retval;

}

// the url is written in the request line and in the Host header:
// a space or a control character in it would end them and start other lines
static bool http_web_socket_client_url_is_visible (const char *url) {

	bool visible = true;

	for (const unsigned char *ch = (const unsigned char *) url; visible && *ch; ch++) {
		visible = (*ch > http_web_socket_client_url_space) && (*ch != http_web_socket_client_url_delete);
	}

	return visible;

}

u8 http_web_socket_client_set_url (
	HttpWebSocketClient *client, const char *url
) {

	u8 retval = 1;

	if (
		client && url && http_web_socket_client_url_is_visible (url)
		&& !strncasecmp (url, HTTP_WEB_SOCKET_CLIENT_SCHEME, HTTP_WEB_SOCKET_CLIENT_SCHEME_LENGTH)
	) {
		const size_t length = strlen (url);

		size_t position = http_web_socket_client_url_host (client, url, HTTP_WEB_SOCKET_CLIENT_SCHEME_LENGTH, length);
		if (position) position = http_web_socket_client_url_port (client, url, position, length);
		if (position) retval = http_web_socket_client_url_path (client, url, position, length);
	}

	return retval;

}

u8 http_web_socket_client_add_header (
	HttpWebSocketClient *client, const char *name, const char *value
) {

	u8 retval = 1;

	// a header that breaks the request is never sent
	if (
		client && name && value
		&& !strpbrk (name, http_web_socket_client_line_end) && !strpbrk (value, http_web_socket_client_line_end)
	) {
		const size_t size = strlen (name) + strlen (value) + HTTP_WEB_SOCKET_CLIENT_VALUES_SEPARATOR_LENGTH + 1;
		char *header = (char *) malloc (size);
		if (header) {
			(void) snprintf (header, size, HTTP_WEB_SOCKET_CLIENT_HEADER_FORMAT, name, value);
			retval = (u8) (dlist_insert_at_end_unsafe (client->headers, header) != 0);
			if (retval) free (header);
		}
	}

	return retval;

}

u8 http_web_socket_client_set_protocols (
	HttpWebSocketClient *client, const char * const *protocols, const unsigned int n_protocols
) {

	return client ? http_web_socket_settings_set_protocols (&client->settings, protocols, n_protocols) : 1;

}

void http_web_socket_client_set_handlers (
	HttpWebSocketClient *client, const HttpWebSocketHandlers *handlers
) {

	if (client) http_web_socket_settings_set_handlers (&client->settings, handlers);

}

void http_web_socket_client_set_data (HttpWebSocketClient *client, void *data) {

	if (client) client->data = data;

}

void *http_web_socket_client_get_data (const HttpWebSocketClient *client) {

	return client ? client->data : NULL;

}

void http_web_socket_client_set_compression (
	HttpWebSocketClient *client, const bool compression
) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = compression;

	http_web_socket_client_set_compression_options (client, &options);

}

void http_web_socket_client_set_compression_options (
	HttpWebSocketClient *client, const HttpWebSocketDeflateOptions *options
) {

	if (client) http_web_socket_settings_set_compression (&client->settings, options);

}

void http_web_socket_client_set_max_message (
	HttpWebSocketClient *client, const size_t max_message
) {

	if (client) http_web_socket_settings_set_max_message (&client->settings, max_message);

}

void http_web_socket_client_set_ping (
	HttpWebSocketClient *client, const unsigned int ping_interval, const unsigned int pong_timeout
) {

	if (client) http_web_socket_settings_set_ping (&client->settings, ping_interval, pong_timeout);

}

void http_web_socket_client_set_close_timeout (
	HttpWebSocketClient *client, const unsigned int close_timeout
) {

	if (client) http_web_socket_settings_set_close_timeout (&client->settings, close_timeout);

}

void http_web_socket_client_set_send_timeout (
	HttpWebSocketClient *client, const unsigned int send_timeout
) {

	if (client) http_web_socket_settings_set_send_timeout (&client->settings, send_timeout);

}

void http_web_socket_client_set_timeouts (
	HttpWebSocketClient *client, const unsigned int connect_timeout, const unsigned int handshake_timeout
) {

	if (client) {
		client->connect_timeout = connect_timeout ? connect_timeout : HTTP_WEB_SOCKET_CLIENT_CONNECT_TIMEOUT;
		client->handshake_timeout = handshake_timeout ? handshake_timeout : HTTP_WEB_SOCKET_CLIENT_HANDSHAKE_TIMEOUT;
	}

}

void http_web_socket_client_set_reconnect (
	HttpWebSocketClient *client, const bool reconnect,
	const unsigned int min, const unsigned int max
) {

	if (client) {
		client->reconnect = reconnect;
		client->reconnect_min = min ? min : HTTP_WEB_SOCKET_CLIENT_RECONNECT_MIN;
		client->reconnect_max = (max >= client->reconnect_min) ? max : client->reconnect_min;
	}

}

unsigned int http_web_socket_client_get_connections (const HttpWebSocketClient *client) {

	return client ? __atomic_load_n (&client->n_connections, __ATOMIC_SEQ_CST) : 0;

}

#pragma endregion

#pragma region connect

static bool http_web_socket_client_is_stopping (HttpWebSocketClient *client);

// empties the eventfd that wakes up the client
static void http_web_socket_client_wake_drain (const HttpWebSocketClient *client) {

	u64 value = 0;
	while (read (client->wake_fd, &value, sizeof (u64)) > 0);

}

// waits until the socket of an attempt is ready for the events or its deadline ends
// a signal does not end the wait, it continues with the time that is left;
// a client that is closed or deleted ends it now, and a wake up of before is discarded
// returns true when the socket is ready
static bool http_web_socket_client_wait_socket (
	HttpWebSocketClient *client, const int fd, const short events,
	const struct timespec *deadline
) {

	bool ready = false;
	bool waiting = true;

	while (waiting) {
		struct pollfd fds[HTTP_WEB_SOCKET_CLIENT_WAIT_FDS] = {
			{ fd, events, 0 }, { client->wake_fd, POLLIN, 0 }
		};

		const int remaining = socket_deadline_remaining (deadline);
		const int result = remaining ? poll (fds, HTTP_WEB_SOCKET_CLIENT_WAIT_FDS, remaining) : 0;

		if (result > 0) {
			if (fds[HTTP_WEB_SOCKET_CLIENT_WAIT_WAKE].revents) {
				if (http_web_socket_client_is_stopping (client)) waiting = false;
				else http_web_socket_client_wake_drain (client);
			}

			if (waiting && fds[HTTP_WEB_SOCKET_CLIENT_WAIT_SOCKET].revents) {
				ready = true;
				waiting = false;
			}
		}

		// the deadline ended, or poll () failed with something that is not a signal
		else if (!result || (errno != EINTR)) {
			waiting = false;
		}
	}

	return ready;

}

// a non blocking connect () to the address that ends before the timeout
// returns the fd of the connected socket, -1 on error
static int http_web_socket_client_connect_address (
	HttpWebSocketClient *client,
	const struct sockaddr_storage *address, const unsigned int timeout
) {

	int fd = socket (address->ss_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP);

	if (fd >= 0) {
		int result = connect (fd, (const struct sockaddr *) address, sock_address_size (address));
		if (result && (errno == EINPROGRESS)) {
			struct timespec deadline;
			socket_deadline_create_milliseconds (&deadline, timeout);

			int error = 0;
			socklen_t error_length = sizeof (int);

			result = (http_web_socket_client_wait_socket (client, fd, POLLOUT, &deadline)
				&& !getsockopt (fd, SOL_SOCKET, SO_ERROR, &error, &error_length)
				&& !error) ? 0 : -1;
		}

		if (result) {
			(void) close (fd);
			fd = -1;
		}

		else {
			// the frames are small and they go out without waiting
			const int enabled = 1;
			(void) setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (int));
		}
	}

	return fd;

}

// tries every address of the host, in the order of the resolution
static int http_web_socket_client_connect_host (HttpWebSocketClient *client) {

	int fd = -1;

	NetworkAddresses addresses;
	if (!network_resolve_addresses (client->host, client->port, false, IPPROTO_TCP, &addresses)) {
		// a client that is stopping does not try the addresses that are left
		for (
			unsigned int i = 0;
			(fd < 0) && (i < addresses.count) && !http_web_socket_client_is_stopping (client);
			i++
		) {
			fd = http_web_socket_client_connect_address (client, &addresses.addresses[i], client->connect_timeout);
			if (fd >= 0) {
				(void) sock_ip_to_string_actual ((const struct sockaddr *) &addresses.addresses[i], client->ip);
			}
		}
	}

	return fd;

}

// the Host of the request, an IPv6 goes in brackets, the default port is not written
static void http_web_socket_client_host_header (
	const HttpWebSocketClient *client, char *host, const size_t size
) {

	const bool ipv6 = strchr (client->host, http_web_socket_client_port_separator) != NULL;
	const bool default_port = (client->port == HTTP_WEB_SOCKET_CLIENT_DEFAULT_PORT);

	if (ipv6) {
		(void) (default_port ?
			snprintf (host, size, HTTP_WEB_SOCKET_CLIENT_IPV6_HOST_FORMAT, client->host) :
			snprintf (host, size, HTTP_WEB_SOCKET_CLIENT_IPV6_HOST_PORT_FORMAT, client->host, (unsigned int) client->port));
	}

	else {
		(void) (default_port ?
			snprintf (host, size, HTTP_WEB_SOCKET_CLIENT_HOST_FORMAT, client->host) :
			snprintf (host, size, HTTP_WEB_SOCKET_CLIENT_HOST_PORT_FORMAT, client->host, (unsigned int) client->port));
	}

}

static bool http_web_socket_client_request_append (
	char *request, size_t *length, const char *text
) {

	const size_t text_length = strlen (text);
	const bool fits = (*length + text_length) < HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE;

	if (fits) {
		(void) memcpy (request + *length, text, text_length);
		*length += text_length;
		request[*length] = '\0';
	}

	return fits;

}

// the protocols that are offered, separated by commas
static bool http_web_socket_client_request_protocols (
	const HttpWebSocketClient *client, char *request, size_t *length
) {

	bool fits = true;

	const HttpWebSocketSettings *settings = &client->settings;
	if (settings->n_protocols) {
		fits = http_web_socket_client_request_append (request, length, http_web_socket_client_protocols_header);
		for (unsigned int i = 0; fits && (i < settings->n_protocols); i++) {
			if (i) fits = http_web_socket_client_request_append (request, length, http_web_socket_client_values_separator);
			if (fits) fits = http_web_socket_client_request_append (request, length, settings->protocols[i]);
		}

		if (fits) fits = http_web_socket_client_request_append (request, length, http_web_socket_client_line_end);
	}

	return fits;

}

static bool http_web_socket_client_request_extras (
	const HttpWebSocketClient *client, char *request, size_t *length
) {

	bool fits = http_web_socket_client_request_protocols (client, request, length);

	if (fits && client->settings.compression.enabled && http_web_socket_deflate_available ()) {
		char offer[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
		fits = http_web_socket_deflate_offer (&client->settings.compression, offer, HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE)
			&& http_web_socket_client_request_append (request, length, http_web_socket_client_extensions_header)
			&& http_web_socket_client_request_append (request, length, offer)
			&& http_web_socket_client_request_append (request, length, http_web_socket_client_line_end);
	}

	for (ListElement *le = dlist_start (client->headers); fits && le; le = le->next) {
		fits = http_web_socket_client_request_append (request, length, (const char *) le->data)
			&& http_web_socket_client_request_append (request, length, http_web_socket_client_line_end);
	}

	return fits && http_web_socket_client_request_append (request, length, http_web_socket_client_line_end);

}

// writes the request of the handshake
// returns its length, 0 when it does not fit
static size_t http_web_socket_client_request (
	const HttpWebSocketClient *client, const char *key, char *request
) {

	char host[HTTP_WEB_SOCKET_CLIENT_HOST_SIZE + HTTP_WEB_SOCKET_CLIENT_VALUE_SIZE];
	http_web_socket_client_host_header (client, host, sizeof (host));

	const int written = snprintf (
		request, HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE, HTTP_WEB_SOCKET_CLIENT_REQUEST_START,
		client->path, host, key
	);

	size_t length = (written > 0) ? (size_t) written : 0;
	const bool fits = (written > 0) && (length < HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE)
		&& http_web_socket_client_request_extras (client, request, &length);

	return fits ? length : 0;

}

// reads the response of the server until its empty line, before the deadline
// returns 0 when it is complete, 1 on error
static u8 http_web_socket_client_read_response (
	HttpWebSocketClient *client,
	Socket *socket, const struct timespec *deadline,
	char *response, size_t *size, HttpWebSocketClientResponse *parsed
) {

	u8 result = HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL;

	while (result == HTTP_WEB_SOCKET_CLIENT_RESPONSE_PARTIAL) {
		if (!http_web_socket_client_wait_socket (client, socket->sock_fd, POLLIN, deadline)) break;

		const ssize_t received = recv (
			socket->sock_fd, response + *size, HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE - *size, MSG_DONTWAIT
		);

		if (received > 0) {
			*size += (size_t) received;
			result = http_web_socket_client_response_parse (response, *size, parsed);
		}

		else if (!received || ((errno != EAGAIN) && (errno != EINTR))) {
			break;
		}
	}

	return (result == HTTP_WEB_SOCKET_CLIENT_RESPONSE_COMPLETE) ? 0 : 1;

}

// the bytes that followed the response, the first frames of the server
// returns 0 on success, 1 when there is no memory for them: they are never lost in silence
static u8 http_web_socket_client_pending (
	HttpWebSocket *web_socket, const char *response, const size_t size,
	const HttpWebSocketClientResponse *parsed
) {

	u8 retval = 0;

	const size_t pending = size - parsed->size;
	if (pending) {
		u8 *bytes = (u8 *) malloc (pending);
		if (bytes) {
			(void) memcpy (bytes, response + parsed->size, pending);
			http_web_socket_set_pending (web_socket, bytes, pending);
		}

		else {
			retval = 1;
		}
	}

	return retval;

}

// the connection takes what the response decided: its compression, its protocol and its first frames
// returns HTTP_WEB_SOCKET_ERROR_NONE on success, HTTP_WEB_SOCKET_ERROR_MEMORY without memory for them
static HttpWebSocketError http_web_socket_client_accepted_take (
	HttpWebSocketClient *client, HttpWebSocket *web_socket,
	const char *response, const size_t size, const HttpWebSocketClientResponse *parsed,
	const char *protocol, const HttpWebSocketDeflateParameters *parameters
) {

	u8 failed = parameters ?
		http_web_socket_set_compression (web_socket, parameters, &client->settings.compression) : 0;

	if (!failed && protocol) {
		web_socket->protocol = strdup (protocol);
		failed = web_socket->protocol ? 0 : 1;
	}

	if (!failed) failed = http_web_socket_client_pending (web_socket, response, size, parsed);

	return failed ? HTTP_WEB_SOCKET_ERROR_MEMORY : HTTP_WEB_SOCKET_ERROR_NONE;

}

// the response of the server is checked, and the connection takes what it decided
static HttpWebSocketError http_web_socket_client_accepted (
	HttpWebSocketClient *client, HttpWebSocket *web_socket,
	const char *response, const size_t size,
	const HttpWebSocketClientResponse *parsed, const char *key
) {

	HttpWebSocketError error = HTTP_WEB_SOCKET_ERROR_HANDSHAKE;

	const char *protocol = NULL;
	bool compression = false;
	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));

	if (http_web_socket_client_response_check (
		parsed, key, client->settings.protocols, client->settings.n_protocols,
		&client->settings.compression, &protocol, &compression, &parameters
	)) {
		error = http_web_socket_client_accepted_take (
			client, web_socket, response, size, parsed,
			protocol, compression ? &parameters : NULL
		);
	}

	return error;

}

// sends the request and checks the response
// returns HTTP_WEB_SOCKET_ERROR_NONE on success
static HttpWebSocketError http_web_socket_client_handshake (
	HttpWebSocketClient *client, HttpWebSocket *web_socket
) {

	HttpWebSocketError error = HTTP_WEB_SOCKET_ERROR_HANDSHAKE;

	struct timespec deadline;
	socket_deadline_create_milliseconds (&deadline, client->handshake_timeout);

	char key[HTTP_WEB_SOCKET_KEY_SIZE];
	char *request = (char *) malloc (HTTP_WEB_SOCKET_CLIENT_REQUEST_SIZE);
	char *response = (char *) malloc (HTTP_WEB_SOCKET_CLIENT_RESPONSE_SIZE);

	const size_t request_length = (request && http_web_socket_key_generate (key)) ?
		http_web_socket_client_request (client, key, request) : 0;

	if (!request || !response) {
		error = HTTP_WEB_SOCKET_ERROR_MEMORY;
	}

	else if (
		request_length
		&& !socket_send_buffer_until (web_socket->socket, request, request_length, 0, &deadline, NULL, NULL)
	) {
		size_t size = 0;
		HttpWebSocketClientResponse parsed;
		if (!http_web_socket_client_read_response (client, web_socket->socket, &deadline, response, &size, &parsed)) {
			error = http_web_socket_client_accepted (client, web_socket, response, size, &parsed, key);
		}
	}

	free (request);
	free (response);

	return error;

}

// the connection could not start, its methods report it
static void http_web_socket_client_failed (
	HttpWebSocket *web_socket, const HttpWebSocketError error
) {

	if (web_socket->settings.handlers.on_error) web_socket->settings.handlers.on_error (web_socket, error);

	if (web_socket->settings.handlers.on_close) {
		web_socket->settings.handlers.on_close (web_socket, HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL, 0);
	}

}

// releases the socket of a connection that ended
static void http_web_socket_client_socket_end (HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&web_socket->mutex);
	Socket *socket = web_socket->socket;
	web_socket->socket = NULL;
	(void) pthread_mutex_unlock (&web_socket->mutex);

	if (socket) {
		(void) socket_close (socket);
		socket_delete (socket);
	}

}

// a new connection: the connect (), the handshake and its settings
// returns the open connection, NULL on error (its methods reported it)
static HttpWebSocket *http_web_socket_client_attempt (HttpWebSocketClient *client) {

	HttpWebSocket *web_socket = http_web_socket_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &client->settings);
	if (web_socket) {
		web_socket->client = client;

		// the methods of a connection that failed also have it
		web_socket->data = client->data;

		HttpWebSocketError error = HTTP_WEB_SOCKET_ERROR_CONNECT;

		const int fd = http_web_socket_client_connect_host (client);
		if (fd >= 0) {
			web_socket->socket = socket_create (fd);
			if (!web_socket->socket) (void) close (fd);

			(void) snprintf (web_socket->ip, CONNECTION_IP_SIZE, HTTP_WEB_SOCKET_CLIENT_HOST_FORMAT, client->ip);
			web_socket->path = str_new (client->path);

			error = web_socket->socket ?
				http_web_socket_client_handshake (client, web_socket) : HTTP_WEB_SOCKET_ERROR_MEMORY;
		}

		if (error != HTTP_WEB_SOCKET_ERROR_NONE) {
			http_web_socket_client_socket_end (web_socket);
			http_web_socket_client_failed (web_socket, error);
			http_web_socket_release (web_socket);
			web_socket = NULL;

			client->n_failures += 1;
		}

		else {
			// a synchronous connect () returns with an open connection
			http_web_socket_opened (web_socket);

			client->n_failures = 0;
			(void) __atomic_add_fetch (&client->n_connections, 1, __ATOMIC_SEQ_CST);

			(void) pthread_mutex_lock (&client->mutex);
			client->web_socket = web_socket;
			(void) pthread_mutex_unlock (&client->mutex);
		}
	}

	return web_socket;

}

static bool http_web_socket_client_is_stopping (HttpWebSocketClient *client) {

	return __atomic_load_n (&client->stopping, __ATOMIC_SEQ_CST);

}

// runs the connection until it ends, and releases it
static void http_web_socket_client_run (HttpWebSocketClient *client, HttpWebSocket *web_socket) {

	// a close of the client before the connection opened
	if (http_web_socket_client_is_stopping (client)) {
		(void) http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_NORMAL, NULL);
	}

	http_web_socket_run (web_socket);

	http_web_socket_client_socket_end (web_socket);

	(void) pthread_mutex_lock (&client->mutex);
	client->web_socket = NULL;
	(void) pthread_mutex_unlock (&client->mutex);

	http_web_socket_release (web_socket);

}

// waits before the next attempt, doubled after each failure, with jitter
// returns false when the client stopped while it waited
static bool http_web_socket_client_backoff (HttpWebSocketClient *client, unsigned int *seed) {

	unsigned int delay = client->reconnect_min;
	for (unsigned int i = 0; (i < client->n_failures) && (delay < client->reconnect_max); i++) delay *= 2;
	if (delay > client->reconnect_max) delay = client->reconnect_max;

	const unsigned int jitter = delay / HTTP_WEB_SOCKET_CLIENT_JITTER_DIVISOR;
	if (jitter) delay += (unsigned int) rand_r (seed) % jitter;

	struct pollfd pfd = { client->wake_fd, POLLIN, 0 };
	(void) poll (&pfd, 1, (int) delay);

	u64 value = 0;
	while (read (client->wake_fd, &value, sizeof (u64)) > 0);

	return !http_web_socket_client_is_stopping (client);

}

static void *http_web_socket_client_thread (void *client_ptr) {

	HttpWebSocketClient *client = (HttpWebSocketClient *) client_ptr;

	// the methods of the client run in this thread until it ends
	http_web_socket_client_running = client;

	unsigned int seed = (unsigned int) time (NULL) ^ (unsigned int) (uintptr_t) client;

	// a connect () that was not synchronous starts here
	(void) pthread_mutex_lock (&client->mutex);
	HttpWebSocket *web_socket = client->web_socket;
	(void) pthread_mutex_unlock (&client->mutex);

	if (!web_socket && !http_web_socket_client_is_stopping (client)) {
		web_socket = http_web_socket_client_attempt (client);
	}

	bool running = true;
	while (running) {
		if (web_socket) http_web_socket_client_run (client, web_socket);

		running = client->reconnect && !http_web_socket_client_is_stopping (client)
			&& http_web_socket_client_backoff (client, &seed);

		web_socket = running ? http_web_socket_client_attempt (client) : NULL;
	}

	http_web_socket_client_running = NULL;

	return NULL;

}

// the connection of a synchronous connect () opened and its thread could not be created:
// it ends like an attempt that failed, its methods report it and it is not one of the connections
// that were opened; it is closed before its socket is released, another thread can be sending
static void http_web_socket_client_abandon (HttpWebSocketClient *client, HttpWebSocket *web_socket) {

	(void) pthread_mutex_lock (&client->mutex);
	client->web_socket = NULL;
	(void) pthread_mutex_unlock (&client->mutex);

	http_web_socket_closed (web_socket);
	http_web_socket_client_socket_end (web_socket);

	(void) __atomic_sub_fetch (&client->n_connections, 1, __ATOMIC_SEQ_CST);
	client->n_failures += 1;

	http_web_socket_client_running = client;
	http_web_socket_client_failed (web_socket, HTTP_WEB_SOCKET_ERROR_MEMORY);
	http_web_socket_client_running = NULL;

	http_web_socket_release (web_socket);

}

// a client that connects again is not stopping, and the wake up of its last close is discarded:
// it ended the first wait between the reconnections of the new connection
static void http_web_socket_client_restart (HttpWebSocketClient *client) {

	__atomic_store_n (&client->stopping, false, __ATOMIC_SEQ_CST);

	http_web_socket_client_wake_drain (client);

}

u8 http_web_socket_client_connect (HttpWebSocketClient *client) {

	u8 retval = 1;

	if (client && !client->thread_started) {
		http_web_socket_client_restart (client);

		// the methods of an attempt that fails run here, in the thread of the caller
		http_web_socket_client_running = client;
		HttpWebSocket *web_socket = http_web_socket_client_attempt (client);
		http_web_socket_client_running = NULL;

		if (web_socket) {
			if (!thread_create_joinable (&client->thread, http_web_socket_client_thread, client)) {
				client->thread_started = true;
				retval = 0;
			}

			else {
				http_web_socket_client_abandon (client, web_socket);
			}
		}
	}

	return retval;

}

u8 http_web_socket_client_connect_async (HttpWebSocketClient *client) {

	u8 retval = 1;

	if (client && !client->thread_started) {
		http_web_socket_client_restart (client);

		if (!thread_create_joinable (&client->thread, http_web_socket_client_thread, client)) {
			client->thread_started = true;
			retval = 0;
		}
	}

	return retval;

}

HttpWebSocket *http_web_socket_client_get_web_socket (HttpWebSocketClient *client) {

	HttpWebSocket *web_socket = NULL;

	if (client) {
		(void) pthread_mutex_lock (&client->mutex);
		web_socket = http_web_socket_retain (client->web_socket);
		(void) pthread_mutex_unlock (&client->mutex);
	}

	return web_socket;

}

u8 http_web_socket_client_send (
	HttpWebSocketClient *client,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	u8 retval = 1;

	HttpWebSocket *web_socket = http_web_socket_client_get_web_socket (client);
	if (web_socket) {
		retval = http_web_socket_send (web_socket, type, data, size);
		http_web_socket_release (web_socket);
	}

	return retval;

}

u8 http_web_socket_client_send_text (
	HttpWebSocketClient *client, const char *text, const size_t length
) {

	return http_web_socket_client_send (client, HTTP_WEB_SOCKET_OPCODE_TEXT, text, length);

}

u8 http_web_socket_client_send_binary (
	HttpWebSocketClient *client, const void *data, const size_t size
) {

	return http_web_socket_client_send (client, HTTP_WEB_SOCKET_OPCODE_BINARY, data, size);

}

u8 http_web_socket_client_close (
	HttpWebSocketClient *client, const u16 code, const char *reason
) {

	u8 retval = 1;

	if (client) {
		__atomic_store_n (&client->stopping, true, __ATOMIC_SEQ_CST);

		// the wait between reconnections ends now
		const u64 value = HTTP_WEB_SOCKET_CLIENT_WAKE_VALUE;
		const ssize_t written = write (client->wake_fd, &value, sizeof (u64));
		(void) written;

		HttpWebSocket *web_socket = http_web_socket_client_get_web_socket (client);
		retval = web_socket ? http_web_socket_close (web_socket, code, reason) : 0;
		http_web_socket_release (web_socket);
	}

	return retval;

}

void http_web_socket_client_wait (HttpWebSocketClient *client) {

	// a method of the client runs in the thread of the client, that can not wait for itself
	if (client && (http_web_socket_client_running != client)) {
		// two threads that wait at once: only one of them joins the thread
		(void) pthread_mutex_lock (&client->join_mutex);

		if (client->thread_started) {
			(void) pthread_join (client->thread, NULL);
			client->thread_started = false;
		}

		(void) pthread_mutex_unlock (&client->join_mutex);
	}

}

#pragma endregion
