#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

// a load generator without dependencies: every request uses its own connection,
// like a proxy that does not keep its connections, and the latency is measured from connect () to the close
// with LOAD_KEEP_ALIVE in the environment each thread keeps its connection for its requests,
// like a proxy that keeps them: the latency is the one of each request, and a connection
// that the server closes (its response says it) is opened again
// with LOAD_AUTHORIZATION in the environment every request has that value in its Authorization header
// usage: load <port> <concurrency> <requests> <GET path | POST path body>
// prints the requests per second, the latency percentiles, the status codes and the connections that were opened

#define LOAD_KEEP_ALIVE_VARIABLE		"LOAD_KEEP_ALIVE"
#define LOAD_AUTHORIZATION_VARIABLE		"LOAD_AUTHORIZATION"

#define LOAD_HEADER_END					"\r\n\r\n"
#define LOAD_CONTENT_LENGTH				"Content-Length:"
#define LOAD_CONNECTION_CLOSE			"Connection: close"
#define LOAD_AUTHORIZATION				"Authorization:"

// the headers of a request after its host, with the longest authorization that is sent
#define LOAD_HEADERS_SIZE				(1 << 11)
#define LOAD_DECIMAL_BASE				10

// a request through a connection that the server closed while it was idle is sent again in a new one
#define LOAD_ATTEMPTS					2

#define LOAD_ADDRESS					"127.0.0.1"
#define LOAD_RESPONSE_SIZE				(1 << 14)
#define LOAD_REQUEST_SIZE				(1 << 12)
#define LOAD_MAX_THREADS				(1 << 10)

#define LOAD_ARGUMENT_PORT				1
#define LOAD_ARGUMENT_CONCURRENCY		2
#define LOAD_ARGUMENT_REQUESTS			3
#define LOAD_ARGUMENT_METHOD			4
#define LOAD_ARGUMENT_PATH				5
#define LOAD_ARGUMENT_BODY				6
#define LOAD_ARGUMENTS_GET				6
#define LOAD_ARGUMENTS_POST				7

#define LOAD_STATUS_OFFSET				9
#define LOAD_STATUS_SIZE				3
#define LOAD_STATUS_OK					200
#define LOAD_STATUS_UNAVAILABLE			503

#define LOAD_PERCENTILE_50				50
#define LOAD_PERCENTILE_95				95
#define LOAD_PERCENTILE_99				99
#define LOAD_PERCENTILE_BASE			100

#define LOAD_SECONDS_TO_MILLISECONDS	1000.0
#define LOAD_NANOSECONDS_PER_SECOND		1e9

typedef struct LoadContext {

	unsigned short port;
	const char *request;
	size_t request_len;

	unsigned int requests;
	unsigned int next;

	double *latencies;

	unsigned int ok;
	unsigned int unavailable;
	unsigned int other;
	unsigned int failed;

	// each thread keeps its connection for its requests
	bool keep_alive;

	// the connections that were opened, updated with atomic operations
	unsigned int connections;

	pthread_mutex_t mutex;

} LoadContext;

static double load_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / LOAD_NANOSECONDS_PER_SECOND);

}

// returns the index of the next request to send, or -1 when all were sent
static int load_next (LoadContext *context) {

	(void) pthread_mutex_lock (&context->mutex);

	int idx = (context->next < context->requests) ? (int) context->next++ : -1;

	(void) pthread_mutex_unlock (&context->mutex);

	return idx;

}

static int load_connect (LoadContext *context) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (context->port);
	(void) inet_pton (AF_INET, LOAD_ADDRESS, &address.sin_addr);

	if ((fd > -1) && connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	if (fd > -1) (void) __atomic_add_fetch (&context->connections, 1, __ATOMIC_RELAXED);

	return fd;

}

// returns the status code of the response, 0 on error
static int load_request (LoadContext *context) {

	int status = 0;

	int fd = load_connect (context);
	if (fd > -1) {
		if (send (fd, context->request, context->request_len, MSG_NOSIGNAL) == (ssize_t) context->request_len) {
			char response[LOAD_RESPONSE_SIZE];
			size_t total = 0;
			ssize_t received = 0;
			while ((received = recv (fd, response + total, LOAD_RESPONSE_SIZE - 1 - total, 0)) > 0) {
				total += (size_t) received;
				if (total >= (LOAD_RESPONSE_SIZE - 1)) total = LOAD_STATUS_OFFSET + LOAD_STATUS_SIZE;
			}

			response[total] = '\0';
			if (total > (LOAD_STATUS_OFFSET + LOAD_STATUS_SIZE)) status = atoi (response + LOAD_STATUS_OFFSET);
		}

		(void) close (fd);
	}

	return status;

}

// the bytes of the response that its header announces: the header and the body of its Content-Length,
// 0 while the header is not complete
static size_t load_response_size (const char *response) {

	size_t size = 0;

	const char *header_end = strstr (response, LOAD_HEADER_END);
	if (header_end) {
		size = (size_t) (header_end - response) + strlen (LOAD_HEADER_END);

		const char *length = strstr (response, LOAD_CONTENT_LENGTH);
		if (length && (length < header_end)) {
			size += (size_t) strtoul (length + strlen (LOAD_CONTENT_LENGTH), NULL, LOAD_DECIMAL_BASE);
		}
	}

	return size;

}

// reads one response of a connection that is kept, up to the end that its header announces
// returns the bytes that were read, 0 when the server closed the connection before a complete response
static size_t load_response_read (const int fd, char *response) {

	size_t total = 0;
	size_t expected = 0;
	bool reading = true;

	while (reading) {
		const ssize_t received = recv (fd, response + total, LOAD_RESPONSE_SIZE - 1 - total, 0);
		if (received > 0) {
			total += (size_t) received;
			response[total] = '\0';

			if (!expected) expected = load_response_size (response);

			reading = (!expected || (total < expected)) && (total < (LOAD_RESPONSE_SIZE - 1));
		}

		else {
			total = 0;
			reading = false;
		}
	}

	return total;

}

// sends the request through the connection of the thread and reads its response
// returns the status code of the response, 0 when the connection failed (it was closed)
static int load_request_through (const LoadContext *context, int *fd) {

	int status = 0;

	char response[LOAD_RESPONSE_SIZE];
	response[0] = '\0';

	const bool sent = (
		send (*fd, context->request, context->request_len, MSG_NOSIGNAL) == (ssize_t) context->request_len
	);

	if (sent && (load_response_read (*fd, response) > (LOAD_STATUS_OFFSET + LOAD_STATUS_SIZE))) {
		status = atoi (response + LOAD_STATUS_OFFSET);
	}

	// the server closes the connection after a response that says it, or it already did
	if (!status || strstr (response, LOAD_CONNECTION_CLOSE)) {
		(void) close (*fd);
		*fd = -1;
	}

	return status;

}

// a request of a thread that keeps its connection: it is opened when the thread has none
// returns the status code of the response, 0 on error
static int load_request_keep (LoadContext *context, int *fd) {

	int status = 0;

	for (unsigned int attempt = 0; !status && (attempt < LOAD_ATTEMPTS); attempt++) {
		if (*fd < 0) *fd = load_connect (context);

		if (*fd > -1) status = load_request_through (context, fd);
	}

	return status;

}

static void load_count (LoadContext *context, const int status) {

	(void) pthread_mutex_lock (&context->mutex);

	if (status == LOAD_STATUS_OK) context->ok += 1;
	else if (status == LOAD_STATUS_UNAVAILABLE) context->unavailable += 1;
	else if (status) context->other += 1;
	else context->failed += 1;

	(void) pthread_mutex_unlock (&context->mutex);

}

static void *load_worker (void *context_ptr) {

	LoadContext *context = (LoadContext *) context_ptr;

	// the connection that the thread keeps
	int fd = -1;

	int idx = -1;
	while ((idx = load_next (context)) > -1) {
		const double start = load_now ();
		const int status = context->keep_alive ? load_request_keep (context, &fd) : load_request (context);
		context->latencies[idx] = load_now () - start;

		load_count (context, status);
	}

	if (fd > -1) (void) close (fd);

	return NULL;

}

static int load_compare (const void *a, const void *b) {

	const double first = *(const double *) a;
	const double second = *(const double *) b;

	return (first > second) - (first < second);

}

static double load_percentile (const double *sorted, const unsigned int count, const unsigned int percentile) {

	unsigned int idx = (count * percentile) / LOAD_PERCENTILE_BASE;
	if (idx >= count) idx = count - 1;

	return sorted[idx] * LOAD_SECONDS_TO_MILLISECONDS;

}

// the headers that every request has after its host:
// a request that uses its own connection asks the server to close it, like a proxy that does not keep them,
// and the environment can give the value of its authorization
static void load_request_headers (char *headers, const bool keep_alive) {

	const char *authorization = getenv (LOAD_AUTHORIZATION_VARIABLE);

	(void) snprintf (
		headers, LOAD_HEADERS_SIZE, "%s%s%s%s",
		keep_alive ? "" : LOAD_CONNECTION_CLOSE "\r\n",
		authorization ? LOAD_AUTHORIZATION " " : "",
		authorization ? authorization : "",
		authorization ? "\r\n" : ""
	);

}

static size_t load_request_create (char *request, int argc, char **argv, const bool keep_alive) {

	char headers[LOAD_HEADERS_SIZE];
	load_request_headers (headers, keep_alive);

	int length = 0;

	if (!strcmp (argv[LOAD_ARGUMENT_METHOD], "POST") && (argc >= LOAD_ARGUMENTS_POST)) {
		const char *body = argv[LOAD_ARGUMENT_BODY];
		length = snprintf (
			request, LOAD_REQUEST_SIZE,
			"POST %s HTTP/1.1\r\nHost: localhost\r\n%sContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
			argv[LOAD_ARGUMENT_PATH], headers, strlen (body), body
		);
	}

	else {
		length = snprintf (
			request, LOAD_REQUEST_SIZE,
			"GET %s HTTP/1.1\r\nHost: localhost\r\n%s\r\n",
			argv[LOAD_ARGUMENT_PATH], headers
		);
	}

	return (size_t) length;

}

int main (int argc, char **argv) {

	if (argc < LOAD_ARGUMENTS_GET) {
		(void) fprintf (stderr, "usage: load <port> <concurrency> <requests> <GET path | POST path body>\n");
		return EXIT_FAILURE;
	}

	char request[LOAD_REQUEST_SIZE];

	LoadContext context;
	(void) memset (&context, 0, sizeof (LoadContext));
	context.port = (unsigned short) atoi (argv[LOAD_ARGUMENT_PORT]);
	context.requests = (unsigned int) atoi (argv[LOAD_ARGUMENT_REQUESTS]);
	context.keep_alive = (getenv (LOAD_KEEP_ALIVE_VARIABLE) != NULL);
	context.request = request;
	context.request_len = load_request_create (request, argc, argv, context.keep_alive);
	context.latencies = (double *) calloc (context.requests, sizeof (double));
	(void) pthread_mutex_init (&context.mutex, NULL);

	unsigned int concurrency = (unsigned int) atoi (argv[LOAD_ARGUMENT_CONCURRENCY]);
	if (concurrency > LOAD_MAX_THREADS) concurrency = LOAD_MAX_THREADS;

	pthread_t threads[LOAD_MAX_THREADS];

	const double start = load_now ();
	for (unsigned int i = 0; i < concurrency; i++) (void) pthread_create (&threads[i], NULL, load_worker, &context);
	for (unsigned int i = 0; i < concurrency; i++) (void) pthread_join (threads[i], NULL);
	const double elapsed = load_now () - start;

	qsort (context.latencies, context.requests, sizeof (double), load_compare);

	(void) printf (
		"requests %u - concurrency %u - %.0f req/s - p50 %.3f ms - p95 %.3f ms - p99 %.3f ms"
		" - 200 %u - 503 %u - other %u - failed %u - connections %u\n",
		context.requests, concurrency, (double) context.requests / elapsed,
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_50),
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_95),
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_99),
		context.ok, context.unavailable, context.other, context.failed,
		__atomic_load_n (&context.connections, __ATOMIC_RELAXED)
	);

	(void) pthread_mutex_destroy (&context.mutex);
	free (context.latencies);

	return (context.failed || context.other) ? EXIT_FAILURE : EXIT_SUCCESS;

}
