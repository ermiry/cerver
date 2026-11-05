#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <pthread.h>

// a load generator without dependencies: every request uses its own connection,
// like nginx does with cerver, and the latency is measured from connect () to the close
// usage: load <port> <concurrency> <requests> <GET path | POST path body>
// prints the requests per second, the latency percentiles and the status codes

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

static int load_connect (const unsigned short port) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, LOAD_ADDRESS, &address.sin_addr);

	if ((fd > -1) && connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

// returns the status code of the response, 0 on error
static int load_request (const LoadContext *context) {

	int status = 0;

	int fd = load_connect (context->port);
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

	int idx = -1;
	while ((idx = load_next (context)) > -1) {
		const double start = load_now ();
		const int status = load_request (context);
		context->latencies[idx] = load_now () - start;

		load_count (context, status);
	}

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

static size_t load_request_create (char *request, int argc, char **argv) {

	int length = 0;

	if (!strcmp (argv[LOAD_ARGUMENT_METHOD], "POST") && (argc >= LOAD_ARGUMENTS_POST)) {
		const char *body = argv[LOAD_ARGUMENT_BODY];
		length = snprintf (
			request, LOAD_REQUEST_SIZE,
			"POST %s HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
			argv[LOAD_ARGUMENT_PATH], strlen (body), body
		);
	}

	else {
		length = snprintf (
			request, LOAD_REQUEST_SIZE,
			"GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n",
			argv[LOAD_ARGUMENT_PATH]
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
	context.request = request;
	context.request_len = load_request_create (request, argc, argv);
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
		" - 200 %u - 503 %u - other %u - failed %u\n",
		context.requests, concurrency, (double) context.requests / elapsed,
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_50),
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_95),
		load_percentile (context.latencies, context.requests, LOAD_PERCENTILE_99),
		context.ok, context.unavailable, context.other, context.failed
	);

	(void) pthread_mutex_destroy (&context.mutex);
	free (context.latencies);

	return (context.failed || context.other) ? EXIT_FAILURE : EXIT_SUCCESS;

}
