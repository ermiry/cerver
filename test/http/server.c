#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/network/network.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>

#include <cerver/threads/thpool.h>

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

#define SERVER_TEST_PORT_BASE				(1 << 13)
#define SERVER_TEST_ADDRESS					"127.0.0.1"

#define SERVER_TEST_RESPONSE_SIZE			(1 << 12)

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

#define SERVER_TEST_POLL_REQUESTS			(1 << 4)
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

#define SERVER_TEST_FILENAME_SIZE			256
#define SERVER_TEST_FILE_TEMPLATE			"/tmp/cerver-server-XXXXXX.mp4"
#define SERVER_TEST_FILE_SUFFIX_LENGTH		4
#define SERVER_TEST_FILE_SIZE				(1 << 22)

// the last bytes of the file that a suffix range asks for: they start at a position
// that is a multiple of the alphabet of the file, so they are checked like a whole body
#define SERVER_TEST_SUFFIX_SIZE				52010

static const char *ok_request = {
	"GET /ok HTTP/1.1\r\n"
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

static const char *null_device = { "/dev/null" };

static const char *unanswered_metric = { "cerver_http_requests_unanswered_total 1\n" };

static const char *status_ok = { "HTTP/1.1 200" };
static const char *status_partial_content = { "HTTP/1.1 206" };
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

// GET /ok
static void server_test_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /block
static void server_test_block_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) http_response_send (oki_doki, http_receive);

}

static char *big_response = NULL;
static unsigned int big_sent = 0;
static unsigned int big_failed = 0;

// GET /big, the client never reads it
static void server_test_big_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	if (http_response_render_text (
		http_receive, HTTP_STATUS_OK, big_response, SERVER_TEST_BIG_RESPONSE_SIZE
	)) {
		(void) __atomic_add_fetch (&big_failed, 1, __ATOMIC_SEQ_CST);
	}

	else {
		(void) __atomic_add_fetch (&big_sent, 1, __ATOMIC_SEQ_CST);
	}

}

// POST /echo
static void server_test_echo_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	if (body && (body->len == SERVER_TEST_SLOW_BODY_SIZE)) {
		(void) http_response_send (oki_doki, http_receive);
	}

	else {
		(void) http_response_send (bad_request_error, http_receive);
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
static void server_test_huge_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	const String *body = http_request_get_body (request);

	bool intact = body && (body->len == server_test_huge_size);
	for (size_t position = 0; intact && (position < server_test_huge_size); position += SERVER_TEST_HUGE_BLOCK_SIZE) {
		intact = !memcmp (body->str + position, server_test_huge_block, server_test_huge_part (position));
	}

	(void) http_response_send (intact ? oki_doki : bad_request_error, http_receive);

}

// GET /unanswered returns without sending a response
static void server_test_unanswered_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

static unsigned int gone_answered = 0;

// GET /gone answers after its client has closed the connection
static void server_test_gone_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) pthread_mutex_lock (&gate_mutex);
	gate_waiting += 1;
	while (!gate_open) (void) pthread_cond_wait (&gate_cond, &gate_mutex);
	(void) pthread_mutex_unlock (&gate_mutex);

	(void) http_response_send (oki_doki, http_receive);

	(void) __atomic_add_fetch (&gone_answered, 1, __ATOMIC_SEQ_CST);

}

// the result of the last response sent by a handler with a non blocking socket
static unsigned int nonblocking_done = 0;
static unsigned int nonblocking_failed = 0;
static unsigned long nonblocking_milliseconds = 0;

static char server_test_file[SERVER_TEST_FILENAME_SIZE] = { 0 };

static double server_test_now (void);

// the handler's socket stops blocking, so a full socket buffer answers send () with EAGAIN
static double server_test_nonblocking_start (const HttpReceive *http_receive) {

	(void) sock_set_blocking (http_receive->cr->connection->socket->sock_fd, false);

	return server_test_now ();

}

static void server_test_nonblocking_end (const double start, const u8 result) {

	const double milliseconds = (server_test_now () - start) * SERVER_TEST_MILLISECONDS_PER_SECOND;

	__atomic_store_n (&nonblocking_milliseconds, (unsigned long) milliseconds, __ATOMIC_SEQ_CST);
	__atomic_store_n (&nonblocking_failed, (unsigned int) (result != 0), __ATOMIC_SEQ_CST);
	(void) __atomic_add_fetch (&nonblocking_done, 1, __ATOMIC_SEQ_CST);

}

static u8 server_test_large_send (const HttpReceive *http_receive);

// GET /nonblocking/large
static void server_test_nonblocking_large_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_nonblocking_start (http_receive);

	server_test_nonblocking_end (start, server_test_large_send (http_receive));

}

// GET /nonblocking/big, the client never reads it
static void server_test_nonblocking_big_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_nonblocking_start (http_receive);

	server_test_nonblocking_end (
		start,
		http_response_render_text (http_receive, HTTP_STATUS_OK, big_response, SERVER_TEST_BIG_RESPONSE_SIZE)
	);

}

// GET /nonblocking/file
static void server_test_nonblocking_file_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_nonblocking_start (http_receive);

	server_test_nonblocking_end (start, http_response_send_file (http_receive, HTTP_STATUS_OK, server_test_file));

}

// GET /zero/file, in a child process, closes its stdin so the file is opened as fd 0
static void server_test_zero_file_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) close (STDIN_FILENO);

	(void) http_response_send_file (http_receive, HTTP_STATUS_OK, server_test_file);

}

// GET /zero/video, in a child process, closes its stdin so the video is opened as fd 0
static void server_test_zero_video_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) close (STDIN_FILENO);

	(void) http_response_handle_video (http_receive, server_test_file);

}

// GET /nonblocking/video
static void server_test_nonblocking_video_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_nonblocking_start (http_receive);

	server_test_nonblocking_end (start, http_response_handle_video (http_receive, server_test_file));

}

// GET /deadline/big, in the blocking socket of the connection, the client never reads it
static void server_test_deadline_big_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_now ();

	server_test_nonblocking_end (
		start,
		http_response_render_text (http_receive, HTTP_STATUS_OK, big_response, SERVER_TEST_BIG_RESPONSE_SIZE)
	);

}

// GET /deadline/video, its header and its file are sent with the same deadline
static void server_test_deadline_video_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const double start = server_test_now ();

	server_test_nonblocking_end (start, http_response_handle_video (http_receive, server_test_file));

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

	CerverHandlerType handler_type;

	// milliseconds of all the sends of a response, 0 uses send_timeout
	unsigned int response_send_timeout;

} ServerTestOptions;

typedef struct ServerTest {

	Cerver *cerver;
	u16 port;

	pthread_t thread;
	u8 start_result;
	bool started;

} ServerTest;

static ServerTestOptions server_test_options_default (void) {

	ServerTestOptions options = {
		1, 1,
		CERVER_OVERLOAD_POLICY_WAIT,
		CERVER_DRAIN_POLICY_RUN, SERVER_TEST_DRAIN_TIMEOUT,
		CERVER_DEFAULT_SEND_TIMEOUT,
		HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT, HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT,
		CERVER_HANDLER_TYPE_THREADS,
		0
	};

	return options;

}

static void *server_test_thread (void *server_ptr) {

	ServerTest *server = (ServerTest *) server_ptr;

	server->start_result = cerver_start (server->cerver);

	return NULL;

}

// a byte of the large response, so a part sent twice or skipped is found
static char server_test_large_byte (const size_t position) {

	return (char) ('a' + (position % SERVER_TEST_LARGE_ALPHABET));

}

// sends a response bigger than the socket buffer, in many partial writes
// returns 0 on success, 1 on error
static u8 server_test_large_send (const HttpReceive *http_receive) {

	u8 retval = 1;

	char *body = (char *) malloc (SERVER_TEST_LARGE_RESPONSE_SIZE);
	if (body) {
		for (size_t idx = 0; idx < SERVER_TEST_LARGE_RESPONSE_SIZE; idx++) {
			body[idx] = server_test_large_byte (idx);
		}

		// the response is never compiled, so it is sent from its header and its data
		HttpResponse *response = http_response_create (HTTP_STATUS_OK, body, SERVER_TEST_LARGE_RESPONSE_SIZE);
		if (response) {
			(void) http_response_add_content_length_header (response, SERVER_TEST_LARGE_RESPONSE_SIZE);
			retval = http_response_send (response, http_receive);
			http_response_delete (response);
		}

		free (body);
	}

	return retval;

}

// GET /group/large
static void server_test_large_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) server_test_large_send (http_receive);

}

static void server_test_routes (HttpCerver *http_cerver) {

	HttpRoute *ok_route = http_route_create (REQUEST_METHOD_GET, "ok", server_test_ok_handler);
	http_cerver_route_register (http_cerver, ok_route);

	HttpRoute *block_route = http_route_create (REQUEST_METHOD_GET, "block", server_test_block_handler);
	http_cerver_route_register (http_cerver, block_route);

	HttpRoute *big_route = http_route_create (REQUEST_METHOD_GET, "big", server_test_big_handler);
	http_cerver_route_register (http_cerver, big_route);

	HttpRoute *echo_route = http_route_create (REQUEST_METHOD_POST, "echo", server_test_echo_handler);
	http_cerver_route_register (http_cerver, echo_route);

	HttpRoute *huge_route = http_route_create (REQUEST_METHOD_POST, "huge", server_test_huge_handler);
	http_cerver_route_register (http_cerver, huge_route);

	HttpGroup *group = http_cerver_group_create (http_cerver, "group");
	test_check_ptr (http_group_get (group, "large", server_test_large_handler));

	HttpRoute *unanswered_route = http_route_create (REQUEST_METHOD_GET, "unanswered", server_test_unanswered_handler);
	http_cerver_route_register (http_cerver, unanswered_route);

	HttpRoute *gone_route = http_route_create (REQUEST_METHOD_GET, "gone", server_test_gone_handler);
	http_cerver_route_register (http_cerver, gone_route);

	HttpGroup *nonblocking = http_cerver_group_create (http_cerver, "nonblocking");
	test_check_ptr (http_group_get (nonblocking, "large", server_test_nonblocking_large_handler));
	test_check_ptr (http_group_get (nonblocking, "big", server_test_nonblocking_big_handler));
	test_check_ptr (http_group_get (nonblocking, "file", server_test_nonblocking_file_handler));
	test_check_ptr (http_group_get (nonblocking, "video", server_test_nonblocking_video_handler));

	HttpGroup *deadline = http_cerver_group_create (http_cerver, "deadline");
	test_check_ptr (http_group_get (deadline, "big", server_test_deadline_big_handler));
	test_check_ptr (http_group_get (deadline, "video", server_test_deadline_video_handler));

	HttpGroup *zero = http_cerver_group_create (http_cerver, "zero");
	test_check_ptr (http_group_get (zero, "file", server_test_zero_file_handler));
	test_check_ptr (http_group_get (zero, "video", server_test_zero_video_handler));

}

static Cerver *server_test_cerver_create (
	const u16 port, const ServerTestOptions *options
) {

	Cerver *cerver = cerver_create_web ("server-test", port, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_handler_type (cerver, options->handler_type);
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, options->threads);
	cerver_set_thpool_queue_size (cerver, options->queue_size);
	cerver_set_overload_policy (cerver, options->overload_policy);
	cerver_set_drain_policy (cerver, options->drain_policy);
	cerver_set_drain_timeout (cerver, options->drain_timeout);
	cerver_set_send_timeout (cerver, options->send_timeout);

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	http_cerver_set_headers_timeout (http_cerver, options->headers_timeout);
	http_cerver_set_request_timeout (http_cerver, options->request_timeout);
	http_cerver_set_send_timeout_milliseconds (http_cerver, options->response_send_timeout);

	server_test_routes (http_cerver);

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

static ServerTest *server_test_start (const ServerTestOptions *options) {

	ServerTest *server = (ServerTest *) calloc (1, sizeof (ServerTest));
	test_check_ptr (server);

	server->port = (u16) server_test_port++;
	server->cerver = server_test_cerver_create (server->port, options);

	test_check_int_eq (pthread_create (&server->thread, NULL, server_test_thread, server), 0, NULL);
	server->started = true;

	// waits until the cerver is listening and has handled a complete request
	int fd = server_test_connect (server->port);
	server_test_send (fd, ok_request);
	char response[SERVER_TEST_RESPONSE_SIZE];
	(void) server_test_read (fd, response);
	(void) close (fd);

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
	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;

	HttpGroup *group = http_cerver_group_create (http_cerver, "broken");
	test_check_ptr (http_group_get (group, "items", server_test_ok_handler));
	test_check_ptr (http_group_get (group, "items", server_test_ok_handler));

	test_check_unsigned_ne (cerver_start (cerver), 0);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 1, NULL);
	test_check_ptr (strstr (http_cerver_routes_error (http_cerver), "already registered"));

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

// the POLL handler serves the same requests as the THREADS one,
// a web cerver handles its requests in its thpool with both types,
// only the cervers of the protocol of cerver create it just with CERVER_HANDLER_TYPE_THREADS
static void test_server_poll (void) {

	ServerTestOptions options = server_test_options_default ();
	options.handler_type = CERVER_HANDLER_TYPE_POLL;

	ServerTest *server = server_test_start (&options);

	test_check_ptr (server->cerver->thpool);

	for (unsigned int i = 0; i < SERVER_TEST_POLL_REQUESTS; i++) {
		server_test_request (server->port, ok_request, status_ok);
	}

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

	ServerTestOptions options = server_test_options_default ();
	ServerTest *server = server_test_start (&options);

	server_test_huge_block_create ();

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

	// half of the body
	http_cerver_set_max_body_size ((HttpCerver *) server->cerver->cerver_data, server_test_huge_size >> 1);

	fd = server_test_connect (server->port);
	server_test_huge_headers (fd);
	server_test_expect (fd, status_payload_too_large);
	(void) close (fd);

	free (body);

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

	HttpCerver *http_cerver = (HttpCerver *) server->cerver->cerver_data;
	(void) pthread_mutex_lock (http_cerver->mutex);
	const size_t n_timeout_requests = http_cerver->n_timeout_requests;
	(void) pthread_mutex_unlock (http_cerver->mutex);
	test_check (n_timeout_requests == 1, NULL);

	server_test_stop (server);

}

// a client that never reads its response does not block the thread forever
static void test_server_send_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	int slow_reader = server_test_connect (server->port);
	server_test_send (slow_reader, big_request);

	// the only thread is free again after the send timeout
	server_test_request (server->port, ok_request, status_ok);

	test_check (__atomic_load_n (&big_failed, __ATOMIC_SEQ_CST) == 1, "the big response did not fail");

	(void) close (slow_reader);

	server_test_stop (server);

	free (big_response);
	big_response = NULL;

}

#pragma endregion

#pragma region responses

static size_t server_test_unanswered_count (const ServerTest *server) {

	const HttpCerver *http_cerver = (const HttpCerver *) server->cerver->cerver_data;

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

	String *metrics = http_cerver_metrics_generate ((const HttpCerver *) server->cerver->cerver_data);
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

	server_test_stop (server);

	(void) unlink (server_test_file);

}

// a non blocking socket that never drains fails when the cerver's send timeout ends,
// before, it failed right away after a few retries
static void test_server_nonblocking_timeout (void) {

	ServerTestOptions options = server_test_options_default ();
	options.send_timeout = SERVER_TEST_SEND_TIMEOUT;
	ServerTest *server = server_test_start (&options);

	big_response = (char *) malloc (SERVER_TEST_BIG_RESPONSE_SIZE);
	test_check_ptr (big_response);
	(void) memset (big_response, 'a', SERVER_TEST_BIG_RESPONSE_SIZE);

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);

	int fd = server_test_connect_small (server->port);
	server_test_send (fd, nonblocking_big_request);

	server_test_wait_count (&nonblocking_done, done + 1);
	(void) close (fd);

	test_check_unsigned_eq (__atomic_load_n (&nonblocking_failed, __ATOMIC_SEQ_CST), 1, NULL);

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

// sends the request with a client that never reads,
// and checks that its response failed within the deadline of the response
static void server_test_response_deadline (const u16 port, const char *request) {

	const unsigned int done = __atomic_load_n (&nonblocking_done, __ATOMIC_SEQ_CST);

	int fd = server_test_connect_small (port);
	server_test_send (fd, request);

	server_test_wait_count (&nonblocking_done, done + 1);
	(void) close (fd);

	test_check_unsigned_eq (__atomic_load_n (&nonblocking_failed, __ATOMIC_SEQ_CST), 1, NULL);

	const unsigned long milliseconds = __atomic_load_n (&nonblocking_milliseconds, __ATOMIC_SEQ_CST);
	test_check (
		milliseconds >= (SERVER_TEST_RESPONSE_SEND_TIMEOUT - SERVER_TEST_SEND_TIMEOUT_MARGIN),
		"the response did not wait for its deadline"
	);

	test_check (
		milliseconds <= (SERVER_TEST_RESPONSE_SEND_TIMEOUT + SERVER_TEST_RESPONSE_SEND_TOLERANCE),
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

	server_test_response_deadline (server->port, deadline_big_request);
	server_test_response_deadline (server->port, deadline_video_request);

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

	server_test_sleep (SERVER_TEST_SLOW_SLEEP);

	// the extra connections are closed by the cerver, instead of staying in the backlog
	unsigned int closed = 0;
	for (unsigned int i = 0; i < SERVER_TEST_FILES_CONNECTIONS; i++) {
		if (server_test_is_closed (fds[i])) closed++;
	}

	test_check (closed > 0, "the cerver did not close the connections that it could not handle");

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

	server_test_request (first->port, ok_request, status_ok);
	server_test_request (second->port, ok_request, status_ok);

	server_test_stop (first);

	server_test_request (second->port, ok_request, status_ok);

	server_test_stop (second);

	ServerTest *third = server_test_start (&options);

	server_test_request (third->port, ok_request, status_ok);

	server_test_stop (third);

}

// a web cerver that was stopped before it started never takes the shared state
static void test_server_stop_before_start (void) {

	ServerTestOptions options = server_test_options_default ();

	const u16 port = (u16) server_test_port++;
	Cerver *cerver = server_test_cerver_create (port, &options);

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);
	test_check_unsigned_eq (cerver_start (cerver), 0, NULL);
	test_check (!((HttpCerver *) cerver->cerver_data)->global_reference, "a stopped cerver took the shared state");
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	ServerTest *server = server_test_start (&options);

	server_test_request (server->port, ok_request, status_ok);

	server_test_stop (server);

}

#pragma endregion

void http_tests_server (void) {

	(void) printf ("Testing HTTP server...\n");

	// a hang fails the suite instead of blocking it forever
	(void) alarm (SERVER_TEST_ALARM);

	test_run (test_server_blocking);
	test_run (test_server_reusable);
	test_run (test_server_policies_strings);

	test_run (test_server_ok);
	test_run (test_server_routes_error);
	test_run (test_server_large_response);
	test_run (test_server_poll);
	test_run (test_server_reset_during_response);
	test_run (test_server_huge_body);
	test_run (test_server_slowloris);
	test_run (test_server_idle);
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
	test_run (test_server_stop_before_start);

	test_run (test_server_out_of_fds);
	test_run (test_server_zero_fd);

	(void) alarm (0);

	(void) printf ("Done!\n");

}
