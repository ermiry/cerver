#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include "cerver/events/internal.h"
#include <cerver/network/network.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include "cerver/http/core/types.h"
#include <cerver/http/core/uploads.h>
#include <cerver/http/headers/fields.h>
#include <cerver/http/requests/request.h>
#include "cerver/http/responses/prepared.h"
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include <cerver/threads/thpool.h>

#include "../handler.h"
#include "../test.h"
#include "http.h"

// runs a real web cerver in a thread and talks to it with raw sockets,
// to test the receive loop, the overload policies, the drain and the shutdown

// bugs that this suite pinned:
// - a client that sent a byte every few secs kept a thread forever (slowloris)
// - a client that never read its response blocked a thread in send () forever
// - a full thpool created detached threads without any limit
// - cerver_shutdown () did not wake up accept (), and an interrupted accept ()
//   stopped the cerver without draining its connections
// - cerver_teardown () released the routes while the threads still used them
// - the queued connections were freed without closing their fds at teardown
// - sock_set_blocking () was inverted and SO_REUSEADDR also set SO_REUSEPORT
// - the first of two HTTP cervers in a process released the common responses
//   and the pools of the other, and the multi-parts pool was never released
// - a handler that returned without a response closed the connection without one
// - a full non blocking socket buffer was retried a few times in a hot loop,
//   so a response, a file or a video arrived truncated
// - a file or a video opened as fd 0 was not sent
// coverage holes:
// - a drain that times out keeps the cerver memory on purpose (see thpool.c)

// the response of POST /json and the requests that are sent to it one after the other
#define SERVER_TEST_JSON_SIZE				(1 << 8)
#define SERVER_TEST_JSON_REQUESTS			(1 << 4)

#define SERVER_TEST_PORT_BASE				(1 << 13)
#define SERVER_TEST_ADDRESS					"127.0.0.1"

#define SERVER_TEST_RESPONSE_SIZE			(1 << 12)

// the event loops of the cervers of these tests, that have up to three of them at once: with the default of
// a web cerver, one for each processor, what they use depends on the machine that runs them
#define SERVER_TEST_IO_THREADS				1
// keeps the event loops that a cerver was created with
#define SERVER_TEST_IO_THREADS_DEFAULT		0

#define SERVER_TEST_CONNECT_RETRIES			(1 << 8)
// 2^22 ns
#define SERVER_TEST_CONNECT_INTERVAL		4194304L
// 2^25 ns
#define SERVER_TEST_SHORT_SLEEP				33554432L
// 2^28 ns
#define SERVER_TEST_SLOW_SLEEP				268435456L

#define SERVER_TEST_RECEIVE_TIMEOUT			16
#define SERVER_TEST_POLL_TIMEOUT			256

#define SERVER_TEST_HEADERS_TIMEOUT			1

// the sleeps of SERVER_TEST_SLOW_SLEEP that leave a cerver idle for more than twice its headers timeout
#define SERVER_TEST_IDLE_CERVER_SLEEPS		8
#define SERVER_TEST_REQUEST_TIMEOUT			2
#define SERVER_TEST_SEND_TIMEOUT			1
#define SERVER_TEST_DRAIN_TIMEOUT			8
#define SERVER_TEST_SLOW_BODY_SIZE			8

// a response that does not fit in the socket buffers
#define SERVER_TEST_BIG_RESPONSE_SIZE		(1 << 24)

// a response that the client reads completely
#define SERVER_TEST_LARGE_RESPONSE_SIZE		(1 << 22)
#define SERVER_TEST_LARGE_ALPHABET			26

#define SERVER_TEST_FILES_LIMIT				(1 << 5)
#define SERVER_TEST_FILES_CONNECTIONS		(1 << 6)

// each event loop keeps its own fds (its socket, its backend, its wake up): with one for each processor
// the cerver of the limit would not have the ones that it needs to start
#define SERVER_TEST_FILES_LOOPS				1

// the connections that a cerver with that limit can never hold
#define SERVER_TEST_FILES_EXTRA				(SERVER_TEST_FILES_CONNECTIONS - SERVER_TEST_FILES_LIMIT)

// a cerver that ran out of fds recovers once it closed what it could not handle,
// that takes longer in a loaded machine, so its first request is tried again until then
#define SERVER_TEST_RECOVER_SECONDS			16

#define SERVER_TEST_PRELOAD_VARIABLE		"LD_PRELOAD"
#define SERVER_TEST_VALGRIND_PRELOAD		"vgpreload"

// generous, so the suite also passes under valgrind
#define SERVER_TEST_JOIN_TIMEOUT			32

#define SERVER_TEST_HUGE_BODY_SIZE			((size_t) 100 << 20)
#define SERVER_TEST_HUGE_SLOW_BODY_SIZE		((size_t) 16 << 20)
#define SERVER_TEST_HUGE_BLOCK_SIZE			(SERVER_TEST_LARGE_ALPHABET * 4096)

#define SERVER_TEST_RESETS					(1 << 3)

#define SERVER_TEST_ALARM					256

#define SERVER_TEST_MILLISECONDS_PER_SECOND	1000

// a small receive window fills the socket buffer of the cerver right away
#define SERVER_TEST_SMALL_RECEIVE_BUFFER	(1 << 12)

// 2^27 ns, the client starts reading after many sends found the socket buffer full
#define SERVER_TEST_READER_DELAY			134217728L

// a poll () rounds its timeout to milliseconds, so the deadline can end a bit earlier
#define SERVER_TEST_SEND_TIMEOUT_MARGIN		(1 << 7)

// secs of the SO_SNDTIMEO of the connections, longer than the deadline of their responses
#define SERVER_TEST_LONG_SEND_TIMEOUT		4

// milliseconds of all the sends of a response, and how much later it can end (also under valgrind)
#define SERVER_TEST_RESPONSE_SEND_TIMEOUT	500
#define SERVER_TEST_RESPONSE_SEND_TOLERANCE	(1 << 8)

// a negative wait keeps the default of the cerver
#define SERVER_TEST_DEFAULT_WAIT_TIMEOUT	(-1)

// secs that a request of a cerver with event loops waits for a place in the thpool
#define SERVER_TEST_WAIT_TIMEOUT			1

// the name of a thread, with its end
#define SERVER_TEST_THREAD_NAME_SIZE		16

// the names of the threads of a thpool start with it, the ones of the event loops do not
#define SERVER_TEST_POOL_THREAD_PREFIX		"thpool"

// the requests that a cerver with two event loops gets: both loops accept some of them
#define SERVER_TEST_LOOPS_REQUESTS			(1 << 6)

#define SERVER_TEST_UPLOADS_PATH			"/tmp/cerver-server-uploads"
#define SERVER_TEST_UPLOADS_MODE			0755

#define SERVER_TEST_FILENAME_SIZE			256
#define SERVER_TEST_FILE_TEMPLATE			"/tmp/cerver-server-XXXXXX.mp4"
#define SERVER_TEST_FILE_SUFFIX_LENGTH		4
#define SERVER_TEST_FILE_SIZE				(1 << 22)

// the last bytes of the file that a suffix range asks for: they start at a position
// that is a multiple of the alphabet of the file, so they are checked like a whole body
#define SERVER_TEST_SUFFIX_SIZE				52010
#define SERVER_TEST_RANGE_SIZE				100

static const char *ok_request = {
	"GET /ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *own_request = {
	"GET /own HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *block_request = {
	"GET /block HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *big_request = {
	"GET /big HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *large_request = {
	"GET /group/large HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *slow_headers_request = {
	"GET /ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"X-Slow: abcdefghijklmnopqrstuvwxyz\r\n"
	"\r\n"
};

static const char *slow_body_headers = {
	"POST /echo HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
};

static const char *slow_body = { "12345678" };

// a literal, so the compiler checks its arguments
#define SERVER_TEST_HUGE_REQUEST_FORMAT		\
	"POST /huge HTTP/1.1\r\n"				\
	"Host: localhost\r\n"					\
	"Content-Length: %zu\r\n"				\
	"\r\n"

static const char *unanswered_request = {
	"GET /unanswered HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *gone_request = {
	"GET /gone HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *nonblocking_large_request = {
	"GET /nonblocking/large HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *nonblocking_big_request = {
	"GET /nonblocking/big HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *nonblocking_file_request = {
	"GET /nonblocking/file HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *nonblocking_video_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=0-\r\n"
	"\r\n"
};

// the last SERVER_TEST_SUFFIX_SIZE bytes of the video
static const char *nonblocking_suffix_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=-52010\r\n"
	"\r\n"
};

// RFC 9110: without a Range the whole video with 200
static const char *video_whole_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

// the last byte of a range is included, SERVER_TEST_RANGE_SIZE bytes
static const char *video_range_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=0-99\r\n"
	"\r\n"
};

// a last byte before the first one is not a range, the whole video
static const char *video_invalid_range_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=9-1\r\n"
	"\r\n"
};

// a range that starts after the end of the video, 416
static const char *video_unsatisfiable_request = {
	"GET /nonblocking/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=1099511627776-\r\n"
	"\r\n"
};

static const char *deadline_big_request = {
	"GET /deadline/big HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *deadline_video_request = {
	"GET /deadline/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=0-\r\n"
	"\r\n"
};

static const char *zero_file_request = {
	"GET /zero/file HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *zero_video_request = {
	"GET /zero/video HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Range: bytes=0-\r\n"
	"\r\n"
};

// a body that is JSON, with an escape in its name and a member that is not read
static const char *json_request = {
	"POST /json HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 59\r\n"
	"\r\n"
	"{\"name\": \"cer\\u0076er\", \"values\": [1, 2, 3], \"other\": null}"
};

static const char *json_response = { "{\"name\": \"cerver\", \"count\": 3}" };

// a body that is cut, and one whose name is not a string
static const char *json_invalid_request = {
	"POST /json HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 18\r\n"
	"\r\n"
	"{\"name\": \"cerver\","
};

static const char *json_other_request = {
	"POST /json HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 11\r\n"
	"\r\n"
	"{\"name\": 7}"
};

static const char *json_empty_request = {
	"POST /json HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Length: 0\r\n"
	"\r\n"
};

static const char *null_device = { "/dev/null" };

static const char *unanswered_metric = { "cerver_http_requests_unanswered_total 1\n" };

// the header that a cerver of these tests adds to its responses to tell which one it is
static const char *server_test_instance_header = { "X-Instance" };
static const char *server_test_first_instance = { "first" };
static const char *server_test_second_instance = { "second" };
static const char *server_test_first_instance_line = { "\r\nX-Instance: first\r\n" };
static const char *server_test_second_instance_line = { "\r\nX-Instance: second\r\n" };

// the value that the handler of /own gives to that header in its response
static const char *server_test_own_instance = { "own" };
static const char *server_test_own_instance_line = { "\r\nX-Instance: own\r\n" };
static const char *server_test_instance_line_start = { "\r\nX-Instance: " };

static const char *status_ok = { "HTTP/1.1 200" };
static const char *status_bad_request = { "HTTP/1.1 400" };
static const char *status_partial_content = { "HTTP/1.1 206" };
static const char *status_range_not_satisfiable = { "HTTP/1.1 416" };
static const char *status_server_error = { "HTTP/1.1 500" };
static const char *status_payload_too_large = { "HTTP/1.1 413" };
static const char *status_request_timeout = { "HTTP/1.1 408" };
static const char *status_service_unavailable = { "HTTP/1.1 503" };
static const char *retry_after_header = { "Retry-After: " HTTP_RESPONSE_RETRY_AFTER_SECONDS };
static const char *content_length_header = { "Content-Length: " };
static const char *headers_end = { "\r\n\r\n" };

static unsigned int server_test_port = SERVER_TEST_PORT_BASE;

#pragma region gate

// keeps the /block requests working until it is opened
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_cond = PTHREAD_COND_INITIALIZER;
static bool gate_open = false;
static unsigned int gate_waiting = 0;

static void server_test_gate_reset (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = false;
	gate_waiting = 0;
	(void) pthread_mutex_unlock (&gate_mutex);

}

static void server_test_gate_open (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_open = true;
	(void) pthread_cond_broadcast (&gate_cond);
	(void) pthread_mutex_unlock (&gate_mutex);

}

static unsigned int server_test_gate_waiting (void) {

	(void) pthread_mutex_lock (&gate_mutex);
	unsigned int waiting = gate_waiting;
	(void) pthread_mutex_unlock (&gate_mutex);

	return waiting;

}

static void server_test_sleep (const long nanoseconds) {

	struct timespec interval = { 0, nanoseconds };
	(void) nanosleep (&interval, NULL);

}

// waits until the /block requests are working
static void server_test_gate_wait_for (const unsigned int waiting) {

	while (server_test_gate_waiting () < waiting) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

}

#pragma endregion

#pragma region routes

// the /ok requests that were answered: the test waits for it after reading a response, because with io_uring the
// cerver sends without a syscall that TSan sees, so the response alone does not order what the cerver did before
static unsigned int server_test_ok_answered = 0;

// GET /ok
static void server_test_ok_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

	(void) __atomic_add_fetch (&server_test_ok_answered, 1, __ATOMIC_SEQ_CST);

}

// GET /own, a response with its own value of the header that its cerver adds to the others
static void server_test_own_handler (CerverHttpContext *context) {

	(void) cerver_http_response_set_header_with_name (
		context, server_test_instance_header, server_test_own_instance
	);

	(void) cerver_http_response_send_text (context, server_test_own_instance, strlen (server_test_own_instance));

}

// GET /block
static void server_test_block_handler (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

static char *big_response = NULL;
static unsigned int big_started = 0;
static unsigned int big_sent = 0;
static unsigned int big_failed = 0;

// GET /big, the client never reads it
static void server_test_big_handler (CerverHttpContext *context) {

	(void) __atomic_add_fetch (&big_started, 1, __ATOMIC_SEQ_CST);

	if (cerver_http_response_send_text (
		context, big_response, SERVER_TEST_BIG_RESPONSE_SIZE
	)) {
		(void) __atomic_add_fetch (&big_failed, 1, __ATOMIC_SEQ_CST);
	}

	else {
		(void) __atomic_add_fetch (&big_sent, 1, __ATOMIC_SEQ_CST);
	}

}

// POST /echo
static void server_test_echo_handler (CerverHttpContext *context) {

	const CerverHttpView body = cerver_http_request_body (context);

	if (!cerver_http_view_is_missing (body) && (body.length == SERVER_TEST_SLOW_BODY_SIZE)) {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// POST /json
// reads its body with the JSON of cerver and answers with what it has: the name and how many values
static void server_test_json_handler (CerverHttpContext *context) {

	char response[SERVER_TEST_JSON_SIZE];
	CerverJsonWriter writer;
	CerverJsonError error = { CERVER_JSON_ERROR_NONE, 0 };

	const CerverJsonValue *json = cerver_http_request_json (context, &error);
	const CerverJsonValue *name = cerver_json_object_get (json, "name");

	// the second call answers the same value: the body is read once
	const bool same = (cerver_http_request_json (context, NULL) == json);

	cerver_json_writer_init (&writer, response, sizeof (response), CERVER_JSON_FLAGS_NONE);
	cerver_json_writer_object_begin (&writer);
	cerver_json_writer_key (&writer, "name");
	cerver_json_writer_string (&writer, cerver_json_string_value (name), cerver_json_string_length (name));
	cerver_json_writer_key (&writer, "count");
	cerver_json_writer_unsigned (&writer, cerver_json_array_size (cerver_json_object_get (json, "values")));
	cerver_json_writer_object_end (&writer);

	if (same && (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE)) {
		(void) cerver_http_response_send_json (
			context, response, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// the huge body repeats a block whose size is a multiple of the alphabet,
// so it is written and checked a block at a time (a byte at a time is too slow with TSan)
static char server_test_huge_block[SERVER_TEST_HUGE_BLOCK_SIZE];

static size_t server_test_huge_size = SERVER_TEST_HUGE_BODY_SIZE;

static void server_test_huge_block_create (void) {

	for (size_t idx = 0; idx < SERVER_TEST_HUGE_BLOCK_SIZE; idx++) {
		server_test_huge_block[idx] = (char) ('a' + (idx % SERVER_TEST_LARGE_ALPHABET));
	}

}

// writes or checks size bytes of the body, starting at a block boundary
static size_t server_test_huge_part (const size_t position) {

	const size_t left = server_test_huge_size - position;

	return (left < SERVER_TEST_HUGE_BLOCK_SIZE) ? left : SERVER_TEST_HUGE_BLOCK_SIZE;

}

// POST /huge checks every byte of the body
static void server_test_huge_handler (CerverHttpContext *context) {

	const CerverHttpView body = cerver_http_request_body (context);

	bool intact = !cerver_http_view_is_missing (body) && (body.length == server_test_huge_size);
	for (size_t position = 0; intact && (position < server_test_huge_size); position += SERVER_TEST_HUGE_BLOCK_SIZE) {
		intact = !memcmp (body.data + position, server_test_huge_block, server_test_huge_part (position));
	}

	(void) cerver_http_response_send_common (
		context, intact ? CERVER_HTTP_COMMON_RESPONSE_OK : CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST
	);

}

// GET /unanswered returns without sending a response
static void server_test_unanswered_handler (CerverHttpContext *context) {

	(void) context;

}

static unsigned int gone_answered = 0;

// GET /gone answers after its client has closed the connection
static void server_test_gone_handler (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

	(void) __atomic_add_fetch (&gone_answered, 1, __ATOMIC_SEQ_CST);

}

// the result of the last response sent by a handler with a non blocking socket
static unsigned int nonblocking_done = 0;
static unsigned int nonblocking_failed = 0;
static unsigned long nonblocking_milliseconds = 0;

static char server_test_file[SERVER_TEST_FILENAME_SIZE] = { 0 };

static double server_test_now (void);

// the handler's socket stops blocking, so a full socket buffer answers send () with EAGAIN
static double server_test_nonblocking_start (const CerverHttpContext *context) {

	(void) sock_set_blocking (cerver_http_context_socket (context), false);

	return server_test_now ();

}

static void server_test_nonblocking_end (const double start, const u8 result) {

	const double milliseconds = (server_test_now () - start) * SERVER_TEST_MILLISECONDS_PER_SECOND;

	__atomic_store_n (&nonblocking_milliseconds, (unsigned long) milliseconds, __ATOMIC_SEQ_CST);
	__atomic_store_n (&nonblocking_failed, (unsigned int) (result != 0), __ATOMIC_SEQ_CST);
	(void) __atomic_add_fetch (&nonblocking_done, 1, __ATOMIC_SEQ_CST);

}

static u8 server_test_large_send (CerverHttpContext *context);

// GET /nonblocking/large
static void server_test_nonblocking_large_handler (CerverHttpContext *context) {

	const double start = server_test_nonblocking_start (context);

	server_test_nonblocking_end (start, server_test_large_send (context));

}

// GET /nonblocking/big, the client never reads it
static void server_test_nonblocking_big_handler (CerverHttpContext *context) {

	const double start = server_test_nonblocking_start (context);

	server_test_nonblocking_end (
		start,
		cerver_http_response_send_text (context, big_response, SERVER_TEST_BIG_RESPONSE_SIZE)
	);

}

// GET /nonblocking/file
static void server_test_nonblocking_file_handler (CerverHttpContext *context) {

	const double start = server_test_nonblocking_start (context);

	server_test_nonblocking_end (start, cerver_http_response_send_file (context, server_test_file));

}

// GET /zero/file, in a child process, closes its stdin so the file is opened as fd 0
static void server_test_zero_file_handler (CerverHttpContext *context) {

	(void) close (STDIN_FILENO);

	(void) cerver_http_response_send_file (context, server_test_file);

}

// GET /zero/video, in a child process, closes its stdin so the video is opened as fd 0
static void server_test_zero_video_handler (CerverHttpContext *context) {

	(void) close (STDIN_FILENO);

	(void) cerver_http_response_send_video (context, server_test_file);

}

// GET /nonblocking/video
static void server_test_nonblocking_video_handler (CerverHttpContext *context) {

	const double start = server_test_nonblocking_start (context);

	server_test_nonblocking_end (start, cerver_http_response_send_video (context, server_test_file));

}

// GET /deadline/big, in the blocking socket of the connection, the client never reads it
static void server_test_deadline_big_handler (CerverHttpContext *context) {

	const double start = server_test_now ();

	server_test_nonblocking_end (
		start,
		cerver_http_response_send_text (context, big_response, SERVER_TEST_BIG_RESPONSE_SIZE)
	);

}

// GET /deadline/video, its header and its file are sent with the same deadline
static void server_test_deadline_video_handler (CerverHttpContext *context) {

	const double start = server_test_now ();

	server_test_nonblocking_end (start, cerver_http_response_send_video (context, server_test_file));

}

#pragma endregion

#pragma region server

typedef struct ServerTestOptions {

	u16 threads;
	unsigned int queue_size;

	CerverOverloadPolicy overload_policy;

	CerverDrainPolicy drain_policy;
	u32 drain_timeout;

	u32 send_timeout;

	unsigned int headers_timeout;
	unsigned int request_timeout;

	// milliseconds of all the sends of a response, 0 uses send_timeout
	unsigned int response_send_timeout;

	// what only a cerver with event loops uses:
	// the secs that a request waits for a place in the thpool, a negative value keeps the default
	int overload_wait_timeout;

	// its I/O threads and its limit of connections, 0 keeps the defaults
	unsigned int io_threads;
	unsigned int max_connections;

	// the max size of the body of a request, 0 keeps the default
	size_t max_body_size;

	// the cerver keeps the files of the uploads instead of deleting them when their request is done
	bool keep_uploads;

	// the value of the header that the cerver adds to its responses to tell which one it is, NULL for none
	const char *instance;

} ServerTestOptions;

typedef struct ServerTest {

	Cerver *cerver;
	u16 port;

	pthread_t thread;
	u8 start_result;
	bool started;

	// cerver_start () returned, and the last error of its thread: read to say why a cerver never listened
	bool ended;
	int start_error;

} ServerTest;

static ServerTestOptions server_test_options_default (void) {

	ServerTestOptions options = {
		1, 1,
		CERVER_OVERLOAD_POLICY_WAIT,
		CERVER_DRAIN_POLICY_RUN, SERVER_TEST_DRAIN_TIMEOUT,
		CERVER_DEFAULT_SEND_TIMEOUT,
		CERVER_HTTP_DEFAULT_HEADERS_TIMEOUT, CERVER_HTTP_DEFAULT_REQUEST_TIMEOUT,
		0,
		SERVER_TEST_DEFAULT_WAIT_TIMEOUT, SERVER_TEST_IO_THREADS, 0,
		0,
		false,
		NULL
	};

	return options;

}

static void *server_test_thread (void *server_ptr) {

	ServerTest *server = (ServerTest *) server_ptr;

	server->start_result = cerver_start (server->cerver);
	server->start_error = errno;

	__atomic_store_n (&server->ended, true, __ATOMIC_SEQ_CST);

	return NULL;

}

// a byte of the large response, so a part sent twice or skipped is found
static char server_test_large_byte (const size_t position) {

	return (char) ('a' + (position % SERVER_TEST_LARGE_ALPHABET));

}

// sends a response bigger than the socket buffer, in many partial writes
// returns 0 on success, 1 on error
static u8 server_test_large_send (CerverHttpContext *context) {

	u8 retval = 1;

	char *body = (char *) malloc (SERVER_TEST_LARGE_RESPONSE_SIZE);
	if (body) {
		for (size_t idx = 0; idx < SERVER_TEST_LARGE_RESPONSE_SIZE; idx++) {
			body[idx] = server_test_large_byte (idx);
		}

		retval = cerver_http_response_send (context, body, SERVER_TEST_LARGE_RESPONSE_SIZE);

		free (body);
	}

	return retval;

}

// GET /group/large
static void server_test_large_handler (CerverHttpContext *context) {

	(void) server_test_large_send (context);

}

// the name of the thread that ran the last custom authentication and the last handler of /secure,
// written by the threads of the cerver and read by the test
static pthread_mutex_t server_test_names_mutex = PTHREAD_MUTEX_INITIALIZER;
static char server_test_authentication_thread[SERVER_TEST_THREAD_NAME_SIZE] = { 0 };
static char server_test_handler_thread[SERVER_TEST_THREAD_NAME_SIZE] = { 0 };

// keeps the name of the thread that calls it
static void server_test_thread_name_take (char *destination) {

	(void) pthread_mutex_lock (&server_test_names_mutex);
	(void) prctl (PR_GET_NAME, destination);
	(void) pthread_mutex_unlock (&server_test_names_mutex);

}

static void server_test_thread_name_clear (char *name) {

	(void) pthread_mutex_lock (&server_test_names_mutex);
	name[0] = '\0';
	(void) pthread_mutex_unlock (&server_test_names_mutex);

}

static bool server_test_thread_name_is_empty (const char *name) {

	(void) pthread_mutex_lock (&server_test_names_mutex);
	const bool empty = (name[0] == '\0');
	(void) pthread_mutex_unlock (&server_test_names_mutex);

	return empty;

}

// returns whether the thread that was kept is one of a thpool
static bool server_test_thread_name_is_pool (const char *name) {

	(void) pthread_mutex_lock (&server_test_names_mutex);
	const bool is_pool = !strncmp (name, SERVER_TEST_POOL_THREAD_PREFIX, strlen (SERVER_TEST_POOL_THREAD_PREFIX));
	(void) pthread_mutex_unlock (&server_test_names_mutex);

	return is_pool;

}

static const char *server_test_valid_authorization = { "Bearer server-test" };

// the custom authentication of /secure: a method of the application that can wait
static unsigned int server_test_authentication (CerverHttpContext *context) {

	server_test_thread_name_take (server_test_authentication_thread);

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	return cerver_http_view_equals (authorization, server_test_valid_authorization) ? 0 : 1;

}

// GET /secure/ok
static void server_test_secure_handler (CerverHttpContext *context) {

	server_test_thread_name_take (server_test_handler_thread);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// POST /upload, the files of its request are deleted when it ends
static void server_test_upload_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// a top level route: a group with the endpoint of its path
static void server_test_route (
	CerverHttp *http_cerver, const CerverHttpMethod method, const char *path,
	const CerverHttpHandler handler
) {

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, path);
	test_check_ptr (group);

	test_check_ptr (cerver_http_group_root_add (group, method, handler));

}

static void server_test_routes (CerverHttp *http_cerver) {

	server_test_route (http_cerver, REQUEST_METHOD_GET, "ok", server_test_ok_handler);
	server_test_route (http_cerver, REQUEST_METHOD_GET, "own", server_test_own_handler);

	CerverHttpGroup *secure = cerver_http_group_create (http_cerver, "secure");
	cerver_http_group_set_auth (secure, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (secure, server_test_authentication);
	test_check_ptr (cerver_http_group_get (secure, "ok", server_test_secure_handler));
	test_check_ptr (cerver_http_group_post (secure, "echo", server_test_echo_handler));

	CerverHttpGroup *files = cerver_http_group_create (http_cerver, "files");
	CerverHttpEndpoint *upload = cerver_http_group_post (files, "upload", server_test_upload_handler);
	test_check_ptr (upload);
	cerver_http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);

	cerver_http_set_uploads_path (http_cerver, SERVER_TEST_UPLOADS_PATH);
	cerver_http_set_default_uploads_filename_generator (http_cerver);
	cerver_http_set_uploads_delete_when_done (http_cerver, true);

	server_test_route (http_cerver, REQUEST_METHOD_GET, "block", server_test_block_handler);

	server_test_route (http_cerver, REQUEST_METHOD_GET, "big", server_test_big_handler);

	server_test_route (http_cerver, REQUEST_METHOD_POST, "echo", server_test_echo_handler);

	server_test_route (http_cerver, REQUEST_METHOD_POST, "json", server_test_json_handler);

	server_test_route (http_cerver, REQUEST_METHOD_POST, "huge", server_test_huge_handler);

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, "group");
	test_check_ptr (cerver_http_group_get (group, "large", server_test_large_handler));

	server_test_route (http_cerver, REQUEST_METHOD_GET, "unanswered", server_test_unanswered_handler);

	server_test_route (http_cerver, REQUEST_METHOD_GET, "gone", server_test_gone_handler);

	CerverHttpGroup *nonblocking = cerver_http_group_create (http_cerver, "nonblocking");
	test_check_ptr (cerver_http_group_get (nonblocking, "large", server_test_nonblocking_large_handler));
	test_check_ptr (cerver_http_group_get (nonblocking, "big", server_test_nonblocking_big_handler));
	test_check_ptr (cerver_http_group_get (nonblocking, "file", server_test_nonblocking_file_handler));
	test_check_ptr (cerver_http_group_get (nonblocking, "video", server_test_nonblocking_video_handler));

	CerverHttpGroup *deadline = cerver_http_group_create (http_cerver, "deadline");
	test_check_ptr (cerver_http_group_get (deadline, "big", server_test_deadline_big_handler));
	test_check_ptr (cerver_http_group_get (deadline, "video", server_test_deadline_video_handler));

	CerverHttpGroup *zero = cerver_http_group_create (http_cerver, "zero");
	test_check_ptr (cerver_http_group_get (zero, "file", server_test_zero_file_handler));
	test_check_ptr (cerver_http_group_get (zero, "video", server_test_zero_video_handler));

}

static Cerver *server_test_cerver_create (
	const u16 port, const ServerTestOptions *options
) {

	Cerver *cerver = cerver_create_web ("server-test", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_events_backend (cerver, test_events_backend ());
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, options->threads);
	cerver_set_thpool_queue_size (cerver, options->queue_size);
	cerver_set_overload_policy (cerver, options->overload_policy);
	cerver_set_drain_policy (cerver, options->drain_policy);
	cerver_set_drain_timeout (cerver, options->drain_timeout);
	cerver_set_send_timeout (cerver, options->send_timeout);

	if (options->overload_wait_timeout >= 0) {
		cerver_set_overload_wait_timeout (cerver, (u32) options->overload_wait_timeout);
	}

	cerver_set_io_threads (cerver, options->io_threads);
	cerver_set_max_connections (cerver, options->max_connections);

	CerverHttp *http_cerver = cerver_http_get (cerver);

	// the handlers of these tests wait in the gate and fill the thpool, they count its jobs and expect
	// the 503 of a full one: they run in the threads of the default pool, not in the event loops
	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME), 0, NULL);

	cerver_http_set_headers_timeout (http_cerver, options->headers_timeout);
	cerver_http_set_request_timeout (http_cerver, options->request_timeout);
	cerver_http_set_send_timeout_milliseconds (http_cerver, options->response_send_timeout);

	if (options->max_body_size) cerver_http_set_max_body_size (http_cerver, options->max_body_size);

	if (options->instance) {
		test_check_unsigned_eq (
			cerver_http_add_responses_header_with_name (http_cerver, server_test_instance_header, options->instance),
			0, NULL
		);
	}

	// the clients of these tests read each response until the cerver closes its connection,
	// the connections that are kept have their own tests (keepalive.c)
	cerver_http_set_keep_alive (http_cerver, false);

	server_test_routes (http_cerver);

	// after the routes, which set the uploads to be deleted when their request is done
	if (options->keep_uploads) cerver_http_set_uploads_delete_when_done (http_cerver, false);

	return cerver;

}

static int server_test_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, SERVER_TEST_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static int server_test_connect (const u16 port) {

	int fd = -1;
	for (unsigned int i = 0; (i < SERVER_TEST_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = server_test_connect_once (port);
		if (fd < 0) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check (fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (fd, SERVER_TEST_RECEIVE_TIMEOUT);

	return fd;

}

static void server_test_send (const int fd, const char *data);

static size_t server_test_read (const int fd, char *response);

// waits until the handler of the next /ok request returned, after the count of answered ones that was read before it:
// what the cerver did before that request is then ordered before what this thread reads of it
static void server_test_ok_wait (const unsigned int answered) {

	test_check (
		test_wait_counter (&server_test_ok_answered, answered + 1, TEST_WAIT_SECONDS) > answered,
		"the handler of the /ok request did not return"
	);

}

// a cerver that never listened: cerver_start () ended, with its result and the last error of its thread
// (the log of the cerver is not started in these tests), or it is still running
static void server_test_start_report (ServerTest *server) {

	if (__atomic_load_n (&server->ended, __ATOMIC_SEQ_CST)) {
		(void) fprintf (
			stderr, "cerver_start () of the cerver in port %u ended with %u - errno %d (%s)\n",
			(unsigned int) server->port, (unsigned int) server->start_result,
			server->start_error, strerror (server->start_error)
		);
	}

	else {
		(void) fprintf (
			stderr, "cerver_start () of the cerver in port %u did not end\n", (unsigned int) server->port
		);
	}

}

// connects to a cerver that is starting, and says what its start did when it never listened
static int server_test_start_connect (ServerTest *server) {

	int fd = -1;
	for (unsigned int i = 0; (i < SERVER_TEST_CONNECT_RETRIES) && (fd < 0); i++) {
		fd = server_test_connect_once (server->port);
		if (fd < 0) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	if (fd < 0) server_test_start_report (server);

	test_check (fd > -1, "failed to connect to the test cerver that started");

	(void) sock_set_timeout (fd, SERVER_TEST_RECEIVE_TIMEOUT);

	return fd;

}

static ServerTest *server_test_start (const ServerTestOptions *options) {

	ServerTest *server = (ServerTest *) calloc (1, sizeof (ServerTest));
	test_check_ptr (server);

	server->port = (u16) server_test_port++;
	server->cerver = server_test_cerver_create (server->port, options);

	const unsigned int answered = __atomic_load_n (&server_test_ok_answered, __ATOMIC_SEQ_CST);

	test_check_int_eq (pthread_create (&server->thread, NULL, server_test_thread, server), 0, NULL);
	server->started = true;

	// waits until the cerver is listening and has handled a complete request
	int fd = server_test_start_connect (server);
	server_test_send (fd, ok_request);
	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

	server_test_ok_wait (answered);

	return server;

}

// cerver_start () must return in time after it was asked to stop
static void server_test_join (ServerTest *server) {

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += SERVER_TEST_JOIN_TIMEOUT;

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);
	server->started = false;

}

static void server_test_stop (ServerTest *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	server_test_join (server);

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

#pragma endregion

#pragma region client

static void server_test_send (const int fd, const char *data) {

	const size_t length = strlen (data);
	test_check (send (fd, data, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

}

// reads the whole response until the cerver closes the connection
static size_t server_test_read (const int fd, char *response) {

	size_t total = 0;
	ssize_t received = 0;
	while (
		(total < (SERVER_TEST_RESPONSE_SIZE - 1))
		&& ((received = recv (fd, response + total, SERVER_TEST_RESPONSE_SIZE - 1 - total, 0)) > 0)
	) {
		total += (size_t) received;
	}

	response[total] = '\0';

	return total;

}

static bool server_test_starts_with (const char *response, const char *prefix) {

	return !strncmp (response, prefix, strlen (prefix));

}

static void server_test_expect (const int fd, const char *status) {

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);

	if (!server_test_starts_with (response, status)) {
		(void) fprintf (stderr, "expected %s - received: %s\n", status, response);
	}

	test_check (server_test_starts_with (response, status), "unexpected response status");

}

// sends a request in a new connection and checks its response
static void server_test_request (const u16 port, const char *request, const char *status) {

	int fd = server_test_connect (port);
	server_test_send (fd, request);
	server_test_expect (fd, status);
	(void) close (fd);

}

// sends an /ok request and waits until its handler returned, like server_test_start ()
static void server_test_request_ok (const u16 port) {

	const unsigned int answered = __atomic_load_n (&server_test_ok_answered, __ATOMIC_SEQ_CST);

	server_test_request (port, ok_request, status_ok);

	server_test_ok_wait (answered);

}

// sends the request in a new connection until its response has the status,
// a connection that the cerver closes without an answer is tried again
// returns true if the status arrived before the deadline
static bool server_test_request_until (
	const u16 port, const char *request, const char *status, const unsigned int seconds
) {

	bool answered = false;

	const double deadline = test_wait_now () + (double) seconds;
	while (!answered && (test_wait_now () < deadline)) {
		int fd = server_test_connect (port);

		// the cerver can close the connection before the request is sent
		(void) send (fd, request, strlen (request), MSG_NOSIGNAL);

		char response[SERVER_TEST_RESPONSE_SIZE];
		(void) server_test_read (fd, response);
		(void) close (fd);

		answered = server_test_starts_with (response, status);
		if (!answered) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	return answered;

}

// returns true if the cerver has sent something to the connection
static bool server_test_has_response (const int fd) {

	struct pollfd poll_fd = { fd, POLLIN, 0 };

	return (poll (&poll_fd, 1, SERVER_TEST_POLL_TIMEOUT) > 0);

}

static double server_test_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / 1e9);

}

#pragma endregion

#pragma region deadlines

static void test_server_ok (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// a handler reads the body of its request as JSON, in the arena of the request, and writes its answer
static void test_server_json (void) {

	char response[SERVER_TEST_RESPONSE_SIZE];

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send (fd, json_request);
	(void) server_test_read (fd, response);
	(void) close (fd);

	test_check (server_test_starts_with (response, status_ok), "the JSON body was not answered with 200");
	test_check_ptr (strstr (response, json_response));

	// a body that is not JSON, one without what the handler asks for and a request without a body
	server_test_request (server->port, json_invalid_request, status_bad_request);
	server_test_request (server->port, json_other_request, status_bad_request);
	server_test_request (server->port, json_empty_request, status_bad_request);

	// and the next request of the thread is read in the same arena
	for (unsigned int i = 0; i < SERVER_TEST_JSON_REQUESTS; i++) {
		server_test_request (server->port, json_request, status_ok);
	}

	server_test_stop (server);

}

// reads the whole response in a buffer of its exact maximum size
static size_t server_test_read_large (const int fd, char *response, const size_t size) {

	size_t total = 0;
	ssize_t received = 0;
	while ((total < size) && ((received = recv (fd, response + total, size - total, 0)) > 0)) {
		total += (size_t) received;
	}

	return total;

}

// a routes registration error stops the cerver before it listens
static void test_server_routes_error (void) {

	ServerTestOptions options = server_test_options_default ();
	const u16 port = (u16) server_test_port++;

	Cerver *cerver = server_test_cerver_create (port, &options);
	CerverHttp *http_cerver = cerver_http_get (cerver);

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, "broken");
	test_check_ptr (cerver_http_group_get (group, "items", server_test_ok_handler));
	test_check_ptr (cerver_http_group_get (group, "items", server_test_ok_handler));

	test_check_unsigned_ne (cerver_start (cerver), 0);
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 1, NULL);
	test_check_ptr (strstr (cerver_http_routes_error (http_cerver), "already registered"));

	// nothing listens in its port
	test_check_int_eq (server_test_connect_once (port), -1, NULL);

	(void) cerver_teardown (cerver);

}

// every byte of a response bigger than the socket buffer arrives once and in order
static void test_server_large_response (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	const size_t size = SERVER_TEST_LARGE_RESPONSE_SIZE + SERVER_TEST_RESPONSE_SIZE;
	char *response = (char *) malloc (size);
	test_check_ptr (response);

	int fd = server_test_connect (server->port);
	server_test_send (fd, large_request);
	const size_t total = server_test_read_large (fd, response, size);
	(void) close (fd);

	test_check (server_test_starts_with (response, status_ok), "the large response is not 200");

	const char *end = strstr (response, headers_end);
	test_check_ptr (end);

	const char *body = end + strlen (headers_end);
	const size_t body_len = total - (size_t) (body - response);
	test_check_unsigned_eq (body_len, SERVER_TEST_LARGE_RESPONSE_SIZE, NULL);

	bool intact = true;
	for (size_t idx = 0; intact && (idx < body_len); idx++) {
		intact = (body[idx] == server_test_large_byte (idx));
	}

	test_check (intact, "the large response arrived with wrong bytes");

	free (response);

	server_test_stop (server);

}

// the client resets the connection while a large response is being sent:
// the cerver does not die from SIGPIPE, releases the connection and keeps serving
static void test_server_reset_during_response (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	char response[SERVER_TEST_RESPONSE_SIZE];
	const struct linger reset = { 1, 0 };

	for (unsigned int i = 0; i < SERVER_TEST_RESETS; i++) {
		int fd = server_test_connect (server->port);
		server_test_send (fd, large_request);

		test_check (recv (fd, response, SERVER_TEST_RESPONSE_SIZE, 0) > 0, "the large response did not start");

		// a close with a zero linger sends a RST
		(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &reset, sizeof (struct linger));
		(void) close (fd);
	}

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

static void server_test_send_all (const int fd, const char *data, const size_t size) {

	size_t total = 0;
	while (total < size) {
		const ssize_t sent = send (fd, data + total, size - total, MSG_NOSIGNAL);
		test_check (sent > 0, "failed to send the huge body");
		total += (size_t) sent;
	}

}

static void server_test_huge_headers (const int fd) {

	char headers[SERVER_TEST_RESPONSE_SIZE];
	(void) snprintf (headers, SERVER_TEST_RESPONSE_SIZE, SERVER_TEST_HUGE_REQUEST_FORMAT, server_test_huge_size);

	server_test_send (fd, headers);

}

// TSan and valgrind make every byte of the body slow, so they send a smaller one
static size_t server_test_huge_body_size (void) {

	#if defined(__SANITIZE_THREAD__)
	return SERVER_TEST_HUGE_SLOW_BODY_SIZE;
	#else
	const char *preload = getenv (SERVER_TEST_PRELOAD_VARIABLE);
	return (preload && strstr (preload, SERVER_TEST_VALGRIND_PRELOAD)) ?
		SERVER_TEST_HUGE_SLOW_BODY_SIZE : SERVER_TEST_HUGE_BODY_SIZE;
	#endif

}

// a body of 100 MB arrives complete with the default limit,
// and a lower limit rejects it with 413 from its Content-Length, before the body is sent
static void test_server_huge_body (void) {

	server_test_huge_size = server_test_huge_body_size ();

	// the handler reads the block in a thread of the cerver, so it is written before the cerver
	// creates its threads: the body that it checks arrives through the socket, which with io_uring
	// is not a synchronization that TSan sees
	server_test_huge_block_create ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	char *body = (char *) malloc (server_test_huge_size);
	test_check_ptr (body);
	for (size_t position = 0; position < server_test_huge_size; position += SERVER_TEST_HUGE_BLOCK_SIZE) {
		(void) memcpy (body + position, server_test_huge_block, server_test_huge_part (position));
	}

	int fd = server_test_connect (server->port);
	server_test_huge_headers (fd);
	server_test_send_all (fd, body, server_test_huge_size);
	server_test_expect (fd, status_ok);
	(void) close (fd);

	free (body);

	server_test_stop (server);

	// half of the body, set before the cerver starts: its loops read the limit without a lock
	options.max_body_size = server_test_huge_size >> 1;
	server = server_test_start (&options);

	fd = server_test_connect (server->port);
	server_test_huge_headers (fd);
	server_test_expect (fd, status_payload_too_large);
	(void) close (fd);

	server_test_stop (server);

}

// a client that sends its headers one byte at a time never finishes them in time
static void test_server_slowloris (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	const double start = server_test_now ();

	int fd = server_test_connect (server->port);

	// every byte arrives before the recv () timeout, but the headers deadline expires
	const size_t length = strlen (slow_headers_request);
	for (size_t i = 0; (i < length) && !server_test_has_response (fd); i++) {
		(void) send (fd, slow_headers_request + i, 1, MSG_NOSIGNAL);
		server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	}

	server_test_expect (fd, status_request_timeout);
	(void) close (fd);

	// answered with the headers deadline, not the recv () timeout
	test_check ((server_test_now () - start) < CERVER_DEFAULT_SOCKET_RECEIVE_TIMEOUT, "the slowloris was not stopped in time");

	// the thread is free for the next request
	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// a connection that never sends anything is closed without a response
static void test_server_idle (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);

	char response[SERVER_TEST_RESPONSE_SIZE];
	test_check (server_test_read (fd, response) == 0, "an idle connection received a response");
	(void) close (fd);

	server_test_stop (server);

}

// a cerver that had nothing to do for longer than its timeouts answers the next request: the deadlines of a
// connection count from the moment when it arrives, also in an event loop that slept all that time
static void test_server_idle_cerver (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	for (unsigned int idx = 0; idx < SERVER_TEST_IDLE_CERVER_SLEEPS; idx++) {
		server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	}

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

static void server_test_send_slow_body (const int fd) {

	server_test_send (fd, slow_body_headers);

	const size_t length = strlen (slow_body);
	for (size_t i = 0; (i < length) && !server_test_has_response (fd); i++) {
		server_test_sleep (SERVER_TEST_SLOW_SLEEP);
		(void) send (fd, slow_body + i, 1, MSG_NOSIGNAL);
	}

}

// the headers deadline does not apply to a body that keeps making progress
static void test_server_slow_body (void) {

	ServerTestOptions options = server_test_options_default ();
	options.headers_timeout = SERVER_TEST_HEADERS_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send_slow_body (fd);
	server_test_expect (fd, status_ok);
	(void) close (fd);

	server_test_stop (server);

}

// the total deadline stops a body that is too slow
static void test_server_request_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.request_timeout = SERVER_TEST_REQUEST_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send_slow_body (fd);
	server_test_expect (fd, status_request_timeout);
	(void) close (fd);

	// the counter is read like the stats and the metrics read it, without the mutex of the cerver
	const CerverHttp *http_cerver = cerver_http_get (server->cerver);
	const size_t n_timeout_requests = CERVER_STATS_GET (http_cerver->n_timeout_requests);
	test_check (n_timeout_requests == 1, NULL);

	server_test_stop (server);

}

// the handler of a cerver with event loops gives its response to the loop of its connection, that sees its send
// fail: the handler does not, and the cerver counts the response that failed
static u64 server_test_failed_responses (const ServerTest *server) {

	const CerverHttp *http_cerver = cerver_http_get (server->cerver);

	return (u64) CERVER_STATS_GET (http_cerver->n_failed_responses);

}

static void server_test_wait_failed_responses (const ServerTest *server, const u64 expected) {

	const double deadline = server_test_now () + SERVER_TEST_JOIN_TIMEOUT;
	while ((server_test_failed_responses (server) < expected) && (server_test_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check (server_test_failed_responses (server) >= expected, "the response did not fail");

}

// a response that a handler wrote failed: the cerver counted it
static void server_test_expect_failed (
	const ServerTest *server, const u64 failed_responses
) {

	server_test_wait_failed_responses (server, failed_responses + 1);

}

// a client that never reads its response does not block the thread forever
static void test_server_send_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	// the test runs once for each way of handling the connections
	__atomic_store_n (&big_failed, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&big_started, 0, __ATOMIC_SEQ_CST);

	const u64 failed_responses = server_test_failed_responses (server);

	int slow_reader = server_test_connect (server->port);
	server_test_send (slow_reader, big_request);

	// its handler has the only thread before the next request is sent: the event loops give the thread
	// to the request that is complete first, not to the connection that was accepted first
	test_check_unsigned_eq (test_wait_counter (&big_started, 1, TEST_WAIT_SECONDS), 1, NULL);

	// the only thread is free again after the send timeout
	server_test_request (server->port, ok_request, status_ok);

	server_test_expect_failed (server, failed_responses);

	(void) close (slow_reader);

	server_test_stop (server);

	free (big_response);
	big_response = NULL;

}

#pragma endregion

#pragma region responses

static size_t server_test_unanswered_count (const ServerTest *server) {

	const CerverHttp *http_cerver = cerver_http_get (server->cerver);

	return CERVER_STATS_GET (http_cerver->n_unanswered_requests);

}

// a handler that returns without a response is answered with 500 and counted,
// before, the connection was closed without any response (a proxy answered 502)
static void test_server_unanswered (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int fd = server_test_connect (server->port);
	server_test_send (fd, unanswered_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

	test_check (server_test_starts_with (response, status_server_error), "the unanswered request is not a 500");
	test_check_ptr (strstr (response, HTTP_RESPONSE_CONNECTION_CLOSE_HEADER));
	test_check_unsigned_eq (server_test_unanswered_count (server), 1, NULL);

	// a handler that responds is not counted
	server_test_request (server->port, ok_request, status_ok);
	test_check_unsigned_eq (server_test_unanswered_count (server), 1, NULL);

	String *metrics = cerver_http_metrics_generate (cerver_http_get (server->cerver));
	test_check_ptr (metrics);
	test_check_ptr (strstr (metrics->str, unanswered_metric));
	str_delete (metrics);

	server_test_stop (server);

}

static void server_test_wait_count (const unsigned int *count, const unsigned int expected) {

	const double deadline = server_test_now () + SERVER_TEST_JOIN_TIMEOUT;
	while ((__atomic_load_n (count, __ATOMIC_SEQ_CST) < expected) && (server_test_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check (__atomic_load_n (count, __ATOMIC_SEQ_CST) >= expected, "the handler did not end in time");

}

// a response to a client that already left is still an answer,
// the request is not answered again with 500
static void test_server_unanswered_gone (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	const unsigned int answered = __atomic_load_n (&gone_answered, __ATOMIC_SEQ_CST);

	int fd = server_test_connect (server->port);
	server_test_send (fd, gone_request);
	server_test_gate_wait_for (1);

	// a close with a zero linger sends a RST
	const struct linger reset = { 1, 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &reset, sizeof (struct linger));
	(void) close (fd);

	server_test_gate_open ();
	server_test_wait_count (&gone_answered, answered + 1);

	server_test_request (server->port, ok_request, status_ok);
	test_check_unsigned_eq (server_test_unanswered_count (server), 0, NULL);

	server_test_stop (server);

}

// a client with a small receive window, so the cerver finds its socket buffer full
static int server_test_connect_small (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	const int size = SERVER_TEST_SMALL_RECEIVE_BUFFER;
	(void) setsockopt (fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof (int));

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, SERVER_TEST_ADDRESS, &address.sin_addr);

	test_check_int_eq (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in)), 0, NULL);

	(void) sock_set_timeout (fd, SERVER_TEST_RECEIVE_TIMEOUT);

	return fd;

}

// a file with the bytes of the large response, the extension makes it a video
static void server_test_file_create (void) {

	(void) strncpy (server_test_file, SERVER_TEST_FILE_TEMPLATE, SERVER_TEST_FILENAME_SIZE - 1);

	const int fd = mkstemps (server_test_file, SERVER_TEST_FILE_SUFFIX_LENGTH);
	test_check (fd > -1, "mkstemps () failed");

	char *contents = (char *) malloc (SERVER_TEST_FILE_SIZE);
	test_check_ptr (contents);
	for (size_t idx = 0; idx < SERVER_TEST_FILE_SIZE; idx++) {
		contents[idx] = server_test_large_byte (idx);
	}

	size_t written = 0;
	while (written < SERVER_TEST_FILE_SIZE) {
		const ssize_t result = write (fd, contents + written, SERVER_TEST_FILE_SIZE - written);
		test_check (result > 0, "failed to write the test file");
		written += (size_t) result;
	}

	(void) close (fd);

	free (contents);

}

// reads the whole response, every byte of its body arrives once and in order
static void server_test_expect_body (
	const int fd, const char *status, const size_t body_size
) {

	const size_t size = body_size + SERVER_TEST_RESPONSE_SIZE;
	char *response = (char *) malloc (size);
	test_check_ptr (response);

	const size_t total = server_test_read_large (fd, response, size);

	test_check (server_test_starts_with (response, status), "unexpected status of the response");

	const char *end = strstr (response, headers_end);
	test_check_ptr (end);

	const char *body = end + strlen (headers_end);
	const size_t body_len = total - (size_t) (body - response);
	test_check_unsigned_eq (body_len, body_size, NULL);

	bool intact = true;
	for (size_t idx = 0; intact && (idx < body_len); idx++) {
		intact = (body[idx] == server_test_large_byte (idx));
	}

	test_check (intact, "the response arrived with wrong bytes");

	free (response);

}

// the client starts reading late, then the whole body arrives
static void server_test_nonblocking_response (
	const u16 port, const char *request,
	const char *status, const size_t body_size
) {

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);

	int fd = server_test_connect_small (port);
	server_test_send (fd, request);
	server_test_sleep (SERVER_TEST_READER_DELAY);
	server_test_expect_body (fd, status, body_size);
	(void) close (fd);

	server_test_wait_count (&nonblocking_done, done + 1);
	test_check_unsigned_eq (__atomic_load_n (&nonblocking_failed, __ATOMIC_SEQ_CST), 0, NULL);

}

// the 416 has no body and says the size of the video in its Content-Range
static void server_test_video_unsatisfiable (const u16 port) {

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);

	char content_range[SERVER_TEST_RESPONSE_SIZE] = { 0 };
	(void) snprintf (content_range, SERVER_TEST_RESPONSE_SIZE, "Content-Range: bytes */%d\r\n", SERVER_TEST_FILE_SIZE);

	int fd = server_test_connect_small (port);
	server_test_send (fd, video_unsatisfiable_request);

	char response[SERVER_TEST_RESPONSE_SIZE] = { 0 };
	(void) server_test_read_large (fd, response, SERVER_TEST_RESPONSE_SIZE - 1);
	(void) close (fd);

	test_check (server_test_starts_with (response, status_range_not_satisfiable), "a range after the video did not get 416");
	test_check (strstr (response, content_range) != NULL, "the 416 does not have the size of the video");

	server_test_wait_count (&nonblocking_done, done + 1);

}

// RFC 9110: 200 without a valid range, the last byte of a range included, 416 after the video
// (every video got a 206, a range sent one byte less and nothing got a 416)
static void server_test_video_ranges (const u16 port) {

	server_test_nonblocking_response (port, video_whole_request, status_ok, SERVER_TEST_FILE_SIZE);

	server_test_nonblocking_response (port, video_range_request, status_partial_content, SERVER_TEST_RANGE_SIZE);

	server_test_nonblocking_response (port, video_invalid_range_request, status_ok, SERVER_TEST_FILE_SIZE);

	server_test_video_unsatisfiable (port);

}

// a full non blocking socket buffer waits until the client reads,
// before, send () and sendfile () gave up after a few retries and the response arrived truncated
static void test_server_nonblocking (void) {

	server_test_file_create ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	server_test_nonblocking_response (
		server->port, nonblocking_large_request, status_ok, SERVER_TEST_LARGE_RESPONSE_SIZE
	);

	server_test_nonblocking_response (
		server->port, nonblocking_file_request, status_ok, SERVER_TEST_FILE_SIZE
	);

	server_test_nonblocking_response (
		server->port, nonblocking_video_request, status_partial_content, HTTP_RESPONSE_VIDEO_CHUNK_SIZE
	);

	// the range of the last bytes of the file, it was ignored
	server_test_nonblocking_response (
		server->port, nonblocking_suffix_request, status_partial_content, SERVER_TEST_SUFFIX_SIZE
	);

	server_test_video_ranges (server->port);

	server_test_stop (server);

	(void) unlink (server_test_file);

}

// a non blocking socket that never drains fails when the cerver's send timeout ends,
// before, it failed right away after a few retries
static void test_server_nonblocking_timeout (void) {

	// the handler reads the response in a thread of the cerver, so it is written before the cerver creates its
	// threads: the request that asks for it arrives through the socket, which with io_uring TSan does not see
	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);
	const u64 failed_responses = server_test_failed_responses (server);

	int fd = server_test_connect_small (server->port);
	server_test_send (fd, nonblocking_big_request);

	server_test_wait_count (&nonblocking_done, done + 1);

	// a client that leaves would fail the send at once
	server_test_expect_failed (server, failed_responses);
	(void) close (fd);

	const unsigned long milliseconds = __atomic_load_n (&nonblocking_milliseconds, __ATOMIC_SEQ_CST);
	test_check (
		milliseconds >= ((SERVER_TEST_SEND_TIMEOUT * SERVER_TEST_MILLISECONDS_PER_SECOND) - SERVER_TEST_SEND_TIMEOUT_MARGIN),
		"the non blocking response did not wait for its send timeout"
	);

	// the only thread is free again
	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

	free (big_response);
	big_response = NULL;

}

// the milliseconds that a response took until it failed: with event loops,
// the test measures from its request until the cerver counted the failure
static unsigned long server_test_response_failed_milliseconds (
	const ServerTest *server, const double start, const u64 failed_responses
) {

	server_test_wait_failed_responses (server, failed_responses + 1);

	return (unsigned long) ((server_test_now () - start) * SERVER_TEST_MILLISECONDS_PER_SECOND);

}

// the deadline of a response of an event loop is a timer of its wheel, that expires at most a tick later
static unsigned long server_test_response_tolerance (void) {

	return SERVER_TEST_RESPONSE_SEND_TOLERANCE + EVENT_WHEEL_TICK;

}

// sends the request with a client that never reads,
// and checks that its response failed within the deadline of the response
static void server_test_response_deadline (const ServerTest *server, const char *request) {

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);
	const u64 failed_responses = server_test_failed_responses (server);

	const double start = server_test_now ();

	int fd = server_test_connect_small (server->port);
	server_test_send (fd, request);

	server_test_wait_count (&nonblocking_done, done + 1);

	// a client that leaves would fail the send at once
	server_test_expect_failed (server, failed_responses);
	(void) close (fd);

	const unsigned long milliseconds = server_test_response_failed_milliseconds (server, start, failed_responses);
	test_check (
		milliseconds >= (SERVER_TEST_RESPONSE_SEND_TIMEOUT - SERVER_TEST_SEND_TIMEOUT_MARGIN),
		"the response did not wait for its deadline"
	);

	if (milliseconds > (SERVER_TEST_RESPONSE_SEND_TIMEOUT + server_test_response_tolerance ())) {
		(void) fprintf (stderr, "the response failed after %lu milliseconds\n", milliseconds);
	}

	test_check (
		milliseconds <= (SERVER_TEST_RESPONSE_SEND_TIMEOUT + server_test_response_tolerance ()),
		"the response waited longer than its deadline"
	);

}

// all the sends of a response end with the send timeout of the HTTP cerver (milliseconds),
// also in the blocking socket of the connection, whose SO_SNDTIMEO (the cerver's send timeout) is longer
static void test_server_response_send_timeout (void) {

	server_test_file_create ();

	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_LONG_SEND_TIMEOUT;
	options.response_send_timeout = SERVER_TEST_RESPONSE_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	server_test_response_deadline (server, deadline_big_request);
	server_test_response_deadline (server, deadline_video_request);

	// the only thread is free again
	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

	free (big_response);
	big_response = NULL;

	(void) unlink (server_test_file);

}

#pragma endregion

#pragma region overload

static void server_test_check_unavailable (const char *response) {

	test_check (server_test_starts_with (response, status_service_unavailable), "the response is not a 503");
	test_check_ptr (strstr (response, retry_after_header));

	// the Content-Length header matches the body
	const char *length_header = strstr (response, content_length_header);
	const char *body = strstr (response, headers_end);
	test_check_ptr (length_header);
	test_check_ptr (body);

	const size_t length = (size_t) strtoul (length_header + strlen (content_length_header), NULL, 10);
	test_check (strlen (body + strlen (headers_end)) == length, "the 503 Content-Length is wrong");

}

// the probe connection of server_test_start () must be done,
// otherwise it can take the only queue slot
static void server_test_wait_idle (Cerver *cerver) {

	while (
		thpool_get_queued (cerver->thpool)
		|| thpool_get_num_threads_working (cerver->thpool)
	) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

}

// fills the only thread and the only queue slot
static void server_test_fill (Cerver *cerver, const u16 port, int *working, int *queued) {

	server_test_wait_idle (cerver);

	*working = server_test_connect (port);
	server_test_send (*working, block_request);
	server_test_gate_wait_for (1);

	*queued = server_test_connect (port);
	server_test_send (*queued, block_request);

}

static void test_server_overload_reject (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);

	// waits until the second connection is queued
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int rejected = server_test_connect (server->port);
	server_test_send (rejected, ok_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (rejected, response);
	server_test_check_unavailable (response);
	(void) close (rejected);

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 1, NULL);

	// the connections that were accepted are still handled
	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_stop (server);

}

// the new connection waits in the listen backlog until there is space
static void test_server_overload_wait (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);

	int waiting = server_test_connect (server->port);
	server_test_send (waiting, ok_request);

	test_check_false (server_test_has_response (waiting));
	test_check_unsigned_eq (thpool_get_num_threads_alive (server->cerver->thpool), 1, NULL);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	server_test_expect (waiting, status_ok);
	(void) close (working);
	(void) close (queued);
	(void) close (waiting);

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 0, NULL);

	server_test_stop (server);

}

#pragma endregion

#pragma region drain

typedef struct ServerTestTeardown {

	ServerTest *server;
	bool done;

} ServerTestTeardown;

static void *server_test_teardown_thread (void *teardown_ptr) {

	ServerTestTeardown *teardown = (ServerTestTeardown *) teardown_ptr;

	server_test_stop (teardown->server);
	__atomic_store_n (&teardown->done, true, __ATOMIC_SEQ_CST);

	return NULL;

}

static void server_test_check_refused (const u16 port) {

	int fd = server_test_connect_once (port);
	test_check (fd < 0, "the cerver still accepts connections");

}

// the connections in progress and the queued ones are completed
static void test_server_drain_run (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);
	const u16 port = server->port;

	int working = -1, queued = -1;
	server_test_fill (server->cerver, port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	// the teardown waits for the connections
	server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	test_check_false (__atomic_load_n (&teardown.done, __ATOMIC_SEQ_CST));
	server_test_check_refused (port);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);

	(void) pthread_join (thread_id, NULL);
	test_check_true (__atomic_load_n (&teardown.done, __ATOMIC_SEQ_CST));

	(void) close (working);
	(void) close (queued);

}

// the queued connections are answered with 503 right away
static void test_server_drain_reject (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.drain_policy = CERVER_DRAIN_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (queued, response);
	server_test_check_unavailable (response);

	server_test_gate_open ();
	server_test_expect (working, status_ok);

	(void) pthread_join (thread_id, NULL);

	(void) close (working);
	(void) close (queued);

}

static void server_test_signal_handler (int signal_number) {

	(void) signal_number;

}

// a signal without SA_RESTART interrupts accept () and stops the cerver,
// so a handler like the one of python can run when cerver_start () returns
static void test_server_signal (void) {

	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	action.sa_flags = 0;
	(void) sigaction (SIGUSR1, &action, NULL);

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	// a signal that arrives between two accept () is lost (cerver_run () handles that case),
	// so it is sent again until one interrupts accept ()
	const double deadline = server_test_now () + SERVER_TEST_JOIN_TIMEOUT;
	while (cerver_is_running (server->cerver) && (server_test_now () < deadline)) {
		test_check_int_eq (pthread_kill (server->thread, SIGUSR1), 0, NULL);
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	server_test_join (server);
	test_check_false (cerver_is_running (server->cerver));
	test_check_unsigned_eq (server->start_result, 0, NULL);

	// shutdown is still valid after the signal
	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

	(void) signal (SIGUSR1, SIG_DFL);

}

#pragma endregion

#pragma region files

static Cerver *server_test_files_cerver = NULL;

// a SIGTERM that arrives while the main thread is not inside accept () does not interrupt it,
// so the handler also stops the cerver, like the handler of the services calls cerver_shutdown ()
// the cerver has no update threads, so its stop mutex is never held when the signal arrives
static void server_test_files_signal_handler (int signal_number) {

	(void) signal_number;

	cerver_stop_request (server_test_files_cerver);

}

// the child runs out of fds, the cerver must keep accepting (and closing)
// connections and serve again when the fds are released
static void server_test_files_child (const u16 port) {

	struct rlimit limit = { SERVER_TEST_FILES_LIMIT, SERVER_TEST_FILES_LIMIT };
	if (setrlimit (RLIMIT_NOFILE, &limit)) _exit (EXIT_FAILURE);

	// every accepted connection is queued, so the cerver runs out of fds
	ServerTestOptions options = server_test_options_default ();
	options.threads = 2;
	options.queue_size = SERVER_TEST_FILES_CONNECTIONS;
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;
	options.io_threads = SERVER_TEST_FILES_LOOPS;

	server_test_files_cerver = server_test_cerver_create (port, &options);

	// SIGTERM stops the cerver, like in the services
	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_files_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGTERM, &action, NULL);

	(void) cerver_start (server_test_files_cerver);
	(void) cerver_teardown (server_test_files_cerver);

	_exit (EXIT_SUCCESS);

}

// returns true if the cerver closed the connection
static bool server_test_is_closed (const int fd) {

	char byte = 0;

	return (recv (fd, &byte, 1, MSG_DONTWAIT) == 0);

}

// the connections that the cerver closed
static unsigned int server_test_closed_count (const int *fds, const unsigned int n_fds) {

	unsigned int closed = 0;

	for (unsigned int i = 0; i < n_fds; i++) {
		if (server_test_is_closed (fds[i])) closed += 1;
	}

	return closed;

}

// valgrind keeps its own fds, so the child can not start with the limit
static bool server_test_files_limit_is_valid (void) {

	const char *preload = getenv (SERVER_TEST_PRELOAD_VARIABLE);

	return !(preload && strstr (preload, SERVER_TEST_VALGRIND_PRELOAD));

}

static void test_server_out_of_fds (void) {

	if (!server_test_files_limit_is_valid ()) return;

	const u16 port = (u16) server_test_port++;

	(void) fflush (stdout);
	pid_t pid = fork ();
	test_check (pid >= 0, "fork () failed");

	if (pid == 0) server_test_files_child (port);

	int fds[SERVER_TEST_FILES_CONNECTIONS];
	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) {
		fds[i] = server_test_connect (port);
	}

	// the connections that the cerver has no fds for are closed by it, instead of staying in the backlog;
	// the wait is for them, a fixed sleep failed when the cerver of the child took longer to start
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;

	unsigned int closed = server_test_closed_count (fds, SERVER_TEST_FILES_CONNECTIONS);
	while ((closed < SERVER_TEST_FILES_EXTRA) && (test_wait_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

		closed = server_test_closed_count (fds, SERVER_TEST_FILES_CONNECTIONS);
	}

	test_check (
		closed >= SERVER_TEST_FILES_EXTRA, "the cerver did not close the connections that it could not handle"
	);

	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) (void) close (fds[i]);

	// the cerver recovers, a request that it still can not take is closed and tried again
	test_check (
		server_test_request_until (port, ok_request, status_ok, SERVER_TEST_RECOVER_SECONDS),
		"the cerver did not recover after it ran out of fds"
	);

	(void) kill (pid, SIGTERM);

	int status = 0;
	(void) waitpid (pid, &status, 0);
	test_check (WIFEXITED (status) && (WEXITSTATUS (status) == EXIT_SUCCESS), "the out of fds cerver did not stop");

}

// the child keeps an open stdin until its handler closes it,
// so the file of its only request is opened as fd 0
static void server_test_zero_child (const u16 port) {

	const int null_fd = open (null_device, O_RDONLY);
	if (null_fd > STDIN_FILENO) {
		(void) dup2 (null_fd, STDIN_FILENO);
		(void) close (null_fd);
	}

	ServerTestOptions options = server_test_options_default ();
	server_test_files_cerver = server_test_cerver_create (port, &options);

	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_files_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGTERM, &action, NULL);

	(void) cerver_start (server_test_files_cerver);
	(void) cerver_teardown (server_test_files_cerver);

	_exit (EXIT_SUCCESS);

}

static void server_test_zero_request (
	const char *request, const char *status, const size_t body_size
) {

	const u16 port = (u16) server_test_port++;

	(void) fflush (stdout);
	pid_t pid = fork ();
	test_check (pid >= 0, "fork () failed");

	if (pid == 0) server_test_zero_child (port);

	int fd = server_test_connect (port);
	server_test_send (fd, request);
	server_test_expect_body (fd, status, body_size);
	(void) close (fd);

	(void) kill (pid, SIGTERM);

	int exit_status = 0;
	(void) waitpid (pid, &exit_status, 0);
	test_check (WIFEXITED (exit_status) && (WEXITSTATUS (exit_status) == EXIT_SUCCESS), "the fd 0 cerver did not stop");

}

// a file or a video opened as fd 0 is sent, before, fd 0 was taken as an error to open them
static void test_server_zero_fd (void) {

	server_test_file_create ();

	server_test_zero_request (zero_file_request, status_ok, SERVER_TEST_FILE_SIZE);
	server_test_zero_request (zero_video_request, status_partial_content, HTTP_RESPONSE_VIDEO_CHUNK_SIZE);

	(void) unlink (server_test_file);

}

#pragma endregion

#pragma region network

static void test_server_blocking (void) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, NULL);

	test_check_true (sock_set_blocking (fd, false));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) != 0, "the socket is not non blocking");

	test_check_true (sock_set_blocking (fd, true));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) == 0, "the socket is not blocking");

	test_check_false (sock_set_blocking (-1, true));

	(void) close (fd);

}

static int server_test_listen (const u16 port, const bool reusable_port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, NULL);

	test_check_int_eq (sock_set_reusable (fd), 0, NULL);
	if (reusable_port) test_check_int_eq (sock_set_reusable_port (fd), 0, NULL);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	if (bind (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in)) || listen (fd, SOMAXCONN)) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

// SO_REUSEADDR alone does not let two sockets listen in the same port
static void test_server_reusable (void) {

	const u16 port = (u16) server_test_port++;

	int first = server_test_listen (port, false);
	test_check (first > -1, NULL);
	test_check (server_test_listen (port, false) < 0, "two sockets listen in the same port");
	(void) close (first);

	const u16 shared_port = (u16) server_test_port++;
	first = server_test_listen (shared_port, true);
	int second = server_test_listen (shared_port, true);
	test_check (first > -1, NULL);
	test_check (second > -1, "SO_REUSEPORT did not share the port");
	(void) close (first);
	(void) close (second);

}

static void test_server_policies_strings (void) {

	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_WAIT), "Wait", NULL);
	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_REJECT), "Reject", NULL);
	test_check_str_eq (cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_MAX), "None", NULL);
	test_check_str_eq (cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_RUN), "Run", NULL);
	test_check_str_eq (cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_REJECT), "Reject", NULL);

	// invalid policies are ignored
	Cerver *cerver = cerver_create_web ("policies", (u16) server_test_port, CERVER_DEFAULT_CONNECTION_QUEUE);
	cerver_set_overload_policy (cerver, CERVER_OVERLOAD_POLICY_MAX);
	cerver_set_drain_policy (cerver, CERVER_DRAIN_POLICY_NONE);
	test_check (cerver->overload_policy == CERVER_DEFAULT_OVERLOAD_POLICY, NULL);
	test_check (cerver->drain_policy == CERVER_DEFAULT_DRAIN_POLICY, NULL);
	test_check (cerver->connection_queue == CERVER_DEFAULT_CONNECTION_QUEUE, NULL);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region process

// two HTTP cervers in the same process share the common responses and the pools:
// the one that ends first leaves them to the other, the last one releases them (ASan checks it),
// and a cerver that starts after both ended creates them again
static void test_server_two_cervers (void) {

	ServerTestOptions options = server_test_options_default ();

	ServerTest *first = server_test_start (&options);
	ServerTest *second = server_test_start (&options);

	// the responses of the second cerver read what the first one owns, which its end releases
	server_test_request_ok (first->port);
	server_test_request_ok (second->port);

	server_test_stop (first);

	server_test_request_ok (second->port);

	server_test_stop (second);

	ServerTest *third = server_test_start (&options);

	server_test_request_ok (third->port);

	server_test_stop (third);

}

// asks a cerver for /ok and checks which instance its response says it is, none when the line is NULL
static void server_test_expect_instance (const u16 port, const char *line) {

	const unsigned int answered = __atomic_load_n (&server_test_ok_answered, __ATOMIC_SEQ_CST);

	int fd = server_test_connect (port);
	server_test_send (fd, ok_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

	server_test_ok_wait (answered);

	test_check (server_test_starts_with (response, status_ok), "unexpected response status");

	if (line) {
		test_check (strstr (response, line) != NULL, "the response does not have the header of its cerver");
	}

	else {
		test_check (
			strstr (response, server_test_instance_header) == NULL,
			"the response has the header of another cerver"
		);
	}

}

// a response that has the header keeps its value: the one of its cerver is not added, so it has it once
static void server_test_expect_own_instance (const u16 port) {

	int fd = server_test_connect (port);
	server_test_send (fd, own_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

	test_check (server_test_starts_with (response, status_ok), "unexpected response status");

	const char *own = strstr (response, server_test_own_instance_line);
	test_check (own != NULL, "the response does not have its own header");

	test_check (
		strstr (response, server_test_instance_line_start) == own,
		"the header of the cerver was added before the one of the response"
	);

	test_check (
		strstr (own + 1, server_test_instance_line_start) == NULL,
		"the header of the cerver was added to a response that has it"
	);

}

// each cerver of a process adds its own headers to its responses, also to the common ones that they share:
// every cerver sent the ones of the first HTTP cerver that started, read while that one released them
static void test_server_default_headers (void) {

	ServerTestOptions options = server_test_options_default ();

	options.instance = server_test_first_instance;
	ServerTest *first = server_test_start (&options);

	options.instance = server_test_second_instance;
	ServerTest *second = server_test_start (&options);

	options.instance = NULL;
	ServerTest *third = server_test_start (&options);

	server_test_expect_instance (first->port, server_test_first_instance_line);
	server_test_expect_instance (second->port, server_test_second_instance_line);
	server_test_expect_instance (third->port, NULL);

	server_test_expect_own_instance (second->port);

	// the cerver that started first ends: the others keep their own
	server_test_stop (first);

	server_test_expect_instance (second->port, server_test_second_instance_line);
	server_test_expect_instance (third->port, NULL);

	server_test_stop (second);

	server_test_expect_instance (third->port, NULL);

	server_test_stop (third);

}

// a web cerver that was given a handler type that is not the event loops does not start, and it is released
static void server_test_handler_type_rejected (const CerverHandlerType handler_type) {

	ServerTestOptions options = server_test_options_default ();

	const u16 port = (u16) server_test_port++;
	Cerver *cerver = server_test_cerver_create (port, &options);

	cerver_set_handler_type (cerver, handler_type);

	test_check_unsigned_eq (cerver_start (cerver), 1, NULL);
	test_check (!cerver_http_get (cerver)->global_reference, "a cerver that did not start took the shared state");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a web cerver always runs on event loops, with one for each processor until it is given a number of them:
// the thread for each connection and the poll of HTTP are gone
static void test_server_handler_type (void) {

	ServerTestOptions options = server_test_options_default ();

	// the cerver keeps the event loops that it was created with
	ServerTestOptions created = options;
	created.io_threads = SERVER_TEST_IO_THREADS_DEFAULT;

	const u16 port = (u16) server_test_port++;
	Cerver *cerver = server_test_cerver_create (port, &created);

	test_check_int_eq ((int) cerver->handler_type, (int) CERVER_HANDLER_TYPE_EVENTS, NULL);
	test_check_unsigned_eq (cerver->n_io_threads, CERVER_IO_THREADS_PER_CORE, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	server_test_handler_type_rejected (CERVER_HANDLER_TYPE_THREADS);
	server_test_handler_type_rejected (CERVER_HANDLER_TYPE_POLL);

	// the cervers that did not start left nothing: the next one serves
	ServerTest *server = server_test_start (&options);

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// a web cerver that was stopped before it started never takes the shared state
static void test_server_stop_before_start (void) {

	ServerTestOptions options = server_test_options_default ();

	const u16 port = (u16) server_test_port++;
	Cerver *cerver = server_test_cerver_create (port, &options);

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);
	test_check_unsigned_eq (cerver_start (cerver), 0, NULL);
	test_check (!cerver_http_get (cerver)->global_reference, "a stopped cerver took the shared state");
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	ServerTest *server = server_test_start (&options);

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

#pragma endregion

#pragma region events

static const char *secure_request = {
	"GET /secure/ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer server-test\r\n"
	"\r\n"
};

static const char *secure_rejected_request = {
	"GET /secure/ok HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer another\r\n"
	"\r\n"
};

static const char *secure_body_headers = {
	"POST /secure/echo HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer server-test\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
};

static const char *secure_rejected_body_headers = {
	"POST /secure/echo HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer another\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
};

static const char *secure_body_request = {
	"POST /secure/echo HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Authorization: Bearer server-test\r\n"
	"Content-Type: text/plain\r\n"
	"Content-Length: 8\r\n"
	"\r\n"
	"12345678"
};

static const char *missing_request = {
	"GET /missing/path HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *options_request = {
	"OPTIONS /group/large HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n"
};

static const char *upload_request = {
	"POST /files/upload HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 151\r\n"
	"\r\n"
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"file\"; filename=\"events.txt\"\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"the file of a request\r\n"
	"--server-test--\r\n"
};

static const char *status_unauthorized = { "HTTP/1.1 401" };
static const char *status_no_content = { "HTTP/1.1 204" };

static void server_test_wait_parked (const Cerver *cerver, const unsigned int expected) {

	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	while ((event_loop_group_parked (cerver) != expected) && (test_wait_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check_unsigned_eq (event_loop_group_parked (cerver), expected, NULL);

}

static void server_test_wait_connections (const Cerver *cerver, const unsigned int expected) {

	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	while ((event_loop_group_connections (cerver) != expected) && (test_wait_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
	}

	test_check_unsigned_eq (event_loop_group_connections (cerver), expected, NULL);

}

// the files that are left in the uploads directory of the cervers
static unsigned int server_test_uploads_count (void) {

	unsigned int count = 0;

	DIR *directory = opendir (SERVER_TEST_UPLOADS_PATH);
	if (directory) {
		const struct dirent *entry = NULL;
		while ((entry = readdir (directory))) {
			if (entry->d_name[0] != '.') count += 1;
		}

		(void) closedir (directory);
	}

	return count;

}

// removes the files that a test left in the uploads directory of the cervers
static void server_test_uploads_remove (void) {

	DIR *directory = opendir (SERVER_TEST_UPLOADS_PATH);
	if (directory) {
		const struct dirent *entry = NULL;
		while ((entry = readdir (directory))) {
			if (entry->d_name[0] != '.') (void) unlinkat (dirfd (directory), entry->d_name, 0);
		}

		(void) closedir (directory);
	}

}

// only a web cerver with a thpool can handle its connections with event loops
static void test_server_events_requirements (void) {

	Cerver *custom = cerver_create (
		CERVER_TYPE_CUSTOM, "events-custom", (u16) server_test_port++, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (custom);
	cerver_set_handler_type (custom, CERVER_HANDLER_TYPE_EVENTS);
	cerver_set_events_backend (custom, test_events_backend ());
	cerver_set_reusable_address_flags (custom, true);

	test_check_unsigned_eq (cerver_start (custom), 1, NULL);
	test_check_unsigned_eq (cerver_teardown (custom), 0, NULL);

	ServerTestOptions options = server_test_options_default ();
	options.threads = 0;

	Cerver *web = server_test_cerver_create ((u16) server_test_port++, &options);

	test_check_unsigned_eq (cerver_start (web), 1, NULL);
	test_check_unsigned_eq (cerver_teardown (web), 0, NULL);

}

// the methods of the application run in the threads of the thpool, never in an event loop:
// the custom authentication and the handler of a request without a body take a single job
static void test_server_events_threads (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	ThpoolStats before;
	thpool_get_stats (server->cerver->thpool, &before);

	server_test_thread_name_clear (server_test_authentication_thread);
	server_test_thread_name_clear (server_test_handler_thread);

	server_test_request (server->port, secure_request, status_ok);

	server_test_wait_connections (server->cerver, 0);

	test_check (
		server_test_thread_name_is_pool (server_test_authentication_thread),
		"the custom authentication did not run in the thpool"
	);

	test_check (
		server_test_thread_name_is_pool (server_test_handler_thread),
		"the handler did not run in the thpool"
	);

	ThpoolStats after;
	thpool_get_stats (server->cerver->thpool, &after);
	test_check_unsigned_eq (after.total_jobs - before.total_jobs, 1, NULL);

	// a rejected one is answered by the same job, its handler never runs
	server_test_thread_name_clear (server_test_handler_thread);

	server_test_request (server->port, secure_rejected_request, status_unauthorized);

	server_test_wait_connections (server->cerver, 0);

	test_check (
		server_test_thread_name_is_empty (server_test_handler_thread),
		"the handler of a rejected request ran"
	);

	server_test_stop (server);

}

// a request with a body is authenticated by a thread of the thpool before its body is received:
// a rejected one is answered without it, an accepted one continues with the body that arrives later
// or with the one that arrived with its headers
static void test_server_events_authentication_body (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int rejected = server_test_connect (server->port);
	server_test_send (rejected, secure_rejected_body_headers);
	server_test_expect (rejected, status_unauthorized);
	(void) close (rejected);

	server_test_wait_connections (server->cerver, 0);
	server_test_wait_idle (server->cerver);

	ThpoolStats before;
	thpool_get_stats (server->cerver->thpool, &before);

	int accepted = server_test_connect (server->port);
	server_test_send (accepted, secure_body_headers);

	// its authentication is a job, the body has not been sent
	const double deadline = test_wait_now () + (double) TEST_WAIT_SECONDS;
	ThpoolStats stats;
	thpool_get_stats (server->cerver->thpool, &stats);
	while ((stats.completed_jobs < (before.completed_jobs + 1)) && (test_wait_now () < deadline)) {
		server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);
		thpool_get_stats (server->cerver->thpool, &stats);
	}

	test_check_unsigned_eq (stats.completed_jobs - before.completed_jobs, 1, NULL);
	test_check_false (server_test_has_response (accepted));

	server_test_send (accepted, slow_body);
	server_test_expect (accepted, status_ok);
	(void) close (accepted);

	// the authentication and the handler
	server_test_wait_connections (server->cerver, 0);
	thpool_get_stats (server->cerver->thpool, &stats);
	test_check_unsigned_eq (stats.total_jobs - before.total_jobs, 2, NULL);

	server_test_request (server->port, secure_body_request, status_ok);

	server_test_stop (server);

}

// what cerver answers by itself does not wait for the thpool:
// its only thread is busy and the routes without a handler are answered
static void test_server_events_inline (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	server_test_wait_idle (server->cerver);

	int working = server_test_connect (server->port);
	server_test_send (working, block_request);
	server_test_gate_wait_for (1);

	ThpoolStats before;
	thpool_get_stats (server->cerver->thpool, &before);

	server_test_request (server->port, missing_request, status_ok);
	server_test_request (server->port, options_request, status_no_content);

	ThpoolStats after;
	thpool_get_stats (server->cerver->thpool, &after);
	test_check_unsigned_eq (after.total_jobs, before.total_jobs, NULL);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	(void) close (working);

	server_test_stop (server);

}

// with the wait policy a request that finds the thpool full waits in its loop, without a thread,
// and it is handled in its turn when there is a place
static void test_server_events_parked (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int first = server_test_connect (server->port);
	server_test_send (first, ok_request);
	server_test_wait_parked (server->cerver, 1);

	int second = server_test_connect (server->port);
	server_test_send (second, ok_request);
	server_test_wait_parked (server->cerver, 2);

	test_check_unsigned_eq (thpool_get_num_threads_alive (server->cerver->thpool), 1, NULL);
	test_check_unsigned_eq (thpool_get_queued (server->cerver->thpool), 1, NULL);
	test_check_false (server_test_has_response (first));

	server_test_gate_open ();

	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	server_test_expect (first, status_ok);
	server_test_expect (second, status_ok);

	(void) close (working);
	(void) close (queued);
	(void) close (first);
	(void) close (second);

	server_test_wait_parked (server->cerver, 0);

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 0, NULL);

	server_test_stop (server);

}

// a request that waited for a place longer than the wait timeout is answered with 503,
// the ones that have their place are still handled
static void test_server_events_wait_timeout (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.overload_wait_timeout = SERVER_TEST_WAIT_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	const double start = server_test_now ();

	int waiting = server_test_connect (server->port);
	server_test_send (waiting, ok_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (waiting, response);
	server_test_check_unavailable (response);
	(void) close (waiting);

	test_check ((server_test_now () - start) >= (double) SERVER_TEST_WAIT_TIMEOUT, "the request did not wait for a place");

	test_check (__atomic_load_n (&server->cerver->stats->total_rejected_connections, __ATOMIC_SEQ_CST) == 1, NULL);
	server_test_wait_parked (server->cerver, 0);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_stop (server);

}

// a wait of 0 secs does not wait: the request is answered with 503 like with the reject policy
static void test_server_events_wait_disabled (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.overload_wait_timeout = 0;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int rejected = server_test_connect (server->port);
	server_test_send (rejected, ok_request);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (rejected, response);
	server_test_check_unavailable (response);
	(void) close (rejected);

	test_check_unsigned_eq (event_loop_group_parked (server->cerver), 0, NULL);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_stop (server);

}

// with the reject policy and a full thpool, a request with a body is rejected with its headers:
// its 503 arrives before its body is sent
static void test_server_events_reject_before_body (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.overload_policy = CERVER_OVERLOAD_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int rejected = server_test_connect (server->port);
	server_test_send (rejected, slow_body_headers);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (rejected, response);
	server_test_check_unavailable (response);
	(void) close (rejected);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_stop (server);

}

// the connections that server_test_fill () leaves in a cerver: one in its thread and one in its queue
#define SERVER_TEST_FILL_CONNECTIONS		2

// the body of a request with a file is parsed, and its file written, by a thread of the thpool: one that finds
// no place there waits before any of its file is written, and it is answered with 503 when its wait ends.
// Nothing of it is left: the cerver keeps the files of the requests that it handles
static void test_server_events_rejected_uploads (void) {

	server_test_gate_reset ();

	(void) mkdir (SERVER_TEST_UPLOADS_PATH, SERVER_TEST_UPLOADS_MODE);

	ServerTestOptions options = server_test_options_default ();
	options.overload_wait_timeout = SERVER_TEST_WAIT_TIMEOUT;

	// set before the cerver starts: its loops read it without a lock when they accept a connection
	options.keep_uploads = true;

	ServerTest *server = server_test_start (&options);

	// the file of a request that is handled stays
	server_test_request (server->port, upload_request, status_ok);
	server_test_wait_connections (server->cerver, 0);
	test_check_unsigned_eq (server_test_uploads_count (), 1, NULL);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	// the request waits for a place for its body: its loop does not write its file
	int waiting = server_test_connect (server->port);
	server_test_send (waiting, upload_request);
	server_test_wait_parked (server->cerver, 1);
	test_check_unsigned_eq (server_test_uploads_count (), 1, NULL);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (waiting, response);
	server_test_check_unavailable (response);
	(void) close (waiting);

	server_test_wait_parked (server->cerver, 0);

	// its connection ended: its file is gone, the one of the request that was handled is still there
	server_test_wait_connections (server->cerver, SERVER_TEST_FILL_CONNECTIONS);
	test_check_unsigned_eq (server_test_uploads_count (), 1, NULL);

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	(void) close (working);
	(void) close (queued);

	server_test_wait_connections (server->cerver, 0);
	test_check_unsigned_eq (server_test_uploads_count (), 1, NULL);

	server_test_stop (server);

	server_test_uploads_remove ();

	(void) rmdir (SERVER_TEST_UPLOADS_PATH);

}

// at its limit of connections the cerver stops accepting: a new connection waits in the listen backlog
// and it is handled when another one ends
static void test_server_events_max_connections (void) {

	ServerTestOptions options = server_test_options_default ();
	options.max_connections = 2;
	ServerTest *server = server_test_start (&options);

	server_test_wait_connections (server->cerver, 0);

	int first = server_test_connect (server->port);
	int second = server_test_connect (server->port);
	server_test_wait_connections (server->cerver, 2);

	int waiting = server_test_connect (server->port);
	server_test_send (waiting, ok_request);

	test_check_false (server_test_has_response (waiting));
	test_check_unsigned_eq (event_loop_group_connections (server->cerver), 2, NULL);

	(void) close (first);

	server_test_expect (waiting, status_ok);
	(void) close (waiting);

	(void) close (second);

	server_test_wait_connections (server->cerver, 0);

	// and it accepts again
	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

// each I/O thread is an event loop with its own listening socket, the kernel shares the connections among them
static void test_server_events_loops (void) {

	ServerTestOptions options = server_test_options_default ();
	options.io_threads = 2;
	options.threads = 2;
	ServerTest *server = server_test_start (&options);

	for (unsigned int idx = 0; idx < SERVER_TEST_LOOPS_REQUESTS; idx++) {
		server_test_request (server->port, ok_request, status_ok);
	}

	server_test_request (server->port, secure_body_request, status_ok);

	server_test_wait_connections (server->cerver, 0);

	const EventLoopGroup *group = server->cerver->event_loops;
	test_check_ptr (group);
	test_check_unsigned_eq (group->n_loops, 2, NULL);

	u64 accepted = 0;
	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		const u64 loop_accepted = __atomic_load_n (&group->loops[idx]->n_accepted, __ATOMIC_SEQ_CST);
		test_check (loop_accepted > 0, "an event loop did not accept any connection");
		accepted += loop_accepted;
	}

	// the request that waited for the cerver to start, the ones of the test
	test_check_unsigned_eq (accepted, SERVER_TEST_LOOPS_REQUESTS + 2, NULL);

	server_test_stop (server);

}

// a request that waits for a place when the cerver is drained gets it and it is handled,
// with the reject policy it is answered with 503 right away
static void test_server_events_drain_parked (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int parked = server_test_connect (server->port);
	server_test_send (parked, ok_request);
	server_test_wait_parked (server->cerver, 1);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	server_test_sleep (SERVER_TEST_SLOW_SLEEP);
	test_check_false (__atomic_load_n (&teardown.done, __ATOMIC_SEQ_CST));

	server_test_gate_open ();
	server_test_expect (working, status_ok);
	server_test_expect (queued, status_ok);
	server_test_expect (parked, status_ok);

	(void) pthread_join (thread_id, NULL);

	(void) close (working);
	(void) close (queued);
	(void) close (parked);

}

static void test_server_events_drain_parked_reject (void) {

	server_test_gate_reset ();

	ServerTestOptions options = server_test_options_default ();
	options.drain_policy = CERVER_DRAIN_POLICY_REJECT;
	ServerTest *server = server_test_start (&options);

	int working = -1, queued = -1;
	server_test_fill (server->cerver, server->port, &working, &queued);
	while (thpool_get_queued (server->cerver->thpool) < 1) server_test_sleep (SERVER_TEST_CONNECT_INTERVAL);

	int parked = server_test_connect (server->port);
	server_test_send (parked, ok_request);
	server_test_wait_parked (server->cerver, 1);

	// a connection that sent a part of its request, and one that sent nothing
	int reading = server_test_connect (server->port);
	test_check (send (reading, slow_headers_request, SERVER_TEST_SLOW_BODY_SIZE, MSG_NOSIGNAL) > 0, "send () failed");

	int silent = server_test_connect (server->port);

	server_test_wait_connections (server->cerver, 5);

	ServerTestTeardown teardown = { server, false };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, server_test_teardown_thread, &teardown), 0, NULL);

	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (parked, response);
	server_test_check_unavailable (response);

	(void) server_test_read (reading, response);
	server_test_check_unavailable (response);

	// closed without a response
	test_check_unsigned_eq (server_test_read (silent, response), 0, NULL);

	(void) server_test_read (queued, response);
	server_test_check_unavailable (response);

	server_test_gate_open ();
	server_test_expect (working, status_ok);

	(void) pthread_join (thread_id, NULL);

	(void) close (working);
	(void) close (queued);
	(void) close (parked);
	(void) close (reading);
	(void) close (silent);

}

static Cerver *server_test_events_stop_cerver = NULL;

static void server_test_events_stop_signal_handler (int signal_number) {

	(void) signal_number;

	cerver_stop_request (server_test_events_stop_cerver);

}

// the handler of a signal that arrives at another thread stops the cerver: the thread that waits in
// cerver_start () is not interrupted, it wakes up with the stop pipe, the only thing that the handler writes
static void test_server_events_stop_from_signal (void) {

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);
	server_test_events_stop_cerver = server->cerver;

	struct sigaction action;
	(void) memset (&action, 0, sizeof (struct sigaction));
	action.sa_handler = server_test_events_stop_signal_handler;
	(void) sigemptyset (&action.sa_mask);
	(void) sigaction (SIGUSR1, &action, NULL);

	// a signal sent to the thread that calls pthread_kill () runs its handler before it returns
	test_check_int_eq (pthread_kill (pthread_self (), SIGUSR1), 0, NULL);

	server_test_join (server);
	test_check_false (cerver_is_running (server->cerver));
	test_check_unsigned_eq (server->start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

	(void) signal (SIGUSR1, SIG_DFL);
	server_test_events_stop_cerver = NULL;

}

// what only a cerver with event loops does
static void server_tests_events (void) {

	(void) printf ("Events only\n");

	(void) alarm (SERVER_TEST_ALARM);

	test_run (test_server_events_requirements);
	test_run (test_server_events_threads);
	test_run (test_server_events_authentication_body);
	test_run (test_server_events_inline);
	test_run (test_server_events_parked);
	test_run (test_server_events_wait_timeout);
	test_run (test_server_events_wait_disabled);
	test_run (test_server_events_reject_before_body);
	test_run (test_server_events_rejected_uploads);
	test_run (test_server_events_max_connections);
	test_run (test_server_events_loops);
	test_run (test_server_events_drain_parked);
	test_run (test_server_events_drain_parked_reject);
	test_run (test_server_events_stop_from_signal);

	(void) alarm (0);

}

#pragma endregion

// the tests of a cerver, that handles its connections with event loops
static void server_tests_model (void) {

	// a hang fails the suite instead of blocking it forever
	(void) alarm (SERVER_TEST_ALARM);

	test_run (test_server_ok);
	test_run (test_server_json);
	test_run (test_server_routes_error);
	test_run (test_server_large_response);
	test_run (test_server_reset_during_response);
	test_run (test_server_huge_body);
	test_run (test_server_slowloris);
	test_run (test_server_idle);
	test_run (test_server_idle_cerver);
	test_run (test_server_slow_body);
	test_run (test_server_request_timeout);
	test_run (test_server_send_timeout);

	test_run (test_server_unanswered);
	test_run (test_server_unanswered_gone);
	test_run (test_server_nonblocking);
	test_run (test_server_nonblocking_timeout);
	test_run (test_server_response_send_timeout);

	test_run (test_server_overload_reject);
	test_run (test_server_overload_wait);

	test_run (test_server_drain_run);
	test_run (test_server_drain_reject);

	test_run (test_server_signal);

	test_run (test_server_two_cervers);
	test_run (test_server_default_headers);
	test_run (test_server_handler_type);
	test_run (test_server_stop_before_start);

	test_run (test_server_out_of_fds);
	test_run (test_server_zero_fd);

	(void) alarm (0);

}

void http_tests_server (void) {

	(void) printf ("Testing HTTP server...\n");

	// what does not depend on how the connections are handled, or is of one model
	(void) alarm (SERVER_TEST_ALARM);

	test_run (test_server_blocking);
	test_run (test_server_reusable);
	test_run (test_server_policies_strings);

	(void) alarm (0);

	server_tests_model ();

	server_tests_events ();

	(void) printf ("Done!\n");

}
