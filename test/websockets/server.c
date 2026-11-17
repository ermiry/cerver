#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include "cerver/events/internal.h"

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/frame.h>
#include "cerver/http/websockets/internal.h"
#include <cerver/http/websockets/registry.h>
#include <cerver/http/websockets/websocket.h>

#include <cerver/threads/thread.h>

#include "../handler.h"
#include "../test.h"
#include "raw.h"
#include "websocket.h"

// the ports of the servers, after the ones of the HTTP suites
#define SERVER_TEST_PORT_BASE				(11264)

#define SERVER_TEST_THREADS					4

// a guard for a test that hangs
#define SERVER_TEST_ALARM					(1 << 9)

// milliseconds of the pings and the timeouts of the endpoints that test them,
// with the scale of the run: under valgrind or a sanitizer a pong does not make it in 256 ms
#define SERVER_TEST_PING_INTERVAL			(test_milliseconds (1 << 8))
#define SERVER_TEST_PONG_TIMEOUT			(test_milliseconds (1 << 8))
#define SERVER_TEST_CLOSE_TIMEOUT			(test_milliseconds (1 << 9))
#define SERVER_TEST_SEND_TIMEOUT			(test_milliseconds (1 << 8))

#define SERVER_TEST_SMALL_MESSAGE			(1 << 10)
#define SERVER_TEST_LARGE_MESSAGE			300000

#define SERVER_TEST_TEXT_SIZE				(1 << 7)
#define SERVER_TEST_CODE_SIZE				(1 << 6)

// the senders of the test of the concurrent sends, and the messages of each one
#define SERVER_TEST_SENDERS					8
#define SERVER_TEST_SENDER_MESSAGES			(1 << 7)

#define SERVER_TEST_ROOM_CLIENTS			4

// A14: more rooms than the 16 of before, and a name longer than their 63 bytes
#define SERVER_TEST_MANY_ROOMS				(1 << 6)
#define SERVER_TEST_LONG_ROOM_SIZE			(1 << 8)

// a value out of the table of an enum
#define SERVER_TEST_UNDEFINED_VALUE			(1 << 10)

// A5: the queue of the drop and strict endpoints, and the messages that fill it
#define SERVER_TEST_SMALL_QUEUE				(1 << 16)
#define SERVER_TEST_FILL_SIZE				(1 << 12)
#define SERVER_TEST_FILL_MESSAGES			(1 << 14)
#define SERVER_TEST_LARGE_BUFFER			(1 << 20)

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

// the decoded data of the requests that the authentication accepts, and what a connection without them sends
static const char *server_test_grants = { "grants" };
static const char *server_test_no_grants = { "none" };

// a protocol that the endpoints do not have
static const char *server_test_other_protocol = { "unknown" };

// the payloads of the control frames that the methods of the connection get
static const char *server_test_ping_data = { "ping-data" };
static const char *server_test_pong_data = { "pong-data" };

// the settings of the groups test: the compression of its group and of its endpoint,
// the limit of its subgroup and the close timeout of its endpoint
#define SERVER_TEST_SETTINGS_LEVEL			1
#define SERVER_TEST_SETTINGS_MIN_SIZE		16
#define SERVER_TEST_SETTINGS_OWN_LEVEL		9
#define SERVER_TEST_SETTINGS_MESSAGE		(1 << 12)
#define SERVER_TEST_SETTINGS_CLOSE_TIMEOUT	(1 << 7)
#define SERVER_TEST_SETTINGS_QUEUE			(1 << 16)

// F6b: a message larger than a socket takes and than the default queue, sent while a method is blocked
#define SERVER_TEST_BLOCKED_SIZE			(1 << 22)

// the time that a message waits for the one that is blocked, 2^27 ns
#define SERVER_TEST_SERIAL_NANOSECONDS		(1L << 27)

// milliseconds that the connections of the patient endpoint wait for the close of their peers,
// and the secs of the drain of the cerver that cuts them before
#define SERVER_TEST_PATIENT_CLOSE_TIMEOUT	(1 << 15)
#define SERVER_TEST_DRAIN_TIMEOUT			1

// the endpoints of the settings test, of the cerver that was started last
static const HttpEndpoint *server_test_inherited = NULL;
static const HttpEndpoint *server_test_own = NULL;

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
	unsigned int foreign;

	// the data of the upgrades that were rejected after they set it
	unsigned int deleted;

	// the control frames that on_ping () and on_pong () were given
	unsigned int pings;
	unsigned int pongs;

	unsigned int last_code;
	unsigned int last_error;

	// the methods that are blocked until the test lets them go
	unsigned int blocked;

	// the rooms that the connections were in when their on_close () ran
	unsigned int closed_rooms;

	// the connections that were already registered in their on_open ()
	unsigned int open_registered;

	// the result of the join of an on_open () (one more than it), and the count of its room then
	unsigned int open_join;
	unsigned int open_room_count;

	// A5: the results of the sends of the last fill, and the on_drain () that ran
	unsigned int fill_queued;
	unsigned int fill_backpressure;
	unsigned int fill_dropped;
	unsigned int fill_closed;
	unsigned int drains;

	// the fills whose method saw its queue empty before it returned
	unsigned int fill_emptied;

} ServerTestCounters;

static ServerTestCounters counters;

// the blocked methods go on
static unsigned int server_test_released = 0;

// the connections join a room in their on_open ()
static unsigned int server_test_join_on_open = 0;
#define SERVER_TEST_OPEN_ROOM				"opened"

// what the send of the method that was blocked returned, one more than the result once it sent
static unsigned int server_test_unblocked = 0;

typedef struct ServerTest {

	Cerver *cerver;
	HttpCerver *http_cerver;
	pthread_t thread;
	u16 port;

	unsigned int fds;

} ServerTest;

#pragma region handlers

static void server_test_on_open (HttpWebSocket *web_socket) {

	// S7: the connection is registered after its on_open (), the broadcasts and the for each do not reach it yet
	HttpWebSocket *registered = http_cerver_web_socket_get_with_id (
		http_web_socket_get_http_cerver (web_socket), http_web_socket_get_id (web_socket)
	);

	if (registered) {
		test_counter_add (&counters.open_registered);
		http_web_socket_release (registered);
	}

	// it joins a room in its on_open (), the counts see it once it is registered
	if (__atomic_load_n (&server_test_join_on_open, __ATOMIC_SEQ_CST)) {
		const HttpWebSocketJoin join = http_web_socket_join (web_socket, SERVER_TEST_OPEN_ROOM);
		__atomic_store_n (&counters.open_join, (unsigned int) join + 1, __ATOMIC_SEQ_CST);

		const HttpWebSocketTarget room = { SERVER_TEST_OPEN_ROOM, NULL, NULL };
		__atomic_store_n (
			&counters.open_room_count, http_cerver_web_socket_count (http_web_socket_get_http_cerver (web_socket), &room),
			__ATOMIC_SEQ_CST
		);
	}

	test_counter_add (&counters.opened);

}

static void server_test_on_close (
	HttpWebSocket *web_socket, u16 code, const char *reason, size_t length
) {

	(void) reason;
	(void) length;

	// A14: the rooms are kept while on_close () runs
	unsigned int n_rooms = 0;
	char **rooms = http_web_socket_get_rooms (web_socket, &n_rooms);
	free (rooms);
	(void) __atomic_add_fetch (&counters.closed_rooms, n_rooms, __ATOMIC_SEQ_CST);

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

// a thread that did not begin the stream tries its pieces and its end
static void *server_test_foreign_thread (void *web_socket_ptr) {

	HttpWebSocket *web_socket = (HttpWebSocket *) web_socket_ptr;

	const bool refused = http_web_socket_send_write (web_socket, "foreign", 7)
		&& http_web_socket_send_end (web_socket);

	if (refused) test_counter_add (&counters.foreign);

	return NULL;

}

// the stream belongs to the thread that began it: the pieces and the end of another thread are errors and the
// stream goes on (they were sent and the end unlocked the mutex of the owner)
static void server_test_foreign_stream (HttpWebSocket *web_socket) {

	test_check_unsigned_eq (http_web_socket_send_begin (web_socket, HTTP_WEB_SOCKET_OPCODE_TEXT), 0, NULL);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, server_test_foreign_thread, web_socket), 0, NULL);
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	test_check_unsigned_eq (http_web_socket_send_write (web_socket, "piece", 5), 0, NULL);
	test_check_unsigned_eq (http_web_socket_send_end (web_socket), 0, NULL);

}

// sends large messages until the queue does not take one, to a client that never reads
static void server_test_flood (HttpWebSocket *web_socket) {

	u8 *data = (u8 *) calloc (1, SERVER_TEST_FLOOD_SIZE);
	test_check_ptr (data);

	unsigned int sent = 0;
	while (
		(sent < SERVER_TEST_FLOOD_MESSAGES)
		&& http_web_socket_send_result_is_taken (http_web_socket_send_binary (web_socket, data, SERVER_TEST_FLOOD_SIZE))
	) {
		sent += 1;
	}

	if (sent < SERVER_TEST_FLOOD_MESSAGES) test_counter_add (&counters.floods);

	free (data);

}

static void server_test_fill_count (const HttpWebSocketSendResult result) {

	switch (result) {
		case HTTP_WEB_SOCKET_SEND_QUEUED: test_counter_add (&counters.fill_queued); break;
		case HTTP_WEB_SOCKET_SEND_BACKPRESSURE: test_counter_add (&counters.fill_backpressure); break;
		case HTTP_WEB_SOCKET_SEND_DROPPED: test_counter_add (&counters.fill_dropped); break;
		default: test_counter_add (&counters.fill_closed); break;
	}

}

// A5: sends messages to a client that does not read until one is not taken, and counts what each send returned
static void server_test_fill (HttpWebSocket *web_socket) {

	u8 *data = (u8 *) calloc (1, SERVER_TEST_FILL_SIZE);
	test_check_ptr (data);

	HttpWebSocketSendResult result = HTTP_WEB_SOCKET_SEND_QUEUED;
	for (unsigned int sent = 0; (sent < SERVER_TEST_FILL_MESSAGES) && http_web_socket_send_result_is_taken (result); sent++) {
		result = http_web_socket_send_binary (web_socket, data, SERVER_TEST_FILL_SIZE);
		server_test_fill_count (result);
	}

	// a control frame never takes the policy, while the policy keeps the connection open
	if ((result == HTTP_WEB_SOCKET_SEND_DROPPED) && (http_web_socket_get_state (web_socket) == HTTP_WEB_SOCKET_STATE_OPEN)) {
		test_check_unsigned_eq (http_web_socket_ping (web_socket, "full", 4), 0, NULL);
		test_check (http_web_socket_get_buffered_size (web_socket) > 0, "the queue was empty");
	}

	test_counter_add (&counters.floods);

	free (data);

}

// a fill whose method waits until its client read the queue: in EVENTS the loop empties it while the job of the
// connection still runs
static void server_test_fill_wait (HttpWebSocket *web_socket) {

	server_test_fill (web_socket);

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	while (http_web_socket_get_buffered_size (web_socket) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	if (!http_web_socket_get_buffered_size (web_socket)) test_counter_add (&counters.fill_emptied);

}

// a method that waits until the test lets it go
static void server_test_block (HttpWebSocket *web_socket) {

	test_counter_add (&counters.blocked);

	(void) test_wait_counter (&server_test_released, 1, TEST_WAIT_SECONDS);

	const HttpWebSocketSendResult result = http_web_socket_send_text (web_socket, "unblocked", 9);
	__atomic_store_n (&server_test_unblocked, (unsigned int) result + 1, __ATOMIC_SEQ_CST);

}

// a text that is a command, the rest are echoed
static bool server_test_command (HttpWebSocket *web_socket, const char *text) {

	bool command = true;
	char argument[SERVER_TEST_TEXT_SIZE] = { 0 };

	if (sscanf (text, "join:%63s", argument) == 1) {
		test_check_unsigned_eq (http_web_socket_join (web_socket, argument), HTTP_WEB_SOCKET_JOIN_JOINED, NULL);
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
	else if (!strcmp (text, "foreign-stream")) server_test_foreign_stream (web_socket);
	else if (!strcmp (text, "flood")) server_test_flood (web_socket);
	else if (!strcmp (text, "block")) server_test_block (web_socket);
	else if (!strcmp (text, "fill")) server_test_fill (web_socket);
	else if (!strcmp (text, "fill-wait")) server_test_fill_wait (web_socket);
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
	NULL, NULL, server_test_on_close, server_test_on_error, NULL
};

// A5: the queue was empty again after its backpressure, its peer learns it with a text
static void server_test_on_drain (HttpWebSocket *web_socket) {

	test_counter_add (&counters.drains);

	(void) http_web_socket_send_text (web_socket, "drained", 7);

}

static const HttpWebSocketHandlers server_test_queue_handlers = {
	NULL, server_test_on_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error, server_test_on_drain
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
	NULL, NULL, server_test_on_close, server_test_on_error, NULL
};

static bool server_test_header_is (
	const HttpRequest *request, const char *name, const char *value
) {

	const char *header = http_request_get_custom_header (request, name);

	return header && !strcmp (header, value);

}

// the data of an upgrade that is rejected is deleted by cerver
static void server_test_delete_data (void *data) {

	free (data);

	test_counter_add (&counters.deleted);

}

// the ways of rejecting an upgrade: with a status (409), with the default one (403),
// with a response of its own (404) and after setting the data of the connection (403, the data is deleted)
static void server_test_upgrade_reject (
	const HttpReceive *http_receive, const HttpRequest *request,
	HttpWebSocketUpgrade *upgrade
) {

	if (server_test_header_is (request, "X-Reject", "yes")) {
		upgrade->status = HTTP_STATUS_CONFLICT;
	}

	else if (server_test_header_is (request, "X-Reject", "answer")) {
		(void) http_response_send (not_found_error, http_receive);
	}

	else if (server_test_header_is (request, "X-Reject", "data")) {
		upgrade->data = strdup (server_test_upgrade_data);
		upgrade->delete_data = server_test_delete_data;
	}

}

// the protocol of an upgrade that is accepted: the last one of the server, none,
// or one that the client did not offer, that is none too
static const char *server_test_upgrade_protocol (const HttpRequest *request) {

	const char *protocol = server_test_protocols[1];

	if (server_test_header_is (request, "X-Protocol", "none")) protocol = NULL;
	else if (server_test_header_is (request, "X-Protocol", "other")) protocol = server_test_other_protocol;

	return protocol;

}

// rejects the upgrades with X-Reject, the others get their data and the last protocol they offered
static bool server_test_on_upgrade (
	const HttpReceive *http_receive, const HttpRequest *request,
	HttpWebSocketUpgrade *upgrade
) {

	// the default status of a rejection
	test_check_int_eq (upgrade->status, HTTP_STATUS_FORBIDDEN, NULL);

	bool accepted = !http_request_get_custom_header (request, "X-Reject");
	if (accepted) {
		upgrade->data = strdup (server_test_upgrade_data);
		upgrade->delete_data = free;
		upgrade->protocol = server_test_upgrade_protocol (request);

		// the headers of the 101: a name can be repeated, the ones of the handshake are of cerver
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Set-Cookie", "first=1; Path=/"), 0, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Set-Cookie", "second=2"), 0, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "X-Upgraded", "yes"), 0, NULL);

		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Upgrade", "h2c"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "connection", "close"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Sec-WebSocket-Accept", "other"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Sec-WebSocket-Protocol", "other"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Sec-WebSocket-Extensions", "other"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Content-Length", "5"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "Transfer-Encoding", "chunked"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "X-Split", "a\r\nX-Other: b"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "X Space", "a"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, NULL, "a"), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (upgrade, "X-Null", NULL), 1, NULL);
		test_check_unsigned_eq (http_web_socket_upgrade_add_header (NULL, "X-Null", "a"), 1, NULL);
	}

	else {
		server_test_upgrade_reject (http_receive, request, upgrade);
	}

	return accepted;

}

// the connection takes the grants of its request when its upgrade asks for them
static bool server_test_adopt_upgrade (
	const HttpReceive *http_receive, const HttpRequest *request,
	HttpWebSocketUpgrade *upgrade
) {

	(void) http_receive;

	upgrade->adopt_decoded_data = (http_request_get_custom_header (request, "X-Adopt") != NULL);

	return true;

}

// sends the grants that the connection adopted
static void server_test_adopt_open (HttpWebSocket *web_socket) {

	test_counter_add (&counters.opened);

	const char *grants = (const char *) http_web_socket_get_decoded_data (web_socket);
	if (!grants) grants = server_test_no_grants;

	(void) http_web_socket_send_text (web_socket, grants, strlen (grants));

}

static const HttpWebSocketHandlers server_test_adopt_handlers = {
	server_test_adopt_upgrade, server_test_adopt_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error, NULL
};

// the control frames of the peer: the ping was already answered by cerver
static void server_test_on_ping (HttpWebSocket *web_socket, const void *data, size_t size) {

	(void) web_socket;

	test_check_unsigned_eq (size, strlen (server_test_ping_data), NULL);
	test_check (!memcmp (data, server_test_ping_data, size), "the data of the ping is wrong");

	test_counter_add (&counters.pings);

}

static void server_test_on_pong (HttpWebSocket *web_socket, const void *data, size_t size) {

	(void) web_socket;

	test_check_unsigned_eq (size, strlen (server_test_pong_data), NULL);
	test_check (!memcmp (data, server_test_pong_data, size), "the data of the pong is wrong");

	test_counter_add (&counters.pongs);

}

static const HttpWebSocketHandlers server_test_control_handlers = {
	NULL, server_test_on_open, server_test_on_message, NULL,
	server_test_on_ping, server_test_on_pong, server_test_on_close, server_test_on_error, NULL
};

// a before middleware that answers the upgrades with X-Stop, the connection never starts
static HttpMiddlewareResult server_test_guard (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	HttpMiddlewareResult result = HTTP_MIDDLEWARE_RESULT_CONTINUE;

	if (http_request_get_custom_header (request, "X-Stop")) {
		(void) http_response_json_key_value_send (
			http_receive, HTTP_STATUS_TOO_MANY_REQUESTS, "error", "stopped"
		);

		result = HTTP_MIDDLEWARE_RESULT_STOP;
	}

	return result;

}

// sends its data, its protocol and the query of its request
static void server_test_upgrade_open (HttpWebSocket *web_socket) {

	test_counter_add (&counters.opened);

	const String *query = http_web_socket_get_query (web_socket);

	char text[SERVER_TEST_TEXT_SIZE];
	const int length = snprintf (
		text, SERVER_TEST_TEXT_SIZE, "%s:%s:%s",
		(const char *) http_web_socket_get_data (web_socket),
		http_web_socket_get_protocol (web_socket) ? http_web_socket_get_protocol (web_socket) : "none",
		query ? query->str : "none"
	);

	(void) http_web_socket_send_text (web_socket, text, (size_t) length);

}

static const HttpWebSocketHandlers server_test_upgrade_handlers = {
	server_test_on_upgrade, server_test_upgrade_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error, NULL
};

// closes the connection as soon as it opens
static void server_test_closer_open (HttpWebSocket *web_socket) {

	test_counter_add (&counters.opened);
	test_check_unsigned_eq (http_web_socket_close (web_socket, 4000, "bye"), 0, NULL);

}

static const HttpWebSocketHandlers server_test_closer_handlers = {
	NULL, server_test_closer_open, server_test_on_message, NULL,
	NULL, NULL, server_test_on_close, server_test_on_error, NULL
};

static unsigned int server_test_authenticate (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];

	const bool authorized = authorization && !strcmp (authorization->str, server_test_secret);

	// the grants of the request, a connection can adopt them in its upgrade
	if (authorized) {
		http_request_set_decoded_data ((HttpRequest *) request, strdup (server_test_grants));
		http_request_set_default_delete_decoded_data ((HttpRequest *) request);
	}

	return authorized ? 0 : 1;

}

static void server_test_plain_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

#pragma endregion

#pragma region server

// a group that sets every setting that a group has, a subgroup that changes three of them
// and an endpoint of the subgroup that changes two more
static void server_test_settings_routes (HttpGroup *ws) {

	HttpGroup *settings = http_group_create (ws, "settings");
	test_check_ptr (settings);

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;
	options.level = SERVER_TEST_SETTINGS_LEVEL;
	options.min_size = SERVER_TEST_SETTINGS_MIN_SIZE;

	HttpWebSocketSettings *group_settings = http_group_get_web_socket_settings (settings);
	http_web_socket_settings_set_max_message (group_settings, SERVER_TEST_SMALL_MESSAGE);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (group_settings, server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);
	http_web_socket_settings_set_compression (group_settings, &options);
	http_web_socket_settings_set_origin_check (group_settings, HTTP_WEB_SOCKET_ORIGIN_CHECK_REQUIRED);
	http_web_socket_settings_set_ping_milliseconds (group_settings, SERVER_TEST_PING_INTERVAL, SERVER_TEST_PONG_TIMEOUT);
	http_web_socket_settings_set_send_timeout_milliseconds (group_settings, SERVER_TEST_SEND_TIMEOUT);
	http_web_socket_settings_set_queue (group_settings, SERVER_TEST_SETTINGS_QUEUE, HTTP_WEB_SOCKET_OVERFLOW_CLOSE);

	server_test_inherited = http_group_web_socket_add (settings, "inherited", NULL);
	test_check_ptr (server_test_inherited);

	HttpGroup *inner = http_group_create (settings, "inner");
	test_check_ptr (inner);

	HttpWebSocketSettings *inner_settings = http_group_get_web_socket_settings (inner);
	http_web_socket_settings_set_max_message (inner_settings, SERVER_TEST_SETTINGS_MESSAGE);
	http_web_socket_settings_set_origin_check (inner_settings, HTTP_WEB_SOCKET_ORIGIN_CHECK_NONE);
	http_web_socket_settings_enable_compression (inner_settings, false);

	HttpEndpoint *own = http_group_web_socket_add (inner, "own", NULL);
	test_check_ptr (own);

	options.level = SERVER_TEST_SETTINGS_OWN_LEVEL;
	HttpWebSocketSettings *own_settings = http_endpoint_get_web_socket_settings (own);
	http_web_socket_settings_set_compression (own_settings, &options);
	http_web_socket_settings_set_close_timeout_milliseconds (own_settings, SERVER_TEST_SETTINGS_CLOSE_TIMEOUT);

	server_test_own = own;

	// the endpoint of the subgroup that sets nothing has the compression of its subgroup
	test_check_ptr (http_group_web_socket_add (inner, "plain", NULL));

}

static void server_test_routes (HttpCerver *http_cerver) {

	HttpGroup *ws = http_cerver_group_create (http_cerver, "ws");
	test_check_ptr (ws);

	// the endpoints without their own handlers take the ones of the group
	http_group_set_web_socket_handlers (ws, &server_test_echo_handlers);

	test_check_ptr (http_group_web_socket_add (ws, "echo", NULL));

	HttpEndpoint *plain = http_group_get (ws, "plain", server_test_plain_handler);
	http_endpoint_set_web_socket_handlers (plain, &server_test_echo_handlers);

	HttpEndpoint *auth = http_group_web_socket_add (ws, "auth", NULL);
	http_endpoint_set_auth (auth, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (auth, server_test_authenticate);

	HttpEndpoint *origin = http_group_web_socket_add (ws, "origin", NULL);
	http_web_socket_settings_set_origin_check (
		http_endpoint_get_web_socket_settings (origin), HTTP_WEB_SOCKET_ORIGIN_CHECK_REQUIRED
	);

	HttpEndpoint *present = http_group_web_socket_add (ws, "present", NULL);
	http_web_socket_settings_set_origin_check (
		http_endpoint_get_web_socket_settings (present), HTTP_WEB_SOCKET_ORIGIN_CHECK_IF_PRESENT
	);

	HttpEndpoint *protocols = http_group_web_socket_add (ws, "protocols", NULL);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (http_endpoint_get_web_socket_settings (protocols), server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);

	HttpEndpoint *upgrade = http_group_web_socket_add (ws, "upgrade", &server_test_upgrade_handlers);
	HttpWebSocketSettings *upgrade_settings = http_endpoint_get_web_socket_settings (upgrade);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (upgrade_settings, server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);

	HttpEndpoint *adopt = http_group_web_socket_add (ws, "adopt", &server_test_adopt_handlers);
	http_endpoint_set_auth (adopt, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_endpoint_set_authentication_handler (adopt, server_test_authenticate);

	HttpEndpoint *guarded = http_group_web_socket_add (ws, "guarded", NULL);
	test_check_unsigned_eq (http_endpoint_add_before (guarded, server_test_guard), 0, NULL);

	test_check_ptr (http_group_web_socket_add (ws, "control", &server_test_control_handlers));

	// its connections wait for the close of their peers longer than the drain of the cerver of its test
	HttpEndpoint *patient = http_group_web_socket_add (ws, "patient", NULL);
	http_web_socket_settings_set_close_timeout_milliseconds (
		http_endpoint_get_web_socket_settings (patient), SERVER_TEST_PATIENT_CLOSE_TIMEOUT
	);

	server_test_settings_routes (ws);

	// more protocols than an endpoint has, and one that is not a token: the list stays as it was
	static const char *many[HTTP_WEB_SOCKET_MAX_PROTOCOLS + 1] = {
		"p0", "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8", "p9", "p10", "p11", "p12", "p13", "p14", "p15", "p16"
	};

	static const char *spaced[] = { "chat", "not a token" };

	test_check_unsigned_eq (http_web_socket_settings_set_protocols (upgrade_settings, many, HTTP_WEB_SOCKET_MAX_PROTOCOLS + 1), 1, NULL);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (upgrade_settings, spaced, 2), 1, NULL);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (upgrade_settings, many, HTTP_WEB_SOCKET_MAX_PROTOCOLS), 0, NULL);
	test_check_unsigned_eq (http_web_socket_settings_set_protocols (upgrade_settings, server_test_protocols, SERVER_TEST_N_PROTOCOLS), 0, NULL);

	HttpEndpoint *compressed = http_group_web_socket_add (ws, "compressed", NULL);
	http_web_socket_settings_enable_compression (http_endpoint_get_web_socket_settings (compressed), true);

	HttpEndpoint *small = http_group_web_socket_add (ws, "small", NULL);
	http_web_socket_settings_set_max_message (http_endpoint_get_web_socket_settings (small), SERVER_TEST_SMALL_MESSAGE);

	HttpEndpoint *ping = http_group_web_socket_add (ws, "ping", NULL);
	http_web_socket_settings_set_ping_milliseconds (
		http_endpoint_get_web_socket_settings (ping), SERVER_TEST_PING_INTERVAL, SERVER_TEST_PONG_TIMEOUT
	);

	HttpEndpoint *slow = http_group_web_socket_add (ws, "slow", NULL);
	http_web_socket_settings_set_send_timeout_milliseconds (http_endpoint_get_web_socket_settings (slow), SERVER_TEST_SEND_TIMEOUT);

	// A5: a small queue that drops what does not fit, and one that closes with 1008
	HttpEndpoint *drop = http_group_web_socket_add (ws, "drop", &server_test_queue_handlers);
	http_web_socket_settings_set_queue (
		http_endpoint_get_web_socket_settings (drop), SERVER_TEST_SMALL_QUEUE, HTTP_WEB_SOCKET_OVERFLOW_DROP
	);

	HttpEndpoint *strict = http_group_web_socket_add (ws, "strict", &server_test_queue_handlers);
	http_web_socket_settings_set_queue (
		http_endpoint_get_web_socket_settings (strict), SERVER_TEST_SMALL_QUEUE, HTTP_WEB_SOCKET_OVERFLOW_CLOSE
	);

	test_check_ptr (http_group_web_socket_add (ws, "stream", &server_test_stream_handlers));
	test_check_ptr (http_group_web_socket_add (ws, "closer", &server_test_closer_handlers));

	// every connection of the cerver waits this for the close frame of its peer
	http_web_socket_settings_set_close_timeout_milliseconds (http_group_get_web_socket_settings (ws), SERVER_TEST_CLOSE_TIMEOUT);

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

	cerver_set_handler_type (cerver, test_handler_type ());
	cerver_set_io_threads (cerver, test_io_threads ());
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

// the status of a request that is sent as it is, like an upgrade that is not valid
static unsigned int server_test_request_status (const u16 port, const char *request) {

	const int fd = raw_connect (port);

	char response[RAW_RESPONSE_SIZE];
	const unsigned int status = (unsigned int) raw_request (fd, request, strlen (request), response);
	(void) close (fd);

	return status;

}

// the places that the upgrades and the connections of the cerver have taken
static unsigned int server_test_reserved (const ServerTest *server) {

	HttpWebSocketRegistry *registry = server->http_cerver->web_sockets;

	(void) pthread_mutex_lock (&registry->mutex);
	const unsigned int reserved = registry->reserved;
	(void) pthread_mutex_unlock (&registry->mutex);

	return reserved;

}

// waits until the places that the upgrades reserved are the expected ones: an upgrade that is rejected
// gives its place back when its request ends, after its client got the response
static unsigned int server_test_wait_reserved (const ServerTest *server, const unsigned int expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;

	unsigned int reserved = server_test_reserved (server);
	while ((reserved != expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		reserved = server_test_reserved (server);
	}

	return reserved;

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

	// A13: an Origin must be one of the whitelist only when it is sent
	test_check_unsigned_eq (server_test_status (port, "/ws/present", "Origin: https://other.test\r\n"), 403, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/present", NULL), 101, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/present", "Origin: https://cerver.test\r\n"), 101, NULL);

	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: yes\r\n"), 409, NULL);

	// the protocol of the response is the first one of the server that the client offered
	const int fd = raw_connect (port);
	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (raw_upgrade (fd, "/ws/protocols", "Sec-WebSocket-Protocol: other, chat, chat.v2\r\n", response), 101, NULL);
	test_check (strstr (response, "Sec-WebSocket-Protocol: chat.v2\r\n") != NULL, "the protocol is not the first one of the server");
	(void) close (fd);

	// echo, auth, origin, present (twice) and protocols were accepted
	server_test_wait_closed (6);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_ORIGIN], 3, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_APPLICATION], 1, NULL);

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
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_CLOSE_TIMEOUT, NULL);

	raw_reader_end (&reader);
	raw_reader_end (&silent_reader);
	(void) close (fd);
	(void) close (silent);

	server_test_stop (server);

}

// a broken frame is answered with its close code and the end of the connection,
// that reports a protocol error and that code: only the error of the last case was checked
static void server_test_protocol_error (
	const u16 port, const char *path, const u8 *bytes, const size_t size,
	const u16 code, const unsigned int closed
) {

	const int fd = raw_open (port, path, NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send (fd, bytes, size);
	server_test_expect_close (&reader, code);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_wait_closed (closed);
	test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), closed, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_PROTOCOL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), code, NULL);

	// the next case must set them again
	__atomic_store_n (&counters.last_error, HTTP_WEB_SOCKET_ERROR_NONE, __ATOMIC_SEQ_CST);
	__atomic_store_n (&counters.last_code, 0, __ATOMIC_SEQ_CST);

}

static void test_server_protocol_errors (void) {

	ServerTest *server = server_test_start (0);
	const u16 port = server->port;

	u8 buffer[SERVER_TEST_SMALL_MESSAGE * 2];
	size_t size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "unmasked", 8, false);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002, 1);

	size = raw_frame (buffer, true, false, 0x3, "reserved", 8, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002, 2);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "\xC0\xAF", 2, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1007, 3);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "alone", 5, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002, 4);

	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xED", 2, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002, 5);

	size = raw_frame (buffer, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "a", 1, true);
	server_test_protocol_error (port, "/ws/echo", buffer, size, 1002, 6);

	u8 large[SERVER_TEST_SMALL_MESSAGE + 1];
	(void) memset (large, 'x', sizeof (large));
	size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, large, sizeof (large), true);
	server_test_protocol_error (port, "/ws/small", buffer, size, 1009, 7);

	// a length that is not the shortest encoding of its value: 5 bytes in the 16 bits form
	static const u8 long_length[] = { 0x81, 0xFE, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 'H', 'e', 'l', 'l', 'o' };
	server_test_protocol_error (port, "/ws/echo", long_length, sizeof (long_length), 1002, 8);

	// none of them was a message for its connection
	test_check_unsigned_eq (__atomic_load_n (&counters.messages, __ATOMIC_SEQ_CST), 0, NULL);

	server_test_stop (server);

}

// a client that leaves without a close: a reset, a close of the socket and in the middle of a frame
static void test_server_abrupt (void) {

	ServerTest *server = server_test_start (0);

	// a reset is an error of its receive, the end of the connection of the peer is not an error
	int fd = raw_open (server->port, "/ws/echo", NULL);
	raw_reset (fd);
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_ERROR_RECEIVE, NULL);

	fd = raw_open (server->port, "/ws/echo", NULL);
	(void) close (fd);
	server_test_wait_closed (2);
	test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), 1, NULL);

	fd = raw_open (server->port, "/ws/echo", NULL);
	u8 buffer[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE + SERVER_TEST_TEXT_SIZE];
	const size_t size = raw_frame (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "never ends", 10, true);
	raw_send (fd, buffer, size / 2);
	(void) shutdown (fd, SHUT_WR);
	server_test_wait_closed (3);
	(void) close (fd);

	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL);
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

		test_check_unsigned_eq (http_web_socket_send_text (sender->web_socket, text, (size_t) length), HTTP_WEB_SOCKET_SEND_QUEUED, NULL);
	}

	return NULL;

}

static void server_test_take (HttpWebSocket *web_socket, void *args) {

	*((HttpWebSocket **) args) = http_web_socket_retain (web_socket);

}

// S7: a connection is registered after its on_open (), that counts it as opened: waits until the registry has them
static void server_test_wait_registered (const ServerTest *server, const unsigned int expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	while (
		(http_cerver_web_socket_count (server->http_cerver, NULL) < expected) && (test_wait_now () < deadline)
	) {
		(void) nanosleep (&interval, NULL);
	}

	test_check_unsigned_eq (http_cerver_web_socket_count (server->http_cerver, NULL), expected, NULL);

}

// keeps only the first connection that it is given
static void server_test_take_first (HttpWebSocket *web_socket, void *args) {

	HttpWebSocket **taken = (HttpWebSocket **) args;
	if (!*taken) *taken = http_web_socket_retain (web_socket);

}

// many threads send to the same connection, every message arrives whole and in the order of its thread
static void test_server_concurrent_sends (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.opened, 1, TEST_WAIT_SECONDS), 1, NULL);
	server_test_wait_registered (server, 1);

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
	test_check_unsigned_eq (http_web_socket_send_text (web_socket, "late", 4), HTTP_WEB_SOCKET_SEND_CLOSED, NULL);
	test_check_int_eq (http_web_socket_get_state (web_socket), HTTP_WEB_SOCKET_STATE_CLOSED, NULL);
	http_web_socket_release (web_socket);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// the WebSocket connections that the loops still have: with more than one I/O thread the connection goes to any of
// them (SO_REUSEPORT)
static unsigned int server_test_loop_web_sockets (const ServerTest *server) {

	unsigned int n_web_sockets = 0;

	const EventLoopGroup *group = server->cerver->event_loops;
	for (unsigned int i = 0; group && (i < group->n_loops); i++) {
		n_web_sockets += __atomic_load_n (&group->loops[i]->n_web_sockets, __ATOMIC_SEQ_CST);
	}

	return n_web_sockets;

}

static void server_test_wait_loop_empty (const ServerTest *server) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;

	while (server_test_loop_web_sockets (server) && (test_wait_now () < deadline)) (void) nanosleep (&interval, NULL);

}

// F6b: with the event loop a connection is read and written by its loop, without a thread of its own,
// and the loop does not end while it has it; with threads every connection has its own
static void test_server_owner (void) {

	ServerTest *server = server_test_start (0);
	const bool events = (test_handler_type () == CERVER_HANDLER_TYPE_EVENTS);
	__atomic_store_n (&server_test_join_on_open, 1, __ATOMIC_SEQ_CST);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.opened, 1, TEST_WAIT_SECONDS), 1, NULL);
	__atomic_store_n (&server_test_join_on_open, 0, __ATOMIC_SEQ_CST);

	test_check_unsigned_eq (thread_group_alive (&server->cerver->connection_threads), events ? 0 : 1, NULL);
	test_check_unsigned_eq (server_test_loop_web_sockets (server), events ? 1 : 0, NULL);

	// S7: it was not registered while its on_open () ran, it is after it
	test_check_unsigned_eq (__atomic_load_n (&counters.open_registered, __ATOMIC_SEQ_CST), 0, NULL);
	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	while (!http_cerver_web_socket_count (server->http_cerver, NULL) && (test_wait_now () < deadline)) (void) nanosleep (&interval, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_count (server->http_cerver, NULL), 1, NULL);

	// it joined its room in its on_open (), where the count did not see it yet; it does now
	test_check_unsigned_eq (__atomic_load_n (&counters.open_join, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_JOIN_JOINED + 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.open_room_count, __ATOMIC_SEQ_CST), 0, NULL);
	const HttpWebSocketTarget opened = { SERVER_TEST_OPEN_ROOM, NULL, NULL };
	test_check_unsigned_eq (http_cerver_web_socket_count (server->http_cerver, &opened), 1, NULL);

	RawReader reader;
	raw_reader_init (&reader, fd);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "owner", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "owner", 5);

	// with the event loop it waits for its next frames in the poller of its pool after its job, and a thread of
	// the pool reads them (F6b)
	if (events) {
		HttpWebSocket *web_socket = NULL;
		http_cerver_web_socket_for_each (server->http_cerver, NULL, server_test_take, &web_socket);
		test_check_ptr (web_socket);
		test_check_ptr (web_socket->park);

		const double parked_deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
		while (
			(__atomic_load_n (&web_socket->park->state, __ATOMIC_SEQ_CST) != HTTP_WEB_SOCKET_PARK_PARKED)
			&& (test_wait_now () < parked_deadline)
		) {
			(void) nanosleep (&interval, NULL);
		}

		test_check_unsigned_eq (__atomic_load_n (&web_socket->park->state, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_PARK_PARKED, NULL);
		http_web_socket_release (web_socket);

		raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "parked", 6);
		server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "parked", 6);
	}

	raw_send_close (fd, 1000, NULL);
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);

	// the loop gives the connection back after its last methods
	server_test_wait_loop_empty (server);
	test_check_unsigned_eq (server_test_loop_web_sockets (server), 0, NULL);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// F6b: the owner of a connection writes its queue while one of its methods runs: with the event loop a message
// larger than what the socket takes (and than the queue) arrives whole while on_message () is blocked, and what
// the method sends next fits again; with a thread of its own the queue waits for the method, so what the method
// sends does not fit (the policy drops it) and the large message arrives after the method
static void test_server_written_while_blocked (void) {

	ServerTest *server = server_test_start (0);
	const bool events = (test_handler_type () == CERVER_HANDLER_TYPE_EVENTS);
	__atomic_store_n (&server_test_released, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&server_test_unblocked, 0, __ATOMIC_SEQ_CST);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.opened, 1, TEST_WAIT_SECONDS), 1, NULL);
	server_test_wait_registered (server, 1);

	HttpWebSocket *web_socket = NULL;
	http_cerver_web_socket_for_each (server->http_cerver, NULL, server_test_take, &web_socket);
	test_check_ptr (web_socket);

	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "block", 5);
	test_check_unsigned_eq (test_wait_counter (&counters.blocked, 1, TEST_WAIT_SECONDS), 1, NULL);

	// one method at a time: the next message waits for the one that is blocked
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "after", 5);
	const struct timespec wait = { 0, SERVER_TEST_SERIAL_NANOSECONDS };
	(void) nanosleep (&wait, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.messages, __ATOMIC_SEQ_CST), 1, "a message ran while another one was blocked");

	u8 *large = (u8 *) malloc (SERVER_TEST_BLOCKED_SIZE);
	test_check_ptr (large);
	(void) memset (large, 'b', SERVER_TEST_BLOCKED_SIZE);

	test_check (
		http_web_socket_send_result_is_taken (http_web_socket_send_binary (web_socket, large, SERVER_TEST_BLOCKED_SIZE)),
		"a message larger than the queue was not taken with an empty queue"
	);

	RawFrame frame;
	if (events) {
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the loop did not write the queue while the method ran");
		test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL);
		test_check_unsigned_eq (frame.size, SERVER_TEST_BLOCKED_SIZE, NULL);
		raw_frame_delete (&frame);

		test_check_unsigned_eq (__atomic_load_n (&server_test_released, __ATOMIC_SEQ_CST), 0, "the method was not blocked");
	}

	__atomic_store_n (&server_test_released, 1, __ATOMIC_SEQ_CST);

	test_check (test_wait_counter (&server_test_unblocked, 1, TEST_WAIT_SECONDS), "the method did not send");
	test_check_unsigned_eq (
		__atomic_load_n (&server_test_unblocked, __ATOMIC_SEQ_CST) - 1,
		events ? HTTP_WEB_SOCKET_SEND_QUEUED : HTTP_WEB_SOCKET_SEND_DROPPED, NULL
	);

	if (events) {
		server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "unblocked", 9);
	}

	else {
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the message did not arrive after the method");
		test_check_unsigned_eq (frame.size, SERVER_TEST_BLOCKED_SIZE, NULL);
		raw_frame_delete (&frame);
	}

	// the message that waited runs after the method, in its order (with a thread its echo finds the queue full)
	if (events) server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "after", 5);
	test_check_unsigned_eq (test_wait_counter (&counters.messages, 2, TEST_WAIT_SECONDS), 2, NULL);

	raw_send_close (fd, 1000, NULL);
	server_test_expect_close (&reader, 1000);
	server_test_wait_closed (1);

	http_web_socket_release (web_socket);
	free (large);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// a client that does not read fills the small queue of the connection of the endpoint, with the command given
static int server_test_fill_start (const ServerTest *server, const char *path, const char *command) {

	const int fd = raw_connect (server->port);
	const int size = SERVER_TEST_SMALL_BUFFER;
	(void) setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (int));

	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (raw_upgrade (fd, path, NULL, response), 101, NULL);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, command, strlen (command));
	test_check_unsigned_eq (test_wait_counter (&counters.floods, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the sends were taken, then over the half of the queue with backpressure, and the last one did not fit
	test_check (__atomic_load_n (&counters.fill_queued, __ATOMIC_SEQ_CST) > 0, "no send was queued");
	test_check (__atomic_load_n (&counters.fill_backpressure, __ATOMIC_SEQ_CST) > 0, "no send had backpressure");
	test_check_unsigned_eq (__atomic_load_n (&counters.fill_dropped, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.fill_closed, __ATOMIC_SEQ_CST), 0, NULL);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_dropped, 1, NULL);

	// the client reads fast from now: with the small buffer every segment waits for a delayed ACK, and the socket
	// would take longer than the send timeout of the queue to make a place for it
	const int large = SERVER_TEST_LARGE_BUFFER;
	(void) setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &large, sizeof (int));

	return fd;

}

// reads the messages that the queue took, up to the first frame that is not binary
static unsigned int server_test_fill_read (RawReader *reader, RawFrame *last) {

	unsigned int received = 0;

	bool binary = true;
	while (binary) {
		test_check (raw_read_frame (reader, last, RAW_TIMEOUT), "the frames of the queue did not arrive");
		binary = (last->opcode == HTTP_WEB_SOCKET_OPCODE_BINARY) || (last->opcode == HTTP_WEB_SOCKET_OPCODE_PING);
		if (last->opcode == HTTP_WEB_SOCKET_OPCODE_BINARY) received += 1;
		if (binary) raw_frame_delete (last);
	}

	return received;

}

static unsigned int server_test_fill_taken (void) {

	return __atomic_load_n (&counters.fill_queued, __ATOMIC_SEQ_CST)
		+ __atomic_load_n (&counters.fill_backpressure, __ATOMIC_SEQ_CST);

}

// A5: with the drop policy the message that does not fit is not taken and the connection stays open; once the
// client reads, every message that was taken arrives, and on_drain () runs once, after the last one
static void test_server_queue_drop (void) {

	ServerTest *server = server_test_start (0);

	const int fd = server_test_fill_start (server, "/ws/drop", "fill");
	test_check_unsigned_eq (__atomic_load_n (&counters.drains, __ATOMIC_SEQ_CST), 0, NULL);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check (stats.n_queued_bytes > 0, "the queue was not counted");

	RawReader reader;
	raw_reader_init (&reader, fd);

	RawFrame frame;
	test_check_unsigned_eq (server_test_fill_read (&reader, &frame), server_test_fill_taken (), NULL);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
	test_check ((frame.size == 7) && !memcmp (frame.data, "drained", 7), "on_drain () did not send its text");
	raw_frame_delete (&frame);

	test_check_unsigned_eq (__atomic_load_n (&counters.drains, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_drains, 1, NULL);
	test_check_unsigned_eq (stats.n_queued_bytes, 0, NULL);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "alive", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "alive", 5);

	raw_send_close (fd, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, NULL);
	server_test_expect_close (&reader, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL);
	server_test_wait_closed (1);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// A5: with the close policy the message that does not fit closes the connection with 1008, after the messages
// that the queue took
static void test_server_queue_close (void) {

	ServerTest *server = server_test_start (0);

	const int fd = server_test_fill_start (server, "/ws/strict", "fill");

	RawReader reader;
	raw_reader_init (&reader, fd);

	RawFrame frame;
	test_check_unsigned_eq (server_test_fill_read (&reader, &frame), server_test_fill_taken (), NULL);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_CODE_POLICY_VIOLATION, NULL);
	raw_frame_delete (&frame);

	raw_send_close (fd, HTTP_WEB_SOCKET_CLOSE_CODE_POLICY_VIOLATION, NULL);
	test_check (raw_wait_end (&reader, RAW_TIMEOUT), "the server did not end the connection");
	server_test_wait_closed (1);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_POLICY_VIOLATION, NULL);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (
		stats.n_closes[HTTP_WEB_SOCKET_CLOSE_CODE_POLICY_VIOLATION - HTTP_WEB_SOCKET_CLOSE_CODES_FIRST], 1, NULL
	);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// A5 in EVENTS: the loop empties the queue while the job of the connection still runs, so on_drain () goes after
// the job, and the connection does not wait in its park with it (it waited there until its peer wrote again)
static void test_server_queue_drained_in_job (void) {

	if (test_handler_type () != CERVER_HANDLER_TYPE_EVENTS) return;

	ServerTest *server = server_test_start (0);

	const int fd = server_test_fill_start (server, "/ws/drop", "fill-wait");

	RawReader reader;
	raw_reader_init (&reader, fd);

	RawFrame frame;
	test_check_unsigned_eq (server_test_fill_read (&reader, &frame), server_test_fill_taken (), NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.fill_emptied, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
	test_check ((frame.size == 7) && !memcmp (frame.data, "drained", 7), "on_drain () did not send its text");
	raw_frame_delete (&frame);

	test_check_unsigned_eq (__atomic_load_n (&counters.drains, __ATOMIC_SEQ_CST), 1, NULL);

	raw_send_close (fd, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, NULL);
	server_test_expect_close (&reader, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL);
	server_test_wait_closed (1);

	raw_reader_end (&reader);
	(void) close (fd);

	server_test_stop (server);

}

// the connections of a room
static unsigned int server_test_room_count (HttpCerver *http_cerver, const char *room) {

	const HttpWebSocketTarget target = { room, NULL, NULL };

	return http_cerver_web_socket_count (http_cerver, &target);

}

static unsigned int server_test_room_broadcast (
	HttpCerver *http_cerver, const char *room, const HttpWebSocket *except,
	const HttpWebSocketOpcode type, const char *text
) {

	const HttpWebSocketTarget target = { room, NULL, except };

	return http_cerver_web_socket_broadcast (http_cerver, &target, type, text, strlen (text));

}

// A14: a connection is in any number of rooms of any length, and every room is in the index
static void server_test_rooms_many (HttpCerver *http_cerver, HttpWebSocket *web_socket) {

	char name[SERVER_TEST_LONG_ROOM_SIZE + 1];
	(void) memset (name, 'r', SERVER_TEST_LONG_ROOM_SIZE);
	name[SERVER_TEST_LONG_ROOM_SIZE] = '\0';
	test_check_unsigned_eq (http_web_socket_join (web_socket, name), HTTP_WEB_SOCKET_JOIN_JOINED, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, name), 1, NULL);

	for (unsigned int i = 0; i < SERVER_TEST_MANY_ROOMS; i++) {
		(void) snprintf (name, sizeof (name), "room-%u", i);
		test_check_unsigned_eq (http_web_socket_join (web_socket, name), HTTP_WEB_SOCKET_JOIN_JOINED, NULL);
	}

	unsigned int n_rooms = 0;
	char **rooms = http_web_socket_get_rooms (web_socket, &n_rooms);
	test_check_ptr (rooms);
	test_check_unsigned_eq (n_rooms, SERVER_TEST_MANY_ROOMS + 2, NULL);

	bool blue = false;
	for (unsigned int i = 0; i < n_rooms; i++) blue = blue || !strcmp (rooms[i], "blue");
	test_check_true (blue);
	free (rooms);

	test_check_unsigned_eq (http_web_socket_leave (web_socket, "room-2"), 0, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "room-2"), 0, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "room-3"), 1, NULL);

	// it leaves every room, and it joins blue again
	test_check_unsigned_eq (http_web_socket_leave_all (web_socket), SERVER_TEST_MANY_ROOMS + 1, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "room-3"), 0, NULL);
	test_check_null_ptr (http_web_socket_get_rooms (web_socket, &n_rooms));
	test_check_unsigned_eq (n_rooms, 0, NULL);
	test_check_unsigned_eq (http_web_socket_join (web_socket, "blue"), HTTP_WEB_SOCKET_JOIN_JOINED, NULL);

}

// the rooms and the broadcasts
// the limits of the rooms of a connection that is in blue, and the closes that it can not send:
// none of these branches had a test
static void server_test_rooms_limits (HttpCerver *http_cerver, HttpWebSocket *web_socket) {

	// joining a room again changes nothing
	test_check_true (http_web_socket_in_room (web_socket, "blue"));
	test_check_unsigned_eq (http_web_socket_join (web_socket, "blue"), HTTP_WEB_SOCKET_JOIN_ALREADY, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "blue"), 1, NULL);

	test_check_false (http_web_socket_in_room (web_socket, "green"));
	test_check_unsigned_eq (http_web_socket_leave (web_socket, "green"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_join (web_socket, NULL), HTTP_WEB_SOCKET_JOIN_INVALID, NULL);
	test_check_unsigned_eq (http_web_socket_join (web_socket, ""), HTTP_WEB_SOCKET_JOIN_INVALID, NULL);
	test_check_unsigned_eq (http_web_socket_join (NULL, "green"), HTTP_WEB_SOCKET_JOIN_INVALID, NULL);
	test_check_unsigned_eq (http_web_socket_leave (web_socket, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_leave_all (NULL), 0, NULL);
	test_check_null_ptr (http_web_socket_get_rooms (NULL, NULL));

	test_check_str_eq (http_web_socket_join_to_string (HTTP_WEB_SOCKET_JOIN_ALREADY), "Already", NULL);
	test_check_str_eq (http_web_socket_join_to_string ((HttpWebSocketJoin) SERVER_TEST_UNDEFINED_VALUE), "Undefined", NULL);
	test_check_str_eq (http_web_socket_join_description ((HttpWebSocketJoin) SERVER_TEST_UNDEFINED_VALUE), "Undefined", NULL);

	server_test_rooms_many (http_cerver, web_socket);
	test_check_true (http_web_socket_in_room (web_socket, "blue"));

	// A9: the id of a registered connection finds it with a reference
	HttpWebSocket *found = http_cerver_web_socket_get_with_id (http_cerver, http_web_socket_get_id (web_socket));
	test_check (found == web_socket, "the connection was not found with its id");
	http_web_socket_release (found);
	test_check_null_ptr (http_cerver_web_socket_get_with_id (http_cerver, 0));
	test_check_null_ptr (http_cerver_web_socket_get_with_id (NULL, 1));

	// the codes that an endpoint does not send and a reason that does not fit: the connection stays open
	char reason[HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE + 2];
	(void) memset (reason, 'r', HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE + 1);
	reason[HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE + 1] = '\0';

	test_check_unsigned_eq (http_web_socket_close (web_socket, 999, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_NO_STATUS, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, reason), 1, NULL);
	test_check_unsigned_eq (http_web_socket_close (web_socket, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, "\xC3"), 1, NULL);
	test_check_unsigned_eq (http_web_socket_close (NULL, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, NULL), 1, NULL);
	test_check_int_eq (http_web_socket_get_state (web_socket), HTTP_WEB_SOCKET_STATE_OPEN, NULL);

}

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
	test_check_unsigned_eq (server_test_room_count (http_cerver, "red"), 3, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "blue"), 1, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "green"), 0, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, ""), 0, NULL);

	test_check_unsigned_eq (server_test_room_broadcast (http_cerver, "red", NULL, HTTP_WEB_SOCKET_OPCODE_TEXT, "red"), 3, NULL);
	for (unsigned int i = 0; i < 3; i++) server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "red", 3);

	RawFrame frame;
	test_check_false (raw_read_frame (&readers[3], &frame, SERVER_TEST_PING_INTERVAL));

	raw_send_frame (fds[1], true, HTTP_WEB_SOCKET_OPCODE_TEXT, "leave:red", 9);
	server_test_expect_frame (&readers[1], HTTP_WEB_SOCKET_OPCODE_TEXT, "left", 4);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "red"), 2, NULL);

	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, NULL, HTTP_WEB_SOCKET_OPCODE_BINARY, "all", 3), 4, NULL);
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_BINARY, "all", 3);
	}

	// every connection is of the echo endpoint
	HttpWebSocket *any = NULL;
	const HttpWebSocketTarget blue = { "blue", NULL, NULL };
	http_cerver_web_socket_for_each (http_cerver, &blue, server_test_take, &any);
	test_check_ptr (any);
	const HttpEndpoint *endpoint = http_web_socket_get_endpoint (any);

	server_test_rooms_limits (http_cerver, any);

	const HttpWebSocketTarget echo = { NULL, endpoint, NULL };
	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, &echo, HTTP_WEB_SOCKET_OPCODE_TEXT, "echo", 4), 4, NULL);
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "echo", 4);
	}

	// a room and an endpoint together, and another endpoint
	const HttpWebSocketTarget red_echo = { "red", endpoint, NULL };
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, &red_echo), 2, NULL);
	const HttpWebSocketTarget other = { NULL, server_test_own, NULL };
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, &other), 0, NULL);

	// the sender is left out
	HttpWebSocket *sender = NULL;
	http_cerver_web_socket_for_each (http_cerver, &blue, server_test_take, &sender);
	test_check_unsigned_eq (server_test_room_broadcast (http_cerver, NULL, sender, HTTP_WEB_SOCKET_OPCODE_TEXT, "others"), 3, NULL);
	const HttpWebSocketTarget others = { NULL, NULL, sender };
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, &others), 3, NULL);
	http_web_socket_release (sender);
	for (unsigned int i = 0; i < 3; i++) server_test_expect_frame (&readers[i], HTTP_WEB_SOCKET_OPCODE_TEXT, "others", 6);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		raw_reader_end (&readers[i]);
		(void) close (fds[i]);
	}

	server_test_wait_closed (SERVER_TEST_ROOM_CLIENTS);
	test_check_unsigned_eq (http_cerver_web_socket_count (http_cerver, NULL), 0, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_broadcast (http_cerver, &echo, HTTP_WEB_SOCKET_OPCODE_TEXT, "none", 4), 0, NULL);

	// A14: on_close () saw the rooms of each connection (2 in red and 1 in blue), and then they were cleared
	test_check_unsigned_eq (__atomic_load_n (&counters.closed_rooms, __ATOMIC_SEQ_CST), 3, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "red"), 0, NULL);

	// a connection that ended left its rooms, and it joins no other one
	test_check_false (http_web_socket_in_room (any, "blue"));
	test_check_unsigned_eq (http_web_socket_join (any, "late"), HTTP_WEB_SOCKET_JOIN_UNREGISTERED, NULL);
	test_check_unsigned_eq (server_test_room_count (http_cerver, "late"), 0, NULL);
	test_check_unsigned_eq (http_web_socket_close (any, HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL, NULL), 1, NULL);
	http_web_socket_release (any);

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
	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check (stats.n_slow >= 1, "the slow send was not counted");

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
	test_check_unsigned_eq (server_test_reserved (server), 0, NULL);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_LIMIT], 1, NULL);

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
	server_test_wait_registered (server, SERVER_TEST_ROOM_CLIENTS);

	// a connection that the application keeps after its cerver ended
	HttpWebSocket *kept = NULL;
	http_cerver_web_socket_for_each (server->http_cerver, NULL, server_test_take_first, &kept);
	test_check_ptr (kept);

	ServerTestStop stop = { server };
	pthread_t stopper;
	test_check_int_eq (pthread_create (&stopper, NULL, server_test_stopper, &stop), 0, NULL);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		RawFrame frame;
		test_check (raw_read_frame (&readers[i], &frame, RAW_TIMEOUT), "the going away did not arrive");
		test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL);
		raw_frame_delete (&frame);

		raw_send_close (fds[i], HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL);
		test_check (raw_wait_end (&readers[i], RAW_TIMEOUT), "the connection did not end");
	}

	test_check_int_eq (pthread_join (stopper, NULL), 0, NULL);

	// the sends of the connection that was kept only return their error:
	// the timeout of a send was read from the cerver, that was already released
	test_check_unsigned_eq (http_web_socket_send_text (kept, "late", 4), HTTP_WEB_SOCKET_SEND_CLOSED, NULL);
	test_check_unsigned_eq (http_web_socket_send_binary (kept, "late", 4), HTTP_WEB_SOCKET_SEND_CLOSED, NULL);
	http_web_socket_release (kept);

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		raw_reader_end (&readers[i]);
		(void) close (fds[i]);
	}

	test_check_unsigned_eq (__atomic_load_n (&counters.closed, __ATOMIC_SEQ_CST), SERVER_TEST_ROOM_CLIENTS, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL);

	test_check_unsigned_eq (raw_open_fds (), server->fds, NULL);
	free (server);

}

// the teardown of a cerver whose clients never read nor answer: every connection ends
// without the close of its peer (1006), and each client finds the 1001 and the end of its connection
static void server_test_silent_clients (
	const char *path, const unsigned int drain_timeout, const HttpWebSocketError error
) {

	ServerTest *server = server_test_start (0);
	if (drain_timeout) cerver_set_drain_timeout (server->cerver, drain_timeout);

	int fds[SERVER_TEST_ROOM_CLIENTS];
	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		fds[i] = raw_open (server->port, path, NULL);
	}

	test_check_unsigned_eq (test_wait_counter (&counters.opened, SERVER_TEST_ROOM_CLIENTS, TEST_WAIT_SECONDS), SERVER_TEST_ROOM_CLIENTS, NULL);

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	test_check_unsigned_eq (__atomic_load_n (&counters.closed, __ATOMIC_SEQ_CST), SERVER_TEST_ROOM_CLIENTS, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.last_code, __ATOMIC_SEQ_CST), HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL, NULL);

	if (error != HTTP_WEB_SOCKET_ERROR_NONE) {
		test_check_unsigned_eq (__atomic_load_n (&counters.errors, __ATOMIC_SEQ_CST), SERVER_TEST_ROOM_CLIENTS, NULL);
		test_check_unsigned_eq (__atomic_load_n (&counters.last_error, __ATOMIC_SEQ_CST), error, NULL);
	}

	for (unsigned int i = 0; i < SERVER_TEST_ROOM_CLIENTS; i++) {
		RawReader reader;
		raw_reader_init (&reader, fds[i]);

		RawFrame frame;
		test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the going away did not arrive");
		test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
		test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_CODE_GOING_AWAY, NULL);
		raw_frame_delete (&frame);

		test_check (raw_wait_end (&reader, RAW_TIMEOUT), "the connection did not end");

		raw_reader_end (&reader);
		(void) close (fds[i]);
	}

	test_check_unsigned_eq (raw_open_fds (), server->fds, NULL);
	free (server);

}

// the teardown does not wait forever for the clients that do not answer its 1001:
// each connection ends after its close timeout, with its error, and the cerver ends;
// and the connections whose close timeout is longer than the drain of the cerver are cut when it ends
// only the teardown with clients that answer had a test
static void test_server_shutdown_silent (void) {

	server_test_silent_clients ("/ws/echo", 0, HTTP_WEB_SOCKET_ERROR_CLOSE_TIMEOUT);

	server_test_silent_clients ("/ws/patient", SERVER_TEST_DRAIN_TIMEOUT, HTTP_WEB_SOCKET_ERROR_NONE);

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

// the pieces and the end of a thread that did not begin the stream are refused, the owner ends it
static void test_server_foreign_stream (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/echo", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "foreign-stream", 14);

	RawFrame frame;
	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the piece of the owner did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
	test_check_false (frame.fin);
	test_check ((frame.size == 5) && !memcmp (frame.data, "piece", 5), "a piece of another thread was sent");
	raw_frame_delete (&frame);

	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the end did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, NULL);
	test_check_true (frame.fin);
	raw_frame_delete (&frame);

	test_check_unsigned_eq (
		test_wait_counter (&counters.foreign, 1, TEST_WAIT_SECONDS),
		1, "the stream took the pieces of another thread"
	);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

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

	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "upgrade-data:chat:none", 22);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (1);

	// the headers that on_upgrade () added to the 101, after the ones of the handshake and in their order,
	// and the query of the request, that the connection keeps with its path
	const int second = raw_connect (server->port);
	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (
		raw_upgrade (second, "/ws/upgrade?room=7&name=a%20b", "Sec-WebSocket-Protocol: chat\r\n", response), 101, NULL
	);

	const char *protocol = strstr (response, "Sec-WebSocket-Protocol: chat\r\n");
	const char *first_cookie = strstr (response, "Set-Cookie: first=1; Path=/\r\n");
	const char *second_cookie = strstr (response, "Set-Cookie: second=2\r\n");
	const char *upgraded = strstr (response, "X-Upgraded: yes\r\n\r\n");
	test_check_ptr (protocol);
	test_check_ptr (first_cookie);
	test_check_ptr (second_cookie);
	test_check_ptr (upgraded);
	test_check ((protocol < first_cookie) && (first_cookie < second_cookie) && (second_cookie < upgraded), "the headers are not in order");

	test_check (strstr (response, "Upgrade: websocket\r\n") != NULL, "the upgrade of the handshake is missing");
	test_check (strstr (response, "h2c") == NULL, "a header of the handshake was replaced");
	test_check (strstr (response, "X-Other") == NULL, "a header was split in two");
	test_check (strstr (response, "Content-Length") == NULL, "the 101 has a length");

	raw_reader_init (&reader, second);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "upgrade-data:chat:room=7&name=a%20b", 35);

	raw_reader_end (&reader);
	(void) close (second);
	server_test_wait_closed (2);

	// a connection without on_upgrade () has the 101 of always
	const int plain = raw_connect (server->port);
	test_check_int_eq (raw_upgrade (plain, "/ws/echo", NULL, response), 101, NULL);
	test_check (strstr (response, "Set-Cookie") == NULL, "a 101 has the headers of another one");
	(void) close (plain);
	server_test_wait_closed (3);

	test_check_ptr_eq (http_web_socket_get_query (NULL), NULL);

	server_test_stop (server);

}

// the request of an upgrade with the headers of its handshake written one by one
#define SERVER_TEST_UPGRADE_REQUEST(headers)										\
	"GET /ws/echo HTTP/1.1\r\nHost: localhost\r\n"									\
	"Upgrade: websocket\r\nConnection: Upgrade\r\n" headers "\r\n"

#define SERVER_TEST_KEY_HEADER				"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
#define SERVER_TEST_VERSION_HEADER			"Sec-WebSocket-Version: 13\r\n"

// the frame that a connection sends when it opens, and its end
static void server_test_expect_first (
	const int fd, const char *text, const unsigned int closed
) {

	RawReader reader;
	raw_reader_init (&reader, fd);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, text, strlen (text));
	raw_reader_end (&reader);

	(void) close (fd);
	server_test_wait_closed (closed);

}

// every way of rejecting an upgrade gives its place back and is counted by its reason:
// a place that leaks in a rejection ends in a 503 for every upgrade, and the places were only checked
// after the limit; and what on_upgrade () decides when it accepts: no protocol and the grants of the request
static void test_server_upgrade_decisions (void) {

	ServerTest *server = server_test_start (0);
	const u16 port = server->port;

	// on_upgrade (): its status, the default one, its own response, and after it set the data of the connection
	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: yes\r\n"), 409, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: default\r\n"), 403, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: answer\r\n"), 404, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/upgrade", "X-Reject: data\r\n"), 403, NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.deleted, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the handshake: another version, no key, a key that is not one, a key that is not in its only form,
	// and a request without an upgrade to an endpoint that only takes connections
	test_check_unsigned_eq (
		server_test_request_status (port, SERVER_TEST_UPGRADE_REQUEST (SERVER_TEST_KEY_HEADER "Sec-WebSocket-Version: 12\r\n")), 426, NULL
	);

	test_check_unsigned_eq (server_test_request_status (port, SERVER_TEST_UPGRADE_REQUEST (SERVER_TEST_VERSION_HEADER)), 400, NULL);

	test_check_unsigned_eq (
		server_test_request_status (port, SERVER_TEST_UPGRADE_REQUEST ("Sec-WebSocket-Key: short\r\n" SERVER_TEST_VERSION_HEADER)), 400, NULL
	);

	test_check_unsigned_eq (
		server_test_request_status (
			port, SERVER_TEST_UPGRADE_REQUEST ("Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAB==\r\n" SERVER_TEST_VERSION_HEADER)
		), 400, NULL
	);

	test_check_unsigned_eq (server_test_request_status (port, "GET /ws/echo HTTP/1.1\r\nHost: localhost\r\n\r\n"), 426, NULL);

	// the request: its authentication and a before middleware that answers it
	test_check_unsigned_eq (server_test_status (port, "/ws/adopt", NULL), 401, NULL);
	test_check_unsigned_eq (server_test_status (port, "/ws/guarded", "X-Stop: yes\r\n"), 429, NULL);

	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_APPLICATION], 4, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_VERSION], 1, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST], 3, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_UPGRADE], 1, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_ORIGIN], 0, NULL);
	test_check_unsigned_eq (stats.n_rejected[HTTP_WEB_SOCKET_REJECTION_LIMIT], 0, NULL);
	test_check_unsigned_eq (stats.n_opened, 0, NULL);

	// none of them kept its place
	test_check_unsigned_eq (server_test_wait_reserved (server, 0), 0, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_count (server->http_cerver, NULL), 0, NULL);

	// the middleware lets the others continue
	const int guarded = raw_open (port, "/ws/guarded", NULL);
	(void) close (guarded);
	server_test_wait_closed (1);

	// on_upgrade () leaves the connection without a protocol, or asks for one that the client did not offer
	char response[RAW_RESPONSE_SIZE];
	const int none = raw_connect (port);
	test_check_int_eq (
		raw_upgrade (none, "/ws/upgrade", "Sec-WebSocket-Protocol: chat.v2, chat\r\nX-Protocol: none\r\n", response), 101, NULL
	);

	test_check (strstr (response, "Sec-WebSocket-Protocol") == NULL, "the 101 has a protocol that on_upgrade () removed");
	server_test_expect_first (none, "upgrade-data:none:none", 2);

	const int other = raw_connect (port);
	test_check_int_eq (
		raw_upgrade (other, "/ws/upgrade", "Sec-WebSocket-Protocol: chat.v2, chat\r\nX-Protocol: other\r\n", response), 101, NULL
	);

	test_check (strstr (response, "Sec-WebSocket-Protocol") == NULL, "the 101 has a protocol that the client did not offer");
	server_test_expect_first (other, "upgrade-data:none:none", 3);

	// the grants of the request are of the connection only when its upgrade adopts them
	const int adopted = raw_open (port, "/ws/adopt", "Authorization: secret\r\nX-Adopt: yes\r\n");
	server_test_expect_first (adopted, server_test_grants, 4);

	const int left = raw_open (port, "/ws/adopt", "Authorization: secret\r\n");
	server_test_expect_first (left, server_test_no_grants, 5);

	test_check_unsigned_eq (server_test_reserved (server), 0, NULL);

	server_test_stop (server);

}

// the next frame of the server that is not one of its pings, they are answered
static void server_test_read_frame (const int fd, RawReader *reader, RawFrame *frame) {

	bool ping = true;
	while (ping) {
		test_check (raw_read_frame (reader, frame, RAW_TIMEOUT), "the frame did not arrive");

		ping = (frame->opcode == HTTP_WEB_SOCKET_OPCODE_PING);
		if (ping) {
			raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PONG, frame->data, frame->size);
			raw_frame_delete (frame);
		}
	}

}

// the settings that the endpoint got when the cerver started
static void server_test_settings_resolved (void) {

	// the endpoint of the group has every setting of its group,
	// and the methods and the close timeout of the group above it
	const HttpWebSocketSettings *inherited = &server_test_inherited->resolved.web_socket;
	test_check (inherited->handlers.on_message == server_test_echo_handlers.on_message, "the methods are not the ones of the first group");
	test_check_unsigned_eq (inherited->max_message, SERVER_TEST_SMALL_MESSAGE, NULL);
	test_check_unsigned_eq (inherited->n_protocols, SERVER_TEST_N_PROTOCOLS, NULL);
	test_check_true (inherited->compression.enabled);
	test_check_int_eq (inherited->compression.level, SERVER_TEST_SETTINGS_LEVEL, NULL);
	test_check_unsigned_eq (inherited->compression.min_size, SERVER_TEST_SETTINGS_MIN_SIZE, NULL);
	test_check_unsigned_eq (inherited->origin_check, HTTP_WEB_SOCKET_ORIGIN_CHECK_REQUIRED, NULL);
	test_check_unsigned_eq (inherited->ping_interval, SERVER_TEST_PING_INTERVAL, NULL);
	test_check_unsigned_eq (inherited->pong_timeout, SERVER_TEST_PONG_TIMEOUT, NULL);
	test_check_unsigned_eq (inherited->send_timeout, SERVER_TEST_SEND_TIMEOUT, NULL);
	test_check_unsigned_eq (inherited->close_timeout, SERVER_TEST_CLOSE_TIMEOUT, NULL);
	test_check_unsigned_eq (inherited->max_queue, SERVER_TEST_SETTINGS_QUEUE, NULL);
	test_check_unsigned_eq (inherited->overflow, HTTP_WEB_SOCKET_OVERFLOW_CLOSE, NULL);

	// the endpoint of the subgroup: its own compression and close timeout, the limit and the origin check
	// of its subgroup, and the rest from the group
	const HttpWebSocketSettings *own = &server_test_own->resolved.web_socket;
	test_check (own->handlers.on_message == server_test_echo_handlers.on_message, "the methods are not the ones of the first group");
	test_check_unsigned_eq (own->max_message, SERVER_TEST_SETTINGS_MESSAGE, NULL);
	test_check_unsigned_eq (own->n_protocols, SERVER_TEST_N_PROTOCOLS, NULL);
	test_check_true (own->compression.enabled);
	test_check_int_eq (own->compression.level, SERVER_TEST_SETTINGS_OWN_LEVEL, NULL);
	test_check_unsigned_eq (own->origin_check, HTTP_WEB_SOCKET_ORIGIN_CHECK_NONE, NULL);
	test_check_unsigned_eq (own->ping_interval, SERVER_TEST_PING_INTERVAL, NULL);
	test_check_unsigned_eq (own->send_timeout, SERVER_TEST_SEND_TIMEOUT, NULL);
	test_check_unsigned_eq (own->close_timeout, SERVER_TEST_SETTINGS_CLOSE_TIMEOUT, NULL);
	test_check_unsigned_eq (own->max_queue, SERVER_TEST_SETTINGS_QUEUE, NULL);

}

// the settings of a group go to its subgroups and to its endpoints one by one: what a subgroup
// or an endpoint sets is its own, the rest is the one of the closest group that set it
// no test called the setters of the groups, nor the compression options and the close timeout of an endpoint
static void test_server_settings (void) {

	ServerTest *server = server_test_start (0);
	const u16 port = server->port;

	// the thread of the cerver resolved the settings when it started: they are read after one of its
	// connections opened, which orders the read after that thread wrote them (a connect alone does not)
	const int first = raw_open (port, "/ws/echo", NULL);
	test_check_unsigned_eq (test_wait_counter (&counters.opened, 1, TEST_WAIT_SECONDS), 1, NULL);
	(void) close (first);
	server_test_wait_closed (1);

	server_test_settings_resolved ();

	const size_t size = SERVER_TEST_SETTINGS_MESSAGE + 1;
	char *message = (char *) malloc (size);
	test_check_ptr (message);
	(void) memset (message, 'm', size);

	// in the connections of the group: its origin check, its protocols, its compression, its pings and its limit
	test_check_unsigned_eq (server_test_status (port, "/ws/settings/inherited", NULL), 403, NULL);

	char response[RAW_RESPONSE_SIZE];
	const int fd = raw_connect (port);
	test_check_int_eq (
		raw_upgrade (
			fd, "/ws/settings/inherited",
			"Origin: https://cerver.test\r\nSec-WebSocket-Protocol: chat\r\nSec-WebSocket-Extensions: permessage-deflate\r\n",
			response
		), 101, NULL
	);

	test_check (strstr (response, "Sec-WebSocket-Protocol: chat\r\n") != NULL, "the protocols of the group are missing");
	test_check (
		(strstr (response, "Sec-WebSocket-Extensions: permessage-deflate") != NULL) == http_web_socket_deflate_available (),
		"the compression is not the one of the group"
	);

	RawReader reader;
	raw_reader_init (&reader, fd);

	RawFrame frame;
	test_check (raw_read_frame (&reader, &frame, RAW_TIMEOUT), "the ping of the group did not arrive");
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_PING, NULL);
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PONG, frame.data, frame.size);
	raw_frame_delete (&frame);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_BINARY, message, SERVER_TEST_SMALL_MESSAGE + 1);
	server_test_read_frame (fd, &reader, &frame);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_CODE_MESSAGE_TOO_BIG, NULL);
	raw_frame_delete (&frame);

	raw_reader_end (&reader);
	(void) close (fd);
	server_test_wait_closed (2);

	// in the ones of the subgroup: no origin check, no compression, and a limit of its own
	const int plain = raw_connect (port);
	test_check_int_eq (
		raw_upgrade (plain, "/ws/settings/inner/plain", "Sec-WebSocket-Extensions: permessage-deflate\r\n", response), 101, NULL
	);

	test_check (strstr (response, "Sec-WebSocket-Extensions") == NULL, "the subgroup did not turn the compression off");

	raw_reader_init (&reader, plain);

	raw_send_frame (plain, true, HTTP_WEB_SOCKET_OPCODE_BINARY, message, SERVER_TEST_SMALL_MESSAGE + 1);
	server_test_read_frame (plain, &reader, &frame);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL);
	test_check_unsigned_eq (frame.size, SERVER_TEST_SMALL_MESSAGE + 1, NULL);
	raw_frame_delete (&frame);

	raw_send_frame (plain, true, HTTP_WEB_SOCKET_OPCODE_BINARY, message, size);
	server_test_read_frame (plain, &reader, &frame);
	test_check_int_eq (frame.opcode, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL);
	test_check_unsigned_eq (raw_close_code (&frame), HTTP_WEB_SOCKET_CLOSE_CODE_MESSAGE_TOO_BIG, NULL);
	raw_frame_delete (&frame);

	raw_reader_end (&reader);
	(void) close (plain);
	server_test_wait_closed (3);

	free (message);

	server_test_stop (server);

}

// on_ping () and on_pong () get the control frames of the peer with their data,
// after cerver answered the ping: they were NULL in every endpoint of the tests
static void test_server_control (void) {

	ServerTest *server = server_test_start (0);

	const int fd = raw_open (server->port, "/ws/control", NULL);
	RawReader reader;
	raw_reader_init (&reader, fd);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PING, server_test_ping_data, strlen (server_test_ping_data));
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_PONG, server_test_ping_data, strlen (server_test_ping_data));
	test_check_unsigned_eq (test_wait_counter (&counters.pings, 1, TEST_WAIT_SECONDS), 1, NULL);

	// a pong that nobody asked for is not answered
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_PONG, server_test_pong_data, strlen (server_test_pong_data));
	test_check_unsigned_eq (test_wait_counter (&counters.pongs, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the connection goes on, and its next frame is the echo
	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "still", 5);
	server_test_expect_frame (&reader, HTTP_WEB_SOCKET_OPCODE_TEXT, "still", 5);

	test_check_unsigned_eq (__atomic_load_n (&counters.pings, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&counters.pongs, __ATOMIC_SEQ_CST), 1, NULL);

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
static void server_test_connection_stats (HttpWebSocket *web_socket, void *args) {

	test_check_unsigned_eq (http_web_socket_get_stats (web_socket, (HttpWebSocketStats *) args), 0, NULL);

}

// the server counts the bytes of a message after it wrote its last frame,
// so a client that read that frame can ask for the metrics before the count
static void server_test_wait_sent_bytes (const ServerTest *server, const uint64_t expected) {

	(void) test_wait_counter_64 (&server->http_cerver->web_sockets->n_bytes_sent, expected, TEST_WAIT_SECONDS);

}

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

	server_test_wait_sent_bytes (server, 32);

	metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check (strstr (metrics->str, "cerver_websocket_messages_sent_total 2\n") != NULL, "the sent messages are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_sent_bytes_total 32\n") != NULL, "the sent bytes are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_received_bytes_total 13\n") != NULL, "the received bytes are wrong");
	str_delete (metrics);

	// the same values without the text of the metrics, of the cerver and of its only connection
	HttpWebSocketStats stats;
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_connections, 1, NULL);
	test_check_unsigned_eq (stats.n_opened, 1, NULL);
	test_check_unsigned_eq (stats.n_closed, 0, NULL);
	test_check_unsigned_eq (stats.n_messages_received, 2, NULL);
	test_check_unsigned_eq (stats.n_messages_sent, 2, NULL);
	test_check_unsigned_eq (stats.n_bytes_received, 13, NULL);
	test_check_unsigned_eq (stats.n_bytes_sent, 32, NULL);

	HttpWebSocketStats connection_stats;
	(void) memset (&connection_stats, 0xFF, sizeof (HttpWebSocketStats));
	http_cerver_web_socket_for_each (server->http_cerver, NULL, server_test_connection_stats, &connection_stats);
	test_check_unsigned_eq (connection_stats.n_messages_received, 2, NULL);
	test_check_unsigned_eq (connection_stats.n_messages_sent, 2, NULL);
	test_check_unsigned_eq (connection_stats.n_bytes_received, 13, NULL);
	test_check_unsigned_eq (connection_stats.n_bytes_sent, 32, NULL);
	test_check_unsigned_eq (connection_stats.n_connections, 0, NULL);
	test_check_unsigned_eq (connection_stats.n_opened, 0, NULL);

	test_check_unsigned_eq (http_cerver_web_socket_get_stats (NULL, &stats), 1, NULL);
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, NULL), 1, NULL);
	test_check_unsigned_eq (http_web_socket_get_stats (NULL, &stats), 1, NULL);

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

	server_test_wait_sent_bytes (server, 35);

	metrics = http_cerver_metrics_generate (server->http_cerver);
	test_check (strstr (metrics->str, "cerver_websocket_messages_received_total 3\n") != NULL, "the fragmented message was not counted");
	test_check (strstr (metrics->str, "cerver_websocket_received_bytes_total 21\n") != NULL, "the bytes of the fragments are wrong");
	test_check (strstr (metrics->str, "cerver_websocket_sent_bytes_total 35\n") != NULL, "the bytes of the answer are wrong");
	str_delete (metrics);

	raw_reader_end (&reader);
	(void) close (stream);
	server_test_wait_closed (2);

	// a close with 1000 and a client that left without one (1006)
	test_check_unsigned_eq (http_cerver_web_socket_get_stats (server->http_cerver, &stats), 0, NULL);
	test_check_unsigned_eq (stats.n_connections, 0, NULL);
	test_check_unsigned_eq (stats.n_opened, 2, NULL);
	test_check_unsigned_eq (stats.n_closed, 2, NULL);
	test_check_unsigned_eq (stats.n_messages_received, 3, NULL);
	test_check_unsigned_eq (stats.n_bytes_received, 21, NULL);
	test_check_unsigned_eq (stats.n_bytes_sent, 35, NULL);
	test_check_unsigned_eq (stats.n_closes[HTTP_WEB_SOCKET_CLOSE_CODE_NORMAL - HTTP_WEB_SOCKET_CLOSE_CODES_FIRST], 1, NULL);
	test_check_unsigned_eq (stats.n_closes[HTTP_WEB_SOCKET_CLOSE_CODE_ABNORMAL - HTTP_WEB_SOCKET_CLOSE_CODES_FIRST], 1, NULL);
	test_check_unsigned_eq (stats.n_closes_other, 0, NULL);

	for (unsigned int i = 0; i < HTTP_WEB_SOCKET_REJECTIONS; i++) {
		test_check_unsigned_eq (stats.n_rejected[i], 0, NULL);
	}

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
	test_run (test_server_owner);
	test_run (test_server_written_while_blocked);
	test_run (test_server_queue_drop);
	test_run (test_server_queue_close);
	test_run (test_server_queue_drained_in_job);
	test_run (test_server_rooms);
	test_run (test_server_slow_reader);
	test_run (test_server_limit);
	test_run (test_server_shutdown);
	test_run (test_server_shutdown_silent);
	test_run (test_server_streaming);
	test_run (test_server_foreign_stream);
	test_run (test_server_refused_sends);
	test_run (test_server_upgrade_data);
	test_run (test_server_upgrade_decisions);
	test_run (test_server_settings);
	test_run (test_server_control);
	test_run (test_server_compression);
	test_run (test_server_metrics);

	(void) alarm (0);

	(void) printf ("Done!\n");

}
