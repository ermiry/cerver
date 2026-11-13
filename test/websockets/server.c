#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <pthread.h>
#include <unistd.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>
#include <cerver/http/metrics.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>
#include <cerver/http/tree.h>

#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include "../test.h"

#include "raw.h"
#include "websocket.h"

// the ports of the servers, after the ones of the HTTP suites
#define SERVER_TEST_PORT_BASE				(11264)

#define SERVER_TEST_THREADS					4

// a guard for a test that hangs
#define SERVER_TEST_ALARM					(1 << 9)

// milliseconds of the pings and the timeouts of the endpoints that test them
#define SERVER_TEST_PING_INTERVAL			(1 << 8)
#define SERVER_TEST_PONG_TIMEOUT			(1 << 8)
#define SERVER_TEST_CLOSE_TIMEOUT			(1 << 9)
#define SERVER_TEST_SEND_TIMEOUT			(1 << 8)

#define SERVER_TEST_SMALL_MESSAGE			(1 << 10)
#define SERVER_TEST_LARGE_MESSAGE			300000

#define SERVER_TEST_TEXT_SIZE				(1 << 7)
#define SERVER_TEST_CODE_SIZE				(1 << 6)

// the senders of the test of the concurrent sends, and the messages of each one
#define SERVER_TEST_SENDERS					8
#define SERVER_TEST_SENDER_MESSAGES			(1 << 7)

#define SERVER_TEST_ROOM_CLIENTS			4

// a socket buffer that fills fast
#define SERVER_TEST_SMALL_BUFFER			(1 << 12)

// the bytes of each message of a flood
#define SERVER_TEST_FLOOD_SIZE				(1 << 16)

// the most messages that a flood sends before its send fails
#define SERVER_TEST_FLOOD_MESSAGES			(1 << 12)

// the pieces of a streamed message
#define SERVER_TEST_STREAM_PIECES			5

// the codes of the closes that the server sends before the sends that it refuses
#define SERVER_TEST_REFUSED_BEGIN_CODE		4001
#define SERVER_TEST_REFUSED_STREAM_CODE		4002
#define SERVER_TEST_CLOSED_STREAM_CODE		4003

static const char *server_test_secret = { "secret" };
static const char *server_test_origin = { "https://cerver.test" };
static const char *server_test_upgrade_data = { "upgrade-data" };

static const char *server_test_protocols[] = { "chat.v2", "chat" };
#define SERVER_TEST_N_PROTOCOLS				2

static u16 server_test_port = SERVER_TEST_PORT_BASE;

// what the methods of the connections saw, read and written atomically
typedef struct ServerTestCounters {

	unsigned int opened;
	unsigned int closed;
	unsigned int errors;
	unsigned int messages;
	unsigned int floods;
	unsigned int refused;

	unsigned int last_code;
	unsigned int last_error;

} ServerTestCounters;

static ServerTestCounters counters;

typedef struct ServerTest {

	Cerver *cerver;
	HttpCerver *http_cerver;
	pthread_t thread;
	u16 port;

	unsigned int fds;

} ServerTest;

#pragma region handlers

static void server_test_on_open (HttpWebSocket *web_socket) {

	(void) web_socket;

	test_counter_add (&counters.opened);

}

static void server_test_on_close (
	HttpWebSocket *web_socket, u16 code, const char *reason, size_t length
) {

	(void) web_socket;
	(void) reason;
	(void) length;

	__atomic_store_n (&counters.last_code, (unsigned int) code, __ATOMIC_SEQ_CST);
	test_counter_add (&counters.closed);

}

static void server_test_on_error (HttpWebSocket *web_socket, HttpWebSocketError error) {

	(void) web_socket;

	__atomic_store_n (&counters.last_error, (unsigned int) error, __ATOMIC_SEQ_CST);
	test_counter_add (&counters.errors);

}

// sends the pieces of a message in its own frames
static void server_test_stream (HttpWebSocket *web_socket) {

	test_check_unsigned_eq (http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT), 0, NULL);
	for (unsigned int i = 0; i < SERVER_TEST_STREAM_PIECES; i++) {
		test_check_unsigned_eq (http_web_socket_send_write (web_socket, "piece", 5), 0, NULL);
	}

	test_check_unsigned_eq (http_web_socket_send_end (web_socket), 0, NULL);

}

// a stream that can not begin holds nothing: the next send of the thread returns its error,
// it waited forever for the message that the stream never started
static void server_test_refused_begin (HttpWebSocket *web_socket) {

	(void) http_web_socket_close (web_socket, SERVER_TEST_REFUSED_BEGIN_CODE, "asked");

	const bool refused = http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT)
		&& http_web_socket_send_text (web_socket, "late", 4);

	if (refused) test_counter_add (&counters.refused);

}

// the pieces and the end of a stream that could not begin are errors that do not abort its connection
static void server_test_refused_stream (HttpWebSocket *web_socket) {

	(void) http_web_socket_close (web_socket, SERVER_TEST_REFUSED_STREAM_CODE, "asked");

	const bool refused = http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT)
		&& http_web_socket_send_write (web_socket, "piece", 5)
		&& http_web_socket_send_end (web_socket)
		&& http_web_socket_send_text (web_socket, "late", 4);

	if (refused) test_counter_add (&counters.refused);

}

// a stream that is open when its connection is closed: its next pieces and its end are errors,
// its mutex is released by its end and the close of the connection stays clean
static void server_test_closed_stream (HttpWebSocket *web_socket) {

	test_check_unsigned_eq (http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT), 0, NULL);
	test_check_unsigned_eq (http_web_socket_send_write (web_socket, "piece", 5), 0, NULL);

	(void) http_web_socket_close (web_socket, SERVER_TEST_CLOSED_STREAM_CODE, "asked");

	const bool refused = http_web_socket_send_write (web_socket, "piece", 5)
		&& http_web_socket_send_end (web_socket)
		&& http_web_socket_send_text (web_socket, "late", 4);

	if (refused) test_counter_add (&counters.refused);

}

// sends large messages until a send fails, to a client that never reads
static void server_test_flood (HttpWebSocket *web_socket) {

	u8 *data = (u8 *) calloc (1, SERVER_TEST_FLOOD_SIZE);
	test_check_ptr (data);

	unsigned int sent = 0;
	while ((sent < SERVER_TEST_FLOOD_MESSAGES) && !http_web_socket_send_binary (web_socket, data, SERVER_TEST_FLOOD_SIZE)) {
		sent += 1;
	}

	if (sent < SERVER_TEST_FLOOD_MESSAGES) test_counter_add (&counters.floods);

	free (data);

}

// a text that is a command, the rest are echoed
static bool server_test_command (HttpWebSocket *web_socket, const char *text) {

	bool command = true;
	char argument[SERVER_TEST_TEXT_SIZE] = { 0 };

	if (sscanf (text, "join:%63s", argument) == 1) {
		test_check_unsigned_eq (http_web_socket_join (web_socket, argument), 0, NULL);
		(void) http_web_socket_send_text (web_socket, "joined", 6);
	}

	else if (sscanf (text, "leave:%63s", argument) == 1) {
		test_check_unsigned_eq (http_web_socket_leave (web_socket, argument), 0, NULL);
		(void) http_web_socket_send_text (web_socket, "left", 4);
	}

	else if (!strcmp (text, "stream")) server_test_stream (web_socket);
	else if (!strcmp (text, "refused-begin")) server_test_refused_begin (web_socket);
	else if (!strcmp (text, "refused-stream")) server_test_refused_stream (web_socket);
	else if (!strcmp (text, "closed-stream")) server_test_closed_stream (web_socket);
	else if (!strcmp (text, "flood")) server_test_flood (web_socket);
	else if (!strcmp (text, "close")) (void) http_web_socket_close (web_socket, 4001, "asked");
	else command = false;

	return command;

}

static void server_test_on_message (
	HttpWebSocket *web_socket,
	HttpWebSocketOpcode type, const void *data, size_t size
) {

	test_counter_add (&counters.messages);

	char text[SERVER_TEST_TEXT_SIZE] = { 0 };
	const bool short_text = (type == HTTP_WEB_SOCKET_OPCODE_TEXT) && (size < SERVER_TEST_TEXT_SIZE);
	if (short_text) (void) memcpy (text, data, size);

	if (!short_text || !server_test_command (web_socket, text)) {
		(void) http_web_socket_send (web_socket, type, data, size);
	}

}

static const HttpWebSocketHandlers server_test_echo_handlers = {
	NULL, server_test_on_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error
};

// the fragments are counted, and the size of the message is the answer at its end
static size_t server_test_fragment_size = 0;
static unsigned int server_test_fragment_pieces = 0;

static void server_test_on_fragment (
	HttpWebSocket *web_socket,
	HttpWebSocketOpcode type, const void *data, size_t size,
	bool first, bool last
) {

	(void) type;
	(void) data;

	if (first) {
		server_test_fragment_size = 0;
		server_test_fragment_pieces = 0;
	}

	server_test_fragment_size += size;
	if (size) server_test_fragment_pieces += 1;

	if (last) {
		char answer[SERVER_TEST_TEXT_SIZE];
		const int length = snprintf (answer, SERVER_TEST_TEXT_SIZE, "%u:%zu", server_test_fragment_pieces, server_test_fragment_size);
		(void) http_web_socket_send_text (web_socket, answer, (size_t) length);
	}

}

static const HttpWebSocketHandlers server_test_stream_handlers = {
	NULL, server_test_on_open, NULL, server_test_on_fragment,
	NULL, NULL, server_test_on_close, server_test_on_error
};

// rejects the upgrades with X-Reject, the others get their data and the last protocol they offered
static bool server_test_on_upgrade (
	const HttpReceive *http_receive, const HttpRequest *request,
	HttpWebSocketUpgrade *upgrade
) {

	(void) http_receive;

	bool accepted = !http_request_get_custom_header (request, "X-Reject");
	if (accepted) {
		upgrade->data = strdup (server_test_upgrade_data);
		upgrade->delete_data = free;
		upgrade->protocol = server_test_protocols[1];
	}

	else {
		upgrade->status = HTTP_STATUS_CONFLICT;
	}

	return accepted;

}

// sends its data and its protocol
static void server_test_upgrade_open (HttpWebSocket *web_socket) {

	test_counter_add (&counters.opened);

	char text[SERVER_TEST_TEXT_SIZE];
	const int length = snprintf (
		text, SERVER_TEST_TEXT_SIZE, "%s:%s",
		(const char *) http_web_socket_get_data (web_socket),
		http_web_socket_get_protocol (web_socket) ? http_web_socket_get_protocol (web_socket) : "none"
	);

	(void) http_web_socket_send_text (web_socket, text, (size_t) length);

}

static const HttpWebSocketHandlers server_test_upgrade_handlers = {
	server_test_on_upgrade, server_test_upgrade_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error
};

// closes the connection as soon as it opens
static void server_test_closer_open (HttpWebSocket *web_socket) {

	test_counter_add (&counters.opened);
	test_check_unsigned_eq (http_web_socket_close (web_socket, 4000, "bye"), 0, NULL);

}

static const HttpWebSocketHandlers server_test_closer_handlers = {
	NULL, server_test_closer_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error
};

static unsigned int server_test_authenticate (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];

	return (authorization && !strcmp (authorization->str, server_test_secret)) ? 0 : 1;

}

static void server_test_plain_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

#pragma endregion

#pragma region server

static void server_test_routes (HttpCerver *http_cerver) {

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	test_check_ptr (ws);

	// the endpoints without their own handlers take the ones of the group
	http_group_set_web_socket (ws, &server_test_echo_handlers);

	test_check_ptr (http_group_web_socket_add (ws, "echo", NULL));

	HttpEndpoint *plain = http_group_get (ws, "plain", server_test_plain_handler);
	http_endpoint_set_web_socket (plain, &server_test_echo_handlers);

	HttpEndpoint *auth = http_group_web_socket_add (ws, "auth", NULL);
	http_endpoint_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (auth, server_test_authenticate);

	HttpEndpoint *origin = http_group_web_socket_add (ws, "origin", NULL);
	http_endpoint_set_web_socket_origin_check (origin, true);

	HttpEndpoint *protocols = http_group_web_socket_add (ws, "protocols", NULL);
	test_check_unsigned_eq (http_endpoint_set_web_socket_protocols (protocols, server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);

	HttpEndpoint *upgrade = http_group_web_socket_add (ws, "upgrade", &server_test_upgrade_handlers);
	test_check_unsigned_eq (http_endpoint_set_web_socket_protocols (upgrade, server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);

	HttpEndpoint *compressed = http_group_web_socket_add (ws, "compressed", NULL);
	http_endpoint_set_web_socket_compression (compressed, true);

	HttpEndpoint *small = http_group_web_socket_add (ws, "small", NULL);
	http_endpoint_set_web_socket_max_message (small, SERVER_TEST_SMALL_MESSAGE);

	HttpEndpoint *ping = http_group_web_socket_add (ws, "ping", NULL);
	http_endpoint_set_web_socket_ping (ping, SERVER_TEST_PING_INTERVAL, SERVER_TEST_PONG_TIMEOUT);

	HttpEndpoint *slow = http_group_web_socket_add (ws, "slow", NULL);
	http_endpoint_set_web_socket_send_timeout (slow, SERVER_TEST_SEND_TIMEOUT);

	test_check_ptr (http_group_web_socket_add (ws, "stream", &server_test_stream_handlers));
	test_check_ptr (http_group_web_socket_add (ws, "closer", &server_test_closer_handlers));

	// every connection of the cerver waits this for the close frame of its peer
	http_group_set_web_socket_close_timeout (ws, SERVER_TEST_CLOSE_TIMEOUT);

	test_check_unsigned_eq (http_cerver_add_origin_to_whitelist (http_cerver, server_test_origin), 0, NULL);

}

static void *server_test_thread (void *server_ptr) {

	ServerTest *server = (ServerTest *) server_ptr;

	(void) cerver_start (server->cerver);

	return NULL;

}

static ServerTest *server_test_start (const unsigned int max_connections) {

	(void) memset (&counters, 0, sizeof (ServerTestCounters));

	ServerTest *server = (ServerTest *) calloc (1, sizeof (ServerTest));
	test_check_ptr (server);

	server->fds = raw_open_fds ();
	server->port = server_test_port++;

	Cerver *cerver = cerver_create_web ("websocket-test", server->port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);
	cerver_set_thpool_n_threads (cerver, SERVER_TEST_THREADS);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	if (max_connections) http_cerver_set_web_socket_max_connections (http_cerver, max_connections);

	server_test_routes (http_cerver);

	server->cerver = cerver;
	server->http_cerver = http_cerver;

	test_check_int_eq (pthread_create (&server->thread, NULL, server_test_thread, server), 0, NULL);

	// the first connection waits until it listens
	(void) close (raw_connect (server->port));

	return server;

}

static void server_test_stop (ServerTest *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	// every connection and its thread ended with the cerver
	if (raw_open_fds () != server->fds) raw_print_fds ();
	test_check_unsigned_eq (raw_open_fds (), server->fds, NULL);

	free (server);

}

// waits until the server ended the connections it opened
static void server_test_wait_closed (const unsigned int expected) {

	test_check_unsigned_eq (test_wait_counter (&counters.closed, expected, TEST_WAIT_SECONDS), expected, NULL);

}

static unsigned int server_test_status (const u16 port, const char *path, const char *headers) {

	const int fd = raw_connect (port);

	char response[RAW_RESPONSE_SIZE];
	const unsigned int status = (unsigned int) raw_upgrade (fd, path, headers, response);
	(void) close (fd);

	return status;

}

// a frame of the server, it must arrive
static void server_test_expect_frame (
	RawReader *reader, const u8 opcode, const void *data, const size_t size
) {

	RawFrame frame;
	test_check (raw_read_frame (reader, &frame, RAW_TIMEOUT), "the frame did not arrive");
	test_check_int_eq (frame.opcode, opcode, NULL);
	test_check_unsigned_eq (frame.size, size, NULL);
	test_check ((!size || !memcmp (frame.data, data, size)), "the payload is wrong");
	test_check_true (frame.fin);
	raw_frame_delete (&frame);

}

// the close frame of the server, and the end of its connection
static void server_test_expect_close (RawReader *reader, const u16 code) {

	RawFrame frame;
	test_check (raw_read_frame (reader, &frame, RAW_TIMEOUT), "the close frame did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), code, NULL);
	raw_frame_delete (&frame);

	test_check (raw_wait_end (reader, RAW_TIMEOUT), "the server did not end the connection");

}

#pragma endregion

#pragma region tests

// the statuses of the upgrades over TCP
static void test_server_handshake (void) {

	ServerTest *server = server_test_start (0);
	const u16 port = server->port;

	test_check_unsigned_eq (server_test_status (port, "/ws/echo", NULL), 101, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/auth", NULL), 401, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/auth", "Authorization: secret\r\n"), 101, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/origin", "Origin: https://other.test\r\n"), 403, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/origin", NULL), 403, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/origin", "Origin: https://cerver.test\r\n"), 101, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: yes\r\n"), 409, NULL);

	// the protocol of the response is the first one of the server that the client offered
	const int fd = raw_connect (port);
	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (raw_upgrade (fd, "/ws/protocols", "Sec-WebSocket-Protocol: other, chat, chat.v2\r\n", response), 101, NULL);
	test_check (strstr (response, "Sec-WebSocket-Protocol: chat.v2\r\n") != NULL, "the protocol is not the first one of the server");
	(void) close (fd);

	// echo, auth, origin and protocols were accepted
	const HttpWebSocketRegistry *registry = server->http_cerver->web_sockets;
	server_test_wait_closed (4);
	test_check_unsigned_eq (CERVER_STATS_GET (registry->n_rejected[HTTP_WEB_SOCKET_REJECTION_ORIGIN]), 2, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (registry->n_rejected[HTTP_WEB_SOCKET_REJECTION_APPLICATION]), 1, NULL);

	server_test_stop (server);

}

// echoes of every size, around the limits of the encodings of the length
static void test_server_echo (void) {

	static const size_t sizes[] = { 0, 1, 125, 126, 127, 1000, 65535, 65536, SERVER_TEST_LARGE_MESSAGE };

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	u8 *data = (u8 *) malloc (SERVER_TEST_LARGE_MESSAGE);
	test_check_ptr (data);
	for (size_t i = 0; i < SERVER_TEST_LARGE_MESSAGE; i++) data[i] = (u8) ('a' + (i % 26));

	for (size_t i = 0; i < (sizeof (sizes) / sizeof (sizes[0])); i++) {
		raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, data, sizes[i]);
		server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, data, sizes[i]);

		raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_BINARY, data, sizes[i]);
		server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_BINARY, data, sizes[i]);
	}

	raw_send_close (fd, 1000, "done");
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), 1000, NULL);

	raw_reader_end (&reader);
	(void) close (fd);
	free (data);

	server_test_stop (server);

}

// the frames that came with the request, and a frame sent one byte at a time
static void test_server_pipelined (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_connect (server->port);

	char request[RAW_RESPONSE_SIZE];
	size_t length = (size_t) snprintf (request, RAW_RESPONSE_SIZE, RAW_UPGRADE_FORMAT, "/ws/echo", "");
	length += raw_frame ((u8 *) request + length, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "first", 5, true);
	length += raw_frame ((u8 *) request + length, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "second", 6, true);
	raw_send (fd, request, length);

	RawReader reader;
	raw_reader_init (&reader, fd);

	// the response is read by the reader too, as the first bytes are not a frame
	char response[RAW_RESPONSE_SIZE] = { 0 };
	size_t received = 0;
	while (!strstr (response, "\r\n\r\n") && (received < (RAW_RESPONSE_SIZE - 1))) {
		test_check (recv (fd, response + received, 1, 0) == 1, "the response did not arrive");
		received += 1;
	}

	test_check (!strncmp (response, "HTTP/1.1 101", 12), "the upgrade was not accepted");
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "first", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "second", 6);

	const int enabled = 1;
	(void) setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (int));

	u8 frame[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE + SERVER_TEST_TEXT_SIZE];
	const size_t size = raw_frame (frame, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "byte by byte", 12, true);
	for (size_t i = 0; i < size; i++) raw_send (fd, frame + i, 1);

	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "byte by byte", 12);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

	server_test_stop (server);

}

// a fragmented message with a ping between its fragments, the pong arrives first
static void test_server_fragments (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "Hel", 3);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PING, "ping", 4);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "lo", 2);

	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_PONG, "ping", 4);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "Hello", 5);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

	server_test_stop (server);

}

// the server closes, the client answers, the server ends the TCP connection
static void test_server_close_server (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/closer", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	RawFrame frame;
	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the close did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), 4000, NULL);
	test_check ((frame.size == 5) && !memcmp (frame.data + 2, "bye", 3), "the reason is wrong");
	raw_frame_delete (&frame);

	raw_send_close (fd, 4000, NULL);
	test_check (raw_wait_end (&reader, RAW_TIMEOUT), "the server did not end the connection");
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), 4000, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), 0, NULL);

	// a client that never answers ends with the close timeout of the server
	const int silent = raw_open (server->port, "/ws/closer", NULL);
	RawReader silent_reader;
	raw_reader_init (&silent_reader, silent);
	test_check (raw_wait_end (&silent_reader, RAW_TIMEOUT), "the close timeout did not end the connection");
	server_test_wait_closed (2);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_CLOSE_TIMEOUT, NULL);

	raw_reader_end (&reader);
	raw_reader_end (&silent_reader);
	(void) close (fd);
	(void) close (silent);

	server_test_stop (server);

}

// a broken frame is answered with its close code and the end of the connection
static void server_test_protocol_error (
	const u16 port, const char *path, const u8 *bytes, const size_t size, const u16 code
) {

	const int fd = raw_open (port, path, NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send (fd, bytes, size);
	server_test_expect_close (&reader, code);

	raw_reader_end (&reader);
	(void) close (fd);

}

static void test_server_protocol_errors (void) {

	ServerTest *server = server_test_start (0);
	const u16 port = server->port;

	u8 buffer[SERVER_TEST_SMALL_MESSAGE * 2];
	size_t size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "unmasked", 8, false);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002);

	size = raw_frame (buffer, true, false, 0x3, "reserved", 8, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "\xC0\xAF", 2, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1007);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "alone", 5, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xED", 2, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002);

	size = raw_frame (buffer, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "a", 1, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002);

	u8 large[SERVER_TEST_SMALL_MESSAGE + 1];
	(void) memset (large, 'x', sizeof (large));
	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, large, sizeof (large), true);
	server_test_protocol_error (port, "/ws/small", buffer, size, 1009);

	server_test_wait_closed (7);
	test_check_unsigned_eq (test_wait_counter (&counters.errors, 7, TEST_WAIT_SECONDS), 7, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_PROTOCOL, NULL);

	server_test_stop (server);

}

// a client that leaves without a close: a reset, a close of the socket and in the middle of a frame
static void test_server_abrupt (void) {

	ServerTest *server = server_test_start (0);

	int fd = raw_open (server->port, "/ws/echo", NULL);
	raw_reset (fd);
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL);

	fd = raw_open (server->port, "/ws/echo", NULL);
	(void) close (fd);
	server_test_wait_closed (2);

	fd = raw_open (server->port, "/ws/echo", NULL);
	u8 buffer[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE + SERVER_TEST_TEXT_SIZE];
	const size_t size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "never ends", 10, true);
	raw_send (fd, buffer, size / 2);
	(void) shutdown (fd, SHUT_WR);
	server_test_wait_closed (3);
	(void) close (fd);

	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_ABNORMAL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.messages, __ATOMIC_SEQ_CST), 0, NULL);

	server_test_stop (server);

}

// the pings of the server: answered, the connection lives, and not answered, it ends
static void test_server_ping (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/ping", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	for (unsigned int i = 0; i < 3; i++) {
		RawFrame frame;
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the ping did not arrive");
		test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_PING, NULL);
		raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PONG, frame.data, frame.size);
		raw_frame_delete (&frame);
	}

	test_check_unsigned_eq (__atomic_load_n (&counters.closed, __ATOMIC_SEQ_CST), 0, NULL);

	// the pings that are not answered end the connection
	test_check (raw_wait_end (&reader, RAW_TIMEOUT), "the connection was not ended without its pongs");
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_PONG_TIMEOUT, NULL);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

typedef struct ServerTestSender {

	HttpWebSocket *web_socket;
	unsigned int id;

} ServerTestSender;

static void *server_test_sender (void *sender_ptr) {

	const ServerTestSender *sender = (const ServerTestSender *) sender_ptr;

	char text[SERVER_TEST_TEXT_SIZE];
	for (unsigned int i = 0; i < SERVER_TEST_SENDER_MESSAGES; i++) {
		// the size of each message changes, so a frame mixed with another one is noticed
		const int length = snprintf (
			text, SERVER_TEST_TEXT_SIZE, "%u:%u:%.*s",
			sender->id, i, (int) (i % 64), "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
		);

		test_check_unsigned_eq (http_web_socket_send_text (sender->web_socket, text, (size_t) length), 0, NULL);
	}

	return NULL;

}

static void server_test_take (HttpWebSocket *web_socket, void *args) {

	*((HttpWebSocket **) args) = http_web_socket_retain (web_socket);

}

// many threads send to the same connection, every message arrives whole and in the order of its thread
static void test_server_concurrent_sends (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.opened, 1, TEST_WAIT_SECONDS), 1, NULL);

	HttpWebSocket *web_socket = NULL;
	http_cerver_web_socket_for_each (server->http_cerver, NULL, server_test_take, &web_socket);
	test_check_ptr (web_socket);

	pthread_t threads[SERVER_TEST_SENDERS];
	ServerTestSender senders[SERVER_TEST_SENDERS];
	for (unsigned int i = 0; i < SERVER_TEST_SENDERS; i++) {
		senders[i].web_socket = web_socket;
		senders[i].id = i;
		test_check_int_eq (pthread_create (&threads[i], NULL, server_test_sender, &senders[i]), 0, NULL);
	}

	RawReader reader;
	raw_reader_init (&reader, fd);

	unsigned int next[SERVER_TEST_SENDERS] = { 0 };
	for (unsigned int n = 0; n < (SERVER_TEST_SENDERS * SERVER_TEST_SENDER_MESSAGES); n++) {
		RawFrame frame;
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "a message did not arrive");
		test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);

		char text[SERVER_TEST_TEXT_SIZE] = { 0 };
		(void) memcpy (text, frame.data, frame.size);
		unsigned int id = 0;
		unsigned int sequence = 0;
		test_check_int_eq (sscanf (text, "%u:%u:", &id, &sequence), 2, NULL);
		test_check (id < SERVER_TEST_SENDERS, "the message is broken");
		test_check_unsigned_eq (sequence, next[id], NULL);
		test_check_unsigned_eq (strlen (strrchr (text, ':') + 1), sequence % 64, NULL);
		next[id] += 1;

		raw_frame_delete (&frame);
	}

	for (unsigned int i = 0; i < SERVER_TEST_SENDERS; i++) {
		test_check_int_eq (pthread_join (threads[i], NULL), 0, NULL);
	}

	raw_send_close (fd, 1000, NULL);
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);

	// a send of a connection that ended fails without touching its socket
	test_check_unsigned_eq (http_web_socket_send_text (web_socket, "late", 4), 1, NULL);
	test_check_int_eq (http_web_socket_get_state (web_socket), HTTP_WEB_SOCKET_STATE_CLOSED, NULL);
	http_web_socket_release (web_socket);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// the rooms and the broadcasts
static void test_server_rooms (void) {

	ServerTest *server = server_test_start (0);
	HttpCerver *http_cerver = server->http_cerver;

	int fds[SERVER_TEST_ROOM_CLIENTS];
	RawReader readers[SERVER_TEST_ROOM_CLIENTS];
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		fds[i] = raw_open (server->port, "/ws/echo", NULL);
		raw_reader_init (&readers[i], fds[i]);

		const char *join = (i < 3) ? "join:red" : "join:blue";
		raw_send_frame (fds[i], true, HTTP_WEB_SOCKET_OPCODE_TEXT, join, strlen (join));
		server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "joined", 6);
	}

	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, NULL), 4, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, "red"), 3, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, "blue"), 1, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, "green"), 0, NULL);

	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, "red", NULL, HTTP_WEB_SOCKET_OPCODE_TEXT, "red", 3), 3, NULL);
	for (unsigned int i = 0; i < 3; i++) server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "red", 3);

	RawFrame frame;
	test_check_false (raw_read_frame (&readers[3], &frame, SERVER_TEST_PING_INTERVAL));

	raw_send_frame (fds[1], true, HTTP_WEB_SOCKET_OPCODE_TEXT, "leave:red", 9);
	server_test_expect_frame (&readers[1], HTTP_WEB_SOCKET_OPCODE_TEXT, "left", 4);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, "red"), 2, NULL);

	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, NULL, NULL, HTTP_WEB_SOCKET_OPCODE_BINARY, "all", 3), 4, NULL);
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_BINARY, "all", 3);
	}

	// every connection is of the echo endpoint
	HttpWebSocket *any = NULL;
	http_cerver_web_socket_for_each (http_cerver, "blue", server_test_take, &any);
	test_check_ptr (any);
	const HttpEndpoint *endpoint = http_web_socket_get_endpoint (any);
	http_web_socket_release (any);

	test_check_unsigned_eq (http_endpoint_web_socket_broadcast (http_cerver, endpoint, HTTP_WEB_SOCKET_OPCODE_TEXT, "echo", 4), 4, NULL);
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "echo", 4);
	}

	// the sender is left out
	HttpWebSocket *sender = NULL;
	http_cerver_web_socket_for_each (http_cerver, "blue", server_test_take, &sender);
	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, NULL, sender, HTTP_WEB_SOCKET_OPCODE_TEXT, "others", 6), 3, NULL);
	http_web_socket_release (sender);
	for (unsigned int i = 0; i < 3; i++) server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "others", 6);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		raw_reader_end (&readers[i]);
		(void) close (fds[i]);
	}

	server_test_wait_closed (SERVER_TEST_ROOM_CLIENTS);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, NULL), 0, NULL);
	test_check_unsigned_eq (http_endpoint_web_socket_broadcast (http_cerver, endpoint, HTTP_WEB_SOCKET_OPCODE_TEXT, "none", 4), 0, NULL);

	server_test_stop (server);

}

// a client that never reads: the sends of its connection end with their deadline,
// and another connection is not affected
static void test_server_slow_reader (void) {

	ServerTest *server = server_test_start (0);

	const int slow = raw_connect (server->port);
	const int size = SERVER_TEST_SMALL_BUFFER;
	(void) setsockopt (slow, SOL_SOCKET, SO_RCVBUF, &size, sizeof (int));

	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (raw_upgrade (slow, "/ws/slow", NULL, response), 101, NULL);

	raw_send_frame (slow, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "flood", 5);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "alive", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "alive", 5);

	test_check_unsigned_eq (test_wait_counter (&counters.floods, 1, TEST_WAIT_SECONDS), 1, NULL);
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_SEND, NULL);
	test_check (CERVER_STATS_GET (server->http_cerver->web_sockets->n_slow) >= 1, "the slow send was not counted");

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "still", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "still", 5);

	raw_reader_end (&reader);
	(void) close (fd);
	(void) close (slow);
	server_test_wait_closed (2);

	server_test_stop (server);

}

// the most connections of the cerver, the place of one that ends is taken again
static void test_server_limit (void) {

	ServerTest *server = server_test_start (2);

	const int first = raw_open (server->port, "/ws/echo", NULL);
	const int second = raw_open (server->port, "/ws/echo", NULL);
	test_check_unsigned_eq (server_test_status (server->port, "/ws/echo", NULL), 503, NULL);

	RawReader reader;
	raw_reader_init (&reader, first);
	raw_send_close (first, 1000, NULL);
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);
	raw_reader_end (&reader);
	(void) close (first);

	const int third = raw_open (server->port, "/ws/echo", NULL);

	(void) close (second);
	(void) close (third);
	server_test_wait_closed (3);

	// the connections left the registry before their on_close
	HttpWebSocketRegistry *registry = server->http_cerver->web_sockets;
	(void) pthread_mutex_lock (&registry->mutex);
	const unsigned int reserved = registry->reserved;
	(void) pthread_mutex_unlock (&registry->mutex);
	test_check_unsigned_eq (reserved, 0, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (registry->n_rejected[HTTP_WEB_SOCKET_REJECTION_LIMIT]), 1, NULL);

	server_test_stop (server);

}

typedef struct ServerTestStop {

	ServerTest *server;

} ServerTestStop;

// the teardown alone, the fds are checked after the clients close theirs
static void *server_test_stopper (void *stop_ptr) {

	ServerTest *server = ((ServerTestStop *) stop_ptr)->server;

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	return NULL;

}

// the teardown closes the connections with 1001, they answer and the cerver ends
static void test_server_shutdown (void) {

	ServerTest *server = server_test_start (0);

	int fds[SERVER_TEST_ROOM_CLIENTS];
	RawReader readers[SERVER_TEST_ROOM_CLIENTS];
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		fds[i] = raw_open (server->port, "/ws/echo", NULL);
		raw_reader_init (&readers[i], fds[i]);
	}

	test_check_unsigned_eq (test_wait_counter (&counters.opened, SERVER_TEST_ROOM_CLIENTS, TEST_WAIT_SECONDS), SERVER_TEST_ROOM_CLIENTS, NULL);

	ServerTestStop stop = { server };
	pthread_t stopper;
	test_check_int_eq (pthread_create (&stopper, NULL, server_test_stopper, &stop), 0, NULL);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		RawFrame frame;
		test_check (raw_read_frame (&readers[i], &frame, RAW_TIMEOUT), "the going away did not arrive");
		test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_GOING_AWAY, NULL);
		raw_frame_delete (&frame);

		raw_send_close (fds[i], HTTP_WEB_SOCKET_CLOSE_GOING_AWAY, NULL);
		test_check (raw_wait_end (&readers[i], RAW_TIMEOUT), "the connection did not end");
	}

	test_check_int_eq (pthread_join (stopper, NULL), 0, NULL);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		raw_reader_end (&readers[i]);
		(void) close (fds[i]);
	}

	test_check_unsigned_eq (__atomic_load_n (&counters.closed, __ATOMIC_SEQ_CST), SERVER_TEST_ROOM_CLIENTS, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_GOING_AWAY, NULL);

	test_check_unsigned_eq (raw_open_fds (), server->fds, NULL);
	free (server);

}

// a message of the server that is sent in pieces, and one of the client that is received in pieces
static void test_server_streaming (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "stream", 6);

	RawFrame frame;
	for (unsigned int i = 0; i < SERVER_TEST_STREAM_PIECES; i++) {
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "a piece did not arrive");
		test_check_int_eq (frame.opcode, i ? HTTP_WEB_SOCKET_OPCODE_CONTINUATION : HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
		test_check_false (frame.fin);
		test_check ((frame.size == 5) && !memcmp (frame.data, "piece", 5), "the piece is wrong");
		raw_frame_delete (&frame);
	}

	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the end did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, NULL);
	test_check_true (frame.fin);
	test_check_unsigned_eq (frame.size, 0, NULL);
	raw_frame_delete (&frame);

	raw_reader_end (&reader);
	(void) close (fd);

	const int stream = raw_open (server->port, "/ws/stream", NULL);
	raw_reader_init (&reader, stream);

	raw_send_frame (stream, false, HTTP_WEB_SOCKET_OPCODE_BINARY, "abc", 3);
	raw_send_frame (stream, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "defg", 4);
	raw_send_frame (stream, true, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "h", 1);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "3:8", 3);

	raw_reader_end (&reader);
	(void) close (stream);
	server_test_wait_closed (2);

	server_test_stop (server);

}

// the client asks for sends after the close of the server, answers the close
// and the connection ends clean: with the code of the server and without errors
static void server_test_refused_case (
	const ServerTest *server, const char *command, const u16 code,
	const bool stream_started, const unsigned int n_closed
) {

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, command, strlen (command));

	RawFrame frame;
	if (stream_started) {
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the piece of the stream did not arrive");
		test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
		test_check_false (frame.fin);
		raw_frame_delete (&frame);
	}

	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the close did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), code, NULL);
	raw_frame_delete (&frame);

	raw_send_close (fd, code, NULL);
	test_check (raw_wait_end (&reader, RAW_TIMEOUT), "the server did not end the connection");
	server_test_wait_closed (n_closed);

	test_check_unsigned_eq (__atomic_load_n (&counters.refused, __ATOMIC_SEQ_CST), n_closed, command);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), code, command);
	test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), 0, command);

	raw_reader_end (&reader);
	(void) close (fd);

}

// the sends of a connection that already sent its close are errors that hold nothing and abort nothing:
// a stream that could not begin kept the mutex of the messages, so the next send of its thread never returned,
// and the pieces of a stream after the close aborted a connection that was closing clean
static void test_server_refused_sends (void) {

	ServerTest *server = server_test_start (0);

	server_test_refused_case (server, "refused-begin", SERVER_TEST_REFUSED_BEGIN_CODE, false, 1);
	server_test_refused_case (server, "refused-stream", SERVER_TEST_REFUSED_STREAM_CODE, false, 2);
	server_test_refused_case (server, "closed-stream", SERVER_TEST_CLOSED_STREAM_CODE, true, 3);

	server_test_stop (server);

}

// on_upgrade gives the data and the protocol of the connection
static void test_server_upgrade_data (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/upgrade", "Sec-WebSocket-Protocol: chat.v2, chat\r\n");
	RawReader reader;
	raw_reader_init (&reader, fd);

	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "upgrade-data:chat", 17);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

	server_test_stop (server);

}

// the decompressed bytes of a message of the server
typedef struct ServerTestSink {

	char data[SERVER_TEST_SMALL_MESSAGE * 2];
	size_t size;

} ServerTestSink;

static int server_test_sink (void *sink_ptr, const u8 *bytes, size_t size) {

	ServerTestSink *sink = (ServerTestSink *) sink_ptr;

	test_check ((sink->size + size) <= sizeof (sink->data), "the message is too big");
	(void) memcpy (sink->data + sink->size, bytes, size);
	sink->size += size;

	return 0;

}

// the messages of a connection with permessage-deflate, compressed both ways
static void test_server_compression (void) {

	if (!http_web_socket_deflate_available ()) return;

	ServerTest *server = server_test_start (0);

	const int fd = raw_connect (server->port);
	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (
		raw_upgrade (fd, "/ws/compressed", "Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n", response), 101, NULL
	);

	test_check (strstr (response, "Sec-WebSocket-Extensions: permessage-deflate\r\n") != NULL, "the extension was not accepted");

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	HttpWebSocketDeflate *deflate = http_web_socket_deflate_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters, &options);
	test_check_ptr (deflate);

	// the server sets RSV1 in its compressed messages
	RawReader reader;
	raw_reader_init (&reader, fd);
	reader.parser.allow_rsv1 = true;

	char message[SERVER_TEST_SMALL_MESSAGE];
	for (size_t i = 0; i < sizeof (message); i++) message[i] = (char) ('a' + (i % 7));

	for (unsigned int n = 0; n < 3; n++) {
		const u8 *compressed = NULL;
		size_t compressed_size = 0;
		test_check_unsigned_eq (http_web_socket_deflate_compress (deflate, message, sizeof (message), true, &compressed, &compressed_size), 0, NULL);

		u8 frame[SERVER_TEST_SMALL_MESSAGE * 2];
		const size_t size = raw_frame (frame, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT, compressed, compressed_size, true);
		raw_send (fd, frame, size);

		RawFrame echo;
		test_check (raw_read_frame (&reader, &echo, RAW_TIMEOUT), "the echo did not arrive");
		test_check_true (echo.rsv1);
		test_check (echo.size < sizeof (message), "the echo was not compressed");

		ServerTestSink sink;
		sink.size = 0;
		test_check_unsigned_eq (http_web_socket_deflate_decompress (deflate, echo.data, echo.size, true, server_test_sink, &sink), 0, NULL);
		test_check_unsigned_eq (sink.size, sizeof (message), NULL);
		test_check (!memcmp (sink.data, message, sizeof (message)), "the decompressed echo is wrong");
		raw_frame_delete (&echo);
	}

	// a message smaller than the minimum is sent without compression
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "tiny", 4);
	RawFrame tiny;
	test_check (raw_read_frame (&reader, &tiny, RAW_TIMEOUT), "the tiny echo did not arrive");
	test_check_false (tiny.rsv1);
	test_check ((tiny.size == 4) && !memcmp (tiny.data, "tiny", 4), "the tiny echo is wrong");
	raw_frame_delete (&tiny);

	http_web_socket_deflate_delete (deflate);
	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

	server_test_stop (server);

}

// the metrics and the admin stats of the connections
static void test_server_metrics (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "metrics", 7);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "metrics", 7);

	String *metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check_ptr (metrics);
	test_check (strstr (metrics->str, "cerver_websocket_connections 1\n") != NULL, "the connections are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_opened_total 1\n") != NULL, "the opened connections are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_messages_received_total 1\n") != NULL, "the messages are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_rejected_total{reason=\"Limit\"} 0\n") != NULL, "the rejections are missing");
	str_delete (metrics);

	// the bytes of a message that is sent in pieces are counted: 7 of the echo and 5 pieces of 5,
	// they were counted as a message of 0 bytes
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "stream", 6);

	RawFrame frame;
	for (unsigned int i = 0; i <= SERVER_TEST_STREAM_PIECES; i++) {
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "a frame of the stream did not arrive");
		raw_frame_delete (&frame);
	}

	metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check (strstr (metrics->str, "cerver_websocket_messages_sent_total 2\n") != NULL, "the sent messages are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_sent_bytes_total 32\n") != NULL, "the sent bytes are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_received_bytes_total 13\n") != NULL, "the received bytes are wrong");
	str_delete (metrics);

	raw_send_close (fd, 1000, NULL);
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);

	metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check (strstr (metrics->str, "cerver_websocket_closes_total{code=\"1000\"} 1\n") != NULL, "the close was not counted");
	test_check (strstr (metrics->str, "cerver_websocket_connections 0\n") != NULL, "the connection was not removed");
	str_delete (metrics);

	raw_reader_end (&reader);
	(void) close (fd);

	// the bytes of every fragment of a message that is received in pieces are counted: 3, 4 and 1,
	// only the ones of its last fragment were
	const int stream = raw_open (server->port, "/ws/stream", NULL);
	raw_reader_init (&reader, stream);

	raw_send_frame (stream, false, HTTP_WEB_SOCKET_OPCODE_BINARY, "abc", 3);
	raw_send_frame (stream, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "defg", 4);
	raw_send_frame (stream, true, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "h", 1);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "3:8", 3);

	metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check (strstr (metrics->str, "cerver_websocket_messages_received_total 3\n") != NULL, "the fragmented message was not counted");
	test_check (strstr (metrics->str, "cerver_websocket_received_bytes_total 21\n") != NULL, "the bytes of the fragments are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_sent_bytes_total 35\n") != NULL, "the bytes of the answer are wrong");
	str_delete (metrics);

	raw_reader_end (&reader);
	(void) close (stream);
	server_test_wait_closed (2);

	server_test_stop (server);

}

#pragma endregion

void web_socket_tests_server (void) {

	(void) printf ("Testing WebSocket server...\n");

	(void) alarm (SERVER_TEST_ALARM);

	// the handler keeps a fd for EMFILE from its first cerver until the process ends
	ServerTest *warm = server_test_start (0);
	test_check_unsigned_eq (cerver_shutdown (warm->cerver), 0, NULL);
	test_check_int_eq (pthread_join (warm->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (warm->cerver), 0, NULL);
	free (warm);

	test_run (test_server_handshake);
	test_run (test_server_echo);
	test_run (test_server_pipelined);
	test_run (test_server_fragments);
	test_run (test_server_close_server);
	test_run (test_server_protocol_errors);
	test_run (test_server_abrupt);
	test_run (test_server_ping);
	test_run (test_server_concurrent_sends);
	test_run (test_server_rooms);
	test_run (test_server_slow_reader);
	test_run (test_server_limit);
	test_run (test_server_shutdown);
	test_run (test_server_streaming);
	test_run (test_server_refused_sends);
	test_run (test_server_upgrade_data);
	test_run (test_server_compression);
	test_run (test_server_metrics);

	(void) alarm (0);

	(void) printf ("Done!\n");

}
