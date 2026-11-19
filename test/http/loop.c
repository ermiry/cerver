#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/data.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/core/types.h>
#include <cerver/events/events.h>
#include "cerver/events/internal.h"
#include <cerver/handler/events.h>
#include <cerver/network/network.h>

#include <cerver/http/clients/client.h>
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include "cerver/http/core/internal.h"
#include <cerver/http/core/limits.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/headers/types.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include "cerver/http/routes/internal.h"
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include "../handler.h"
#include "../test.h"
#include "http.h"

// runs a real web cerver with event loops in a thread and talks to it with raw sockets, to test the handlers
// that run in the event loop itself (the pool HTTP_POOL_LOOP_NAME) and the requests of a pipeline that the
// loop handles in a row

// what this suite pins:
// - every request crossed from its loop to a pool of threads and back; a handler that never waits runs in the
//   loop that read its request, chosen by its endpoint, one of its groups, its route or its cerver
// - each request of a pipeline cost a step of the loop and a send; the ones that the loop handles end when
//   their response is written, and their responses leave in one send
// - the responses that the loop wrote must not wait for anything else: a request of the pipeline that is not
//   complete, or one that goes to a pool of threads

#define LOOP_PORT_BASE						((1 << 13) + (1 << 11))
#define LOOP_ADDRESS						"127.0.0.1"

#define LOOP_CONNECT_RETRIES				(1 << 8)
// 2^22 ns
#define LOOP_PAUSE_INTERVAL					4194304L

// secs that a client waits for a response
#define LOOP_RECEIVE_TIMEOUT				16

// secs that a cerver has to stop
#define LOOP_STOP_TIMEOUT					(1 << 4)
#define LOOP_DRAIN_TIMEOUT					(1 << 3)

// secs that the test waits for a state of its cerver
#define LOOP_WAIT_TIMEOUT					(1 << 4)

// secs of the request timeout of the cerver whose deferred responses are not completed in time
#define LOOP_DEFERRED_REQUEST_TIMEOUT		1

// the pauses of the thread that completes a deferred request after its cerver was asked to stop
#define LOOP_DEFERRED_LATE_PAUSES			(1 << 6)

#define LOOP_ALARM							256

#define LOOP_THREADS						2
#define LOOP_QUEUE_SIZE						(1 << 4)

#define LOOP_THREAD_NAME_SIZE				(1 << 4)

// where the files of the requests of the test are saved, and where the body of the request with one is cut:
// it arrives in more than one read
#define LOOP_UPLOADS_PATH					"/tmp/cerver-loop-uploads"
#define LOOP_UPLOADS_MODE					0755

// the requests with a file that the test of the files sends to the endpoint of the event loops
#define LOOP_FILES_REQUESTS					3

// the static path of the test with a custom authentication, and its file
#define LOOP_STATIC_PATH					"/tmp/cerver-loop-static"
#define LOOP_STATIC_FILE					LOOP_STATIC_PATH "/secret.txt"
#define LOOP_STATIC_FILE_MODE				0644

// the bytes that the handlers of the parts of the test keep of all the parts of a request, the parts of its
// request and the bytes of its body (its Content-Length)
#define LOOP_PARTS_DATA_SIZE				256
#define LOOP_PARTS_COUNT					2

// the pieces of the stream that is written when the piece before each one was sent
#define LOOP_DRAINED_PIECES					4
#define LOOP_PARTS_BODY_LENGTH				221
#define LOOP_FILES_BODY_SPLIT				64

// the bytes of the url that the handlers of the test ask with the HTTP client of their cerver
#define LOOP_CLIENT_URL_SIZE				(1 << 6)

// the requests of the test of that client after the first one: their connections were kept
#define LOOP_CLIENT_REQUESTS				8

// what a client keeps of the responses that it has not read
#define LOOP_BUFFER_SIZE					(1 << 15)
// the body of a response that a test checks
#define LOOP_BODY_SIZE						(1 << 6)

// the requests of a pipeline that a client writes at once
#define LOOP_PIPELINE_SIZE					(1 << 12)

// a pipeline that the loop handles in one batch, and its only send
#define LOOP_PIPELINE_REQUESTS				(1 << 4)
#define LOOP_PIPELINE_SENDS					1

// a pipeline of more requests than a batch: the first batch and the request that ends it leave in one send,
// the rest in another one
#define LOOP_PIPELINE_LONG_REQUESTS			40
#define LOOP_PIPELINE_LONG_SENDS			2

// a request of the loop, one of a pool of threads and one of the loop: a send for each one
#define LOOP_PIPELINE_MIXED_SENDS			3

// requests of the loop whose responses are joined in one part, and then one of a pool of threads: a send for
// the joined responses and one for the response of the pool. The responses joined after the first one have more
// bytes than the one of the pool
#define LOOP_PIPELINE_JOINED_ANSWERS		4
#define LOOP_PIPELINE_JOINED_SENDS			2

// two pipelines of one connection, that together have more requests than a batch: a send for each one
#define LOOP_PIPELINE_AGAIN_REQUESTS		20
#define LOOP_PIPELINE_AGAIN_SENDS			2

// the requests of a pipeline whose second one closes the connection, and the ones that are answered
#define LOOP_PIPELINE_CLOSED_ANSWERS		2

// the complete requests of a pipeline that ends in the middle of a request
#define LOOP_PIPELINE_PARTIAL_ANSWERS		2

// the requests of the pipeline of a client that resets its connection before any response is sent
#define LOOP_PIPELINE_RESET_REQUESTS		4

// the requests of a pipeline that no endpoint handles: cerver answers each one in the loop, and its response
// leaves before the next one is read, a send for each one
#define LOOP_PIPELINE_SHORT_REQUESTS		4

// the requests of a pipeline whose cerver stops while the loop handles the first one, and the ones answered
#define LOOP_PIPELINE_STOPPING_REQUESTS		4
#define LOOP_PIPELINE_STOPPING_ANSWERS		1

// the body of the response that does not fit in a socket: 2^22 bytes
#define LOOP_BIG_SIZE						((size_t) 1 << 22)
#define LOOP_BIG_LETTERS					26
#define LOOP_BIG_CHUNK						(1 << 14)

// the file that a response sends, the first letters of the big body: it fits in the body that a test checks
#define LOOP_FILE_SIZE						LOOP_BIG_LETTERS
#define LOOP_FILE_PATH_SIZE					(1 << 6)

#define LOOP_STATUS_BASE					10

// the connections for each event loop of the cerver that has one for each processor
#define LOOP_PER_CORE_CONNECTIONS			4

static const char *loop_cerver_name = { "loop" };

static const char *loop_file_template = { "/tmp/cerver-loop-XXXXXX" };
static const char *loop_missing_name = { "missing" };

// how the name of the thread of an event loop starts, and the one of a thread of a pool
static const char *loop_events_thread = { "events-" };
static const char *loop_pool_thread = { "thpool-" };

static const char *loop_status_prefix = { "HTTP/1.1 " };
static const char *loop_header_end = { "\r\n\r\n" };
static const char *loop_content_length = { "Content-Length: " };

static const char *loop_valid_authorization = { "valid" };

static const char *loop_thread_request = { "GET /loop/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_thread_close_request = {
	"GET /loop/thread HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_pool_request = { "GET /loop/pool/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_close_request = {
	"GET /loop/pool/thread HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};
static const char *loop_pool_gate_request = { "GET /loop/pool/gate HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_deferred_request = { "GET /loop/deferred HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_deferred_early_request = { "GET /loop/early HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_deferred_request = { "GET /loop/pool/deferred HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_deferred_early_request = { "GET /loop/pool/early HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a request with a file: its head, for the event loops and for the pool, and its body
static const char *loop_files_head = {
	"POST /loop/files HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 151\r\n"
	"\r\n"
};

static const char *loop_pool_files_head = {
	"POST /loop/pool/files HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 151\r\n"
	"\r\n"
};

static const char *loop_files_body = {
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"file\"; filename=\"events.txt\"\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"the file of a request\r\n"
	"--server-test--\r\n"
};

// the content of the file of every request of the tests with one
static const char *loop_files_content = { "the file of a request" };

// the head of a request with a value and a file for the endpoint whose files cerver saves
static const char *loop_files_form_head = {
	"POST /loop/files HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 221\r\n"
	"\r\n"
};

// a request with a value and a file, for the endpoints whose parts are taken by handlers: its head, for the
// event loops and for the pool, and its body; and what the handlers get of it
static const char *loop_parts_head = {
	"POST /loop/parts HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 221\r\n"
	"\r\n"
};

static const char *loop_pool_parts_head = {
	"POST /loop/pool/parts HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 221\r\n"
	"\r\n"
};

static const char *loop_parts_body = {
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"field\"\r\n"
	"\r\n"
	"hello\r\n"
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"file\"; filename=\"events.txt\"\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"the file of a request\r\n"
	"--server-test--\r\n"
};

static const char *loop_parts_expected = { "hellothe file of a request" };

// a request whose first part the handlers of the test reject
static const char *loop_parts_forbidden_request = {
	"POST /loop/parts HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"Content-Type: multipart/form-data; boundary=server-test\r\n"
	"Content-Length: 221\r\n"
	"\r\n"
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"forbidden\"\r\n"
	"\r\n"
	"h\r\n"
	"--server-test\r\n"
	"Content-Disposition: form-data; name=\"file\"; filename=\"events.txt\"\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"the file of a request\r\n"
	"--server-test--\r\n"
};

// the requests of the endpoints whose authentication defers its verdict, in the event loops and in the pool
static const char *loop_verdict_request = { "GET /loop/verdict HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_verdict_request = { "GET /loop/pool/verdict HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_verdict_body_request = {
	"POST /loop/verdict HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nbody"
};
static const char *loop_pool_verdict_body_request = {
	"POST /loop/pool/verdict HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_late_request = {
	"POST /loop/late HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nbody"
};
static const char *loop_pool_late_request = {
	"POST /loop/pool/late HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_decided_request = {
	"GET /loop/decided HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};
static const char *loop_decided_bad_request = {
	"GET /loop/decided HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};
static const char *loop_pool_decided_request = {
	"GET /loop/pool/decided HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};
static const char *loop_pool_decided_bad_request = {
	"GET /loop/pool/decided HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};

static const char *loop_checked_request = {
	"GET /loop/checked HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};
static const char *loop_checked_bad_request = {
	"GET /loop/checked HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};
static const char *loop_pool_checked_request = {
	"GET /loop/pool/checked HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};
static const char *loop_pool_checked_bad_request = {
	"GET /loop/pool/checked HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};

// the file of the static path with a custom authentication: its request and its content
static const char *loop_static_request = { "GET /secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_static_content = { "the file of a private static path" };

static const char *loop_client_request = { "GET /loop/client HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_client_request = { "GET /loop/pool/client HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_deferred_after_request = { "GET /loop/after HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_held_request = { "GET /loop/held HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_held_request = { "GET /loop/pool/held HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// what the thread that completes a deferred request answers it with
static const char *loop_deferred_body = { "deferred" };

static const char *loop_stream_request = { "GET /loop/stream HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_stream_request = { "GET /loop/pool/stream HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// the pieces of a deferred stream, and how each one arrives in a chunked body
static const char *loop_stream_first = { "first" };
static const char *loop_stream_second = { "second" };
static const char *loop_stream_chunked = { "Transfer-Encoding: chunked\r\n" };
static const char *loop_stream_first_chunk = { "\r\n5\r\nfirst\r\n" };
static const char *loop_stream_second_chunk = { "\r\n6\r\nsecond\r\n" };
static const char *loop_stream_last_chunk = { "\r\n0\r\n\r\n" };

// the stream whose pieces are written when the one before each one was sent: its requests and its whole body
static const char *loop_drained_request = { "GET /loop/drained HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_pool_drained_request = { "GET /loop/pool/drained HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_drained_body = {
	"\r\n\r\n5\r\nfirst\r\n5\r\nfirst\r\n5\r\nfirst\r\n5\r\nfirst\r\n0\r\n\r\n"
};

static const char *loop_explicit_request = { "GET /explicit/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_endpoint_request = { "GET /plain/chosen HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_plain_request = { "GET /plain/thread HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_old_request = { "GET /old HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static const char *loop_private_request = {
	"GET /loop/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\n\r\n"
};

static const char *loop_private_bad_request = {
	"GET /loop/private HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\n\r\n"
};

// with a body: the parser pauses when the headers are complete, to authenticate before the body is received
static const char *loop_upload_request = {
	"POST /loop/upload HTTP/1.1\r\nHost: localhost\r\nAuthorization: valid\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_upload_bad_request = {
	"POST /loop/upload HTTP/1.1\r\nHost: localhost\r\nAuthorization: other\r\nContent-Length: 4\r\n\r\nbody"
};

static const char *loop_count_request = { "GET /loop/count HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_count_close_request = {
	"GET /loop/count HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_gate_request = { "GET /loop/gate HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a path that no endpoint handles
static const char *loop_missing_request = { "GET /loop/missing HTTP/1.1\r\nHost: localhost\r\n\r\n" };
static const char *loop_missing_close_request = {
	"GET /loop/missing HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
};

static const char *loop_big_request = { "GET /loop/big HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n" };

static const char *loop_file_request = { "GET /loop/file HTTP/1.1\r\nHost: localhost\r\n\r\n" };

// a request in two pieces
static const char *loop_count_start = { "GET /loop/count HTTP/1.1\r\nHo" };
static const char *loop_count_end = { "st: localhost\r\nConnection: close\r\n\r\n" };

static const char *loop_processors_request = { "GET /loop/processors HTTP/1.1\r\nHost: localhost\r\n\r\n" };

static unsigned int loop_port = LOOP_PORT_BASE;

// the cervers of the test that runs now have an event loop for each processor, each one in its own
static bool loop_per_core = false;

// the request timeout of the cervers of the test that runs now, in seconds; 0 keeps the default one
static unsigned int loop_request_timeout = 0;

#pragma region routes

// the requests that the count and the gate routes handled, added by the threads of the cervers
static unsigned int loop_handled = 0;

// the endpoint GET /loop/count of the last cerver: its stats count the requests that ended
static CerverHttpEndpoint *loop_count_endpoint = NULL;

// the requests of the last cerver that its loops ended, added by their observer
static unsigned int loop_observed = 0;

// the name of the thread that ran the last authentication
static pthread_mutex_t loop_threads_mutex = PTHREAD_MUTEX_INITIALIZER;
static char loop_authentication_thread[LOOP_THREAD_NAME_SIZE] = { 0 };

// keeps the gate requests working until it is opened
static pthread_mutex_t loop_gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t loop_gate_cond = PTHREAD_COND_INITIALIZER;
static bool loop_gate_is_open = false;
static unsigned int loop_gate_waiting = 0;

// the body of the big response, the letters in order
static char *loop_big_body = NULL;

// the file that GET /loop/file sends, created by its test
static char loop_file_path[LOOP_FILE_PATH_SIZE] = { 0 };

static void loop_pause (void) {

	struct timespec interval = { 0, LOOP_PAUSE_INTERVAL };
	(void) nanosleep (&interval, NULL);

}

static void loop_gate_reset (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_is_open = false;
	loop_gate_waiting = 0;
	(void) pthread_mutex_unlock (&loop_gate_mutex);

}

static void loop_gate_open (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_is_open = true;
	(void) pthread_cond_broadcast (&loop_gate_cond);
	(void) pthread_mutex_unlock (&loop_gate_mutex);

}

static unsigned int loop_gate_count (void) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	const unsigned int waiting = loop_gate_waiting;
	(void) pthread_mutex_unlock (&loop_gate_mutex);

	return waiting;

}

static void loop_gate_wait_for (const unsigned int waiting) {

	const double deadline = test_wait_now () + (double) (LOOP_WAIT_TIMEOUT * test_time_scale ());
	while ((loop_gate_count () < waiting) && (test_wait_now () < deadline)) loop_pause ();

	test_check_unsigned_eq (loop_gate_count (), waiting, NULL);

}

static void loop_authentication_thread_reset (void) {

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memset (loop_authentication_thread, 0, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

}

static void loop_authentication_thread_expect (const char *prefix) {

	char saved[LOOP_THREAD_NAME_SIZE] = { 0 };

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memcpy (saved, loop_authentication_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	if (strncmp (saved, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", saved, prefix);
	}

	test_check_int_eq (strncmp (saved, prefix, strlen (prefix)), 0, NULL);

}

// answers with the name of the thread that handles the request
static void loop_thread_handler (CerverHttpContext *context) {

	char name[LOOP_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, LOOP_THREAD_NAME_SIZE);

	(void) cerver_http_response_send_text (context, name, strlen (name));

}

// the HTTP client of the cerver of the test, and the url that its handlers ask with it: another endpoint of
// the same cerver, that answers with the name of its thread
static CerverHttpClient *loop_http_client = NULL;
static char loop_client_url[LOOP_CLIENT_URL_SIZE] = { 0 };

// the url of the endpoint that an authentication asks
static char loop_check_url[LOOP_CLIENT_URL_SIZE] = { 0 };

// what the request that a handler of an event loop tried to wait for returned
static unsigned int loop_client_refused = 0;

// the response of the request that a handler sent with the client of its cerver, in the loop that sent it: its
// body answers the request that waited for it
static void loop_client_response_handler (const CerverHttpClientResponse *response, void *context_ptr) {

	CerverHttpContext *context = (CerverHttpContext *) context_ptr;

	const CerverHttpView body = cerver_http_client_response_body (response);

	if (!cerver_http_client_response_error (response) && body.data) {
		(void) cerver_http_response_send_text (context, body.data, body.length);
	}

	(void) cerver_http_context_complete (context);

}

// an event loop does not wait for a request of the client of its cerver
static void loop_client_probe (void) {

	CerverHttpClientRequest *probe = cerver_http_client_request_create (
		loop_http_client, REQUEST_METHOD_GET, loop_client_url
	);

	__atomic_store_n (
		&loop_client_refused, (unsigned int) cerver_http_client_request_perform (probe), __ATOMIC_SEQ_CST
	);

	cerver_http_client_request_delete (probe);

}

// defers its response and asks another endpoint of its cerver for it: its event loop sends that request and
// goes on, and the handler of the response completes this one
static void loop_client_handler (CerverHttpContext *context) {

	loop_client_probe ();

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		loop_http_client, REQUEST_METHOD_GET, loop_client_url
	);

	if (request && !cerver_http_context_defer (context)) {
		if (cerver_http_client_request_send (request, loop_client_response_handler, context)) {
			(void) cerver_http_context_complete (context);
		}
	}

}

// a handler of a pool of threads waits for the response of the request that it sends
static void loop_client_waiting_handler (CerverHttpContext *context) {

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		loop_http_client, REQUEST_METHOD_GET, loop_client_url
	);

	if (request && !cerver_http_client_request_perform (request)) {
		const CerverHttpView body = cerver_http_client_response_body (cerver_http_client_request_response (request));

		if (body.data) (void) cerver_http_response_send_text (context, body.data, body.length);
	}

	cerver_http_client_request_delete (request);

}

// the context of the request whose response was deferred, that a test completes from its own thread
static CerverHttpContext *loop_deferred_context = NULL;

// the handlers that deferred their response and returned
static unsigned int loop_deferred_count = 0;

// what a second deferral of the same response returned, and what the calls of the handler that completes its
// request before it returns did
static unsigned int loop_deferred_again = 0;
static unsigned int loop_deferred_early_completed = 0;
static unsigned int loop_deferred_early_again = 0;

// the handlers that completed their request before they returned: a thread of a pool sends the response while
// its handler still runs, so its client can read it before the handler counted what it did
static unsigned int loop_deferred_early_count = 0;

// what the deferral of an after middleware returned, and the after middlewares that ran
static unsigned int loop_deferred_after = 0;
static unsigned int loop_deferred_after_count = 0;

// defers its response and returns: the test writes it and completes the request
static void loop_deferred_handler (CerverHttpContext *context) {

	test_check_unsigned_eq (cerver_http_context_defer (context), 0, NULL);

	// a response is deferred once
	__atomic_store_n (&loop_deferred_again, (unsigned int) cerver_http_context_defer (context), __ATOMIC_SEQ_CST);

	__atomic_store_n (&loop_deferred_context, context, __ATOMIC_SEQ_CST);

	test_counter_add (&loop_deferred_count);

}

// begins a stream, defers its response and returns: the test writes the pieces of the stream over time
static void loop_deferred_stream_handler (CerverHttpContext *context) {

	test_check_unsigned_eq (cerver_http_response_stream_begin (context), 0, NULL);

	test_check_unsigned_eq (cerver_http_context_defer (context), 0, NULL);

	__atomic_store_n (&loop_deferred_context, context, __ATOMIC_SEQ_CST);

	test_counter_add (&loop_deferred_count);

}

// the pieces that the stream of the test has, the ones that its handler wrote, the times that its loop told
// it that what it wrote was sent, and what a handler of a pool of threads got when it asked to be told
static unsigned int loop_drained_written = 0;
static unsigned int loop_drained_calls = 0;
static unsigned int loop_drained_refused = 0;

static void loop_drained_piece (CerverHttpContext *context) {

	(void) cerver_http_response_stream_write (context, loop_stream_first, strlen (loop_stream_first));

	loop_drained_written += 1;

}

// what was written of the stream was sent: the next piece is written, and after the last one the stream ends
// and its request is completed
static void loop_stream_drained (CerverHttpContext *context, void *data) {

	(void) data;

	test_counter_add (&loop_drained_calls);

	if (loop_drained_written < LOOP_DRAINED_PIECES) {
		loop_drained_piece (context);
	}

	else {
		(void) cerver_http_response_stream_end (context);

		(void) cerver_http_context_complete (context);
	}

}

// starts a stream and writes its first piece. In an event loop it asks to be told when it was sent, defers its
// response and returns: its loop asks it for each piece. In a pool of threads it is not told: it writes them all
static void loop_drained_handler (CerverHttpContext *context) {

	loop_drained_written = 0;

	test_check_unsigned_eq (cerver_http_response_stream_begin (context), 0, NULL);

	loop_drained_piece (context);

	const u8 refused = cerver_http_response_stream_on_drained (context, loop_stream_drained, NULL);

	__atomic_store_n (&loop_drained_refused, (unsigned int) refused, __ATOMIC_SEQ_CST);

	if (refused) {
		while (loop_drained_written < LOOP_DRAINED_PIECES) loop_drained_piece (context);
	}

	else {
		test_check_unsigned_eq (cerver_http_context_defer (context), 0, NULL);
	}

}

// defers its response, writes it and completes its request before it returns: the request ends when it does
static void loop_deferred_early_handler (CerverHttpContext *context) {

	test_check_unsigned_eq (cerver_http_context_defer (context), 0, NULL);

	(void) cerver_http_response_send_text (context, loop_deferred_body, strlen (loop_deferred_body));

	__atomic_store_n (
		&loop_deferred_early_completed, (unsigned int) cerver_http_context_complete (context), __ATOMIC_SEQ_CST
	);

	// a request is completed once
	__atomic_store_n (
		&loop_deferred_early_again, (unsigned int) cerver_http_context_complete (context), __ATOMIC_SEQ_CST
	);

	test_counter_add (&loop_deferred_early_count);

}

// only the handler of a request defers its response: an after middleware runs when the request was answered
static void loop_deferred_after_middleware (CerverHttpContext *context) {

	__atomic_store_n (&loop_deferred_after, (unsigned int) cerver_http_context_defer (context), __ATOMIC_SEQ_CST);

	test_counter_add (&loop_deferred_after_count);

}

// keeps its thread until the gate is opened, with the context of its request in sight of the test
static void loop_held_handler (CerverHttpContext *context) {

	__atomic_store_n (&loop_deferred_context, context, __ATOMIC_SEQ_CST);

	test_counter_add (&loop_deferred_count);

	(void) pthread_mutex_lock (&loop_gate_mutex);
	while (!loop_gate_is_open) (void) pthread_cond_wait (&loop_gate_cond, &loop_gate_mutex);
	(void) pthread_mutex_unlock (&loop_gate_mutex);

	(void) cerver_http_response_send_text (context, loop_deferred_body, strlen (loop_deferred_body));

}

// the processors that the thread of the caller can run in, 0 when they can not be read
static unsigned int loop_processors (void) {

	cpu_set_t processors;
	CPU_ZERO (&processors);

	return !sched_getaffinity (0, sizeof (cpu_set_t), &processors) ? (unsigned int) CPU_COUNT (&processors) : 0;

}

// answers with the name of the thread that handles the request and the processors that it can run in
static void loop_processors_handler (CerverHttpContext *context) {

	char name[LOOP_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, LOOP_THREAD_NAME_SIZE);

	char text[LOOP_BODY_SIZE] = { 0 };
	const int length = snprintf (text, LOOP_BODY_SIZE, "%s %u", name, loop_processors ());

	(void) cerver_http_response_send_text (context, text, (size_t) length);

}

static void loop_count_handler (CerverHttpContext *context) {

	test_counter_add (&loop_handled);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// answers when the gate is opened: only a test keeps a loop waiting
static void loop_gate_handler (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&loop_gate_mutex);
	loop_gate_waiting += 1;
	while (!loop_gate_is_open) (void) pthread_cond_wait (&loop_gate_cond, &loop_gate_mutex);
	(void) pthread_mutex_unlock (&loop_gate_mutex);

	test_counter_add (&loop_handled);

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

static void loop_big_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_text (context, loop_big_body, LOOP_BIG_SIZE);

}

// its headers are written as bytes and its body as a file, that the loop sends after them
static void loop_file_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_file (context, loop_file_path);

}

// only with the valid authorization, and tells which thread checked it
static unsigned int loop_authentication_handler (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) pthread_getname_np (pthread_self (), loop_authentication_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	return cerver_http_view_equals (authorization, loop_valid_authorization) ? 0 : 1;

}

// the thread that named the last file that was uploaded: the one that parses the body of its request and
// writes its files
static char loop_upload_thread[LOOP_THREAD_NAME_SIZE] = { 0 };

static void loop_uploads_filename_generator (CerverHttpContext *context) {

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) pthread_getname_np (pthread_self (), loop_upload_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	cerver_http_default_uploads_filename_generator (context);

}

static void loop_upload_thread_reset (void) {

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memset (loop_upload_thread, 0, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

}

static void loop_upload_thread_expect (const char *prefix) {

	char saved[LOOP_THREAD_NAME_SIZE] = { 0 };

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memcpy (saved, loop_upload_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	if (strncmp (saved, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", saved, prefix);
	}

	test_check_int_eq (strncmp (saved, prefix, strlen (prefix)), 0, NULL);

}

// what the handlers of the parts of a request saw: the parts that started and the ones that ended, the bytes
// of all of them one after the other, and the thread that took them
static unsigned int loop_parts_started = 0;
static unsigned int loop_parts_ended = 0;
static char loop_parts_data[LOOP_PARTS_DATA_SIZE] = { 0 };
static size_t loop_parts_length = 0;
static char loop_parts_thread[LOOP_THREAD_NAME_SIZE] = { 0 };

// the name of the part that the handlers of the test do not take
static const char *loop_parts_forbidden = { "forbidden" };

// a part starts: one with the forbidden name rejects its request
static CerverHttpStatus loop_part_started (CerverHttpContext *context, const CerverHttpPart *part) {

	(void) context;

	test_counter_add (&loop_parts_started);

	return cerver_http_view_equals (cerver_http_part_name (part), loop_parts_forbidden) ?
		HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE : HTTP_STATUS_NONE;

}

static CerverHttpStatus loop_part_data (
	CerverHttpContext *context, const CerverHttpPart *part, const char *data, const size_t length
) {

	(void) context;
	(void) part;

	(void) pthread_mutex_lock (&loop_threads_mutex);

	(void) pthread_getname_np (pthread_self (), loop_parts_thread, LOOP_THREAD_NAME_SIZE);

	if ((loop_parts_length + length) < LOOP_PARTS_DATA_SIZE) {
		(void) memcpy (loop_parts_data + loop_parts_length, data, length);
		loop_parts_length += length;
	}

	(void) pthread_mutex_unlock (&loop_threads_mutex);

	return HTTP_STATUS_NONE;

}

static CerverHttpStatus loop_part_ended (CerverHttpContext *context, const CerverHttpPart *part) {

	(void) context;
	(void) part;

	test_counter_add (&loop_parts_ended);

	return HTTP_STATUS_NONE;

}

static const CerverHttpPartHandlers loop_part_handlers = {
	.on_part = loop_part_started,
	.on_data = loop_part_data,
	.on_end = loop_part_ended
};

static void loop_parts_reset (void) {

	__atomic_store_n (&loop_parts_started, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&loop_parts_ended, 0, __ATOMIC_SEQ_CST);

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memset (loop_parts_data, 0, LOOP_PARTS_DATA_SIZE);
	loop_parts_length = 0;
	(void) memset (loop_parts_thread, 0, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

}

// the handlers of the parts took the expected bytes, in a thread of the pool
static void loop_parts_expect (const unsigned int parts, const char *data) {

	test_check_unsigned_eq (__atomic_load_n (&loop_parts_started, __ATOMIC_SEQ_CST), parts, NULL);
	test_check_unsigned_eq (__atomic_load_n (&loop_parts_ended, __ATOMIC_SEQ_CST), parts, NULL);

	char saved[LOOP_PARTS_DATA_SIZE] = { 0 };
	char thread[LOOP_THREAD_NAME_SIZE] = { 0 };

	(void) pthread_mutex_lock (&loop_threads_mutex);
	(void) memcpy (saved, loop_parts_data, LOOP_PARTS_DATA_SIZE);
	(void) memcpy (thread, loop_parts_thread, LOOP_THREAD_NAME_SIZE);
	(void) pthread_mutex_unlock (&loop_threads_mutex);

	test_check_str_eq (saved, data, NULL);

	test_check_int_eq (strncmp (thread, loop_pool_thread, strlen (loop_pool_thread)), 0, NULL);

}

// the cerver of the test has a static path with a custom authentication
static bool loop_static_private = false;

// the endpoints of the event loops that take files: the one whose files cerver saves, and the one whose parts
// are taken by handlers of the test; of the cerver that runs
static const CerverHttpEndpoint *loop_files_endpoint = NULL;
static const CerverHttpEndpoint *loop_parts_endpoint = NULL;

// the context of the request whose authentication deferred its verdict, that a test gives from its own thread,
// and the authentication handlers that deferred theirs
static CerverHttpContext *loop_verdict_context = NULL;
static unsigned int loop_verdict_count = 0;

// what an authentication handler that deferred its verdict returns: it is not read, and it would reject
#define LOOP_VERDICT_IGNORED				1

// defers its verdict and returns: the test gives it
static unsigned int loop_verdict_authentication_handler (CerverHttpContext *context) {

	// an authentication handler does not defer the response of its request, and it defers its verdict once
	test_check_unsigned_eq (cerver_http_context_defer (context), 1, NULL);
	test_check_unsigned_eq (cerver_http_context_authentication_defer (context), 0, NULL);
	test_check_unsigned_eq (cerver_http_context_authentication_defer (context), 1, NULL);

	__atomic_store_n (&loop_verdict_context, context, __ATOMIC_SEQ_CST);

	test_counter_add (&loop_verdict_count);

	return LOOP_VERDICT_IGNORED;

}

// defers its verdict and gives it before it returns: its request goes on when it does
static unsigned int loop_decided_authentication_handler (CerverHttpContext *context) {

	const bool valid = cerver_http_view_equals (
		cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION), loop_valid_authorization
	);

	test_check_unsigned_eq (cerver_http_context_authentication_defer (context), 0, NULL);
	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, valid), 0, NULL);

	// a verdict is given once
	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, valid), 1, NULL);

	return LOOP_VERDICT_IGNORED;

}

// the service that an authentication asks: only the valid authorization gets a 200
static void loop_check_handler (CerverHttpContext *context) {

	const bool valid = cerver_http_view_equals (
		cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION), loop_valid_authorization
	);

	(void) cerver_http_response_send_common (
		context, valid ? CERVER_HTTP_COMMON_RESPONSE_OK : CERVER_HTTP_COMMON_RESPONSE_BAD_AUTH
	);

}

// the response of the service decides: its event loop gives the verdict
static bool loop_checked_response (CerverHttpContext *context, const CerverHttpClientResponse *response) {

	(void) context;

	return !cerver_http_client_response_error (response)
		&& (cerver_http_client_response_status (response) == HTTP_STATUS_OK);

}

// asks another endpoint of its cerver with the authorization of its request, without waiting for it
static unsigned int loop_checked_authentication_handler (CerverHttpContext *context) {

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		loop_http_client, REQUEST_METHOD_GET, loop_check_url
	);

	if (request && authorization.data) {
		(void) cerver_http_client_request_add_header (request, "Authorization", authorization.data);
	}

	return cerver_http_context_authentication_request (context, request, loop_checked_response);

}

// what the endpoint handler of a request does with the verdict of its authentication: nothing
static unsigned int loop_verdict_refused = 0;

// the handler of an endpoint is not its authentication handler: it defers no verdict
static void loop_verdict_handler (CerverHttpContext *context) {

	__atomic_store_n (
		&loop_verdict_refused, (unsigned int) cerver_http_context_authentication_defer (context), __ATOMIC_SEQ_CST
	);

	loop_thread_handler (context);

}

// an endpoint with a custom authentication, that asks for it before its body or after it
static void loop_route_authenticated (
	CerverHttpEndpoint *endpoint, const CerverHttpAuthenticationHandler handler, const bool with_body
) {

	test_check_ptr (endpoint);

	cerver_http_endpoint_set_auth (endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_endpoint_set_authentication_handler (endpoint, handler);
	cerver_http_endpoint_set_authentication_with_body (endpoint, with_body);

}

// the endpoints whose authentication defers its verdict, in a group: a GET and a POST that the test decides,
// a POST that is authenticated after its body, a GET that decides before its handler returns, and a GET that
// asks another endpoint
static void loop_routes_verdict (CerverHttpGroup *group) {

	loop_route_authenticated (
		cerver_http_group_get (group, "verdict", loop_verdict_handler), loop_verdict_authentication_handler, false
	);

	loop_route_authenticated (
		cerver_http_group_post (group, "verdict", loop_thread_handler), loop_verdict_authentication_handler, false
	);

	loop_route_authenticated (
		cerver_http_group_post (group, "late", loop_thread_handler), loop_verdict_authentication_handler, true
	);

	loop_route_authenticated (
		cerver_http_group_get (group, "decided", loop_thread_handler), loop_decided_authentication_handler, false
	);

	loop_route_authenticated (
		cerver_http_group_get (group, "checked", loop_thread_handler), loop_checked_authentication_handler, false
	);

}

// the group /loop chooses no pool, its requests run where the cerver says (GET /loop/private and
// POST /loop/upload have a custom authentication); its group /loop/pool goes to the thpool of the cerver;
// the group /explicit chooses the pool of the event loops;
// the group /plain chooses no pool, and its endpoint /plain/chosen the one of the event loops;
// GET /old, the root endpoint of a top level group, in the pool of the event loops
static void loop_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *group = cerver_http_group_create (http_cerver, "loop");
	test_check_ptr (group);

	test_check_ptr (cerver_http_group_get (group, "thread", loop_thread_handler));

	loop_count_endpoint = cerver_http_group_get (group, "count", loop_count_handler);
	test_check_ptr (loop_count_endpoint);

	test_check_ptr (cerver_http_group_get (group, "gate", loop_gate_handler));
	test_check_ptr (cerver_http_group_get (group, "big", loop_big_handler));
	test_check_ptr (cerver_http_group_get (group, "file", loop_file_handler));
	test_check_ptr (cerver_http_group_get (group, "processors", loop_processors_handler));

	CerverHttpEndpoint *private_endpoint = cerver_http_group_get (group, "private", loop_thread_handler);
	test_check_ptr (private_endpoint);
	cerver_http_endpoint_set_auth (private_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_endpoint_set_authentication_handler (private_endpoint, loop_authentication_handler);

	CerverHttpEndpoint *upload_endpoint = cerver_http_group_post (group, "upload", loop_thread_handler);
	test_check_ptr (upload_endpoint);
	cerver_http_endpoint_set_auth (upload_endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_endpoint_set_authentication_handler (upload_endpoint, loop_authentication_handler);

	CerverHttpGroup *pool = cerver_http_group_subgroup_add (group, "pool");
	test_check_ptr (pool);
	cerver_http_group_set_pool (pool, HTTP_POOL_DEFAULT_NAME);

	test_check_ptr (cerver_http_group_get (pool, "thread", loop_thread_handler));
	test_check_ptr (cerver_http_group_get (pool, "gate", loop_gate_handler));

	// the handlers that defer their response, in the event loops and in the pool of threads
	test_check_ptr (cerver_http_group_get (group, "deferred", loop_deferred_handler));
	test_check_ptr (cerver_http_group_get (group, "early", loop_deferred_early_handler));
	test_check_ptr (cerver_http_group_get (pool, "deferred", loop_deferred_handler));
	test_check_ptr (cerver_http_group_get (pool, "early", loop_deferred_early_handler));
	test_check_ptr (cerver_http_group_get (group, "stream", loop_deferred_stream_handler));
	test_check_ptr (cerver_http_group_get (pool, "stream", loop_deferred_stream_handler));

	// the stream that is written piece by piece, each one when the one before it was sent
	test_check_ptr (cerver_http_group_get (group, "drained", loop_drained_handler));
	test_check_ptr (cerver_http_group_get (pool, "drained", loop_drained_handler));

	// who can not defer a response: an after middleware, and a thread that is not the one of the handler
	CerverHttpEndpoint *after_endpoint = cerver_http_group_get (group, "after", loop_thread_handler);
	test_check_ptr (after_endpoint);
	test_check_unsigned_eq (
		cerver_http_endpoint_add_after (after_endpoint, loop_deferred_after_middleware), 0, NULL
	);

	test_check_ptr (cerver_http_group_get (group, "held", loop_held_handler));
	test_check_ptr (cerver_http_group_get (pool, "held", loop_held_handler));

	// the handlers that ask another endpoint for their response, with the HTTP client of the cerver
	test_check_ptr (cerver_http_group_get (group, "client", loop_client_handler));
	test_check_ptr (cerver_http_group_get (pool, "client", loop_client_waiting_handler));

	// the authentications that defer their verdict, in the event loops and in the pool of threads, and the
	// service that one of them asks
	loop_routes_verdict (group);
	loop_routes_verdict (pool);

	test_check_ptr (cerver_http_group_get (group, "check", loop_check_handler));

	// the endpoints that take files, in the event loops and in the pool of threads
	CerverHttpEndpoint *files_endpoint = cerver_http_group_post (group, "files", loop_thread_handler);
	test_check_ptr (files_endpoint);
	cerver_http_endpoint_set_modifier (files_endpoint, HTTP_ROUTE_MODIFIER_MULTI_PART);
	loop_files_endpoint = files_endpoint;

	CerverHttpEndpoint *pool_files_endpoint = cerver_http_group_post (pool, "files", loop_thread_handler);
	test_check_ptr (pool_files_endpoint);
	cerver_http_endpoint_set_modifier (pool_files_endpoint, HTTP_ROUTE_MODIFIER_MULTI_PART);

	// the endpoints whose parts are taken by handlers of the test: cerver saves nothing of them
	CerverHttpEndpoint *parts_endpoint = cerver_http_group_post (group, "parts", loop_thread_handler);
	test_check_ptr (parts_endpoint);
	cerver_http_endpoint_set_modifier (parts_endpoint, HTTP_ROUTE_MODIFIER_MULTI_PART);
	cerver_http_endpoint_set_part_handlers (parts_endpoint, &loop_part_handlers);
	loop_parts_endpoint = parts_endpoint;

	CerverHttpEndpoint *pool_parts_endpoint = cerver_http_group_post (pool, "parts", loop_thread_handler);
	test_check_ptr (pool_parts_endpoint);
	cerver_http_endpoint_set_modifier (pool_parts_endpoint, HTTP_ROUTE_MODIFIER_MULTI_PART);
	cerver_http_endpoint_set_part_handlers (pool_parts_endpoint, &loop_part_handlers);

	// a static path whose files need a custom authentication, that defers its verdict
	if (loop_static_private) {
		HttpStaticPath *static_path = cerver_http_static_path_add (http_cerver, LOOP_STATIC_PATH);
		test_check_ptr (static_path);

		cerver_http_static_path_set_auth (static_path, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
		cerver_http_static_path_set_authentication_handler (static_path, loop_verdict_authentication_handler);
	}

	cerver_http_set_uploads_path (http_cerver, LOOP_UPLOADS_PATH);
	cerver_http_set_uploads_filename_generator (http_cerver, loop_uploads_filename_generator);
	cerver_http_set_uploads_delete_when_done (http_cerver, true);

	CerverHttpGroup *explicit_group = cerver_http_group_create (http_cerver, "explicit");
	test_check_ptr (explicit_group);
	cerver_http_group_set_pool (explicit_group, HTTP_POOL_LOOP_NAME);

	test_check_ptr (cerver_http_group_get (explicit_group, "thread", loop_thread_handler));

	CerverHttpGroup *plain = cerver_http_group_create (http_cerver, "plain");
	test_check_ptr (plain);

	test_check_ptr (cerver_http_group_get (plain, "thread", loop_thread_handler));

	CerverHttpEndpoint *chosen = cerver_http_group_get (plain, "chosen", loop_thread_handler);
	test_check_ptr (chosen);
	cerver_http_endpoint_set_pool (chosen, HTTP_POOL_LOOP_NAME);

	CerverHttpGroup *old = cerver_http_group_create (http_cerver, "/old");
	test_check_ptr (old);

	CerverHttpEndpoint *old_endpoint = cerver_http_group_root_add (old, REQUEST_METHOD_GET, loop_thread_handler);
	test_check_ptr (old_endpoint);
	cerver_http_endpoint_set_pool (old_endpoint, HTTP_POOL_LOOP_NAME);

}

#pragma endregion

#pragma region server

typedef struct LoopServer {

	Cerver *cerver;
	CerverHttp *http_cerver;
	u16 port;

	pthread_t thread;

} LoopServer;

static void *loop_server_thread (void *server_ptr) {

	LoopServer *server = (LoopServer *) server_ptr;

	test_check_unsigned_eq (cerver_start (server->cerver), 0, NULL);

	return NULL;

}

static Cerver *loop_cerver_create (void) {

	Cerver *cerver = cerver_create_web (loop_cerver_name, (u16) loop_port++, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_events_backend (cerver, test_events_backend ());
	cerver_set_reusable_address_flags (cerver, true);

	cerver_set_thpool_n_threads (cerver, LOOP_THREADS);
	cerver_set_thpool_queue_size (cerver, LOOP_QUEUE_SIZE);
	cerver_set_drain_timeout (cerver, LOOP_DRAIN_TIMEOUT);

	if (loop_per_core) {
		cerver_set_io_threads (cerver, CERVER_IO_THREADS_PER_CORE);
		cerver_set_io_threads_affinity (cerver, true);
	}

	return cerver;

}

// the loop calls it with each request that ended, right before its data is released
static void loop_observe (const HttpReceive *http_receive, void *args) {

	(void) http_receive;
	(void) args;

	test_counter_add (&loop_observed);

}

// the loops exist when their cerver starts and their threads do not yet: they read their observer after this
static void *loop_observer_set (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	const EventLoopGroup *group = event_data->cerver->event_loops;
	for (unsigned int idx = 0; group && (idx < group->n_loops); idx++) {
		group->loops[idx]->observer = loop_observe;
	}

	cerver_event_data_delete (event_data);

	return NULL;

}

// a cerver whose handlers run in its event loops when in_loop is true (it sets no pool: they are the default),
// in its thpool otherwise;
// the clients connect when it listens: no request is sent to wait for it, the tests count its sends
static LoopServer *loop_server_start (const bool in_loop) {

	LoopServer *server = (LoopServer *) calloc (1, sizeof (LoopServer));
	test_check_ptr (server);

	Cerver *cerver = loop_cerver_create ();

	__atomic_store_n (&loop_observed, 0, __ATOMIC_SEQ_CST);

	test_check_unsigned_eq (
		cerver_event_register (cerver, CERVER_EVENT_STARTED, loop_observer_set, NULL, NULL, false, false), 0, NULL
	);

	CerverHttp *http_cerver = cerver_http_get (cerver);

	cerver_http_set_not_found_handler (http_cerver);

	// the cerver of the deferred responses that expire does not take a half close as a client that is gone:
	// the client of a response that expired is gone because of what cerver did, not because of what a probe finds
	if (loop_request_timeout) {
		cerver_http_set_request_timeout (http_cerver, loop_request_timeout);
		cerver_http_set_half_close_is_gone (http_cerver, false);
	}

	if (!in_loop) {
		test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME), 0, NULL);
	}

	server->cerver = cerver;
	server->http_cerver = http_cerver;
	server->port = (u16) (loop_port - 1);

	loop_http_client = cerver_http_client_create (http_cerver);
	test_check_ptr (loop_http_client);

	(void) snprintf (
		loop_client_url, LOOP_CLIENT_URL_SIZE, "http://127.0.0.1:%u/loop/thread", (unsigned int) server->port
	);

	(void) snprintf (
		loop_check_url, LOOP_CLIENT_URL_SIZE, "http://127.0.0.1:%u/loop/check", (unsigned int) server->port
	);

	loop_routes (http_cerver);

	test_check_int_eq (pthread_create (&server->thread, NULL, loop_server_thread, server), 0, NULL);

	return server;

}

// cerver_start () must return in time after it was asked to stop; the threads of its loops are joined by
// the teardown, until then they can still end their connections
static void loop_server_shutdown (LoopServer *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	struct timespec deadline;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) (LOOP_STOP_TIMEOUT * test_time_scale ());

	const int result = pthread_timedjoin_np (server->thread, NULL, &deadline);
	if (result) (void) fprintf (stderr, "the cerver in port %u did not stop\n", (unsigned int) server->port);
	test_check_int_eq (result, 0, NULL);

}

// the HTTP client of the cerver is deleted before the cerver: its loops close the connections that they kept
static void loop_server_end (LoopServer *server) {

	cerver_http_client_delete (loop_http_client);
	loop_http_client = NULL;

	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);

	free (server);

}

static void loop_server_stop (LoopServer *server) {

	loop_server_shutdown (server);

	loop_server_end (server);

}

static void loop_server_wait (
	const LoopServer *server, unsigned int (*value) (const LoopServer *), const unsigned int expected
) {

	const double deadline = test_wait_now () + (double) (LOOP_WAIT_TIMEOUT * test_time_scale ());
	while ((value (server) != expected) && (test_wait_now () < deadline)) loop_pause ();

	test_check_unsigned_eq (value (server), expected, NULL);

}

// the connections that the loops of a cerver still have: a loop counts one out after its last send
static unsigned int loop_server_connections (const LoopServer *server) {

	const EventLoopGroup *group = server->cerver->event_loops;
	test_check_ptr (group);

	unsigned int connections = 0;
	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		connections += event_loop_connections (group->loops[idx]);
	}

	return connections;

}

// the sends that the loops of a cerver that stopped asked their backends for
static u64 loop_server_sends (const LoopServer *server) {

	const EventLoopGroup *group = server->cerver->event_loops;
	test_check_ptr (group);

	u64 sends = 0;
	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		sends += group->loops[idx]->backend->stats.n_sends;
	}

	return sends;

}

// stops the cerver, that must have asked for some sends
static void loop_server_stop_with_sends (LoopServer *server, const u64 sends) {

	loop_server_shutdown (server);

	// the loops are joined only by the teardown: their counters are read when their connections ended,
	// after the sends of those connections
	loop_server_wait (server, loop_server_connections, 0);

	const u64 asked = loop_server_sends (server);
	if (asked != sends) {
		(void) fprintf (
			stderr, "the cerver asked for %lu sends instead of %lu\n", (unsigned long) asked, (unsigned long) sends
		);
	}

	test_check_true (asked == sends);

	loop_server_end (server);

}

static unsigned int loop_failed_responses (const LoopServer *server) {

	return (unsigned int) CERVER_STATS_GET (server->http_cerver->n_failed_responses);

}

static unsigned int loop_observed_requests (const LoopServer *server) {

	(void) server;

	return __atomic_load_n (&loop_observed, __ATOMIC_SEQ_CST);

}

// the requests of GET /loop/count that were logged and counted in its stats
static unsigned int loop_counted_requests (const LoopServer *server) {

	(void) server;

	HttpRouteStats snapshot;
	cerver_http_route_stats_snapshot (loop_count_endpoint->stats, &snapshot);

	return (unsigned int) snapshot.n_requests;

}

#pragma endregion

#pragma region client

// a connection of a client and the bytes of the responses that it has not read
typedef struct LoopClient {

	int fd;

	char buffer[LOOP_BUFFER_SIZE];
	size_t length;

	// its last read ended with its receive timeout, not with the end of the connection
	bool timed_out;

} LoopClient;

// a response
typedef struct LoopAnswer {

	unsigned int status;

	char body[LOOP_BODY_SIZE];
	size_t body_length;

} LoopAnswer;

static int loop_connect_once (const u16 port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > -1, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, LOOP_ADDRESS, &address.sin_addr);

	if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

static LoopClient *loop_client_connect (const LoopServer *server) {

	LoopClient *client = (LoopClient *) calloc (1, sizeof (LoopClient));
	test_check_ptr (client);

	client->fd = -1;
	for (unsigned int i = 0; (i < LOOP_CONNECT_RETRIES) && (client->fd < 0); i++) {
		client->fd = loop_connect_once (server->port);

		if (client->fd < 0) loop_pause ();
	}

	test_check (client->fd > -1, "failed to connect to the test cerver");

	(void) sock_set_timeout (client->fd, LOOP_RECEIVE_TIMEOUT);

	return client;

}

static void loop_client_close (LoopClient *client) {

	(void) close (client->fd);

	free (client);

}

// the cerver gets a reset instead of the end of the connection
static void loop_client_reset (LoopClient *client) {

	struct linger reset = { 1, 0 };
	test_check_int_eq (setsockopt (client->fd, SOL_SOCKET, SO_LINGER, &reset, sizeof (struct linger)), 0, NULL);

	loop_client_close (client);

}

static void loop_client_send_bytes (const LoopClient *client, const char *bytes, const size_t length) {

	test_check (send (client->fd, bytes, length, MSG_NOSIGNAL) == (ssize_t) length, "send () failed");

}

static void loop_client_send (const LoopClient *client, const char *request) {

	loop_client_send_bytes (client, request, strlen (request));

}

// the bytes of the first response that the client has, 0 when it is not complete
static size_t loop_client_answer_size (const LoopClient *client, size_t *header_size) {

	size_t size = 0;

	const char *end = (const char *) memmem (
		client->buffer, client->length, loop_header_end, strlen (loop_header_end)
	);

	if (end) {
		*header_size = (size_t) (end - client->buffer) + strlen (loop_header_end);

		const char *length = (const char *) memmem (
			client->buffer, *header_size, loop_content_length, strlen (loop_content_length)
		);

		const size_t body_size = length ?
			(size_t) strtoul (length + strlen (loop_content_length), NULL, LOOP_STATUS_BASE) : 0;

		if (client->length >= (*header_size + body_size)) size = *header_size + body_size;
	}

	return size;

}

// takes the first response of the buffer of the client
static void loop_client_answer_take (
	LoopClient *client, const size_t size, const size_t header_size, LoopAnswer *answer
) {

	const size_t body_size = size - header_size;

	(void) memset (answer, 0, sizeof (LoopAnswer));

	test_check_int_eq (strncmp (client->buffer, loop_status_prefix, strlen (loop_status_prefix)), 0, NULL);

	answer->status = (unsigned int) strtoul (
		client->buffer + strlen (loop_status_prefix), NULL, LOOP_STATUS_BASE
	);

	answer->body_length = (body_size < (LOOP_BODY_SIZE - 1)) ? body_size : (LOOP_BODY_SIZE - 1);
	(void) memcpy (answer->body, client->buffer + header_size, answer->body_length);

	client->length -= size;
	(void) memmove (client->buffer, client->buffer + size, client->length);

}

// reads the next response of the connection: the ones of a pipeline can arrive together
// returns true with a complete response, false when the cerver closed the connection before one
static bool loop_client_answer (LoopClient *client, LoopAnswer *answer) {

	size_t header_size = 0;
	size_t size = loop_client_answer_size (client, &header_size);

	ssize_t received = 1;
	while (!size && (received > 0) && (client->length < LOOP_BUFFER_SIZE)) {
		received = recv (client->fd, client->buffer + client->length, LOOP_BUFFER_SIZE - client->length, 0);

		if (received > 0) {
			client->length += (size_t) received;

			size = loop_client_answer_size (client, &header_size);
		}

		else if (received < 0) {
			client->timed_out = (errno == EAGAIN) || (errno == EWOULDBLOCK);
		}
	}

	if (size) loop_client_answer_take (client, size, header_size, answer);

	return size != 0;

}

// the next response is a 200 whose body starts with a prefix
static void loop_client_expect_thread (LoopClient *client, const char *prefix) {

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));

	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);

	if (strncmp (answer.body, prefix, strlen (prefix))) {
		(void) fprintf (stderr, "the thread \"%s\" is not one of \"%s\"\n", answer.body, prefix);
	}

	test_check_int_eq (strncmp (answer.body, prefix, strlen (prefix)), 0, NULL);

}

static void loop_client_expect_status (LoopClient *client, const unsigned int status) {

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));

	test_check_unsigned_eq (answer.status, status, NULL);

}

// the cerver closed the connection, and it sent nothing else
static void loop_client_expect_end (LoopClient *client) {

	LoopAnswer answer;
	test_check_false (loop_client_answer (client, &answer));

	// a client that waited until its receive timeout did not see its connection end
	test_check_false (client->timed_out);

	test_check_true (client->length == 0);

}

// a request and its response, in the thread of the expected kind
static void loop_client_thread (LoopClient *client, const char *request, const char *prefix) {

	loop_client_send (client, request);

	loop_client_expect_thread (client, prefix);

}

// writes a pipeline: a request some times, and then the one that ends it
static size_t loop_pipeline_write (
	char *pipeline, const char *request, const unsigned int times, const char *last
) {

	size_t length = 0;

	for (unsigned int idx = 0; idx < times; idx++) {
		test_check_true ((length + strlen (request)) < LOOP_PIPELINE_SIZE);

		(void) memcpy (pipeline + length, request, strlen (request));
		length += strlen (request);
	}

	test_check_true ((length + strlen (last)) < LOOP_PIPELINE_SIZE);

	(void) memcpy (pipeline + length, last, strlen (last));
	length += strlen (last);

	return length;

}

#pragma endregion

#pragma region pool

// every cerver has the pool of its event loops: it has no threads, it is not one of the pools that are counted,
// and no pool can be created with its name
static void test_loop_pool (void) {

	Cerver *cerver = loop_cerver_create ();
	CerverHttp *http_cerver = cerver_http_get (cerver);

	const HttpPool *pool = cerver_http_pool_get_with_name (http_cerver, HTTP_POOL_LOOP_NAME);
	test_check_ptr (pool);
	test_check_str_eq (cerver_http_pool_get_name (pool), HTTP_POOL_LOOP_NAME, NULL);
	test_check_true (pool->in_loop);
	test_check_null_ptr (pool->thpool);
	test_check_unsigned_eq (pool->n_threads, 0, NULL);
	test_check_ptr_eq (http_cerver->loop_pool, pool);

	const HttpPool *default_pool = cerver_http_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME);
	test_check_ptr (default_pool);
	test_check_false (default_pool->in_loop);
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	test_check_null_ptr (cerver_http_pool_create (http_cerver, HTTP_POOL_LOOP_NAME, LOOP_THREADS));
	test_check_unsigned_eq (http_cerver->n_pools, 1, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the pool of the requests that chose none: the event loops until the cerver sets any of its pools,
// and the event loops again without a name
static void test_loop_handlers_pool (void) {

	Cerver *cerver = loop_cerver_create ();
	CerverHttp *http_cerver = cerver_http_get (cerver);

	HttpPool *default_pool = cerver_http_pool_get_with_name (http_cerver, HTTP_POOL_DEFAULT_NAME);

	test_check_ptr (http_cerver->loop_pool);
	test_check_ptr_eq (http_cerver->handlers_pool, http_cerver->loop_pool);

	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME), 0, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, default_pool);

	// a pool that the cerver does not have keeps the one that was set
	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, loop_missing_name), 1, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, default_pool);

	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_LOOP_NAME), 0, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, http_cerver->loop_pool);

	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME), 0, NULL);
	test_check_unsigned_eq (cerver_http_set_handlers_pool (http_cerver, NULL), 0, NULL);
	test_check_ptr_eq (http_cerver->handlers_pool, http_cerver->loop_pool);

	test_check_unsigned_eq (cerver_http_set_handlers_pool (NULL, HTTP_POOL_LOOP_NAME), 1, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region handlers

// a cerver that sets nothing runs its handlers in its event loops: in the loop that read the request,
// and the ones of a group that chose the thpool of the cerver in one of its threads
static void test_loop_cerver (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_thread (client, loop_thread_request, loop_events_thread);
	loop_client_thread (client, loop_pool_request, loop_pool_thread);
	loop_client_thread (client, loop_plain_request, loop_events_thread);
	loop_client_thread (client, loop_thread_close_request, loop_events_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// a cerver that sets its thpool for its handlers runs them there, and in its event loops only the ones of the
// group, the endpoint and the route that chose their pool
static void test_loop_chosen (void) {

	LoopServer *server = loop_server_start (false);

	LoopClient *client = loop_client_connect (server);

	loop_client_thread (client, loop_thread_request, loop_pool_thread);
	loop_client_thread (client, loop_explicit_request, loop_events_thread);
	loop_client_thread (client, loop_plain_request, loop_pool_thread);
	loop_client_thread (client, loop_endpoint_request, loop_events_thread);
	loop_client_thread (client, loop_old_request, loop_events_thread);
	loop_client_thread (client, loop_thread_close_request, loop_pool_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// the authentication of a request of the pool of the event loops runs in its loop too:
// one that passes is handled there, one that does not is answered and never handled
static void test_loop_authentication (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_authentication_thread_reset ();

	loop_client_thread (client, loop_private_request, loop_events_thread);
	loop_authentication_thread_expect (loop_events_thread);

	loop_authentication_thread_reset ();

	loop_client_send (client, loop_private_bad_request);
	loop_client_expect_status (client, HTTP_STATUS_UNAUTHORIZED);
	loop_authentication_thread_expect (loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a request with a body is authenticated when its headers are complete, before its body: in its loop too, the
// requests without a body above are authenticated by the job that handles them and never stop there
static void test_loop_authentication_body (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_authentication_thread_reset ();

	loop_client_thread (client, loop_upload_request, loop_events_thread);
	loop_authentication_thread_expect (loop_events_thread);

	loop_authentication_thread_reset ();

	loop_client_send (client, loop_upload_bad_request);
	loop_client_expect_status (client, HTTP_STATUS_UNAUTHORIZED);
	loop_authentication_thread_expect (loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a cerver that asks for an event loop for each processor, each one in its own: it has as many loops as
// processors this process can run in, and the thread of every loop that handles a request can run in only one
static void test_loop_per_core (void) {

	const unsigned int processors = loop_processors ();
	test_check_unsigned_gt (processors, 0);

	loop_per_core = true;

	LoopServer *server = loop_server_start (true);

	loop_per_core = false;

	// the kernel shares the connections among the loops
	for (unsigned int idx = 0; idx < (processors * LOOP_PER_CORE_CONNECTIONS); idx++) {
		LoopClient *client = loop_client_connect (server);

		loop_client_send (client, loop_processors_request);

		LoopAnswer answer;
		test_check_true (loop_client_answer (client, &answer));
		test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
		test_check_int_eq (strncmp (answer.body, loop_events_thread, strlen (loop_events_thread)), 0, NULL);

		const char *count = strchr (answer.body, ' ');
		test_check_ptr (count);
		test_check_unsigned_eq ((unsigned int) strtoul (count, NULL, LOOP_STATUS_BASE), 1, NULL);

		loop_client_close (client);
	}

	loop_server_shutdown (server);

	test_check_ptr (server->cerver->event_loops);
	test_check_unsigned_eq (server->cerver->event_loops->n_loops, processors, NULL);

	loop_server_end (server);

}

// a response that does not fit in the socket of its client: the loop that wrote it sends it in parts, whole
static void test_loop_big (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send (client, loop_big_request);

	// the headers, with the first bytes of the body behind them
	ssize_t received = recv (client->fd, client->buffer, LOOP_BIG_CHUNK, 0);
	test_check_true (received > 0);
	client->length = (size_t) received;

	const char *end = (const char *) memmem (
		client->buffer, client->length, loop_header_end, strlen (loop_header_end)
	);

	test_check_ptr (end);
	test_check_int_eq (strncmp (client->buffer, loop_status_prefix, strlen (loop_status_prefix)), 0, NULL);

	size_t offset = (size_t) (end - client->buffer) + strlen (loop_header_end);
	size_t position = 0;
	bool matches = true;

	while (received > 0) {
		for (; offset < client->length; offset++, position++) {
			matches = matches && (client->buffer[offset] == (char) ('a' + (position % LOOP_BIG_LETTERS)));
		}

		received = recv (client->fd, client->buffer, LOOP_BIG_CHUNK, 0);

		client->length = (received > 0) ? (size_t) received : 0;
		offset = 0;
	}

	test_check_true (position == LOOP_BIG_SIZE);
	test_check_true (matches);

	loop_client_close (client);

	loop_server_stop (server);

}

#pragma endregion

#pragma region pipeline

// the requests of a pipeline that the loop handles: every one is answered, and counted
static void loop_pipeline (const unsigned int requests, const u64 sends) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, requests - 1, loop_count_close_request)
	);

	for (unsigned int idx = 0; idx < requests; idx++) loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + requests, NULL);

	// each request ended once, also the ones whose response left with the ones behind them
	loop_server_wait (server, loop_counted_requests, requests);
	loop_server_wait (server, loop_observed_requests, requests);

	loop_server_stop_with_sends (server, sends);

}

// the responses of the requests of a pipeline that arrived together leave in one send
static void test_loop_pipeline (void) {

	loop_pipeline (LOOP_PIPELINE_REQUESTS, LOOP_PIPELINE_SENDS);

}

// a pipeline of more requests than the loop handles in a row: it sends the responses that it has, handles
// its other connections, and goes on in its next step
static void test_loop_pipeline_long (void) {

	loop_pipeline (LOOP_PIPELINE_LONG_REQUESTS, LOOP_PIPELINE_LONG_SENDS);

}

// a request of a pool of threads between two of the loop: the responses arrive in the order of their requests,
// and the one that the loop wrote first leaves before the job of the second one is given to its pool, it does
// not wait for a handler that takes long
static void test_loop_pipeline_mixed (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	size_t length = loop_pipeline_write (pipeline, loop_thread_request, 1, loop_pool_gate_request);
	(void) memcpy (pipeline + length, loop_thread_close_request, strlen (loop_thread_close_request));
	length += strlen (loop_thread_close_request);

	loop_client_send_bytes (client, pipeline, length);

	// the handler of the second request has not answered
	loop_client_expect_thread (client, loop_events_thread);

	loop_gate_wait_for (1);
	loop_gate_open ();

	loop_client_expect_status (client, HTTP_STATUS_OK);
	loop_client_expect_thread (client, loop_events_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_MIXED_SENDS);

}

// the responses that the loop joined in one part count in the bytes that their connection has to send: once they
// left, the thread of a pool that writes the response of the next request does not wait for the client to read
static void test_loop_pipeline_joined (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_JOINED_ANSWERS, loop_pool_close_request)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_JOINED_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_thread (client, loop_pool_thread);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_JOINED_SENDS);

}

// the batch of a loop starts again with the bytes of each read: the requests of a second pipeline of the same
// connection leave in one send too
static void test_loop_pipeline_again (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_AGAIN_REQUESTS - 1, loop_count_request)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_AGAIN_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (
			pipeline, loop_count_request, LOOP_PIPELINE_AGAIN_REQUESTS - 1, loop_count_close_request
		)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_AGAIN_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_AGAIN_SENDS);

}

// the short responses that cerver writes by itself in the loop are not a batch of the requests of its pool:
// each one leaves on its own, also in a cerver whose handlers run in its loops
static void test_loop_pipeline_short (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (
			pipeline, loop_missing_request, LOOP_PIPELINE_SHORT_REQUESTS - 1, loop_missing_close_request
		)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_SHORT_REQUESTS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_NOT_FOUND);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_SHORT_REQUESTS);

}

// the file of GET /loop/file, with the first letters of the big body
static void loop_file_create (void) {

	(void) strncpy (loop_file_path, loop_file_template, LOOP_FILE_PATH_SIZE - 1);

	const int fd = mkstemp (loop_file_path);
	test_check (fd > -1, "mkstemp () failed");

	test_check (write (fd, loop_big_body, LOOP_FILE_SIZE) == (ssize_t) LOOP_FILE_SIZE, "write () failed");

	(void) close (fd);

}

// a response with a file in a pipeline: the bytes of a part are sent before its file, so the response that the
// loop writes behind it is never joined to that part, it leaves after the file
static void test_loop_pipeline_file (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	loop_file_create ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline, loop_pipeline_write (pipeline, loop_file_request, 1, loop_count_close_request)
	);

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));
	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
	test_check_true (answer.body_length == LOOP_FILE_SIZE);
	test_check_int_eq (memcmp (answer.body, loop_big_body, LOOP_FILE_SIZE), 0, NULL);

	loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

	test_check_int_eq (unlink (loop_file_path), 0, NULL);

}

// a request of a pipeline that closes its connection: the ones behind it are not handled
static void test_loop_pipeline_closed (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	size_t length = loop_pipeline_write (pipeline, loop_count_request, 1, loop_count_close_request);
	(void) memcpy (pipeline + length, loop_count_request, strlen (loop_count_request));
	length += strlen (loop_count_request);

	loop_client_send_bytes (client, pipeline, length);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_CLOSED_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_CLOSED_ANSWERS, NULL
	);

	loop_server_stop (server);

}

// a pipeline that ends in the middle of a request: the responses of the complete ones do not wait for the
// rest of that request
static void test_loop_pipeline_partial (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_count_request, LOOP_PIPELINE_PARTIAL_ANSWERS, loop_count_start)
	);

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_PARTIAL_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_send (client, loop_count_end);

	loop_client_expect_status (client, HTTP_STATUS_OK);

	loop_client_expect_end (client);

	loop_client_close (client);

	loop_server_stop (server);

}

// a client that resets its connection before the responses of its pipeline are sent: the loop handled every
// request, their only send fails, and it is counted once
static void test_loop_pipeline_reset (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_gate_request, LOOP_PIPELINE_RESET_REQUESTS - 1, loop_gate_request)
	);

	// the loop has the whole pipeline and waits in the handler of its first request
	loop_gate_wait_for (1);

	loop_client_reset (client);

	// the reset reaches the socket of the cerver before its loop goes on
	loop_pause ();

	loop_gate_open ();

	loop_server_wait (server, loop_failed_responses, 1);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_RESET_REQUESTS, NULL
	);

	loop_server_stop_with_sends (server, LOOP_PIPELINE_SENDS);

}

// a cerver that stops while its loop handles the first request of a pipeline: that one is answered, the loop
// does not go on with the ones behind it and closes the connection, like one that it does not keep
static void test_loop_pipeline_stopping (void) {

	char pipeline[LOOP_PIPELINE_SIZE];

	const unsigned int handled = __atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST);

	loop_gate_reset ();

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_send_bytes (
		client, pipeline,
		loop_pipeline_write (pipeline, loop_gate_request, LOOP_PIPELINE_STOPPING_REQUESTS - 1, loop_gate_request)
	);

	// the loop has the whole pipeline and waits in the handler of its first request
	loop_gate_wait_for (1);

	// cerver_start () returns when its loops were asked to stop, while this one still waits in the handler
	loop_server_shutdown (server);

	loop_gate_open ();

	for (unsigned int idx = 0; idx < LOOP_PIPELINE_STOPPING_ANSWERS; idx++) {
		loop_client_expect_status (client, HTTP_STATUS_OK);
	}

	loop_client_expect_end (client);

	loop_client_close (client);

	test_check_unsigned_eq (
		__atomic_load_n (&loop_handled, __ATOMIC_SEQ_CST), handled + LOOP_PIPELINE_STOPPING_ANSWERS, NULL
	);

	loop_server_end (server);

}

#pragma endregion

#pragma region deferred

static void loop_client_expect_text (LoopClient *client, const char *text) {

	LoopAnswer answer;
	test_check_true (loop_client_answer (client, &answer));

	test_check_unsigned_eq (answer.status, HTTP_STATUS_OK, NULL);
	test_check_str_eq (answer.body, text, NULL);

}

// waits for the handler that deferred its response, and takes the context of its request
static CerverHttpContext *loop_deferred_take (const unsigned int count) {

	test_check_unsigned_eq (
		test_wait_counter (&loop_deferred_count, count, LOOP_WAIT_TIMEOUT * test_time_scale ()), count, NULL
	);

	// the handler counted itself right before it returned: a request is completed both before and after that
	loop_pause ();

	return __atomic_load_n (&loop_deferred_context, __ATOMIC_SEQ_CST);

}

static unsigned int loop_deferred_handlers (void) {

	return __atomic_load_n (&loop_deferred_count, __ATOMIC_SEQ_CST);

}

// writes the response of a deferred request and completes it, in the thread of the caller
static void loop_deferred_complete (CerverHttpContext *context) {

	test_check_unsigned_eq (
		cerver_http_response_send_text (context, loop_deferred_body, strlen (loop_deferred_body)), 0, NULL
	);

	test_check_unsigned_eq (cerver_http_context_complete (context), 0, NULL);

}

// a request whose handler defers its response, completed by the thread of the test
static void loop_deferred_exchange (LoopClient *client, const char *request) {

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	loop_deferred_complete (loop_deferred_take (handlers + 1));

	loop_client_expect_text (client, loop_deferred_body);

	test_check_unsigned_eq (__atomic_load_n (&loop_deferred_again, __ATOMIC_SEQ_CST), 1, NULL);

}

// a handler defers its response and returns, in its event loop and in a thread of a pool: neither waits.
// The thread of the test writes the response and completes the request: its client gets it, and its
// connection is kept for the requests that follow
static void test_loop_deferred (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_deferred_exchange (client, loop_deferred_request);
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_deferred_exchange (client, loop_pool_deferred_request);
	loop_client_thread (client, loop_pool_request, loop_pool_thread);

	loop_deferred_exchange (client, loop_deferred_request);

	loop_client_close (client);

	loop_server_stop (server);

}

// a request that its handler completes before it returns ends when the handler does, like any other
static void loop_deferred_early_exchange (LoopClient *client, const char *request) {

	__atomic_store_n (&loop_deferred_early_completed, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n (&loop_deferred_early_again, 0, __ATOMIC_SEQ_CST);

	const unsigned int handlers = __atomic_load_n (&loop_deferred_early_count, __ATOMIC_SEQ_CST);

	loop_client_send (client, request);

	loop_client_expect_text (client, loop_deferred_body);

	test_check_unsigned_eq (
		test_wait_counter (&loop_deferred_early_count, handlers + 1, LOOP_WAIT_TIMEOUT * test_time_scale ()),
		handlers + 1, NULL
	);

	test_check_unsigned_eq (__atomic_load_n (&loop_deferred_early_completed, __ATOMIC_SEQ_CST), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&loop_deferred_early_again, __ATOMIC_SEQ_CST), 1, NULL);

}

static void test_loop_deferred_early (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_deferred_early_exchange (client, loop_deferred_early_request);
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_deferred_early_exchange (client, loop_pool_deferred_early_request);
	loop_client_thread (client, loop_pool_request, loop_pool_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// the requests of a pipeline behind one whose response is deferred wait for it: their responses arrive in the
// order of their requests
static void loop_deferred_pipeline_exchange (
	LoopClient *client, const char *deferred_request, const char *request, const char *prefix
) {

	char pipeline[LOOP_PIPELINE_SIZE] = { 0 };
	const size_t length = loop_pipeline_write (pipeline, deferred_request, 1, request);

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send_bytes (client, pipeline, length);

	loop_deferred_complete (loop_deferred_take (handlers + 1));

	loop_client_expect_text (client, loop_deferred_body);
	loop_client_expect_thread (client, prefix);

}

static void test_loop_deferred_pipeline (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_deferred_pipeline_exchange (client, loop_deferred_request, loop_thread_request, loop_events_thread);
	loop_deferred_pipeline_exchange (client, loop_pool_deferred_request, loop_thread_request, loop_events_thread);
	loop_deferred_pipeline_exchange (client, loop_deferred_request, loop_pool_request, loop_pool_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a deferred request that is completed without a response is answered with 500, like the one of a handler
// that returns without one
static void loop_deferred_unanswered_exchange (LoopClient *client, const char *request) {

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	test_check_unsigned_eq (cerver_http_context_complete (loop_deferred_take (handlers + 1)), 0, NULL);

	loop_client_expect_status (client, HTTP_STATUS_INTERNAL_SERVER_ERROR);

}

static void test_loop_deferred_unanswered (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_deferred_unanswered_exchange (client, loop_deferred_request);
	loop_deferred_unanswered_exchange (client, loop_pool_deferred_request);

	loop_client_close (client);

	loop_server_stop (server);

}

// a thread that does not run the handler of a request can not defer its response: the handler still answers it
static void loop_deferred_foreign_exchange (LoopClient *client, const char *request) {

	loop_gate_reset ();

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	test_check_unsigned_eq (cerver_http_context_defer (loop_deferred_take (handlers + 1)), 1, NULL);

	loop_gate_open ();

	loop_client_expect_text (client, loop_deferred_body);

}

// only the handler of a request defers its response, in the thread that runs it: an after middleware and any
// other thread are refused, their request ends like any other and its connection takes the next one
static void test_loop_deferred_refused (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	__atomic_store_n (&loop_deferred_after, 0, __ATOMIC_SEQ_CST);

	const unsigned int middlewares = __atomic_load_n (&loop_deferred_after_count, __ATOMIC_SEQ_CST);

	loop_client_thread (client, loop_deferred_after_request, loop_events_thread);

	test_check_unsigned_eq (
		test_wait_counter (&loop_deferred_after_count, middlewares + 1, LOOP_WAIT_TIMEOUT * test_time_scale ()),
		middlewares + 1, NULL
	);

	test_check_unsigned_eq (__atomic_load_n (&loop_deferred_after, __ATOMIC_SEQ_CST), 1, NULL);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_deferred_foreign_exchange (client, loop_held_request);
	loop_deferred_foreign_exchange (client, loop_pool_held_request);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a handler asks another endpoint of its own cerver for its response, with the HTTP client of the cerver: the
// one of an event loop defers its response and its loop sends the request (it can not wait for it), the one of a
// pool of threads waits. The loops keep the connections of the client for the requests that follow
static void test_loop_client (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	__atomic_store_n (&loop_client_refused, 0, __ATOMIC_SEQ_CST);

	loop_client_thread (client, loop_client_request, loop_events_thread);

	test_check_unsigned_eq (__atomic_load_n (&loop_client_refused, __ATOMIC_SEQ_CST), 1, NULL);

	loop_client_thread (client, loop_pool_client_request, loop_events_thread);

	for (unsigned int idx = 0; idx < LOOP_CLIENT_REQUESTS; idx++) {
		loop_client_thread (client, loop_client_request, loop_events_thread);
	}

	test_check_true (cerver_http_client_get_reused (loop_http_client) > 0);
	test_check_true (cerver_http_client_get_connections (loop_http_client) <= LOOP_CLIENT_REQUESTS);

	// the connection of the test still takes its own requests
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

// a request with a file whose body arrives after its head, in two pieces: a thread of the pool parses each read
// and writes the file, and the request is handled where its endpoint runs
static void loop_files_exchange (LoopClient *client, const char *head, const char *prefix) {

	loop_upload_thread_reset ();

	loop_client_send (client, head);
	loop_pause ();

	loop_client_send_bytes (client, loop_files_body, LOOP_FILES_BODY_SPLIT);
	loop_pause ();

	loop_client_send (client, loop_files_body + LOOP_FILES_BODY_SPLIT);

	loop_client_expect_thread (client, prefix);

	loop_upload_thread_expect (loop_pool_thread);

}

// the same request in one write, with another request behind it: the bytes that follow its body are of the
// next request, that its loop handles after it
static void loop_files_pipeline (LoopClient *client, const char *head, const char *prefix) {

	char pipeline[LOOP_PIPELINE_SIZE] = { 0 };
	const int length = snprintf (pipeline, LOOP_PIPELINE_SIZE, "%s%s%s", head, loop_files_body, loop_thread_request);
	test_check_true ((length > 0) && (length < LOOP_PIPELINE_SIZE));

	loop_upload_thread_reset ();

	loop_client_send_bytes (client, pipeline, (size_t) length);

	loop_client_expect_thread (client, prefix);
	loop_client_expect_thread (client, loop_events_thread);

	loop_upload_thread_expect (loop_pool_thread);

}

// the files that an endpoint counted and their sizes: only the parts that are files count, the bytes of a value
// are not the ones of a file
static void loop_file_stats_expect (const CerverHttpEndpoint *endpoint, const size_t files) {

	const size_t size = strlen (loop_files_content);

	HttpRouteFileStats snapshot;
	cerver_http_route_file_stats_snapshot (endpoint->file_stats, &snapshot);

	test_check_unsigned_eq (snapshot.n_uploaded_files, files, NULL);
	test_check_unsigned_eq (snapshot.min_file_size, size, NULL);
	test_check_unsigned_eq (snapshot.max_file_size, size, NULL);
	test_check_true (test_double_equal (snapshot.mean_file_size, (double) size));

}

// an event loop does not write the disk: the body of a request that is saved there (a multipart one) is parsed
// by the threads of the pool, one job for each read of its loop, also when its endpoint runs in the loop
static void test_loop_files (void) {

	(void) mkdir (LOOP_UPLOADS_PATH, LOOP_UPLOADS_MODE);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_files_exchange (client, loop_files_head, loop_events_thread);
	loop_files_exchange (client, loop_pool_files_head, loop_pool_thread);

	loop_files_pipeline (client, loop_files_head, loop_events_thread);
	loop_files_pipeline (client, loop_pool_files_head, loop_pool_thread);

	// a form with a value and a file: only its file is one
	loop_client_send (client, loop_files_form_head);
	loop_client_send (client, loop_parts_body);
	loop_client_expect_thread (client, loop_events_thread);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	// the requests before the last one ended: the endpoint of the loops counted a file for each of its three
	loop_file_stats_expect (loop_files_endpoint, LOOP_FILES_REQUESTS);

	loop_client_close (client);

	loop_server_stop (server);

	// the files of each request were deleted when it ended: the directory is empty
	test_check_int_eq (rmdir (LOOP_UPLOADS_PATH), 0, NULL);

}

// a request whose parts are taken by handlers of its endpoint: they get each part, in order and in a thread of
// the pool, and the request is handled where its endpoint runs
static void loop_parts_exchange (LoopClient *client, const char *head, const char *prefix) {

	test_check_unsigned_eq (strlen (loop_parts_body), LOOP_PARTS_BODY_LENGTH, NULL);

	loop_parts_reset ();

	loop_client_send (client, head);
	loop_pause ();

	loop_client_send_bytes (client, loop_parts_body, LOOP_FILES_BODY_SPLIT);
	loop_pause ();

	loop_client_send (client, loop_parts_body + LOOP_FILES_BODY_SPLIT);

	loop_client_expect_thread (client, prefix);

	loop_parts_expect (LOOP_PARTS_COUNT, loop_parts_expected);

}

// the parts of a multipart request can be taken by methods of the application while its body arrives, instead of
// cerver: it saves no file. A handler that rejects a part ends the request with the status that it returns
static void test_loop_parts (void) {

	(void) mkdir (LOOP_UPLOADS_PATH, LOOP_UPLOADS_MODE);

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_parts_exchange (client, loop_parts_head, loop_events_thread);
	loop_parts_exchange (client, loop_pool_parts_head, loop_pool_thread);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	// the bytes of the value that the handlers took are not the ones of a file
	loop_file_stats_expect (loop_parts_endpoint, 1);

	// the rejected request: its status, and its connection is closed
	loop_parts_reset ();

	loop_client_send (client, loop_parts_forbidden_request);
	loop_client_expect_status (client, HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE);
	loop_client_expect_end (client);

	test_check_unsigned_eq (__atomic_load_n (&loop_parts_started, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&loop_parts_ended, __ATOMIC_SEQ_CST), 0, NULL);

	loop_client_close (client);

	loop_server_stop (server);

	// no file was saved
	test_check_int_eq (rmdir (LOOP_UPLOADS_PATH), 0, NULL);

}

// waits for the authentication handler that deferred its verdict, and takes the context of its request
static CerverHttpContext *loop_verdict_take (const unsigned int count) {

	test_check_unsigned_eq (
		test_wait_counter (&loop_verdict_count, count, LOOP_WAIT_TIMEOUT * test_time_scale ()), count, NULL
	);

	return __atomic_load_n (&loop_verdict_context, __ATOMIC_SEQ_CST);

}

// a request whose authentication deferred its verdict waits, without a thread, until the test gives it: one that
// is accepted is handled where its endpoint runs, one that is rejected is answered with a 401
static void loop_verdict_exchange (
	LoopClient *client, const char *request, const bool authenticated, const char *prefix
) {

	const unsigned int handlers = __atomic_load_n (&loop_verdict_count, __ATOMIC_SEQ_CST);

	loop_client_send (client, request);

	CerverHttpContext *context = loop_verdict_take (handlers + 1);

	// a request that waits for its verdict was not handled: it is not completed like a deferred response
	test_check_unsigned_eq (cerver_http_context_complete (context), 1, NULL);

	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, authenticated), 0, NULL);

	if (authenticated) loop_client_expect_thread (client, prefix);
	else loop_client_expect_status (client, HTTP_STATUS_UNAUTHORIZED);

}

// a request that is rejected is answered with the response that was written for it before its verdict
static void loop_verdict_answered_exchange (LoopClient *client, const char *request) {

	const unsigned int handlers = __atomic_load_n (&loop_verdict_count, __ATOMIC_SEQ_CST);

	loop_client_send (client, request);

	CerverHttpContext *context = loop_verdict_take (handlers + 1);

	test_check_unsigned_eq (
		cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST), 0, NULL
	);

	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, false), 0, NULL);

	loop_client_expect_status (client, HTTP_STATUS_BAD_REQUEST);

}

// a rejected request, in its own connection
static void loop_verdict_rejected (const LoopServer *server, const char *request) {

	LoopClient *client = loop_client_connect (server);

	loop_verdict_exchange (client, request, false, NULL);

	loop_client_close (client);

}

// an authentication handler defers its verdict and returns, in its event loop and in a thread of a pool: the
// request waits without a thread and the test gives the verdict from its own. Before its body, without one and
// after it: the request that is accepted goes on where its endpoint runs, and its connection is kept
static void test_loop_verdict (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	__atomic_store_n (&loop_verdict_refused, 0, __ATOMIC_SEQ_CST);

	loop_verdict_exchange (client, loop_verdict_request, true, loop_events_thread);

	// the handler of the endpoint could not defer the verdict of its request
	test_check_unsigned_eq (__atomic_load_n (&loop_verdict_refused, __ATOMIC_SEQ_CST), 1, NULL);

	loop_verdict_exchange (client, loop_pool_verdict_request, true, loop_pool_thread);

	loop_verdict_exchange (client, loop_verdict_body_request, true, loop_events_thread);
	loop_verdict_exchange (client, loop_pool_verdict_body_request, true, loop_pool_thread);

	loop_verdict_exchange (client, loop_late_request, true, loop_events_thread);
	loop_verdict_exchange (client, loop_pool_late_request, true, loop_pool_thread);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_verdict_rejected (server, loop_verdict_request);
	loop_verdict_rejected (server, loop_pool_verdict_request);
	loop_verdict_rejected (server, loop_verdict_body_request);
	loop_verdict_rejected (server, loop_pool_verdict_body_request);
	loop_verdict_rejected (server, loop_late_request);
	loop_verdict_rejected (server, loop_pool_late_request);

	client = loop_client_connect (server);
	loop_verdict_answered_exchange (client, loop_verdict_request);
	loop_client_close (client);

	client = loop_client_connect (server);
	loop_verdict_answered_exchange (client, loop_pool_verdict_request);
	loop_client_close (client);

	loop_server_stop (server);

}

static void loop_static_file_create (void) {

	(void) mkdir (LOOP_STATIC_PATH, LOOP_UPLOADS_MODE);

	const int fd = open (LOOP_STATIC_FILE, O_WRONLY | O_CREAT | O_TRUNC, LOOP_STATIC_FILE_MODE);
	test_check_true (fd > -1);

	const size_t length = strlen (loop_static_content);
	test_check (write (fd, loop_static_content, length) == (ssize_t) length, "write () failed");

	(void) close (fd);

}

// the authentication of a file of a static path defers its verdict too: the file is served, by the pool of
// threads, only when the verdict accepts its request
static void test_loop_verdict_static (void) {

	loop_static_file_create ();

	loop_static_private = true;

	LoopServer *server = loop_server_start (true);

	loop_static_private = false;

	LoopClient *client = loop_client_connect (server);

	const unsigned int handlers = __atomic_load_n (&loop_verdict_count, __ATOMIC_SEQ_CST);

	loop_client_send (client, loop_static_request);

	CerverHttpContext *context = loop_verdict_take (handlers + 1);

	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, true), 0, NULL);

	loop_client_expect_text (client, loop_static_content);

	// its connection takes the next request
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_verdict_rejected (server, loop_static_request);

	loop_server_stop (server);

	test_check_int_eq (unlink (LOOP_STATIC_FILE), 0, NULL);
	test_check_int_eq (rmdir (LOOP_STATIC_PATH), 0, NULL);

}

// a request with a status, in its own connection
static void loop_status_exchange (const LoopServer *server, const char *request, const unsigned int status) {

	LoopClient *client = loop_client_connect (server);

	loop_client_send (client, request);
	loop_client_expect_status (client, status);

	loop_client_close (client);

}

// an authentication handler that gives its verdict before it returns: its request goes on like any other
static void test_loop_verdict_early (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_client_thread (client, loop_decided_request, loop_events_thread);
	loop_client_thread (client, loop_pool_decided_request, loop_pool_thread);
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_status_exchange (server, loop_decided_bad_request, HTTP_STATUS_UNAUTHORIZED);
	loop_status_exchange (server, loop_pool_decided_bad_request, HTTP_STATUS_UNAUTHORIZED);

	loop_server_stop (server);

}

// an authentication handler that asks another service with the HTTP client of its cerver: no thread waits for
// that response, that decides. The request that it accepts is handled where its endpoint runs
static void test_loop_verdict_request (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	for (unsigned int idx = 0; idx < LOOP_CLIENT_REQUESTS; idx++) {
		loop_client_thread (client, loop_checked_request, loop_events_thread);
		loop_client_thread (client, loop_pool_checked_request, loop_pool_thread);
	}

	loop_client_close (client);

	loop_status_exchange (server, loop_checked_bad_request, HTTP_STATUS_UNAUTHORIZED);
	loop_status_exchange (server, loop_pool_checked_bad_request, HTTP_STATUS_UNAUTHORIZED);

	loop_server_stop (server);

}

// a verdict that is not given before the request timeout loses its client: its connection is shut down, and the
// request is rejected when its verdict arrives, whatever it is: its handler never runs
static void loop_verdict_timeout_exchange (const LoopServer *server, const char *request) {

	LoopClient *client = loop_client_connect (server);

	const unsigned int handlers = __atomic_load_n (&loop_verdict_count, __ATOMIC_SEQ_CST);

	loop_client_send (client, request);

	CerverHttpContext *context = loop_verdict_take (handlers + 1);

	loop_client_expect_end (client);

	test_check_true (cerver_http_context_client_is_gone (context));

	test_check_unsigned_eq (cerver_http_context_authentication_complete (context, true), 0, NULL);

	loop_client_close (client);

	// the request ended: its connection left its loop
	loop_server_wait (server, loop_server_connections, 0);

}

static void test_loop_verdict_timeout (void) {

	loop_request_timeout = LOOP_DEFERRED_REQUEST_TIMEOUT;

	LoopServer *server = loop_server_start (true);

	__atomic_store_n (&loop_verdict_refused, 0, __ATOMIC_SEQ_CST);

	loop_verdict_timeout_exchange (server, loop_verdict_request);
	loop_verdict_timeout_exchange (server, loop_pool_verdict_request);
	loop_verdict_timeout_exchange (server, loop_verdict_body_request);
	loop_verdict_timeout_exchange (server, loop_pool_verdict_body_request);

	// the handler of the endpoint of the GET, that writes this, never ran
	test_check_unsigned_eq (__atomic_load_n (&loop_verdict_refused, __ATOMIC_SEQ_CST), 0, NULL);

	// the cerver still serves
	LoopClient *client = loop_client_connect (server);
	loop_client_thread (client, loop_thread_request, loop_events_thread);
	loop_client_close (client);

	loop_server_stop (server);

	loop_request_timeout = 0;

}

// a deferred response that is not completed before the request timeout loses its client: its connection is
// shut down, what is written of the response fails, and the request is still completed
static void loop_deferred_timeout_exchange (const LoopServer *server, const char *request) {

	LoopClient *client = loop_client_connect (server);

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	CerverHttpContext *context = loop_deferred_take (handlers + 1);

	loop_client_expect_end (client);

	test_check_true (cerver_http_context_client_is_gone (context));

	test_check_unsigned_eq (
		cerver_http_response_send_text (context, loop_deferred_body, strlen (loop_deferred_body)), 1, NULL
	);

	test_check_unsigned_eq (cerver_http_context_complete (context), 0, NULL);

	loop_client_close (client);

}

static void test_loop_deferred_timeout (void) {

	loop_request_timeout = LOOP_DEFERRED_REQUEST_TIMEOUT;

	LoopServer *server = loop_server_start (true);

	loop_deferred_timeout_exchange (server, loop_deferred_request);
	loop_deferred_timeout_exchange (server, loop_pool_deferred_request);

	// the cerver still serves
	LoopClient *client = loop_client_connect (server);
	loop_client_thread (client, loop_thread_request, loop_events_thread);
	loop_client_close (client);

	loop_server_stop (server);

	loop_request_timeout = 0;

}

// completes a deferred request a while after the cerver was asked to stop
static void *loop_deferred_late_thread (void *context_ptr) {

	for (unsigned int idx = 0; idx < LOOP_DEFERRED_LATE_PAUSES; idx++) loop_pause ();

	loop_deferred_complete ((CerverHttpContext *) context_ptr);

	return NULL;

}

// a cerver that stops waits for the requests whose response is deferred, that no thread of its pools has:
// the one that is completed after the cerver was asked to stop still reaches its client
static void loop_deferred_stop_exchange (const char *request) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	CerverHttpContext *context = loop_deferred_take (handlers + 1);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, loop_deferred_late_thread, context), 0, NULL);

	loop_server_stop (server);

	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	loop_client_expect_text (client, loop_deferred_body);

	loop_client_close (client);

}

static void test_loop_deferred_stop (void) {

	loop_deferred_stop_exchange (loop_deferred_request);
	loop_deferred_stop_exchange (loop_pool_deferred_request);

}

static bool loop_client_has_text (const LoopClient *client, const char *text) {

	return memmem (client->buffer, client->length, text, strlen (text)) != NULL;

}

// reads until what the client received has the text
// returns false when its connection ended, or its receive timeout, before the text arrived
static bool loop_client_wait_text (LoopClient *client, const char *text) {

	ssize_t received = 1;
	while (!loop_client_has_text (client, text) && (received > 0) && (client->length < LOOP_BUFFER_SIZE)) {
		received = recv (client->fd, client->buffer + client->length, LOOP_BUFFER_SIZE - client->length, 0);

		if (received > 0) client->length += (size_t) received;
	}

	return loop_client_has_text (client, text);

}

static void loop_deferred_stream_piece (
	LoopClient *client, CerverHttpContext *context, const char *piece, const char *chunk
) {

	test_check_unsigned_eq (cerver_http_response_stream_write (context, piece, strlen (piece)), 0, NULL);

	test_check_true (loop_client_wait_text (client, chunk));

}

// a handler begins a stream, defers its response and returns: the start of the stream reaches its client before
// any piece is written, each piece when another thread writes it, and its end when the request is completed
// (cerver ends the stream that was left open). Its connection is kept
static void loop_deferred_stream_exchange (LoopClient *client, const char *request) {

	const unsigned int handlers = loop_deferred_handlers ();

	loop_client_send (client, request);

	CerverHttpContext *context = loop_deferred_take (handlers + 1);

	test_check_true (loop_client_wait_text (client, loop_header_end));
	test_check_true (loop_client_has_text (client, loop_stream_chunked));
	test_check_false (loop_client_has_text (client, loop_stream_first));

	loop_deferred_stream_piece (client, context, loop_stream_first, loop_stream_first_chunk);
	test_check_false (loop_client_has_text (client, loop_stream_second));

	loop_deferred_stream_piece (client, context, loop_stream_second, loop_stream_second_chunk);
	test_check_false (loop_client_has_text (client, loop_stream_last_chunk));

	test_check_unsigned_eq (cerver_http_context_complete (context), 0, NULL);

	test_check_true (loop_client_wait_text (client, loop_stream_last_chunk));

	// the whole response was read
	client->length = 0;

}

// the whole stream of the test arrives, with its pieces in order
static void loop_drained_exchange (LoopClient *client, const char *request) {

	loop_client_send (client, request);

	test_check_true (loop_client_wait_text (client, loop_stream_last_chunk));
	test_check_true (loop_client_has_text (client, loop_stream_chunked));
	test_check_true (loop_client_has_text (client, loop_drained_body));

	// the whole response was read
	client->length = 0;

}

// a handler of an event loop writes a stream piece by piece: its loop tells it when what it wrote was sent,
// once for each piece that follows the first one and once more for its end. A handler of a pool of threads is
// not told: it writes its pieces by itself. Their connection is kept
static void test_loop_stream_drained (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	__atomic_store_n (&loop_drained_calls, 0, __ATOMIC_SEQ_CST);

	loop_drained_exchange (client, loop_drained_request);

	test_check_unsigned_eq (__atomic_load_n (&loop_drained_refused, __ATOMIC_SEQ_CST), 0, NULL);
	test_check_unsigned_eq (__atomic_load_n (&loop_drained_calls, __ATOMIC_SEQ_CST), LOOP_DRAINED_PIECES, NULL);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_drained_exchange (client, loop_pool_drained_request);

	test_check_unsigned_eq (__atomic_load_n (&loop_drained_refused, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&loop_drained_calls, __ATOMIC_SEQ_CST), LOOP_DRAINED_PIECES, NULL);

	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

static void test_loop_deferred_stream (void) {

	LoopServer *server = loop_server_start (true);

	LoopClient *client = loop_client_connect (server);

	loop_deferred_stream_exchange (client, loop_stream_request);
	loop_client_thread (client, loop_thread_request, loop_events_thread);

	loop_deferred_stream_exchange (client, loop_pool_stream_request);
	loop_client_thread (client, loop_pool_request, loop_pool_thread);

	loop_client_close (client);

	loop_server_stop (server);

}

#pragma endregion

static void loop_big_body_create (void) {

	loop_big_body = (char *) malloc (LOOP_BIG_SIZE);
	test_check_ptr (loop_big_body);

	for (size_t position = 0; position < LOOP_BIG_SIZE; position++) {
		loop_big_body[position] = (char) ('a' + (position % LOOP_BIG_LETTERS));
	}

}

void http_tests_loop (void) {

	(void) printf ("Testing HTTP loop handlers...\n");

	loop_big_body_create ();

	// a hang fails the suite instead of blocking it forever
	(void) alarm (LOOP_ALARM * test_time_scale ());

	test_run (test_loop_pool);
	test_run (test_loop_handlers_pool);

	test_run (test_loop_cerver);
	test_run (test_loop_chosen);
	test_run (test_loop_authentication);
	test_run (test_loop_authentication_body);
	test_run (test_loop_per_core);
	test_run (test_loop_big);

	test_run (test_loop_pipeline);
	test_run (test_loop_pipeline_long);
	test_run (test_loop_pipeline_mixed);
	test_run (test_loop_pipeline_joined);
	test_run (test_loop_pipeline_again);
	test_run (test_loop_pipeline_short);
	test_run (test_loop_pipeline_file);
	test_run (test_loop_pipeline_closed);
	test_run (test_loop_pipeline_partial);
	test_run (test_loop_pipeline_reset);
	test_run (test_loop_pipeline_stopping);

	test_run (test_loop_deferred);
	test_run (test_loop_deferred_early);
	test_run (test_loop_deferred_pipeline);
	test_run (test_loop_deferred_unanswered);
	test_run (test_loop_deferred_refused);
	test_run (test_loop_client);
	test_run (test_loop_verdict);
	test_run (test_loop_verdict_early);
	test_run (test_loop_verdict_request);
	test_run (test_loop_verdict_timeout);
	test_run (test_loop_verdict_static);
	test_run (test_loop_files);
	test_run (test_loop_parts);
	test_run (test_loop_stream_drained);
	test_run (test_loop_deferred_timeout);
	test_run (test_loop_deferred_stop);
	test_run (test_loop_deferred_stream);

	(void) alarm (0);

	free (loop_big_body);
	loop_big_body = NULL;

	(void) printf ("Done!\n");

}
