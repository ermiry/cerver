#include <stddef.h>
#include <stdint.h>

#include <cerver/types/string.h>

#include <cerver/collections/arena.h>

#include <cerver/connection/end.h>

#include "cerver/http/core/handler.h"
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/parser/parser.h>

#include "fuzz.h"

// raw bytes to a connection that is kept: when a request ends, its receive is reset
// and the bytes that it left are the start of the next request, so the state of a request
// (its headers, its body, its uploads, its parser) must never reach the next one
// the receive uses the arena and the buffers that a worker thread lends to it
// the first byte selects the size of the pieces the bytes are split in

#define PIPELINE_REQUESTS_MAX			64

#define PIPELINE_BUFFERS				1
#define PIPELINE_HEAD_BUFFER			0

static Arena *pipeline_arena = NULL;
static String *pipeline_buffers[PIPELINE_BUFFERS] = { NULL };

// the receive is not in the arena of its requests
static HttpReceive pipeline_receive;

static void pipeline_resources_create (void) {

	if (pipeline_arena) return;

	pipeline_arena = arena_create (ARENA_DEFAULT_CHUNK_SIZE);
	if (!pipeline_arena) abort ();

	for (unsigned int idx = 0; idx < PIPELINE_BUFFERS; idx++) {
		pipeline_buffers[idx] = str_new (NULL);
		if (!pipeline_buffers[idx]) abort ();
	}

}

// the responses are read without waiting, the connection stays open
static void pipeline_drain_available (const int fd) {

	char response[FUZZ_RESPONSE_BUFFER_SIZE];
	while (recv (fd, response, FUZZ_RESPONSE_BUFFER_SIZE, MSG_DONTWAIT) > 0);

}

// the request stopped: it was completed (it is paused), it was rejected or the parser failed
static bool pipeline_request_ended (const HttpReceive *http_receive) {

	const HttpReceiveStatus status = http_receive->receive_status;

	return http_receive->responded
		|| (cerver_http_parser_get_error (&http_receive->parser) != CERVER_HTTP_PARSER_ERROR_NONE)
		|| ((status != HTTP_RECEIVE_STATUS_NONE) && (status != HTTP_RECEIVE_STATUS_HEADERS) && (status != HTTP_RECEIVE_STATUS_BODY));

}

static void pipeline_feed (
	const uint8_t *data, const size_t data_len, const size_t piece_len
) {

	int fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, fds)) return;

	Connection *connection = fuzz_connection_create (fds[FUZZ_SERVER]);

	CerverReceive cerver_receive;
	(void) memset (&cerver_receive, 0, sizeof (CerverReceive));
	cerver_receive.cerver = fuzz_cerver;
	cerver_receive.socket = connection->socket;
	cerver_receive.connection = connection;

	const HttpReceiveResources resources = {
		pipeline_arena, pipeline_buffers[PIPELINE_HEAD_BUFFER]
	};

	HttpReceive *http_receive = &pipeline_receive;
	if (http_receive_prepare (http_receive, &cerver_receive, &resources)) abort ();

	// a request that is rejected does not close the connection either:
	// the receive is used again after every kind of request
	http_receive->keep_alive = true;

	const size_t step = piece_len ? piece_len : data_len;

	size_t start = 0;
	unsigned int n_requests = 0;
	while (
		(start < data_len) && (n_requests < PIPELINE_REQUESTS_MAX)
		&& (connection->socket->sock_fd != -1) && !http_receive->upgraded
	) {
		const size_t left = data_len - start;
		const size_t piece = (left < step) ? left : step;

		fuzz_feed_piece (http_receive, data + start, piece);
		start += piece;

		if (pipeline_request_ended (http_receive)) {
			// the bytes of the piece after the request that ended
			if (http_receive->unparsed <= piece) start -= http_receive->unparsed;

			pipeline_drain_available (fds[FUZZ_CLIENT]);

			if (http_receive_reset (http_receive)) abort ();

			http_receive->keep_alive = true;

			n_requests += 1;
		}
	}

	if (connection->socket->sock_fd != -1) connection_end (connection);

	(void) shutdown (fds[FUZZ_CLIENT], SHUT_WR);
	fuzz_drain (fds[FUZZ_CLIENT]);

	http_receive_delete (http_receive);
	arena_reset (pipeline_arena);

	connection_delete (connection);

	(void) close (fds[FUZZ_CLIENT]);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < 1) return 0;

	fuzz_cerver_create ();
	pipeline_resources_create ();

	// the cerver keeps the connection after each request that it completes
	cerver_http_set_keep_alive ((HttpCerver *) fuzz_cerver->cerver_data, true);

	pipeline_feed (data + 1, size - 1, (size_t) data[0]);

	return 0;

}
