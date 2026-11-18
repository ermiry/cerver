#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cerver/core/cerver.h>
#include <cerver/events/events.h>
#include "cerver/events/internal.h"

#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>

#include "../handler.h"
#include "fuzz.h"

// raw bytes to a connection of an event loop: the loop reads them from its socket in pieces,
// parses them and gives the request to its dispatcher, that runs its jobs in the same thread
// (the authentication of a request before its body, its handler), so every run is the same
// the requests reach the pauses of the parser, the bytes that are kept between them,
// the responses that the loop writes and the closes of each state
// the cerver keeps its connections: the requests that follow one that was answered are handled too,
// the ones that arrived behind it (pipelined) and the ones that an idle connection receives
// the first byte selects the size of the pieces the bytes are split in, and whether the handlers run in
// the loop itself (its pool, where the requests of a pipeline are handled in a row) or through the dispatcher
// the backend of the loop is the one that CERVER_TEST_BACKEND asks for (test/handler.h)

// the steps that the loop takes at most to end a connection whose client is done,
// and some more for each byte: every pipelined request that is left takes them
#define LOOP_STEPS_MAX					64
#define LOOP_STEPS_PER_BYTE				4

// the bit of the first byte that runs the handlers in the loop, the others are the size of the pieces
#define LOOP_HANDLERS_BIT				0x80
#define LOOP_PIECE_MASK					0x7F

static EventDispatcher loop_dispatcher;
static EventLoop *loop_events = NULL;

// the cerver is the one of the other targets, without its WebSocket endpoint:
// an upgraded connection of an event loop starts the thread of its WebSocket
static void loop_create (void) {

	if (loop_events) return;

	fuzz_web_sockets = false;

	fuzz_cerver_create ();

	// a response that does not fit in the socket fails instead of waiting for its client
	cerver_set_send_timeout (fuzz_cerver, FUZZ_SEND_TIMEOUT_SECONDS);

	http_cerver_set_keep_alive ((HttpCerver *) fuzz_cerver->cerver_data, true);

	cerver_set_events_backend (fuzz_cerver, test_events_backend ());

	event_dispatcher_inline (&loop_dispatcher);

	loop_events = event_loop_create (fuzz_cerver, &loop_dispatcher);
	if (!loop_events) abort ();

}

// the responses are read without waiting, so the cerver never finds the socket full
static void loop_drain_available (const int fd) {

	char response[FUZZ_RESPONSE_BUFFER_SIZE];
	while (recv (fd, response, FUZZ_RESPONSE_BUFFER_SIZE, MSG_DONTWAIT) > 0);

}

// a piece is one read of the loop: it is sent whole and the loop takes its steps
// returns false when the connection ended, the rest of the bytes is not sent
static bool loop_feed_piece (const int fd, const uint8_t *piece, const size_t piece_len) {

	size_t sent = 0;
	bool open = true;

	while ((sent < piece_len) && open) {
		const ssize_t result = send (fd, piece + sent, piece_len - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (result > 0) sent += (size_t) result;

		(void) event_loop_step (loop_events, 0);

		loop_drain_available (fd);

		open = (event_loop_connections (loop_events) > 0);
	}

	return open;

}

static void loop_feed (
	const uint8_t *data, const size_t data_len, const size_t piece_len
) {

	int fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds)) return;

	struct sockaddr_storage address;
	(void) memset (&address, 0, sizeof (struct sockaddr_storage));

	if (event_loop_adopt (loop_events, fds[FUZZ_SERVER], &address)) abort ();

	const size_t step = piece_len ? piece_len : data_len;

	bool open = true;
	for (size_t start = 0; (start < data_len) && open; start += step) {
		const size_t left = data_len - start;

		open = loop_feed_piece (fds[FUZZ_CLIENT], data + start, (left < step) ? left : step);
	}

	// the client is done: a request that is not complete ends with it,
	// and a connection that reads what is left of a rejected request finds its end
	(void) shutdown (fds[FUZZ_CLIENT], SHUT_WR);

	for (
		size_t steps = 0;
		event_loop_connections (loop_events) && (steps < (LOOP_STEPS_MAX + (data_len * LOOP_STEPS_PER_BYTE)));
		steps++
	) {
		(void) event_loop_step (loop_events, 0);

		loop_drain_available (fds[FUZZ_CLIENT]);
	}

	// a connection that is still there would only end with its timers: a state without an exit
	if (event_loop_connections (loop_events)) abort ();

	(void) close (fds[FUZZ_CLIENT]);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < 1) return 0;

	loop_create ();

	// each input chooses where its handlers run: the cerver reads its pool with each request
	(void) http_cerver_set_handlers_pool (
		(HttpCerver *) fuzz_cerver->cerver_data,
		(data[0] & LOOP_HANDLERS_BIT) ? HTTP_POOL_LOOP_NAME : NULL
	);

	loop_feed (data + 1, size - 1, (size_t) (data[0] & LOOP_PIECE_MASK));

	return 0;

}
