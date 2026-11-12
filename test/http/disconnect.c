#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <pthread.h>

#include <cerver/cerver.h>
#include <cerver/files.h>
#include <cerver/network.h>

#include <cerver/types/string.h>

#include <cerver/threads/jobs.h>
#include <cerver/threads/thpool.h>

#include <cerver/http/auth.h>
#include <cerver/http/http.h>
#include <cerver/http/metrics.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>
#include <cerver/http/route.h>
#include <cerver/http/stream.h>

#include "../test.h"

#include "http.h"

// a client that leaves in every part of the HTTP process: at each byte of a request,
// while its handler runs, while it waits in the queue and in the middle of its response,
// in the four ways a client can leave: a close (FIN), a reset (RST),
// a half-close (shutdown (SHUT_WR)) and vanishing (it stops sending and reading)
// after each case the cerver must answer a new request, its fds must be the ones it had,
// its uploads must be empty and its counters must have changed by exactly what is expected

// bugs that this suite pinned:
// - every client that left was counted with the parser errors (n_incompleted_requests)
// - a response that could not be sent was counted as a completed request with status 0
// - a stream whose write failed kept sending its pieces and its last chunk
// - the files and the directory of an incomplete multi-part request stayed in the uploads path
// - a "100 Continue" that could not be sent was ignored
// - a 408 was counted as a timeout and as an incompleted request
// coverage holes:
// - a handler can not be cancelled when its client leaves, it can only ask for its state

#define DISCONNECT_PORT_BASE				(10240)

#define DISCONNECT_THREADS					16
#define DISCONNECT_QUEUE_SIZE				1024

// milliseconds, a client that stops sending is ended by the idle timeout
#define DISCONNECT_IDLE_TIMEOUT				200
#define DISCONNECT_HEADERS_TIMEOUT			8000

// secs, a client that stops reading its response
#define DISCONNECT_SEND_TIMEOUT				1

// valgrind runs one thread at a time, so a client that reads a large response
// can wait longer than the send timeout for its turn
#define DISCONNECT_VALGRIND_SEND_TIMEOUT	16
#define DISCONNECT_PRELOAD_VARIABLE			"LD_PRELOAD"
#define DISCONNECT_VALGRIND_PRELOAD			"vgpreload"

// the cases that run at the same time, each one with a thread of the cerver,
// so none waits in the queue longer than the idle timeout
#define DISCONNECT_BATCH_SIZE				DISCONNECT_THREADS

// secs to wait for the cerver to take or to end a batch
#define DISCONNECT_WAIT_SECONDS				64

// under valgrind a client that disappears keeps its sends blocked for the longer send timeout,
// so a batch takes longer to end in a slow runner
#define DISCONNECT_VALGRIND_WAIT_FACTOR		4

// secs that a client waits for a response
#define DISCONNECT_CLIENT_TIMEOUT			8

#define DISCONNECT_POLL_NANOSECONDS			1000000L
#define DISCONNECT_NANOSECONDS				1000000000L
#define DISCONNECT_MILLISECONDS_PER_SECOND	1000

// milliseconds that the gone handler waits for its client to leave
#define DISCONNECT_GONE_WAIT				1000

#define DISCONNECT_RESPONSE_SIZE			(1 << 12)
// a request with a bearer of up to HTTP_JWT_BEARER_SIZE
#define DISCONNECT_REQUEST_SIZE				(1 << 14)
#define DISCONNECT_PATH_SIZE				(1 << 10)

// the large responses do not fit in the socket buffers,
// so a client that leaves in the middle always makes them fail
#define DISCONNECT_LARGE_SIZE				(1 << 22)
#define DISCONNECT_STREAM_PIECE_SIZE		(1 << 16)
#define DISCONNECT_SMALL_BUFFER				(1 << 12)
#define DISCONNECT_READ_BUFFER				(1 << 16)

// the bytes that a client reads from a large response before it leaves
static const size_t disconnect_response_cuts[] = {
	1, 1 << 8, 1 << 12, (1 << 12) + 1, 1 << 16, 1 << 20
};

static const size_t disconnect_n_response_cuts = sizeof (disconnect_response_cuts) / sizeof (size_t);

// the offsets of a request that the sample adds to its stage boundaries
#define DISCONNECT_SAMPLE_RANDOM_OFFSETS	4

#define DISCONNECT_LEVEL_VARIABLE			"DISCONNECT_LEVEL"
#define DISCONNECT_SEED_VARIABLE			"DISCONNECT_SEED"
#define DISCONNECT_LEVEL_FULL				"full"

#define DISCONNECT_ROOT_PATH				"/tmp/cerver-test-disconnect"
#define DISCONNECT_UPLOADS_PATH				DISCONNECT_ROOT_PATH "/uploads"
#define DISCONNECT_PUBLIC_PATH				DISCONNECT_ROOT_PATH "/public"
#define DISCONNECT_PUBLIC_FILE				DISCONNECT_PUBLIC_PATH "/index.html"
#define DISCONNECT_LARGE_FILE				DISCONNECT_ROOT_PATH "/large.mp4"

#define DISCONNECT_FD_DIRECTORY				"/proc/self/fd"

#define DISCONNECT_BOUNDARY					"disconnectboundary"
#define DISCONNECT_DELIMITER				"\r\n--" DISCONNECT_BOUNDARY

#define DISCONNECT_MANY_HEADERS				32

#define DISCONNECT_CONTINUE_RESPONSE		"HTTP/1.1 100 Continue\r\n\r\n"

#define DISCONNECT_UPLOADS_MAX				2

// the least that a client reads of the compressed hex body, zlib saves about half of it
#define DISCONNECT_GZIP_LEAST_SIZE			(DISCONNECT_LARGE_SIZE / 4)

#define DISCONNECT_HEX_SEED					0x5eed
#define DISCONNECT_HEX_DIGITS				16

static const char disconnect_hex_digits[] = { "0123456789abcdef" };

static const char *disconnect_public_content = { "<html>disconnect</html>" };

static const char *disconnect_ok_status = { "HTTP/1.1 200" };

#pragma region forms

// the ways in which a client leaves
typedef enum DisconnectForm {

	DISCONNECT_FORM_CLOSE		= 0,
	DISCONNECT_FORM_RESET		= 1,
	DISCONNECT_FORM_HALF_CLOSE	= 2,
	DISCONNECT_FORM_VANISH		= 3,

	DISCONNECT_FORMS			= 4

} DisconnectForm;

static const char *disconnect_form_names[DISCONNECT_FORMS] = {
	"close", "reset", "half-close", "vanish"
};

#pragma endregion

#pragma region counters

// the counters of the cerver that a case changes
typedef struct DisconnectCounters {

	size_t aborted[HTTP_STAGES][HTTP_ABORT_REASONS];
	size_t vanished[HTTP_STAGES];

	size_t failed_responses;
	size_t incompleted;
	size_t timeout;
	size_t unanswered;

	size_t ok_responses;
	size_t timeout_responses;

	// the responses of the status of disconnect_other_status
	size_t other_responses;

	unsigned int handled;
	unsigned int before_middlewares;

	// the cases that either completed or failed their response,
	// a reset that races with the response of a complete request
	unsigned int either;

	// the cases whose 200 is counted with the header, and whose body may fail after it,
	// a client that leaves right after a request answered with many sends
	unsigned int maybe_failed;

	// the races of a request rejected before its body (a 401, a 413) end in the body stage
	bool races_in_body;

} DisconnectCounters;

static unsigned int disconnect_handled = 0;
static unsigned int disconnect_before_middlewares = 0;

// the status that a batch of the error responses counts, 0 (never sent) in any other batch
static http_status disconnect_other_status = HTTP_STATUS_NONE;

static void disconnect_counters_read (const HttpCerver *http_cerver, DisconnectCounters *counters) {

	(void) memset (counters, 0, sizeof (DisconnectCounters));

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			counters->aborted[stage][reason] = CERVER_STATS_GET (http_cerver->n_aborted_requests[stage][reason]);
		}

		counters->vanished[stage] = CERVER_STATS_GET (http_cerver->n_vanished_requests[stage]);
	}

	counters->failed_responses = CERVER_STATS_GET (http_cerver->n_failed_responses);
	counters->incompleted = CERVER_STATS_GET (http_cerver->n_incompleted_requests);
	counters->timeout = CERVER_STATS_GET (http_cerver->n_timeout_requests);
	counters->unanswered = CERVER_STATS_GET (http_cerver->n_unanswered_requests);

	counters->ok_responses = CERVER_STATS_GET (http_cerver->n_responses_by_status[HTTP_STATUS_OK]);
	counters->timeout_responses = CERVER_STATS_GET (http_cerver->n_responses_by_status[HTTP_STATUS_REQUEST_TIMEOUT]);
	counters->other_responses = CERVER_STATS_GET (http_cerver->n_responses_by_status[disconnect_other_status]);

	counters->handled = __atomic_load_n (&disconnect_handled, __ATOMIC_SEQ_CST);
	counters->before_middlewares = __atomic_load_n (&disconnect_before_middlewares, __ATOMIC_SEQ_CST);

}

// the change between two readings
static void disconnect_counters_delta (
	const DisconnectCounters *before, const DisconnectCounters *after, DisconnectCounters *delta
) {

	(void) memset (delta, 0, sizeof (DisconnectCounters));

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			delta->aborted[stage][reason] = after->aborted[stage][reason] - before->aborted[stage][reason];
		}

		delta->vanished[stage] = after->vanished[stage] - before->vanished[stage];
	}

	delta->failed_responses = after->failed_responses - before->failed_responses;
	delta->incompleted = after->incompleted - before->incompleted;
	delta->timeout = after->timeout - before->timeout;
	delta->unanswered = after->unanswered - before->unanswered;

	delta->ok_responses = after->ok_responses - before->ok_responses;
	delta->timeout_responses = after->timeout_responses - before->timeout_responses;
	delta->other_responses = after->other_responses - before->other_responses;

	delta->handled = after->handled - before->handled;
	delta->before_middlewares = after->before_middlewares - before->before_middlewares;

}

static void disconnect_counters_print (const char *title, const DisconnectCounters *counters) {

	(void) fprintf (stderr, "\t%s:\n", title);

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			if (counters->aborted[stage][reason]) {
				(void) fprintf (
					stderr, "\t\taborted %s %s: %zu\n",
					http_stage_label ((HttpStage) stage), http_abort_reason_label ((HttpAbortReason) reason),
					counters->aborted[stage][reason]
				);
			}
		}

		if (counters->vanished[stage]) {
			(void) fprintf (
				stderr, "\t\tvanished %s: %zu\n",
				http_stage_label ((HttpStage) stage), counters->vanished[stage]
			);
		}
	}

	(void) fprintf (
		stderr,
		"\t\tfailed responses: %zu - incompleted: %zu - timeout: %zu - unanswered: %zu\n"
		"\t\t200: %zu - 408: %zu - %d: %zu - handled: %u - before middlewares: %u - either: %u - maybe failed: %u\n",
		counters->failed_responses, counters->incompleted, counters->timeout, counters->unanswered,
		counters->ok_responses, counters->timeout_responses,
		(int) disconnect_other_status, counters->other_responses,
		counters->handled, counters->before_middlewares, counters->either, counters->maybe_failed
	);

}

// a reset that races with the response is either a 200 or a failed response,
// and a response sent in many pieces may fail after its 200 was counted,
// the failures are where the client left while the handler or the response ran
static bool disconnect_counters_either_match (
	const DisconnectCounters *expected, const DisconnectCounters *delta
) {

	const size_t reset = HTTP_ABORT_REASON_RESET;
	const size_t body = expected->races_in_body ? 1 : 0;

	const size_t expected_gone = expected->aborted[HTTP_STAGE_HANDLER][reset]
		+ expected->aborted[HTTP_STAGE_RESPONSE][reset]
		+ (body * expected->aborted[HTTP_STAGE_BODY][reset]);

	const size_t gone = delta->aborted[HTTP_STAGE_HANDLER][reset]
		+ delta->aborted[HTTP_STAGE_RESPONSE][reset]
		+ (body * delta->aborted[HTTP_STAGE_BODY][reset]);

	// a response that completed is a 200 or the status of the batch
	const size_t expected_completed = expected->ok_responses + expected->other_responses;
	const size_t completed = delta->ok_responses + delta->other_responses;

	bool match = (delta->failed_responses >= expected->failed_responses) && (completed >= expected_completed);

	if (match) {
		const size_t failed = delta->failed_responses - expected->failed_responses;
		const size_t ok = completed - expected_completed;

		// the either cases that did not complete failed, the rest of the failures are the maybe ones
		match = (ok <= (size_t) expected->either) && (gone == (expected_gone + failed));
		if (match) {
			const size_t either_failed = (size_t) expected->either - ok;

			match = (failed >= either_failed) && ((failed - either_failed) <= (size_t) expected->maybe_failed);
		}
	}

	return match;

}

// the counters that a race can not change are compared exactly
static bool disconnect_counters_match (
	const DisconnectCounters *expected, const DisconnectCounters *delta
) {

	bool match = disconnect_counters_either_match (expected, delta);

	for (unsigned int stage = 0; match && (stage < HTTP_STAGES); stage++) {
		for (unsigned int reason = 0; match && (reason < HTTP_ABORT_REASONS); reason++) {
			const bool races = (expected->either || expected->maybe_failed)
				&& (reason == HTTP_ABORT_REASON_RESET)
				&& (
					(stage == HTTP_STAGE_HANDLER) || (stage == HTTP_STAGE_RESPONSE)
					|| (expected->races_in_body && (stage == HTTP_STAGE_BODY))
				);

			if (!races) match = (delta->aborted[stage][reason] == expected->aborted[stage][reason]);
		}

		if (match) match = (delta->vanished[stage] == expected->vanished[stage]);
	}

	// a batch of another status never answers with 200,
	// and a batch without races has exactly the responses that it expects
	const bool exact = !expected->either && !expected->maybe_failed;
	const bool statuses = (disconnect_other_status == HTTP_STATUS_NONE)
		|| (delta->ok_responses == expected->ok_responses);

	return match && statuses
		&& (!exact || ((delta->ok_responses == expected->ok_responses) && (delta->other_responses == expected->other_responses)))
		&& (delta->incompleted == expected->incompleted)
		&& (delta->timeout == expected->timeout)
		&& (delta->unanswered == expected->unanswered)
		&& (delta->timeout_responses == expected->timeout_responses)
		&& (delta->handled == expected->handled)
		&& (delta->before_middlewares == expected->before_middlewares);

}

#pragma endregion

#pragma region models

// a request that the clients send, with the offsets where its stages change
typedef struct DisconnectModel {

	const char *name;

	char request[DISCONNECT_REQUEST_SIZE];
	size_t length;

	// the offset after the request line and after the headers
	size_t request_line_end;
	size_t headers_end;

	// the files of a multi-part body: the offset of their first byte
	// and the offset after the delimiter that ends them
	unsigned int n_uploads;
	size_t upload_start[DISCONNECT_UPLOADS_MAX];
	size_t upload_end[DISCONNECT_UPLOADS_MAX];

	// the client waits for "100 Continue" before it sends the body
	bool expect_continue;

	// the complete request is answered with 200 by a handler of the suite
	bool handled;

	// the response is sent in many pieces (a header and then a file)
	bool many_sends;

} DisconnectModel;

#define DISCONNECT_MODELS					8

static DisconnectModel disconnect_models[DISCONNECT_MODELS];

static const char *disconnect_body = { "0123456789abcdef0123456789abcdef" };

static size_t disconnect_offset_after (const char *request, const char *start, const char *marker) {

	const char *found = strstr (start, marker);
	test_check (found != NULL, "the marker of a model was not found");

	return (size_t) (found - request) + strlen (marker);

}

// finds the stage boundaries of the model from its bytes
static void disconnect_model_markers (DisconnectModel *model) {

	model->length = strlen (model->request);

	model->request_line_end = disconnect_offset_after (model->request, model->request, "\r\n");
	model->headers_end = disconnect_offset_after (model->request, model->request, "\r\n\r\n");

	model->n_uploads = 0;
	const char *walk = model->request + model->headers_end;
	const char *file = NULL;
	while ((model->n_uploads < DISCONNECT_UPLOADS_MAX) && (file = strstr (walk, "filename=")) != NULL) {
		const size_t start = disconnect_offset_after (model->request, file, "\r\n\r\n");
		const size_t end = disconnect_offset_after (model->request, model->request + start, DISCONNECT_DELIMITER);

		model->upload_start[model->n_uploads] = start;
		model->upload_end[model->n_uploads] = end;
		model->n_uploads += 1;

		walk = model->request + end;
	}

}

static void disconnect_model_set (
	DisconnectModel *model, const char *name,
	const char *request, const bool expect_continue
) {

	model->name = name;
	(void) snprintf (model->request, DISCONNECT_REQUEST_SIZE, "%s", request);
	model->expect_continue = expect_continue;
	model->handled = true;
	model->many_sends = false;

	disconnect_model_markers (model);

}

static void disconnect_model_many_headers (DisconnectModel *model) {

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	int length = snprintf (request, DISCONNECT_REQUEST_SIZE, "GET /ok HTTP/1.1\r\nHost: localhost\r\n");

	for (unsigned int i = 0; i < DISCONNECT_MANY_HEADERS; i++) {
		length += snprintf (
			request + length, (size_t) (DISCONNECT_REQUEST_SIZE - length),
			"X-Disconnect-%02u: value-%02u\r\n", i, i
		);
	}

	(void) snprintf (request + length, (size_t) (DISCONNECT_REQUEST_SIZE - length), "\r\n");

	disconnect_model_set (model, "many headers", request, false);

}

static void disconnect_model_multipart (DisconnectModel *model) {

	const char *body = {
		"--" DISCONNECT_BOUNDARY "\r\n"
		"Content-Disposition: form-data; name=\"first\"; filename=\"first.txt\"\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"first-file-content"
		DISCONNECT_DELIMITER "\r\n"
		"Content-Disposition: form-data; name=\"second\"; filename=\"second.txt\"\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"second-file-content"
		DISCONNECT_DELIMITER "--\r\n"
	};

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"POST /upload HTTP/1.1\r\n"
		"Host: localhost\r\n"
		"Content-Type: multipart/form-data; boundary=" DISCONNECT_BOUNDARY "\r\n"
		"Content-Length: %zu\r\n"
		"\r\n"
		"%s",
		strlen (body), body
	);

	disconnect_model_set (model, "multi-part", request, false);

	test_check_unsigned_eq (model->n_uploads, DISCONNECT_UPLOADS_MAX, NULL);

}

static void disconnect_model_bearer (DisconnectModel *model, HttpCerver *http_cerver) {

	HttpJwt *http_jwt = http_cerver_auth_jwt_new ();
	test_check_ptr (http_jwt);

	http_cerver_auth_jwt_add_value (http_jwt, "id", "disconnect-test");
	http_cerver_auth_jwt_add_value_int (http_jwt, "exp", (int64_t) (time (NULL) + DISCONNECT_WAIT_SECONDS * DISCONNECT_WAIT_SECONDS));

	test_check_unsigned_eq (http_cerver_auth_generate_bearer_jwt (http_cerver, http_jwt), 0, NULL);

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"GET /private HTTP/1.1\r\nHost: localhost\r\nAuthorization: %s\r\n\r\n",
		http_jwt->bearer
	);

	http_cerver_auth_jwt_delete (http_jwt);

	disconnect_model_set (model, "bearer", request, false);

}

static void disconnect_models_create (HttpCerver *http_cerver) {

	disconnect_model_set (
		&disconnect_models[0], "get",
		"GET /ok HTTP/1.1\r\nHost: localhost\r\nUser-Agent: disconnect\r\n\r\n", false
	);

	disconnect_model_many_headers (&disconnect_models[1]);

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s",
		strlen (disconnect_body), disconnect_body
	);

	disconnect_model_set (&disconnect_models[2], "content length", request, false);

	disconnect_model_set (
		&disconnect_models[3], "chunked",
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
		"10\r\n0123456789abcdef\r\n10\r\nfedcba9876543210\r\n0\r\n\r\n",
		false
	);

	disconnect_model_multipart (&disconnect_models[4]);

	(void) snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: %zu\r\n\r\n%s",
		strlen (disconnect_body), disconnect_body
	);

	disconnect_model_set (&disconnect_models[5], "expect continue", request, true);

	disconnect_model_bearer (&disconnect_models[6], http_cerver);

	disconnect_model_set (
		&disconnect_models[7], "static file",
		"GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n", false
	);

	// a static file is not answered by a handler of the suite,
	// its header and its file are sent with different calls
	disconnect_models[7].handled = false;
	disconnect_models[7].many_sends = true;

}

// the stage where the cerver is after receiving the first offset bytes of an incomplete request
static HttpStage disconnect_model_stage (const DisconnectModel *model, const size_t offset) {

	HttpStage stage = HTTP_STAGE_BODY;

	if (!offset) stage = HTTP_STAGE_QUEUED;
	else if (offset <= model->request_line_end) stage = HTTP_STAGE_REQUEST_LINE;
	else if (offset < model->headers_end) stage = HTTP_STAGE_HEADERS;

	for (unsigned int i = 0; i < model->n_uploads; i++) {
		if ((offset > model->upload_start[i]) && (offset < model->upload_end[i])) stage = HTTP_STAGE_UPLOAD;
	}

	return stage;

}

#pragma endregion

#pragma region handlers

static void disconnect_handled_add (void) {

	(void) __atomic_add_fetch (&disconnect_handled, 1, __ATOMIC_SEQ_CST);

}

static HttpMiddlewareResult disconnect_before_middleware (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

	(void) __atomic_add_fetch (&disconnect_before_middlewares, 1, __ATOMIC_SEQ_CST);

	return HTTP_MIDDLEWARE_RESULT_CONTINUE;

}

// GET /ok and GET /private
static void disconnect_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	(void) http_response_send (oki_doki, http_receive);

}

// POST /echo
static void disconnect_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	disconnect_handled_add ();

	const String *body = http_request_get_body (request);
	const bool intact = body && (body->len == strlen (disconnect_body));

	(void) http_response_send (intact ? oki_doki : bad_request_error, http_receive);

}

// POST /upload, the files are discarded, so the uploads path ends empty
static void disconnect_upload_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	disconnect_handled_add ();

	const bool saved = (http_request_get_n_files (request) == DISCONNECT_UPLOADS_MAX);

	http_request_multi_part_discard_files (request);

	(void) http_response_send (saved ? oki_doki : bad_request_error, http_receive);

}

// the states of the clients that the gone handlers found
static unsigned int gone_waiting = 0;
static unsigned int gone_states[HTTP_CLIENT_STATE_RESET + 1] = { 0 };
static unsigned int gone_left = 0;

// waits until the client leaves or DISCONNECT_GONE_WAIT milliseconds
static HttpClientState disconnect_gone_wait (const HttpReceive *http_receive) {

	(void) __atomic_add_fetch (&gone_waiting, 1, __ATOMIC_SEQ_CST);

	const struct timespec interval = { 0, DISCONNECT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + ((double) DISCONNECT_GONE_WAIT / DISCONNECT_MILLISECONDS_PER_SECOND);

	HttpClientState state = http_receive_get_client_state (http_receive);
	while ((state == HTTP_CLIENT_STATE_CONNECTED) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		state = http_receive_get_client_state (http_receive);
	}

	(void) __atomic_add_fetch (&gone_states[state], 1, __ATOMIC_SEQ_CST);

	if (http_receive_is_client_gone (http_receive)) {
		(void) __atomic_add_fetch (&gone_left, 1, __ATOMIC_SEQ_CST);
	}

	return state;

}

// GET /gone answers after its client left, or after it waited for it
static void disconnect_gone_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	(void) disconnect_gone_wait (http_receive);

	(void) http_response_send (oki_doki, http_receive);

}

static char *disconnect_large_body = NULL;

// the socket of the handler keeps a small buffer, so the response never fits in it
static void disconnect_small_send_buffer (const HttpReceive *http_receive) {

	const int size = DISCONNECT_SMALL_BUFFER;
	(void) setsockopt (
		http_receive_get_sock_fd (http_receive), SOL_SOCKET, SO_SNDBUF, &size, sizeof (int)
	);

}

// GET /gone/large answers with a large response after its client left
static void disconnect_gone_large_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	(void) disconnect_gone_wait (http_receive);

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, disconnect_large_body, DISCONNECT_LARGE_SIZE);

}

// GET /large
static void disconnect_large_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, disconnect_large_body, DISCONNECT_LARGE_SIZE);

}

// the pieces that a stream tried to write after one of them failed
static unsigned int stream_writes_after_failure = 0;

// GET /stream stops writing when a piece fails, then checks that the stream refuses more pieces
static void disconnect_stream_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	HttpResponse *response = http_response_get ();
	http_response_set_status (response, HTTP_STATUS_OK);
	(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_TXT);

	if (!http_response_stream_begin (http_receive, response)) {
		bool failed = false;
		for (size_t sent = 0; !failed && (sent < DISCONNECT_LARGE_SIZE); sent += DISCONNECT_STREAM_PIECE_SIZE) {
			failed = http_response_stream_write (
				http_receive, disconnect_large_body + sent, DISCONNECT_STREAM_PIECE_SIZE
			) != 0;
		}

		// a failed stream is not active anymore, it does not send anything else,
		// and its client is gone
		const bool refused = failed
			&& !http_response_stream_is_active (http_receive)
			&& http_response_stream_write (http_receive, disconnect_large_body, 1)
			&& http_receive_is_client_gone (http_receive);

		if (failed && !refused) {
			(void) __atomic_add_fetch (&stream_writes_after_failure, 1, __ATOMIC_SEQ_CST);
		}
	}

	http_response_return (response);

}

// GET /file
static void disconnect_file_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	(void) http_response_send_file (http_receive, HTTP_STATUS_OK, DISCONNECT_LARGE_FILE);

}

// GET /video
static void disconnect_video_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	(void) http_response_handle_video (http_receive, DISCONNECT_LARGE_FILE);

}

// the queue of a worker that runs the jobs of GET /job
static JobQueue *disconnect_job_queue = NULL;

// the job asks for the state of the client from the thread of the worker
static void disconnect_job_work (void *http_receive_ptr) {

	(void) disconnect_gone_wait ((const HttpReceive *) http_receive_ptr);

}

// GET /job hands its work to a worker and waits for it, like a handler that calls another service
static void disconnect_job_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	job_handler_wait (disconnect_job_queue, (void *) http_receive, NULL);

	(void) http_response_send (oki_doki, http_receive);

}

// GET /fail answers with 500
static void disconnect_fail_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	(void) http_response_send (server_error, http_receive);

}

static const char *disconnect_etag_body = { "{\"disconnect\": \"the body of an entity tag\"}" };

// GET /features/etag, a body with an entity tag, answered with 304 when the client has it
static void disconnect_etag_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	(void) http_response_render_json (
		http_receive, HTTP_STATUS_OK, disconnect_etag_body, strlen (disconnect_etag_body)
	);

}

// a body that zlib compresses to about half, so the compressed response never fits in the buffers
static char *disconnect_hex_body = NULL;

// GET /features/gzip
static void disconnect_gzip_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	disconnect_small_send_buffer (http_receive);

	(void) http_response_render_text (http_receive, HTTP_STATUS_OK, disconnect_hex_body, DISCONNECT_LARGE_SIZE);

}

// GET /block waits in the gate until the test opens it
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool gate_open = true;
static unsigned int gate_waiting = 0;

static void disconnect_block_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	disconnect_handled_add ();

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) http_response_send (oki_doki, http_receive);

}

static void disconnect_gate_close (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = false;
	gate_waiting = 0;
	(void) pthread_mutex_unlock (&gate_mutex);

}

static void disconnect_gate_open (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = true;
	(void) pthread_cond_broadcast (&gate_cond);
	(void) pthread_mutex_unlock (&gate_mutex);

}

static unsigned int disconnect_gate_waiting (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	const unsigned int waiting = gate_waiting;
	(void) pthread_mutex_unlock (&gate_mutex);

	return waiting;

}

static void disconnect_route (
	HttpCerver *http_cerver, const RequestMethod method, const char *path,
	void (*handler) (const HttpReceive *, const HttpRequest *)
) {

	HttpRoute *route = http_route_create (method, path, handler);
	test_check_ptr (route);

	http_cerver_route_register (http_cerver, route);

}

static void disconnect_routes (HttpCerver *http_cerver) {

	disconnect_route (http_cerver, REQUEST_METHOD_GET, "ok", disconnect_ok_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_POST, "echo", disconnect_echo_handler);

	HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", disconnect_upload_handler);
	http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
	http_cerver_route_register (http_cerver, upload_route);

	HttpRoute *private_route = http_route_create (REQUEST_METHOD_GET, "private", disconnect_ok_handler);
	http_route_set_auth (private_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
	http_cerver_route_register (http_cerver, private_route);

	HttpRoute *gone_route = http_route_create (REQUEST_METHOD_GET, "gone", disconnect_gone_handler);
	HttpRoute *gone_large_route = http_route_create (REQUEST_METHOD_GET, "large", disconnect_gone_large_handler);
	http_route_child_add (gone_route, gone_large_route);
	http_cerver_route_register (http_cerver, gone_route);

	disconnect_route (http_cerver, REQUEST_METHOD_GET, "large", disconnect_large_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "stream", disconnect_stream_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "file", disconnect_file_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "video", disconnect_video_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "block", disconnect_block_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "job", disconnect_job_handler);
	disconnect_route (http_cerver, REQUEST_METHOD_GET, "fail", disconnect_fail_handler);

	HttpGroup *features = http_cerver_group_create (http_cerver, "features");
	http_group_set_etag (features, true);
	http_group_set_compression (features, true);
	test_check_ptr (http_group_get (features, "etag", disconnect_etag_handler));
	test_check_ptr (http_group_get (features, "gzip", disconnect_gzip_handler));

	// a path without a route is answered with 404, a method that its path does not have with 405
	http_cerver_set_not_found_handler (http_cerver);
	http_cerver_set_method_not_allowed (http_cerver, true);

	test_check_unsigned_eq (http_cerver_add_before (http_cerver, disconnect_before_middleware), 0, NULL);

}

#pragma endregion

#pragma region files

static void disconnect_directory_create (const char *path) {

	if (mkdir (path, HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE) && (errno != EEXIST)) {
		fail ("failed to create a directory of the disconnect tests");
	}

}

static void disconnect_file_write (const char *filename, const char *data, const size_t length) {

	FILE *file = fopen (filename, "w");
	test_check_ptr (file);

	test_check_unsigned_eq ((unsigned int) fwrite (data, 1, length, file), (unsigned int) length, NULL);

	(void) fclose (file);

}

static void disconnect_files_create (void) {

	disconnect_directory_create (DISCONNECT_ROOT_PATH);
	disconnect_directory_create (DISCONNECT_UPLOADS_PATH);
	disconnect_directory_create (DISCONNECT_PUBLIC_PATH);

	disconnect_file_write (DISCONNECT_PUBLIC_FILE, disconnect_public_content, strlen (disconnect_public_content));

	disconnect_large_body = (char *) malloc (DISCONNECT_LARGE_SIZE);
	test_check_ptr (disconnect_large_body);

	for (size_t i = 0; i < DISCONNECT_LARGE_SIZE; i++) {
		disconnect_large_body[i] = (char) ('a' + (i % ('z' - 'a' + 1)));
	}

	disconnect_file_write (DISCONNECT_LARGE_FILE, disconnect_large_body, DISCONNECT_LARGE_SIZE);

	// hex digits of a pseudo random sequence: zlib saves about half of them
	disconnect_hex_body = (char *) malloc (DISCONNECT_LARGE_SIZE);
	test_check_ptr (disconnect_hex_body);

	unsigned int hex_seed = DISCONNECT_HEX_SEED;
	for (size_t i = 0; i < DISCONNECT_LARGE_SIZE; i++) {
		disconnect_hex_body[i] = disconnect_hex_digits[rand_r (&hex_seed) % DISCONNECT_HEX_DIGITS];
	}

}

static void disconnect_files_delete (void) {

	(void) remove (DISCONNECT_LARGE_FILE);
	(void) remove (DISCONNECT_PUBLIC_FILE);
	(void) rmdir (DISCONNECT_PUBLIC_PATH);
	(void) rmdir (DISCONNECT_UPLOADS_PATH);
	(void) rmdir (DISCONNECT_ROOT_PATH);

	free (disconnect_large_body);
	disconnect_large_body = NULL;

	free (disconnect_hex_body);
	disconnect_hex_body = NULL;

}

// the entries of a directory, without "." and ".."
static size_t disconnect_directory_count (const char *path) {

	size_t count = 0;

	DIR *directory = opendir (path);
	test_check_ptr (directory);

	struct dirent *entry = NULL;
	while ((entry = readdir (directory)) != NULL) {
		if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, "..")) count += 1;
	}

	(void) closedir (directory);

	return count;

}

static size_t disconnect_open_fds (void) {

	// the directory has its own fd while it is read
	return disconnect_directory_count (DISCONNECT_FD_DIRECTORY);

}

#pragma endregion

#pragma region server

typedef struct DisconnectServer {

	Cerver *cerver;
	HttpCerver *http_cerver;

	// the thpool that cerver_start () created, taken after a handler answered
	Thpool *thpool;

	u16 port;

	pthread_t thread;
	unsigned int start_result;

} DisconnectServer;

typedef struct DisconnectOptions {

	CerverHandlerType handler_type;
	unsigned int threads;
	unsigned int queue_size;
	CerverOverloadPolicy overload_policy;

} DisconnectOptions;

static u16 disconnect_port = DISCONNECT_PORT_BASE;

static DisconnectServer *disconnect_server = NULL;

static void *disconnect_server_thread (void *server_ptr) {

	DisconnectServer *server = (DisconnectServer *) server_ptr;

	server->start_result = cerver_start (server->cerver);

	return NULL;

}

static void disconnect_sleep (void) {

	const struct timespec interval = { 0, DISCONNECT_POLL_NANOSECONDS };
	(void) nanosleep (&interval, NULL);

}

static int disconnect_connect_once (const u16 port, const bool small_receive_buffer) {

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	struct sockaddr_storage storage;
	(void) memset (&storage, 0, sizeof (struct sockaddr_storage));

	socklen_t length = 0;
	if (ipv6) {
		struct sockaddr_in6 *address6 = (struct sockaddr_in6 *) &storage;
		address6->sin6_family = AF_INET6;
		address6->sin6_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET6, address, &address6->sin6_addr), 1, NULL);
		length = sizeof (struct sockaddr_in6);
	}

	else {
		struct sockaddr_in *address4 = (struct sockaddr_in *) &storage;
		address4->sin_family = AF_INET;
		address4->sin_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET, address, &address4->sin_addr), 1, NULL);
		length = sizeof (struct sockaddr_in);
	}

	int fd = socket (ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	// a small window keeps a large response from fitting in the buffers
	if (small_receive_buffer) {
		const int size = DISCONNECT_SMALL_BUFFER;
		(void) setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (int));
	}

	if (connect (fd, (const struct sockaddr *) &storage, length)) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static bool disconnect_under_valgrind (void) {

	const char *preload = getenv (DISCONNECT_PRELOAD_VARIABLE);

	return preload && strstr (preload, DISCONNECT_VALGRIND_PRELOAD);

}

// the secs to wait for the cerver, scaled under valgrind like its send timeout
static unsigned int disconnect_wait_seconds (void) {

	return disconnect_under_valgrind () ?
		DISCONNECT_WAIT_SECONDS * DISCONNECT_VALGRIND_WAIT_FACTOR : DISCONNECT_WAIT_SECONDS;

}

static int disconnect_connect (const u16 port, const bool small_receive_buffer) {

	int fd = -1;

	const double deadline = test_wait_now () + (double) disconnect_wait_seconds ();
	while ((fd < 0) && (test_wait_now () < deadline)) {
		fd = disconnect_connect_once (port, small_receive_buffer);
		if (fd < 0) disconnect_sleep ();
	}

	test_check (fd > -1, "failed to connect to the disconnect cerver");

	(void) sock_set_timeout (fd, DISCONNECT_CLIENT_TIMEOUT);

	return fd;

}

static void disconnect_send (const int fd, const char *data, const size_t length) {

	size_t sent = 0;
	while (sent < length) {
		const ssize_t rc = send (fd, data + sent, length - sent, MSG_NOSIGNAL);
		test_check (rc > 0, "failed to send a request to the disconnect cerver");

		sent += (size_t) rc;
	}

}

// reads exactly length bytes, unless the connection ends before
// the first bytes are kept in response
static size_t disconnect_read (
	const int fd, const size_t length, char *response, const size_t kept
) {

	char buffer[DISCONNECT_READ_BUFFER];

	size_t total = 0;
	while (total < length) {
		const size_t wanted = ((length - total) < DISCONNECT_READ_BUFFER) ? (length - total) : DISCONNECT_READ_BUFFER;
		const ssize_t rc = recv (fd, buffer, wanted, 0);
		if (rc <= 0) break;

		if (response && (total < kept)) {
			const size_t copied = (((size_t) rc) < (kept - total)) ? (size_t) rc : (kept - total);
			(void) memcpy (response + total, buffer, copied);
		}

		total += (size_t) rc;
	}

	return total;

}

static void disconnect_leave (const int fd, const DisconnectForm form) {

	switch (form) {
		case DISCONNECT_FORM_CLOSE:
			(void) close (fd);
			break;

		case DISCONNECT_FORM_RESET: {
			const struct linger linger = { 1, 0 };
			(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &linger, sizeof (struct linger));
			(void) close (fd);
		} break;

		case DISCONNECT_FORM_HALF_CLOSE:
			(void) shutdown (fd, SHUT_WR);
			break;

		default: break;
	}

}

static void disconnect_thpool_stats (const DisconnectServer *server, ThpoolStats *stats) {

	thpool_get_stats (server->thpool, stats);

}

// waits until the thpool took (or finished) the expected jobs
static void disconnect_wait_jobs (
	const DisconnectServer *server, const u64 expected, const bool completed
) {

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	const double deadline = test_wait_now () + (double) disconnect_wait_seconds ();
	u64 value = completed ? stats.completed_jobs : stats.total_jobs;
	while ((value < expected) && (test_wait_now () < deadline)) {
		disconnect_sleep ();

		disconnect_thpool_stats (server, &stats);
		value = completed ? stats.completed_jobs : stats.total_jobs;
	}

	if (value < expected) {
		(void) fprintf (
			stderr, "the disconnect cerver %s %lu jobs, expected %lu\n",
			completed ? "completed" : "took", (unsigned long) value, (unsigned long) expected
		);
	}

	test_check (value >= expected, "the disconnect cerver did not handle its connections in time");

}

static void disconnect_wait_value (const unsigned int *counter, const unsigned int expected) {

	test_check_unsigned_eq (test_wait_counter (counter, expected, disconnect_wait_seconds ()), expected, NULL);

}

static const char *ok_request = { "GET /ok HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a new request is answered after every case,
// its job is finished before the next case reads the stats of the thpool
static void disconnect_server_check (const DisconnectServer *server) {

	// the thpool exists once the cerver accepts connections,
	// and the job of this one can not finish before its request is sent
	const int fd = disconnect_connect (server->port, false);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	disconnect_send (fd, ok_request, strlen (ok_request));

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
	(void) disconnect_read (fd, DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
	(void) close (fd);

	test_check (
		!strncmp (response, disconnect_ok_status, strlen (disconnect_ok_status)),
		"the disconnect cerver did not answer a new request"
	);

	disconnect_wait_jobs (server, stats.completed_jobs + 1, true);

}

// waits until every job that the thpool took is finished
static void disconnect_wait_idle (const DisconnectServer *server) {

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	disconnect_wait_jobs (server, stats.total_jobs, true);

}

// the first request is answered by a handler that counts itself, the thread of the handler
// was created after cerver_start () created the thpool, so the thpool can be taken
static void disconnect_server_ready (DisconnectServer *server) {

	const unsigned int handled = __atomic_load_n (&disconnect_handled, __ATOMIC_SEQ_CST);

	const int fd = disconnect_connect (server->port, false);
	disconnect_send (fd, ok_request, strlen (ok_request));

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
	(void) disconnect_read (fd, DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
	(void) close (fd);

	disconnect_wait_value (&disconnect_handled, handled + 1);

	server->thpool = server->cerver->thpool;

	disconnect_wait_idle (server);

}

static DisconnectOptions disconnect_options_default (void) {

	DisconnectOptions options;
	options.handler_type = CERVER_HANDLER_TYPE_THREADS;
	options.threads = DISCONNECT_THREADS;
	options.queue_size = DISCONNECT_QUEUE_SIZE;
	options.overload_policy = CERVER_OVERLOAD_POLICY_WAIT;

	return options;

}

static unsigned int disconnect_send_timeout (void) {

	return disconnect_under_valgrind () ?
		DISCONNECT_VALGRIND_SEND_TIMEOUT : DISCONNECT_SEND_TIMEOUT;

}

static DisconnectServer *disconnect_server_start (const DisconnectOptions *options) {

	DisconnectServer *server = (DisconnectServer *) calloc (1, sizeof (DisconnectServer));
	test_check_ptr (server);

	server->port = disconnect_port++;

	Cerver *cerver = cerver_create_web ("disconnect-test", server->port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, options->handler_type);
	cerver_set_reusable_address_flags (cerver, true);
	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);

	cerver_set_thpool_n_threads (cerver, options->threads);
	cerver_set_thpool_queue_size (cerver, options->queue_size);
	cerver_set_overload_policy (cerver, options->overload_policy);
	cerver_set_send_timeout (cerver, disconnect_send_timeout ());

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	http_cerver_set_idle_timeout_milliseconds (http_cerver, DISCONNECT_IDLE_TIMEOUT);
	http_cerver_set_headers_timeout_milliseconds (http_cerver, DISCONNECT_HEADERS_TIMEOUT);

	http_cerver_set_uploads_path (http_cerver, DISCONNECT_UPLOADS_PATH);
	http_cerver_set_default_uploads_filename_generator (http_cerver);
	http_cerver_set_default_uploads_dirname_generator (http_cerver);

	test_check_ptr (http_cerver_static_path_add (http_cerver, DISCONNECT_PUBLIC_PATH));

	http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
	http_cerver_auth_set_jwt_priv_key_filename (http_cerver, private_key);
	http_cerver_auth_set_jwt_pub_key_filename (http_cerver, public_key);

	disconnect_routes (http_cerver);

	server->cerver = cerver;
	server->http_cerver = http_cerver;

	test_check_int_eq (pthread_create (&server->thread, NULL, disconnect_server_thread, server), 0, NULL);

	disconnect_server_ready (server);

	return server;

}

static void disconnect_server_stop (DisconnectServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

#pragma endregion

#pragma region cases

// a client of a batch: a request (a model cut at an offset, or a whole request),
// the bytes of the response that it reads, and how it leaves
typedef struct DisconnectCase {

	const DisconnectModel *model;
	size_t offset;

	const char *request;

	// bytes of the response read before leaving
	size_t cut;
	bool small_receive_buffer;

	// leaves after its handler started waiting for it
	bool wait_handler;

	DisconnectForm form;

	// a half-close client reads its response, it starts with the expected status
	// and has at least expected_size bytes, without a status it reads nothing after its cut
	const char *expected_status;
	size_t expected_size;

	int fd;
	char response[DISCONNECT_RESPONSE_SIZE];
	size_t received;

} DisconnectCase;

static void disconnect_case_init (DisconnectCase *dc, const DisconnectForm form) {

	(void) memset (dc, 0, sizeof (DisconnectCase));

	dc->form = form;
	dc->fd = -1;

}

static void disconnect_case_describe (const DisconnectCase *dc);

// sends the first offset bytes of the model,
// a client that expects "100 Continue" waits for it before the body
static void disconnect_case_send_model (DisconnectCase *dc) {

	const DisconnectModel *model = dc->model;

	if (model->expect_continue && (dc->offset >= model->headers_end)) {
		disconnect_send (dc->fd, model->request, model->headers_end);

		char continue_response[DISCONNECT_RESPONSE_SIZE] = { 0 };
		const size_t continue_length = strlen (DISCONNECT_CONTINUE_RESPONSE);
		test_check_unsigned_eq (
			(unsigned int) disconnect_read (dc->fd, continue_length, continue_response, continue_length),
			(unsigned int) continue_length, NULL
		);

		test_check_str_eq (continue_response, DISCONNECT_CONTINUE_RESPONSE, NULL);

		if (dc->offset > model->headers_end) {
			disconnect_send (dc->fd, model->request + model->headers_end, dc->offset - model->headers_end);
		}
	}

	else if (dc->offset) {
		disconnect_send (dc->fd, model->request, dc->offset);
	}

}

static void disconnect_case_send (DisconnectCase *dc) {

	if (dc->model) disconnect_case_send_model (dc);
	else if (dc->request) disconnect_send (dc->fd, dc->request, strlen (dc->request));

	if (dc->cut) {
		dc->received = disconnect_read (dc->fd, dc->cut, dc->response, DISCONNECT_RESPONSE_SIZE - 1);
		if (dc->received != dc->cut) disconnect_case_describe (dc);
		test_check (dc->received == dc->cut, "the response ended before the cut");
	}

}

// a client that leaves with a close or a reset has no fd anymore
static void disconnect_case_leave (DisconnectCase *dc) {

	disconnect_leave (dc->fd, dc->form);

	if ((dc->form == DISCONNECT_FORM_CLOSE) || (dc->form == DISCONNECT_FORM_RESET)) dc->fd = -1;

}

static void disconnect_case_describe (const DisconnectCase *dc) {

	(void) fprintf (
		stderr, "\t\t%s - %s - offset %zu - cut %zu - received %zu\n",
		dc->model ? dc->model->name : dc->request,
		disconnect_form_names[dc->form], dc->offset, dc->cut, dc->received
	);

}

// keeps what a half-close client read, the first bytes in its response
static void disconnect_case_keep (DisconnectCase *dc, const char *buffer, const size_t length) {

	const size_t kept = DISCONNECT_RESPONSE_SIZE - 1;
	if (dc->received < kept) {
		const size_t copied = (length < (kept - dc->received)) ? length : (kept - dc->received);
		(void) memcpy (dc->response + dc->received, buffer, copied);
	}

	dc->received += length;

}

// the half-close clients read the rest of their responses at the same time, until their ends,
// like real clients do, so a large response never waits for a client that is reading another one
static void disconnect_cases_read (DisconnectCase *cases, const size_t n_cases) {

	struct pollfd *descriptors = (struct pollfd *) calloc (n_cases, sizeof (struct pollfd));
	test_check_ptr (descriptors);

	size_t reading = 0;
	for (size_t i = 0; i < n_cases; i++) {
		const bool reads = (cases[i].form == DISCONNECT_FORM_HALF_CLOSE);
		descriptors[i].fd = reads ? cases[i].fd : -1;
		descriptors[i].events = POLLIN;
		if (reads) reading += 1;
	}

	char buffer[DISCONNECT_READ_BUFFER];

	const double deadline = test_wait_now () + (double) disconnect_wait_seconds ();
	while (reading && (test_wait_now () < deadline)) {
		if (poll (descriptors, (nfds_t) n_cases, DISCONNECT_MILLISECONDS_PER_SECOND) <= 0) continue;

		for (size_t i = 0; i < n_cases; i++) {
			if ((descriptors[i].fd > -1) && descriptors[i].revents) {
				const ssize_t rc = recv (descriptors[i].fd, buffer, DISCONNECT_READ_BUFFER, MSG_DONTWAIT);
				if (rc > 0) disconnect_case_keep (&cases[i], buffer, (size_t) rc);

				else if (!rc || ((errno != EAGAIN) && (errno != EINTR))) {
					descriptors[i].fd = -1;
					reading -= 1;
				}
			}
		}
	}

	free (descriptors);

	test_check (!reading, "a half-close client did not receive the end of its response");

}

// checks what a half-close client read, and closes every client that is still connected
static void disconnect_case_finish (DisconnectCase *dc) {

	if (dc->form == DISCONNECT_FORM_HALF_CLOSE) {
		const bool expected = dc->expected_status ?
			(!strncmp (dc->response, dc->expected_status, strlen (dc->expected_status)) && (dc->received >= dc->expected_size)) :
			(dc->received == dc->cut);

		if (!expected) disconnect_case_describe (dc);
		test_check (expected, "a half-close client did not receive the expected response");
	}

	if (dc->fd > -1) (void) close (dc->fd);
	dc->fd = -1;

}

// the counters that a case of the request matrix changes
static void disconnect_case_expect (const DisconnectCase *dc, DisconnectCounters *expected) {

	const DisconnectModel *model = dc->model;

	if (dc->offset < model->length) {
		const HttpStage stage = disconnect_model_stage (model, dc->offset);

		switch (dc->form) {
			case DISCONNECT_FORM_RESET:
				expected->aborted[stage][HTTP_ABORT_REASON_RESET] += 1;
				break;

			case DISCONNECT_FORM_VANISH:
				expected->vanished[stage] += 1;
				if (dc->offset) {
					expected->timeout += 1;
					expected->timeout_responses += 1;
				}
				break;

			default:
				expected->aborted[stage][HTTP_ABORT_REASON_CLOSED] += 1;
				break;
		}
	}

	// the complete request is handled, a reset can arrive before or after its response
	else {
		if (model->handled) {
			expected->handled += 1;
			expected->before_middlewares += 1;
		}

		if (dc->form == DISCONNECT_FORM_RESET) expected->either += 1;
		else expected->ok_responses += 1;

		// a client that left right after its request can make the pieces after the header fail
		const bool left = (dc->form == DISCONNECT_FORM_RESET) || (dc->form == DISCONNECT_FORM_CLOSE);
		if (model->many_sends && left) expected->maybe_failed += 1;
	}

}

// the counters must have changed by exactly the expected values since before
static void disconnect_counters_expect (
	const HttpCerver *http_cerver, const DisconnectCounters *before,
	const DisconnectCounters *expected, const char *description,
	const DisconnectCase *cases, const size_t n_cases
) {

	DisconnectCounters after, delta;
	disconnect_counters_read (http_cerver, &after);
	disconnect_counters_delta (before, &after, &delta);

	if (!disconnect_counters_match (expected, &delta)) {
		(void) fprintf (stderr, "disconnect case failed: %s\n", description);
		disconnect_counters_print ("expected", expected);
		disconnect_counters_print ("found", &delta);
		for (size_t i = 0; i < n_cases; i++) disconnect_case_describe (&cases[i]);

		fail ("the counters of the disconnect cerver did not change as expected");
	}

}

// the cerver closed every connection of the case and its uploads path is empty
static void disconnect_resources_expect (const size_t fds, const char *description) {

	const size_t fds_after = disconnect_open_fds ();
	if (fds_after != fds) {
		(void) fprintf (stderr, "disconnect case: %s - fds %zu before, %zu after\n", description, fds, fds_after);
	}

	test_check (fds_after == fds, "the disconnect cerver did not close its connections");

	test_check (
		disconnect_directory_count (DISCONNECT_UPLOADS_PATH) == 0,
		"the disconnect cerver left files in its uploads path"
	);

}

// runs the cases at the same time: each one connects, sends its request, reads its cut,
// leaves, and then the counters of the cerver must have changed by expected,
// its fds must be the ones it had and its uploads path must be empty
static void disconnect_batch_run (
	const DisconnectServer *server, DisconnectCase *cases, const size_t n_cases,
	const DisconnectCounters *expected, const char *description
) {

	const size_t fds = disconnect_open_fds ();
	const unsigned int gone_before = __atomic_load_n (&gone_waiting, __ATOMIC_SEQ_CST);

	DisconnectCounters before;
	disconnect_counters_read (server->http_cerver, &before);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);
	const u64 total_jobs = stats.total_jobs;
	const u64 completed_jobs = stats.completed_jobs;

	// every connection is accepted before its client acts, but a client that reads a cut
	// of its response connects right before its request: the ones before it take a while
	// (a compressed response), and its connection would idle until the idle timeout
	size_t accepted = 0;
	for (size_t i = 0; i < n_cases; i++) {
		if (!cases[i].cut) {
			cases[i].fd = disconnect_connect (server->port, cases[i].small_receive_buffer);
			accepted += 1;
		}
	}

	disconnect_wait_jobs (server, total_jobs + accepted, false);

	// a client leaves as soon as it sent its part, so its connection never idles,
	// unless it waits for its handler to start
	unsigned int waiting = 0;
	for (size_t i = 0; i < n_cases; i++) {
		if (cases[i].cut) cases[i].fd = disconnect_connect (server->port, cases[i].small_receive_buffer);

		disconnect_case_send (&cases[i]);

		if (cases[i].wait_handler) waiting += 1;
		else disconnect_case_leave (&cases[i]);
	}

	if (waiting) {
		disconnect_wait_value (&gone_waiting, gone_before + waiting);

		for (size_t i = 0; i < n_cases; i++) {
			if (cases[i].wait_handler) disconnect_case_leave (&cases[i]);
		}
	}

	disconnect_cases_read (cases, n_cases);

	disconnect_wait_jobs (server, completed_jobs + n_cases, true);

	for (size_t i = 0; i < n_cases; i++) disconnect_case_finish (&cases[i]);

	disconnect_counters_expect (server->http_cerver, &before, expected, description, cases, n_cases);

	disconnect_resources_expect (fds, description);

	disconnect_server_check (server);

}

#pragma endregion

#pragma region requests

#define DISCONNECT_DESCRIPTION_SIZE			256

static bool disconnect_full = false;
static unsigned int disconnect_seed = 0;

static void disconnect_offset_choose (bool *chosen, const size_t length, const size_t offset) {

	if (offset <= length) chosen[offset] = true;

}

// an offset, the one before and the one after
static void disconnect_offset_choose_around (bool *chosen, const size_t length, const size_t offset) {

	if (offset) disconnect_offset_choose (chosen, length, offset - 1);
	disconnect_offset_choose (chosen, length, offset);
	disconnect_offset_choose (chosen, length, offset + 1);

}

// the full level uses every offset of the request,
// the sample uses the ones around its stage boundaries and some random ones
static size_t disconnect_offsets (
	const DisconnectModel *model, const bool full, size_t *offsets
) {

	const size_t length = model->length;

	bool *chosen = (bool *) calloc (length + 1, sizeof (bool));
	test_check_ptr (chosen);

	if (full) {
		for (size_t offset = 0; offset <= length; offset++) chosen[offset] = true;
	}

	else {
		disconnect_offset_choose_around (chosen, length, 0);
		disconnect_offset_choose_around (chosen, length, model->request_line_end);
		disconnect_offset_choose_around (chosen, length, model->headers_end);

		for (unsigned int i = 0; i < model->n_uploads; i++) {
			disconnect_offset_choose_around (chosen, length, model->upload_start[i]);
			disconnect_offset_choose_around (chosen, length, model->upload_end[i]);
		}

		disconnect_offset_choose_around (chosen, length, length);

		for (unsigned int i = 0; i < DISCONNECT_SAMPLE_RANDOM_OFFSETS; i++) {
			disconnect_offset_choose (chosen, length, (size_t) rand_r (&disconnect_seed) % (length + 1));
		}
	}

	size_t n_offsets = 0;
	for (size_t offset = 0; offset <= length; offset++) {
		if (chosen[offset]) offsets[n_offsets++] = offset;
	}

	free (chosen);

	return n_offsets;

}

static void disconnect_request_batch (
	const DisconnectServer *server, const DisconnectModel *model, const DisconnectForm form,
	const size_t *offsets, const size_t n_offsets
) {

	DisconnectCase *cases = (DisconnectCase *) calloc (n_offsets, sizeof (DisconnectCase));
	test_check_ptr (cases);

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));

	for (size_t i = 0; i < n_offsets; i++) {
		DisconnectCase *dc = &cases[i];
		disconnect_case_init (dc, form);

		dc->model = model;
		dc->offset = offsets[i];

		// a complete request is answered, an incomplete one is not
		if (dc->offset == model->length) {
			dc->expected_status = disconnect_ok_status;
			dc->expected_size = strlen (disconnect_ok_status);
		}

		disconnect_case_expect (dc, &expected);
	}

	char description[DISCONNECT_DESCRIPTION_SIZE] = { 0 };
	(void) snprintf (
		description, DISCONNECT_DESCRIPTION_SIZE, "request %s - %s - offsets %zu to %zu",
		model->name, disconnect_form_names[form], offsets[0], offsets[n_offsets - 1]
	);

	disconnect_batch_run (server, cases, n_offsets, &expected, description);

	free (cases);

}

// every model cut at each offset, in the four ways a client leaves
static void disconnect_request_matrix (const DisconnectServer *server, const bool full) {

	for (unsigned int m = 0; m < DISCONNECT_MODELS; m++) {
		const DisconnectModel *model = &disconnect_models[m];

		size_t *offsets = (size_t *) calloc (model->length + 1, sizeof (size_t));
		test_check_ptr (offsets);

		const size_t n_offsets = disconnect_offsets (model, full, offsets);

		for (unsigned int form = 0; form < DISCONNECT_FORMS; form++) {
			for (size_t start = 0; start < n_offsets; start += DISCONNECT_BATCH_SIZE) {
				const size_t left = n_offsets - start;

				disconnect_request_batch (
					server, model, (DisconnectForm) form,
					offsets + start, (left < DISCONNECT_BATCH_SIZE) ? left : DISCONNECT_BATCH_SIZE
				);
			}
		}

		free (offsets);
	}

}

static void test_disconnect_requests (void) {

	disconnect_request_matrix (disconnect_server, disconnect_full);

}

#pragma endregion

#pragma region handler

static const char *gone_request = { "GET /gone HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *gone_large_request = { "GET /gone/large HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// the client leaves while the handler waits for it,
// the handler must find it in the expected state, and the response it sends then
// must change the counters as expected
static void disconnect_gone_case (
	const char *request, const DisconnectForm form,
	const HttpClientState state, const bool gone,
	const DisconnectCounters *expected, const size_t expected_size
) {

	unsigned int states[HTTP_CLIENT_STATE_RESET + 1] = { 0 };
	for (unsigned int i = 0; i <= HTTP_CLIENT_STATE_RESET; i++) {
		states[i] = __atomic_load_n (&gone_states[i], __ATOMIC_SEQ_CST);
	}

	const unsigned int left = __atomic_load_n (&gone_left, __ATOMIC_SEQ_CST);

	DisconnectCase dc;
	disconnect_case_init (&dc, form);
	dc.request = request;
	dc.wait_handler = true;
	dc.small_receive_buffer = true;
	dc.expected_status = disconnect_ok_status;
	dc.expected_size = expected_size;

	char description[DISCONNECT_DESCRIPTION_SIZE] = { 0 };
	(void) snprintf (
		description, DISCONNECT_DESCRIPTION_SIZE, "handler %s - %s", request, disconnect_form_names[form]
	);

	disconnect_batch_run (disconnect_server, &dc, 1, expected, description);

	test_check_unsigned_eq (
		__atomic_load_n (&gone_states[state], __ATOMIC_SEQ_CST), states[state] + 1,
		"the handler did not find its client in the expected state"
	);

	test_check_unsigned_eq (
		__atomic_load_n (&gone_left, __ATOMIC_SEQ_CST), left + (gone ? 1 : 0),
		"http_receive_is_client_gone () did not tell that the client was gone"
	);

}

static DisconnectCounters disconnect_handled_counters (void) {

	DisconnectCounters counters;
	(void) memset (&counters, 0, sizeof (DisconnectCounters));

	counters.handled = 1;
	counters.before_middlewares = 1;

	return counters;

}

// a small response fits in the buffers, so only a reset makes it fail
static void disconnect_gone_small (void) {

	DisconnectCounters expected = disconnect_handled_counters ();
	expected.ok_responses = 1;

	disconnect_gone_case (gone_request, DISCONNECT_FORM_CLOSE, HTTP_CLIENT_STATE_CLOSED, true, &expected, 0);
	disconnect_gone_case (gone_request, DISCONNECT_FORM_HALF_CLOSE, HTTP_CLIENT_STATE_CLOSED, true, &expected, 0);
	disconnect_gone_case (gone_request, DISCONNECT_FORM_VANISH, HTTP_CLIENT_STATE_CONNECTED, false, &expected, 0);

	expected = disconnect_handled_counters ();
	expected.failed_responses = 1;
	expected.aborted[HTTP_STAGE_HANDLER][HTTP_ABORT_REASON_RESET] = 1;

	disconnect_gone_case (gone_request, DISCONNECT_FORM_RESET, HTTP_CLIENT_STATE_RESET, true, &expected, 0);

}

// a large response does not fit in the buffers: it fails for a client that closed or vanished
static void disconnect_gone_large (void) {

	DisconnectCounters expected = disconnect_handled_counters ();
	expected.ok_responses = 1;

	disconnect_gone_case (
		gone_large_request, DISCONNECT_FORM_HALF_CLOSE, HTTP_CLIENT_STATE_CLOSED, true,
		&expected, DISCONNECT_LARGE_SIZE
	);

	expected = disconnect_handled_counters ();
	expected.failed_responses = 1;
	expected.aborted[HTTP_STAGE_RESPONSE][HTTP_ABORT_REASON_RESET] = 1;

	disconnect_gone_case (gone_large_request, DISCONNECT_FORM_CLOSE, HTTP_CLIENT_STATE_CLOSED, true, &expected, 0);

	expected = disconnect_handled_counters ();
	expected.failed_responses = 1;
	expected.aborted[HTTP_STAGE_HANDLER][HTTP_ABORT_REASON_RESET] = 1;

	disconnect_gone_case (gone_large_request, DISCONNECT_FORM_RESET, HTTP_CLIENT_STATE_RESET, true, &expected, 0);

	expected = disconnect_handled_counters ();
	expected.failed_responses = 1;
	expected.vanished[HTTP_STAGE_RESPONSE] = 1;

	disconnect_gone_case (gone_large_request, DISCONNECT_FORM_VANISH, HTTP_CLIENT_STATE_CONNECTED, false, &expected, 0);

}

static void test_disconnect_handler (void) {

	disconnect_gone_small ();

	disconnect_gone_large ();

}

// a cerver can take a half-close client as still connected
static void test_disconnect_half_close_not_gone (void) {

	http_cerver_set_half_close_is_gone (disconnect_server->http_cerver, false);

	DisconnectCounters expected = disconnect_handled_counters ();
	expected.ok_responses = 1;

	disconnect_gone_case (gone_request, DISCONNECT_FORM_HALF_CLOSE, HTTP_CLIENT_STATE_CLOSED, false, &expected, 0);

	http_cerver_set_half_close_is_gone (disconnect_server->http_cerver, HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE);

}

#pragma endregion

#pragma region responses

// a large response that the client leaves after reading some of its bytes
typedef struct DisconnectResponse {

	const char *request;
	const char *status;

	// the bytes that a client that reads it all receives, at least
	size_t size;

	// the 200 of its header is counted before its body is sent
	bool header_counted;

} DisconnectResponse;

static const DisconnectResponse disconnect_responses[] = {
	{ "GET /large HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200", DISCONNECT_LARGE_SIZE, false },
	{ "GET /stream HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200", DISCONNECT_LARGE_SIZE, true },
	{ "GET /file HTTP/1.1\r\nHost: localhost\r\n\r\n", "HTTP/1.1 200", DISCONNECT_LARGE_SIZE, true },
	{ "GET /video HTTP/1.1\r\nHost: localhost\r\nRange: bytes=0-\r\n\r\n", "HTTP/1.1 206", HTTP_RESPONSE_VIDEO_CHUNK_SIZE, false },
	{ "GET /features/gzip HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n", "HTTP/1.1 200", DISCONNECT_GZIP_LEAST_SIZE, false }
};

static const size_t disconnect_n_responses = sizeof (disconnect_responses) / sizeof (DisconnectResponse);

static void disconnect_response_expect (
	const DisconnectResponse *response, const DisconnectForm form, DisconnectCounters *expected
) {

	expected->handled += 1;
	expected->before_middlewares += 1;

	switch (form) {
		case DISCONNECT_FORM_HALF_CLOSE:
			if (!strcmp (response->status, disconnect_ok_status)) expected->ok_responses += 1;
			break;

		case DISCONNECT_FORM_VANISH:
			expected->failed_responses += 1;
			expected->vanished[HTTP_STAGE_RESPONSE] += 1;
			if (response->header_counted) expected->ok_responses += 1;
			break;

		default:
			expected->failed_responses += 1;
			expected->aborted[HTTP_STAGE_RESPONSE][HTTP_ABORT_REASON_RESET] += 1;
			if (response->header_counted) expected->ok_responses += 1;
			break;
	}

}

static void disconnect_response_batch (const DisconnectResponse *response, const DisconnectForm form) {

	DisconnectCase cases[sizeof (disconnect_response_cuts) / sizeof (size_t)];

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));

	for (size_t i = 0; i < disconnect_n_response_cuts; i++) {
		DisconnectCase *dc = &cases[i];
		disconnect_case_init (dc, form);

		dc->request = response->request;
		dc->cut = disconnect_response_cuts[i];
		dc->small_receive_buffer = true;
		dc->expected_status = response->status;
		dc->expected_size = response->size;

		disconnect_response_expect (response, form, &expected);
	}

	char description[DISCONNECT_DESCRIPTION_SIZE] = { 0 };
	(void) snprintf (
		description, DISCONNECT_DESCRIPTION_SIZE, "response %s - %s",
		response->request, disconnect_form_names[form]
	);

	disconnect_batch_run (disconnect_server, cases, disconnect_n_response_cuts, &expected, description);

}

static void test_disconnect_responses (void) {

	for (size_t r = 0; r < disconnect_n_responses; r++) {
		for (unsigned int form = 0; form < DISCONNECT_FORMS; form++) {
			disconnect_response_batch (&disconnect_responses[r], (DisconnectForm) form);
		}
	}

	// a failed stream refused every piece after the failure
	test_check_unsigned_eq (__atomic_load_n (&stream_writes_after_failure, __ATOMIC_SEQ_CST), 0, NULL);

}

#pragma endregion

#pragma region worker

static const char *job_request = { "GET /job HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// the client leaves while the job of its handler runs in a worker,
// the job finds it from its own thread and the handler answers after the job
static void test_disconnect_worker (void) {

	DisconnectCounters expected = disconnect_handled_counters ();
	expected.ok_responses = 1;

	disconnect_gone_case (job_request, DISCONNECT_FORM_CLOSE, HTTP_CLIENT_STATE_CLOSED, true, &expected, 0);
	disconnect_gone_case (job_request, DISCONNECT_FORM_HALF_CLOSE, HTTP_CLIENT_STATE_CLOSED, true, &expected, 0);
	disconnect_gone_case (job_request, DISCONNECT_FORM_VANISH, HTTP_CLIENT_STATE_CONNECTED, false, &expected, 0);

	expected = disconnect_handled_counters ();
	expected.failed_responses = 1;
	expected.aborted[HTTP_STAGE_HANDLER][HTTP_ABORT_REASON_RESET] = 1;

	disconnect_gone_case (job_request, DISCONNECT_FORM_RESET, HTTP_CLIENT_STATE_RESET, true, &expected, 0);

}

#pragma endregion

#pragma region statuses

#define DISCONNECT_STATUS_SIZE				16
#define DISCONNECT_ETAG_SIZE				128

// a complete request answered with another status, in the four ways a client leaves:
// the response is small, so only a reset races with it
static void disconnect_status_batch (
	const char *name, const char *request, const http_status status, const bool handled,
	const bool before_body
) {

	char expected_status[DISCONNECT_STATUS_SIZE] = { 0 };
	(void) snprintf (expected_status, DISCONNECT_STATUS_SIZE, "HTTP/1.1 %d", (int) status);

	DisconnectCase cases[DISCONNECT_FORMS];

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));
	expected.races_in_body = before_body;

	for (unsigned int form = 0; form < DISCONNECT_FORMS; form++) {
		DisconnectCase *dc = &cases[form];
		disconnect_case_init (dc, (DisconnectForm) form);

		dc->request = request;
		dc->expected_status = expected_status;
		dc->expected_size = strlen (expected_status);

		if (handled) {
			expected.handled += 1;
			expected.before_middlewares += 1;
		}

		if (form == DISCONNECT_FORM_RESET) expected.either += 1;
		else expected.other_responses += 1;
	}

	disconnect_other_status = status;

	char description[DISCONNECT_DESCRIPTION_SIZE] = { 0 };
	(void) snprintf (description, DISCONNECT_DESCRIPTION_SIZE, "status %s (%d)", name, (int) status);

	disconnect_batch_run (disconnect_server, cases, DISCONNECT_FORMS, &expected, description);

	disconnect_other_status = HTTP_STATUS_NONE;

}

// the entity tag of the etag route, from the response of a normal request
static void disconnect_etag_get (char *etag) {

	const char *request = { "GET /features/etag HTTP/1.1\r\nHost: localhost\r\n\r\n" };

	const int fd = disconnect_connect (disconnect_server->port, false);
	disconnect_send (fd, request, strlen (request));

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
	(void) disconnect_read (fd, DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
	(void) close (fd);

	const char *header = strstr (response, "ETag: ");
	test_check (header != NULL, "the etag route did not send its entity tag");

	header += strlen ("ETag: ");
	const char *end = strstr (header, "\r\n");
	test_check (end != NULL, "the entity tag was not complete");

	(void) snprintf (etag, DISCONNECT_ETAG_SIZE, "%.*s", (int) (end - header), header);

	disconnect_wait_idle (disconnect_server);

}

// the errors that cerver or a handler answer, and a 304
static void test_disconnect_statuses (void) {

	disconnect_status_batch (
		"not found", "GET /missing HTTP/1.1\r\nHost: localhost\r\n\r\n", HTTP_STATUS_NOT_FOUND, false, false
	);

	disconnect_status_batch (
		"method not allowed", "DELETE /ok HTTP/1.1\r\nHost: localhost\r\n\r\n",
		HTTP_STATUS_METHOD_NOT_ALLOWED, false, false
	);

	disconnect_status_batch (
		"handler error", "GET /fail HTTP/1.1\r\nHost: localhost\r\n\r\n",
		HTTP_STATUS_INTERNAL_SERVER_ERROR, true, false
	);

	// answered before the body, the client leaves while cerver drains what it sent
	disconnect_status_batch (
		"unauthorized", "GET /private HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer wrong\r\n\r\n",
		HTTP_STATUS_UNAUTHORIZED, false, true
	);

	disconnect_status_batch (
		"payload too large",
		"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1073741824\r\n\r\n0123456789abcdef",
		HTTP_STATUS_PAYLOAD_TOO_LARGE, false, true
	);

	char etag[DISCONNECT_ETAG_SIZE] = { 0 };
	disconnect_etag_get (etag);

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	(void) snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"GET /features/etag HTTP/1.1\r\nHost: localhost\r\nIf-None-Match: %s\r\n\r\n", etag
	);

	disconnect_status_batch ("not modified", request, HTTP_STATUS_NOT_MODIFIED, true, false);

}

#pragma endregion

#pragma region signals

#define DISCONNECT_TASKS_DIRECTORY			"/proc/self/task"
#define DISCONNECT_TASK_PATH_SIZE			128

static void disconnect_signal_handler (int signal_number) {

	(void) signal_number;

}

// the system call that a thread of the process is waiting in, -1 if it can not be read
static long disconnect_task_syscall (const char *task) {

	char path[DISCONNECT_TASK_PATH_SIZE] = { 0 };
	(void) snprintf (path, DISCONNECT_TASK_PATH_SIZE, "%s/%s/syscall", DISCONNECT_TASKS_DIRECTORY, task);

	long number = -1;

	FILE *file = fopen (path, "r");
	if (file) {
		if (fscanf (file, "%ld", &number) != 1) number = -1;
		(void) fclose (file);
	}

	return number;

}

#define DISCONNECT_PROBE_SECONDS			8
#define DISCONNECT_PROBE_TASK_SIZE			32

static pid_t disconnect_probe_task = 0;

// keeps its task id and waits in the same recv () as the connections of the cerver
static void *disconnect_receive_probe (void *fd_ptr) {

	const int fd = *(const int *) fd_ptr;

	__atomic_store_n (&disconnect_probe_task, (pid_t) syscall (SYS_gettid), __ATOMIC_SEQ_CST);

	char byte = 0;
	(void) recv (fd, &byte, sizeof (byte), 0);

	return NULL;

}

static long disconnect_receive_probe_read (const double deadline) {

	long number = -1;
	char task[DISCONNECT_PROBE_TASK_SIZE] = { 0 };

	while ((number == -1) && (test_wait_now () < deadline)) {
		const pid_t probe = __atomic_load_n (&disconnect_probe_task, __ATOMIC_SEQ_CST);
		if (probe) {
			(void) snprintf (task, DISCONNECT_PROBE_TASK_SIZE, "%d", (int) probe);
			number = disconnect_task_syscall (task);
		}

		if (number == -1) disconnect_sleep ();
	}

	return number;

}

// the number that /proc reports for a thread that waits in recv (),
// which is not SYS_recvfrom when the binary runs emulated (an amd64 binary on arm64 reports the numbers of arm64)
// returns -1 if it could not be read
static long disconnect_receive_syscall (void) {

	long number = -1;

	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	__atomic_store_n (&disconnect_probe_task, 0, __ATOMIC_SEQ_CST);

	pthread_t thread;
	if (!pthread_create (&thread, NULL, disconnect_receive_probe, &fds[0])) {
		number = disconnect_receive_probe_read (test_wait_now () + (double) DISCONNECT_PROBE_SECONDS);

		(void) send (fds[1], "", 1, MSG_NOSIGNAL);
		(void) pthread_join (thread, NULL);
	}

	(void) close (fds[0]);
	(void) close (fds[1]);

	return number;

}

// interrupts with SIGUSR1 every thread of the process that waits in the receive system call
// returns how many were interrupted
static unsigned int disconnect_interrupt_receives (const long receive_syscall) {

	unsigned int interrupted = 0;

	DIR *tasks = opendir (DISCONNECT_TASKS_DIRECTORY);
	test_check_ptr (tasks);

	struct dirent *entry = NULL;
	while ((entry = readdir (tasks)) != NULL) {
		if (entry->d_name[0] == '.') continue;

		if (disconnect_task_syscall (entry->d_name) == receive_syscall) {
			const pid_t task = (pid_t) strtol (entry->d_name, NULL, 10);
			if (!syscall (SYS_tgkill, getpid (), task, SIGUSR1)) interrupted += 1;
		}
	}

	(void) closedir (tasks);

	return interrupted;

}

// a signal without SA_RESTART interrupts the recv () of a request in the middle of its headers:
// the request continues (it was taken as a reset client), so the client still gets its response
static void test_disconnect_interrupted (void) {

	const DisconnectServer *server = disconnect_server;
	const DisconnectModel *model = &disconnect_models[0];

	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = disconnect_signal_handler;
	(void) sigemptyset (&action.sa_mask);

	struct sigaction previous;
	test_check_int_eq (sigaction (SIGUSR1, &action, &previous), 0, NULL);

	const long receive_syscall = disconnect_receive_syscall ();
	test_check (receive_syscall > -1, "the system call of a thread in recv () could not be read");

	const size_t fds = disconnect_open_fds ();

	DisconnectCounters before;
	disconnect_counters_read (server->http_cerver, &before);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	DisconnectCase dc;
	disconnect_case_init (&dc, DISCONNECT_FORM_CLOSE);
	dc.model = model;
	dc.offset = model->request_line_end;
	dc.fd = disconnect_connect (server->port, false);

	disconnect_case_send (&dc);

	// the thread of the connection waits for the rest of the headers, well inside the idle timeout
	unsigned int interrupted = 0;
	const double deadline = test_wait_now () + ((double) DISCONNECT_IDLE_TIMEOUT / (2 * DISCONNECT_MILLISECONDS_PER_SECOND));
	while (!interrupted && (test_wait_now () < deadline)) interrupted = disconnect_interrupt_receives (receive_syscall);

	disconnect_send (dc.fd, model->request + dc.offset, model->length - dc.offset);

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
	(void) disconnect_read (dc.fd, DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
	(void) close (dc.fd);
	dc.fd = -1;

	disconnect_wait_jobs (server, stats.completed_jobs + 1, true);

	test_check_int_eq (sigaction (SIGUSR1, &previous, NULL), 0, NULL);

	test_check (interrupted > 0, "the thread of the connection was not interrupted in recvfrom ()");
	test_check (
		!strncmp (response, disconnect_ok_status, strlen (disconnect_ok_status)),
		"an interrupted recv () ended the request"
	);

	DisconnectCounters expected = disconnect_handled_counters ();
	expected.ok_responses = 1;

	disconnect_counters_expect (server->http_cerver, &before, &expected, "interrupted", &dc, 1);

	disconnect_resources_expect (fds, "interrupted");

}

#pragma endregion

#pragma region queue

static const char *block_request = { "GET /block HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// fills every thread of the cerver with a handler that waits in the gate
static void disconnect_gate_fill (const DisconnectServer *server, int *blocked, const unsigned int n_blocked) {

	disconnect_gate_close ();

	for (unsigned int i = 0; i < n_blocked; i++) {
		blocked[i] = disconnect_connect (server->port, false);
		disconnect_send (blocked[i], block_request, strlen (block_request));
	}

	const double deadline = test_wait_now () + (double) disconnect_wait_seconds ();
	while ((disconnect_gate_waiting () < n_blocked) && (test_wait_now () < deadline)) disconnect_sleep ();

	test_check_unsigned_eq (disconnect_gate_waiting (), n_blocked, "the handlers did not wait in the gate");

}

// the blocked clients receive their responses after the gate opens
static void disconnect_gate_release (int *blocked, const unsigned int n_blocked) {

	disconnect_gate_open ();

	for (unsigned int i = 0; i < n_blocked; i++) {
		char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
		(void) disconnect_read (blocked[i], DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
		test_check (
			!strncmp (response, disconnect_ok_status, strlen (disconnect_ok_status)),
			"a blocked handler did not answer"
		);

		(void) close (blocked[i]);
	}

}

#define DISCONNECT_QUEUED_CASES				5

// the clients leave while their connections wait in the queue of the thpool
static void test_disconnect_queue (void) {

	const DisconnectServer *server = disconnect_server;
	const DisconnectModel *model = &disconnect_models[0];

	const size_t fds = disconnect_open_fds ();

	DisconnectCounters before;
	disconnect_counters_read (server->http_cerver, &before);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	int blocked[DISCONNECT_THREADS];
	disconnect_gate_fill (server, blocked, DISCONNECT_THREADS);

	// four clients that leave before sending anything, and one that sends its complete request
	DisconnectCase cases[DISCONNECT_QUEUED_CASES];
	for (unsigned int form = 0; form < DISCONNECT_FORMS; form++) {
		disconnect_case_init (&cases[form], (DisconnectForm) form);
		cases[form].model = model;
	}

	disconnect_case_init (&cases[DISCONNECT_FORMS], DISCONNECT_FORM_CLOSE);
	cases[DISCONNECT_FORMS].model = model;
	cases[DISCONNECT_FORMS].offset = model->length;

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));
	expected.handled = DISCONNECT_THREADS;
	expected.before_middlewares = DISCONNECT_THREADS;
	expected.ok_responses = DISCONNECT_THREADS;

	for (unsigned int i = 0; i < DISCONNECT_QUEUED_CASES; i++) {
		cases[i].fd = disconnect_connect (server->port, false);
		disconnect_case_expect (&cases[i], &expected);
	}

	disconnect_wait_jobs (server, stats.total_jobs + DISCONNECT_THREADS + DISCONNECT_QUEUED_CASES, false);

	for (unsigned int i = 0; i < DISCONNECT_QUEUED_CASES; i++) {
		disconnect_case_send (&cases[i]);
		disconnect_case_leave (&cases[i]);
	}

	disconnect_gate_release (blocked, DISCONNECT_THREADS);

	disconnect_cases_read (cases, DISCONNECT_QUEUED_CASES);

	disconnect_wait_jobs (server, stats.completed_jobs + DISCONNECT_THREADS + DISCONNECT_QUEUED_CASES, true);

	for (unsigned int i = 0; i < DISCONNECT_QUEUED_CASES; i++) disconnect_case_finish (&cases[i]);

	disconnect_counters_expect (
		server->http_cerver, &before, &expected, "queue", cases, DISCONNECT_QUEUED_CASES
	);

	disconnect_resources_expect (fds, "queue");

	disconnect_server_check (server);

}

#pragma endregion

#pragma region pipelined

static const char *pipelined_request = {
	"GET /ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /ok HTTP/1.1\r\nHost: localhost\r\n\r\n"
	"GET /ok HTTP/1.1\r\nHost: local"
};

// the bytes after a complete request are drained, only one request is answered
static void test_disconnect_pipelined (void) {

	DisconnectCase cases[DISCONNECT_FORMS];

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));

	for (unsigned int form = 0; form < DISCONNECT_FORMS; form++) {
		DisconnectCase *dc = &cases[form];
		disconnect_case_init (dc, (DisconnectForm) form);

		dc->request = pipelined_request;
		dc->expected_status = disconnect_ok_status;
		dc->expected_size = strlen (disconnect_ok_status);

		expected.handled += 1;
		expected.before_middlewares += 1;

		if (form == DISCONNECT_FORM_RESET) expected.either += 1;
		else expected.ok_responses += 1;
	}

	disconnect_batch_run (disconnect_server, cases, DISCONNECT_FORMS, &expected, "pipelined");

	// the half-close client received a single response
	const char *second = strstr (cases[DISCONNECT_FORM_HALF_CLOSE].response + 1, disconnect_ok_status);
	test_check (second == NULL, "a pipelined request was answered");

}

#pragma endregion

#pragma region overload

// a client that leaves before reading the 503 of a full cerver
static void test_disconnect_overload (void) {

	const size_t fds = disconnect_open_fds ();

	DisconnectOptions options = disconnect_options_default ();
	options.threads = 1;
	options.queue_size = 1;
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;

	DisconnectServer *server = disconnect_server_start (&options);

	int blocked = -1;
	disconnect_gate_fill (server, &blocked, 1);

	const int queued = disconnect_connect (server->port, false);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	const u64 rejected = __atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST);

	// the queue is full, the cerver answers 503 to a client that already left
	const int extra = disconnect_connect (server->port, false);
	disconnect_leave (extra, DISCONNECT_FORM_RESET);

	test_check (
		test_wait_counter_64 (&server->cerver->stats->total_rejected_connections, rejected + 1, disconnect_wait_seconds ()) == rejected + 1,
		"the full cerver did not reject the connection"
	);

	disconnect_gate_release (&blocked, 1);

	disconnect_send (queued, ok_request, strlen (ok_request));

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };
	(void) disconnect_read (queued, DISCONNECT_RESPONSE_SIZE - 1, response, DISCONNECT_RESPONSE_SIZE - 1);
	(void) close (queued);

	test_check (
		!strncmp (response, disconnect_ok_status, strlen (disconnect_ok_status)),
		"the queued connection was not answered"
	);

	disconnect_server_check (server);

	disconnect_server_stop (server);

	test_check (disconnect_open_fds () == fds, "the full cerver did not close its connections");

}

#pragma endregion

#pragma region drain

static void *disconnect_shutdown_thread (void *cerver_ptr) {

	(void) cerver_shutdown ((Cerver *) cerver_ptr);

	return NULL;

}

// clients that leave while the cerver drains its connections to stop
static void test_disconnect_drain (void) {

	const size_t fds = disconnect_open_fds ();

	DisconnectOptions options = disconnect_options_default ();
	options.threads = 1;

	DisconnectServer *server = disconnect_server_start (&options);
	const DisconnectModel *model = &disconnect_models[0];

	DisconnectCounters before;
	disconnect_counters_read (server->http_cerver, &before);

	ThpoolStats stats;
	disconnect_thpool_stats (server, &stats);

	int blocked = -1;
	disconnect_gate_fill (server, &blocked, 1);

	// one client resets its queued connection, the other sends its complete request and closes
	DisconnectCase cases[2];
	disconnect_case_init (&cases[0], DISCONNECT_FORM_RESET);
	cases[0].model = model;

	disconnect_case_init (&cases[1], DISCONNECT_FORM_CLOSE);
	cases[1].model = model;
	cases[1].offset = model->length;

	DisconnectCounters expected;
	(void) memset (&expected, 0, sizeof (DisconnectCounters));
	expected.handled = 1;
	expected.before_middlewares = 1;
	expected.ok_responses = 1;

	for (unsigned int i = 0; i < 2; i++) {
		cases[i].fd = disconnect_connect (server->port, false);
		disconnect_case_expect (&cases[i], &expected);
	}

	disconnect_wait_jobs (server, stats.total_jobs + 3, false);

	for (unsigned int i = 0; i < 2; i++) {
		disconnect_case_send (&cases[i]);
		disconnect_case_leave (&cases[i]);
	}

	pthread_t shutdown_thread;
	test_check_int_eq (pthread_create (&shutdown_thread, NULL, disconnect_shutdown_thread, server->cerver), 0, NULL);

	disconnect_gate_release (&blocked, 1);

	test_check_int_eq (pthread_join (shutdown_thread, NULL), 0, NULL);
	test_check_int_eq (pthread_join (server->thread, NULL), 0, NULL);

	// the jobs that were drained finish before their counters are read
	disconnect_wait_idle (server);

	disconnect_counters_expect (server->http_cerver, &before, &expected, "drain", cases, 2);

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);
	free (server);

	test_check (disconnect_open_fds () == fds, "the drained cerver did not close its connections");

}

#pragma endregion

#pragma region main

static void disconnect_level_init (void) {

	const char *level = getenv (DISCONNECT_LEVEL_VARIABLE);
	disconnect_full = level && !strcmp (level, DISCONNECT_LEVEL_FULL);

	const char *seed = getenv (DISCONNECT_SEED_VARIABLE);
	disconnect_seed = (seed && seed[0]) ? (unsigned int) strtoul (seed, NULL, 10) : (unsigned int) time (NULL);

	(void) printf (
		"\tlevel %s - seed %u (%s=%u repeats it)\n",
		disconnect_full ? "full" : "sample", disconnect_seed, DISCONNECT_SEED_VARIABLE, disconnect_seed
	);

}

// the request matrix also runs with a POLL cerver, with the sample
static void test_disconnect_requests_poll (void) {

	DisconnectOptions options = disconnect_options_default ();
	options.handler_type = CERVER_HANDLER_TYPE_POLL;

	DisconnectServer *server = disconnect_server_start (&options);

	disconnect_request_matrix (server, false);

	disconnect_server_stop (server);

}

void http_tests_disconnect (void) {

	(void) printf ("Testing HTTP disconnect...\n");

	disconnect_level_init ();

	disconnect_files_create ();

	disconnect_job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);
	test_check_ptr (disconnect_job_queue);
	job_queue_set_handler (disconnect_job_queue, disconnect_job_work);
	test_check_unsigned_eq (job_queue_start (disconnect_job_queue), 0, NULL);

	DisconnectOptions options = disconnect_options_default ();
	disconnect_server = disconnect_server_start (&options);

	disconnect_models_create (disconnect_server->http_cerver);

	test_run (test_disconnect_requests);
	test_run (test_disconnect_handler);
	test_run (test_disconnect_half_close_not_gone);
	test_run (test_disconnect_worker);
	test_run (test_disconnect_responses);
	test_run (test_disconnect_statuses);
	test_run (test_disconnect_interrupted);
	test_run (test_disconnect_queue);
	test_run (test_disconnect_pipelined);

	disconnect_server_stop (disconnect_server);
	disconnect_server = NULL;

	(void) job_queue_stop (disconnect_job_queue);
	job_queue_delete (disconnect_job_queue);
	disconnect_job_queue = NULL;

	test_run (test_disconnect_requests_poll);
	test_run (test_disconnect_overload);
	test_run (test_disconnect_drain);

	disconnect_files_delete ();

	(void) printf ("Done!\n");

}

#pragma endregion
