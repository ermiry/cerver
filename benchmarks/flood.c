#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

// a load generator by events, without dependencies: a few threads keep many connections, each thread with its
// own epoll, so the generator is not what limits a server that answers in microseconds (benchmarks/load.c has a
// thread that waits for each connection). Every connection is kept, and it can send its requests in batches
// without waiting for each response (pipelining), the way the plaintext tests of the public benchmarks do
// usage: flood <port> <connections> <requests> <GET path | POST path body>
// FLOOD_THREADS in the environment is the threads of the generator (half of the processors without it),
// FLOOD_PIPELINE the requests that a connection sends together (1 without it),
// and LOAD_AUTHORIZATION the value of the Authorization header of every request
// prints the requests per second, the latency percentiles of a request (from the send of its batch to its
// response), the status codes and the connections that were opened

#define FLOOD_THREADS_VARIABLE			"FLOOD_THREADS"
#define FLOOD_PIPELINE_VARIABLE			"FLOOD_PIPELINE"
#define FLOOD_AUTHORIZATION_VARIABLE	"LOAD_AUTHORIZATION"

#define FLOOD_HEADER_END				"\r\n\r\n"
#define FLOOD_HEADER_END_SIZE			4
#define FLOOD_CONTENT_LENGTH			"content-length:"
#define FLOOD_CONNECTION_CLOSE			"connection: close"
#define FLOOD_AUTHORIZATION				"Authorization:"

#define FLOOD_HEADERS_SIZE				(1 << 11)
#define FLOOD_DECIMAL_BASE				10

#define FLOOD_ADDRESS					"127.0.0.1"
#define FLOOD_REQUEST_SIZE				(1 << 12)
#define FLOOD_RESPONSE_SIZE				(1 << 14)
#define FLOOD_MAX_THREADS				(1 << 8)
#define FLOOD_MAX_PIPELINE				(1 << 8)
#define FLOOD_EVENTS					(1 << 8)

// a thread waits for its connections in pieces, to see that the others ended
#define FLOOD_WAIT_MILLISECONDS			(1 << 8)

#define FLOOD_ARGUMENT_PORT				1
#define FLOOD_ARGUMENT_CONNECTIONS		2
#define FLOOD_ARGUMENT_REQUESTS			3
#define FLOOD_ARGUMENT_METHOD			4
#define FLOOD_ARGUMENT_PATH				5
#define FLOOD_ARGUMENT_BODY				6
#define FLOOD_ARGUMENTS_GET				6
#define FLOOD_ARGUMENTS_POST			7

#define FLOOD_STATUS_OFFSET				9
#define FLOOD_STATUS_OK					200
#define FLOOD_STATUS_UNAVAILABLE		503

#define FLOOD_PERCENTILE_50				50
#define FLOOD_PERCENTILE_95				95
#define FLOOD_PERCENTILE_99				99
#define FLOOD_PERCENTILE_BASE			100

#define FLOOD_SECONDS_TO_MILLISECONDS	1000.0
#define FLOOD_NANOSECONDS_PER_SECOND	1e9

// what all the threads share, its counters are updated with atomic operations
typedef struct FloodContext {

	unsigned short port;

	// the requests of a full batch, one after the other
	const char *batch;
	size_t request_size;
	unsigned int pipeline;

	unsigned int requests;
	// the requests that no connection has taken
	unsigned int remaining;
	// the responses that were read
	unsigned int done;

	double *latencies;

	unsigned int ok;
	unsigned int unavailable;
	unsigned int other;
	unsigned int failed;

	unsigned int connections;

} FloodContext;

// a connection of a thread
typedef struct FloodConnection {

	int fd;

	// the responses of its batch that it has not read, and when the batch was sent
	unsigned int in_flight;
	double sent_at;

	// the bytes of its batch that the socket has not taken
	size_t send_offset;
	size_t send_size;
	bool waits_writable;

	// the bytes that it read and that are not a whole response yet
	char response[FLOOD_RESPONSE_SIZE];
	size_t response_size;
	// the bytes of the body of a response that are still to be read, and dropped
	size_t skip;
	// the response that is being read asks to close the connection
	bool closes;

	// it has no more requests to send
	bool ended;

} FloodConnection;

typedef struct FloodThread {

	FloodContext *context;

	int epoll_fd;

	FloodConnection *connections;
	unsigned int n_connections;
	unsigned int n_ended;

} FloodThread;

static double flood_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / FLOOD_NANOSECONDS_PER_SECOND);

}

// takes the requests of the next batch of a connection
// returns how many, 0 when all were taken
static unsigned int flood_take (FloodContext *context) {

	unsigned int taken = 0;

	unsigned int remaining = __atomic_load_n (&context->remaining, __ATOMIC_RELAXED);
	while (remaining && !taken) {
		const unsigned int wanted = (remaining < context->pipeline) ? remaining : context->pipeline;

		if (__atomic_compare_exchange_n (
			&context->remaining, &remaining, remaining - wanted, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
		)) {
			taken = wanted;
		}
	}

	return taken;

}

static void flood_count (FloodContext *context, const int status, const double latency) {

	const unsigned int position = __atomic_fetch_add (&context->done, 1, __ATOMIC_RELAXED);
	if (position < context->requests) context->latencies[position] = latency;

	if (status == FLOOD_STATUS_OK) (void) __atomic_add_fetch (&context->ok, 1, __ATOMIC_RELAXED);
	else if (status == FLOOD_STATUS_UNAVAILABLE) (void) __atomic_add_fetch (&context->unavailable, 1, __ATOMIC_RELAXED);
	else if (status) (void) __atomic_add_fetch (&context->other, 1, __ATOMIC_RELAXED);
	else (void) __atomic_add_fetch (&context->failed, 1, __ATOMIC_RELAXED);

}

static int flood_connect (FloodContext *context) {

	int fd = socket (AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (context->port);
	(void) inet_pton (AF_INET, FLOOD_ADDRESS, &address.sin_addr);

	if ((fd > -1) && connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	if (fd > -1) {
		const int enabled = 1;
		(void) setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (enabled));

		(void) __atomic_add_fetch (&context->connections, 1, __ATOMIC_RELAXED);
	}

	return fd;

}

static void flood_watch (
	const FloodThread *thread, FloodConnection *connection, const int operation, const bool writable
) {

	struct epoll_event event;
	(void) memset (&event, 0, sizeof (struct epoll_event));
	event.events = EPOLLIN | (writable ? EPOLLOUT : 0);
	event.data.ptr = connection;

	(void) epoll_ctl (thread->epoll_fd, operation, connection->fd, &event);

	connection->waits_writable = writable;

}

// a connection without a socket has no more requests
static void flood_connection_end (FloodThread *thread, FloodConnection *connection) {

	if (connection->fd > -1) {
		(void) close (connection->fd);
		connection->fd = -1;
	}

	if (!connection->ended) {
		connection->ended = true;
		thread->n_ended += 1;
	}

}

// the requests of a batch whose connection failed are counted as failed
static void flood_connection_failed (FloodThread *thread, FloodConnection *connection) {

	for (unsigned int i = 0; i < connection->in_flight; i++) {
		flood_count (thread->context, 0, flood_now () - connection->sent_at);
	}

	connection->in_flight = 0;

}

// returns true when the connection has a socket
static bool flood_connection_open (FloodThread *thread, FloodConnection *connection) {

	if (connection->fd > -1) (void) close (connection->fd);

	connection->fd = flood_connect (thread->context);
	connection->response_size = 0;
	connection->skip = 0;
	connection->closes = false;
	connection->send_offset = 0;
	connection->send_size = 0;

	if (connection->fd > -1) flood_watch (thread, connection, EPOLL_CTL_ADD, false);

	return connection->fd > -1;

}

// gives the socket the bytes of the batch that it has not taken; a socket that is full is watched until it
// can be written
static void flood_connection_send (FloodThread *thread, FloodConnection *connection) {

	const FloodContext *context = thread->context;

	bool full = false;
	bool failed = false;

	while ((connection->send_offset < connection->send_size) && !full && !failed) {
		const ssize_t sent = send (
			connection->fd, context->batch + connection->send_offset,
			connection->send_size - connection->send_offset, MSG_NOSIGNAL | MSG_DONTWAIT
		);

		if (sent > 0) connection->send_offset += (size_t) sent;
		else if ((sent < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) full = true;
		else if ((sent < 0) && (errno != EINTR)) failed = true;
	}

	if (failed) {
		flood_connection_failed (thread, connection);
		flood_connection_end (thread, connection);
	}

	else if (full != connection->waits_writable) {
		flood_watch (thread, connection, EPOLL_CTL_MOD, full);
	}

}

// the next batch of a connection: the requests that it takes are sent together
static void flood_connection_fire (FloodThread *thread, FloodConnection *connection) {

	FloodContext *context = thread->context;

	const unsigned int taken = flood_take (context);
	if (!taken) {
		flood_connection_end (thread, connection);
	}

	else if ((connection->fd > -1) || flood_connection_open (thread, connection)) {
		connection->in_flight = taken;
		connection->sent_at = flood_now ();

		connection->send_offset = 0;
		connection->send_size = (size_t) taken * context->request_size;

		flood_connection_send (thread, connection);
	}

	else {
		connection->in_flight = taken;
		connection->sent_at = flood_now ();

		flood_connection_failed (thread, connection);
		flood_connection_end (thread, connection);
	}

}

// the value of a header of the head of a response, whatever the case of its name
// returns where its value starts, NULL when the head does not have it
static const char *flood_header_find (const char *head, const size_t head_size, const char *name) {

	const char *value = NULL;

	const size_t name_size = strlen (name);

	for (size_t i = 0; !value && ((i + name_size) <= head_size); i++) {
		if (!strncasecmp (head + i, name, name_size)) value = head + i + name_size;
	}

	return value;

}

// where the head of the response that starts the bytes of the connection ends
// returns its size with its empty line, 0 while it is not complete
static size_t flood_response_head (const FloodConnection *connection) {

	size_t head_size = 0;

	for (size_t i = 0; !head_size && ((i + FLOOD_HEADER_END_SIZE) <= connection->response_size); i++) {
		if (!memcmp (connection->response + i, FLOOD_HEADER_END, FLOOD_HEADER_END_SIZE)) {
			head_size = i + FLOOD_HEADER_END_SIZE;
		}
	}

	return head_size;

}

// a whole response was read: it is counted, and the one that ends its batch starts the next one,
// in a new connection when the server closes this one
static void flood_response_done (FloodThread *thread, FloodConnection *connection, const int status) {

	flood_count (thread->context, status, flood_now () - connection->sent_at);

	if (connection->in_flight) connection->in_flight -= 1;

	if (connection->closes) {
		// the requests of the batch behind a response that closes the connection are never answered
		flood_connection_failed (thread, connection);

		(void) close (connection->fd);
		connection->fd = -1;
		connection->response_size = 0;
	}

	if (!connection->in_flight) flood_connection_fire (thread, connection);

}

// takes the responses that are whole in the bytes of the connection; the body of one that does not fit
// is dropped as it arrives
// returns false when the bytes are not the responses of the requests
static bool flood_responses_parse (FloodThread *thread, FloodConnection *connection) {

	bool valid = true;
	bool more = true;

	while (valid && more && connection->response_size && (connection->fd > -1) && !connection->skip) {
		const size_t head_size = flood_response_head (connection);

		if (!head_size) {
			more = false;
			valid = connection->response_size < FLOOD_RESPONSE_SIZE;
		}

		else if (head_size <= FLOOD_STATUS_OFFSET) {
			valid = false;
		}

		else {
			const int status = atoi (connection->response + FLOOD_STATUS_OFFSET);

			const char *length = flood_header_find (connection->response, head_size, FLOOD_CONTENT_LENGTH);
			const size_t body_size = length ? (size_t) strtoul (length, NULL, FLOOD_DECIMAL_BASE) : 0;

			connection->closes = flood_header_find (connection->response, head_size, FLOOD_CONNECTION_CLOSE) != NULL;

			const size_t total = head_size + body_size;
			if (connection->response_size >= total) {
				connection->response_size -= total;
				if (connection->response_size) {
					(void) memmove (connection->response, connection->response + total, connection->response_size);
				}

				flood_response_done (thread, connection, status);
			}

			else if (body_size > (FLOOD_RESPONSE_SIZE - head_size)) {
				// a body that does not fit: its head is counted now and the rest is dropped as it arrives
				connection->skip = total - connection->response_size;
				connection->response_size = 0;

				flood_response_done (thread, connection, status);
			}

			else {
				more = false;
			}
		}
	}

	return valid;

}

static void flood_connection_read (FloodThread *thread, FloodConnection *connection) {

	bool failed = false;
	bool empty = false;

	while (!failed && !empty && (connection->fd > -1)) {
		const size_t room = FLOOD_RESPONSE_SIZE - connection->response_size;

		const ssize_t received = recv (
			connection->fd, connection->response + connection->response_size, room, MSG_DONTWAIT
		);

		if (received > 0) {
			size_t bytes = (size_t) received;

			// the body of a response that did not fit is dropped
			if (connection->skip) {
				const size_t dropped = (bytes < connection->skip) ? bytes : connection->skip;

				connection->skip -= dropped;
				bytes -= dropped;

				if (bytes) (void) memmove (connection->response, connection->response + dropped, bytes);
			}

			connection->response_size += bytes;

			failed = !flood_responses_parse (thread, connection);
		}

		else if ((received < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
			empty = true;
		}

		else if ((received < 0) && (errno == EINTR)) {
			empty = false;
		}

		else {
			failed = true;
		}
	}

	// the server closed the connection, or it did not answer the requests: what is left of the batch failed,
	// and the next one uses a new connection
	if (failed && (connection->fd > -1)) {
		flood_connection_failed (thread, connection);

		(void) close (connection->fd);
		connection->fd = -1;

		flood_connection_fire (thread, connection);
	}

}

static void flood_connection_ready (FloodThread *thread, FloodConnection *connection, const unsigned int events) {

	if ((events & EPOLLOUT) && (connection->fd > -1)) flood_connection_send (thread, connection);

	if ((events & (EPOLLIN | EPOLLERR | EPOLLHUP)) && (connection->fd > -1)) {
		flood_connection_read (thread, connection);
	}

}

static void *flood_worker (void *thread_ptr) {

	FloodThread *thread = (FloodThread *) thread_ptr;

	struct epoll_event events[FLOOD_EVENTS];

	for (unsigned int i = 0; i < thread->n_connections; i++) {
		flood_connection_fire (thread, &thread->connections[i]);
	}

	while (thread->n_ended < thread->n_connections) {
		const int n_events = epoll_wait (thread->epoll_fd, events, FLOOD_EVENTS, FLOOD_WAIT_MILLISECONDS);

		for (int i = 0; i < n_events; i++) {
			flood_connection_ready (thread, (FloodConnection *) events[i].data.ptr, events[i].events);
		}
	}

	return NULL;

}

static int flood_compare (const void *a, const void *b) {

	const double first = *(const double *) a;
	const double second = *(const double *) b;

	return (first > second) - (first < second);

}

static double flood_percentile (const double *sorted, const unsigned int count, const unsigned int percentile) {

	unsigned int idx = (count * percentile) / FLOOD_PERCENTILE_BASE;
	if (idx >= count) idx = count - 1;

	return sorted[idx] * FLOOD_SECONDS_TO_MILLISECONDS;

}

static size_t flood_request_create (char *request, int argc, char **argv) {

	const char *authorization = getenv (FLOOD_AUTHORIZATION_VARIABLE);

	char headers[FLOOD_HEADERS_SIZE];
	(void) snprintf (
		headers, FLOOD_HEADERS_SIZE, "%s%s%s",
		authorization ? FLOOD_AUTHORIZATION " " : "",
		authorization ? authorization : "",
		authorization ? "\r\n" : ""
	);

	int length = 0;

	if (!strcmp (argv[FLOOD_ARGUMENT_METHOD], "POST") && (argc >= FLOOD_ARGUMENTS_POST)) {
		const char *body = argv[FLOOD_ARGUMENT_BODY];
		length = snprintf (
			request, FLOOD_REQUEST_SIZE,
			"POST %s HTTP/1.1\r\nHost: localhost\r\n%sContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
			argv[FLOOD_ARGUMENT_PATH], headers, strlen (body), body
		);
	}

	else {
		length = snprintf (
			request, FLOOD_REQUEST_SIZE,
			"GET %s HTTP/1.1\r\nHost: localhost\r\n%s\r\n",
			argv[FLOOD_ARGUMENT_PATH], headers
		);
	}

	return (size_t) length;

}

// a value of the environment that is a count, with its default and its limit
static unsigned int flood_setting (const char *variable, const unsigned int value, const unsigned int max) {

	const char *text = getenv (variable);

	unsigned int setting = (text && text[0]) ? (unsigned int) strtoul (text, NULL, FLOOD_DECIMAL_BASE) : value;

	if (!setting) setting = 1;
	if (setting > max) setting = max;

	return setting;

}

// half of the processors are of the generator, the others of the server that it measures
static unsigned int flood_default_threads (void) {

	const long processors = sysconf (_SC_NPROCESSORS_ONLN);

	return (processors > 1) ? (unsigned int) (processors / 2) : 1;

}

// the connections are shared among the threads, the first ones take the ones that are left over
static unsigned int flood_threads_start (
	FloodContext *context, FloodThread *threads, pthread_t *ids,
	const unsigned int n_threads, const unsigned int n_connections
) {

	unsigned int started = 0;

	for (unsigned int i = 0; i < n_threads; i++) {
		FloodThread *thread = &threads[i];

		thread->context = context;
		thread->n_connections = (n_connections / n_threads) + ((i < (n_connections % n_threads)) ? 1 : 0);
		thread->connections = (FloodConnection *) calloc (thread->n_connections, sizeof (FloodConnection));
		thread->epoll_fd = epoll_create1 (EPOLL_CLOEXEC);

		for (unsigned int c = 0; thread->connections && (c < thread->n_connections); c++) {
			thread->connections[c].fd = -1;
		}

		if (thread->connections && (thread->epoll_fd > -1) && !pthread_create (&ids[i], NULL, flood_worker, thread)) {
			started += 1;
		}
	}

	return started;

}

int main (int argc, char **argv) {

	if (argc < FLOOD_ARGUMENTS_GET) {
		(void) fprintf (stderr, "usage: flood <port> <connections> <requests> <GET path | POST path body>\n");
		return EXIT_FAILURE;
	}

	char request[FLOOD_REQUEST_SIZE];

	FloodContext context;
	(void) memset (&context, 0, sizeof (FloodContext));
	context.port = (unsigned short) atoi (argv[FLOOD_ARGUMENT_PORT]);
	context.requests = (unsigned int) atoi (argv[FLOOD_ARGUMENT_REQUESTS]);
	context.remaining = context.requests;
	context.request_size = flood_request_create (request, argc, argv);
	context.pipeline = flood_setting (FLOOD_PIPELINE_VARIABLE, 1, FLOOD_MAX_PIPELINE);
	context.latencies = (double *) calloc (context.requests ? context.requests : 1, sizeof (double));

	// the requests of a whole batch, one after the other
	char *batch = (char *) malloc (context.request_size * context.pipeline);
	for (unsigned int i = 0; batch && (i < context.pipeline); i++) {
		(void) memcpy (batch + ((size_t) i * context.request_size), request, context.request_size);
	}

	context.batch = batch;

	unsigned int n_connections = (unsigned int) atoi (argv[FLOOD_ARGUMENT_CONNECTIONS]);
	if (!n_connections) n_connections = 1;

	unsigned int n_threads = flood_setting (FLOOD_THREADS_VARIABLE, flood_default_threads (), FLOOD_MAX_THREADS);
	if (n_threads > n_connections) n_threads = n_connections;

	FloodThread threads[FLOOD_MAX_THREADS];
	pthread_t ids[FLOOD_MAX_THREADS];
	(void) memset (threads, 0, sizeof (threads));

	if (!batch || !context.latencies) return EXIT_FAILURE;

	const double start = flood_now ();
	const unsigned int started = flood_threads_start (&context, threads, ids, n_threads, n_connections);
	for (unsigned int i = 0; i < started; i++) (void) pthread_join (ids[i], NULL);
	const double elapsed = flood_now () - start;

	const unsigned int done = (context.done < context.requests) ? context.done : context.requests;

	qsort (context.latencies, done, sizeof (double), flood_compare);

	(void) printf (
		"requests %u - concurrency %u - %.0f req/s - p50 %.3f ms - p95 %.3f ms - p99 %.3f ms"
		" - 200 %u - 503 %u - other %u - failed %u - connections %u - threads %u - pipeline %u\n",
		context.requests, n_connections, (double) done / elapsed,
		done ? flood_percentile (context.latencies, done, FLOOD_PERCENTILE_50) : 0.0,
		done ? flood_percentile (context.latencies, done, FLOOD_PERCENTILE_95) : 0.0,
		done ? flood_percentile (context.latencies, done, FLOOD_PERCENTILE_99) : 0.0,
		context.ok, context.unavailable, context.other, context.failed,
		context.connections, started, context.pipeline
	);

	free (batch);
	free (context.latencies);

	return (context.failed || context.other || (started < n_threads) || (done < context.requests)) ?
		EXIT_FAILURE : EXIT_SUCCESS;

}
