#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <pthread.h>

// a WebSocket load generator without dependencies: each connection upgrades once
// and sends its messages one after the other, waiting for the echo of each one,
// the latency is measured from the send of a message to the end of its echo
// usage: messages <port> <connections> <messages per connection> <size> [path]
// prints the messages per second, the latency percentiles and the failures

#define MESSAGES_ADDRESS				"127.0.0.1"
#define MESSAGES_DEFAULT_PATH			"/ws/echo"
#define MESSAGES_MAX_CONNECTIONS		(1 << 10)

// the largest message that has a header of 16 bits
#define MESSAGES_MAX_SIZE				65535

#define MESSAGES_RESPONSE_SIZE			(1 << 12)
#define MESSAGES_REQUEST_SIZE			(1 << 10)

#define MESSAGES_ARGUMENT_PORT			1
#define MESSAGES_ARGUMENT_CONNECTIONS	2
#define MESSAGES_ARGUMENT_MESSAGES		3
#define MESSAGES_ARGUMENT_SIZE			4
#define MESSAGES_ARGUMENT_PATH			5
#define MESSAGES_ARGUMENTS				5

#define MESSAGES_UPGRADE_FORMAT										\
	"GET %s HTTP/1.1\r\n"											\
	"Host: localhost\r\n"											\
	"Upgrade: websocket\r\n"										\
	"Connection: Upgrade\r\n"										\
	"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"				\
	"Sec-WebSocket-Version: 13\r\n"									\
	"\r\n"

#define MESSAGES_SWITCHING				"HTTP/1.1 101"
#define MESSAGES_HEADERS_END			"\r\n\r\n"

// the frames of a binary message with its mask (client) and without it (server)
#define MESSAGES_FIN_BINARY				0x82
#define MESSAGES_FIN_CLOSE				0x88
#define MESSAGES_MASK_BIT				0x80
#define MESSAGES_SMALL_HEADER			2
#define MESSAGES_MEDIUM_HEADER			4
#define MESSAGES_LENGTH_16				126
#define MESSAGES_SMALL_LENGTH			125
#define MESSAGES_MASK_SIZE				4
#define MESSAGES_MAX_HEADER				8
#define MESSAGES_BITS_PER_BYTE			8
#define MESSAGES_BYTE_BITS				0xFF

#define MESSAGES_PERCENTILE_50			50
#define MESSAGES_PERCENTILE_95			95
#define MESSAGES_PERCENTILE_99			99
#define MESSAGES_PERCENTILE_BASE		100

#define MESSAGES_SECONDS_TO_MILLISECONDS	1000.0
#define MESSAGES_NANOSECONDS_PER_SECOND		1e9

static const unsigned char messages_mask[MESSAGES_MASK_SIZE] = { 0x12, 0x34, 0x56, 0x78 };

typedef struct MessagesContext {

	unsigned short port;
	const char *path;

	unsigned int messages;
	size_t size;

	// the latencies of every message, each connection writes its own part
	double *latencies;

	unsigned int failed;

	pthread_mutex_t mutex;

} MessagesContext;

typedef struct MessagesConnection {

	MessagesContext *context;
	unsigned int index;

} MessagesConnection;

static double messages_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / MESSAGES_NANOSECONDS_PER_SECOND);

}

static int messages_connect (const unsigned short port) {

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = htons (port);
	(void) inet_pton (AF_INET, MESSAGES_ADDRESS, &address.sin_addr);

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd >= 0) {
		if (connect (fd, (const struct sockaddr *) &address, sizeof (struct sockaddr_in))) {
			(void) close (fd);
			fd = -1;
		}

		else {
			const int enabled = 1;
			(void) setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof (int));
		}
	}

	return fd;

}

static bool messages_send_all (const int fd, const void *data, const size_t size) {

	size_t sent = 0;
	while (sent < size) {
		const ssize_t written = send (fd, (const unsigned char *) data + sent, size - sent, MSG_NOSIGNAL);
		if (written <= 0) {
			if ((written < 0) && (errno == EINTR)) continue;
			break;
		}

		sent += (size_t) written;
	}

	return sent == size;

}

static bool messages_receive_all (const int fd, void *data, const size_t size) {

	size_t received = 0;
	while (received < size) {
		const ssize_t read = recv (fd, (unsigned char *) data + received, size - received, 0);
		if (read <= 0) {
			if ((read < 0) && (errno == EINTR)) continue;
			break;
		}

		received += (size_t) read;
	}

	return received == size;

}

// sends the upgrade and reads its response one byte at a time, until its empty line
static bool messages_upgrade (const int fd, const char *path) {

	char request[MESSAGES_REQUEST_SIZE];
	const int length = snprintf (request, MESSAGES_REQUEST_SIZE, MESSAGES_UPGRADE_FORMAT, path);

	bool upgraded = (length > 0) && messages_send_all (fd, request, (size_t) length);

	char response[MESSAGES_RESPONSE_SIZE] = { 0 };
	size_t size = 0;
	while (upgraded && (size < (MESSAGES_RESPONSE_SIZE - 1)) && !strstr (response, MESSAGES_HEADERS_END)) {
		upgraded = messages_receive_all (fd, response + size, 1);
		size += 1;
	}

	return upgraded && !strncmp (response, MESSAGES_SWITCHING, strlen (MESSAGES_SWITCHING));

}

// the masked frame of a message of size bytes
static size_t messages_frame (unsigned char *frame, const size_t size) {

	size_t header = 0;
	frame[header++] = MESSAGES_FIN_BINARY;

	if (size <= MESSAGES_SMALL_LENGTH) {
		frame[header++] = (unsigned char) (MESSAGES_MASK_BIT | size);
	}

	else {
		frame[header++] = MESSAGES_MASK_BIT | MESSAGES_LENGTH_16;
		frame[header++] = (unsigned char) ((size >> MESSAGES_BITS_PER_BYTE) & MESSAGES_BYTE_BITS);
		frame[header++] = (unsigned char) (size & MESSAGES_BYTE_BITS);
	}

	(void) memcpy (frame + header, messages_mask, MESSAGES_MASK_SIZE);
	header += MESSAGES_MASK_SIZE;

	// the payload is all zeros, so its masked bytes are the mask itself
	for (size_t i = 0; i < size; i++) frame[header + i] = messages_mask[i % MESSAGES_MASK_SIZE];

	return header + size;

}

// reads the echo of the server, a frame without a mask
static bool messages_echo (const int fd, unsigned char *payload, const size_t size) {

	const size_t header = (size <= MESSAGES_SMALL_LENGTH) ? MESSAGES_SMALL_HEADER : MESSAGES_MEDIUM_HEADER;

	unsigned char bytes[MESSAGES_MAX_HEADER];
	return messages_receive_all (fd, bytes, header)
		&& (bytes[0] == MESSAGES_FIN_BINARY)
		&& messages_receive_all (fd, payload, size);

}

static void messages_failed (MessagesContext *context) {

	(void) pthread_mutex_lock (&context->mutex);
	context->failed += 1;
	(void) pthread_mutex_unlock (&context->mutex);

}

static void *messages_thread (void *connection_ptr) {

	const MessagesConnection *connection = (const MessagesConnection *) connection_ptr;
	MessagesContext *context = connection->context;

	unsigned char *frame = (unsigned char *) malloc (context->size + MESSAGES_MAX_HEADER);
	unsigned char *payload = (unsigned char *) malloc (context->size ? context->size : 1);
	const size_t frame_size = frame ? messages_frame (frame, context->size) : 0;

	const int fd = messages_connect (context->port);
	bool connected = (fd >= 0) && frame && payload && messages_upgrade (fd, context->path);

	double *latencies = context->latencies + ((size_t) connection->index * context->messages);
	for (unsigned int i = 0; i < context->messages; i++) {
		const double start = messages_now ();

		if (connected && messages_send_all (fd, frame, frame_size) && messages_echo (fd, payload, context->size)) {
			latencies[i] = messages_now () - start;
		}

		else {
			latencies[i] = 0;
			connected = false;
			messages_failed (context);
		}
	}

	// the close of the client, the server answers and ends the connection
	if (fd >= 0) {
		unsigned char close_frame[MESSAGES_SMALL_HEADER + MESSAGES_MASK_SIZE] = { MESSAGES_FIN_CLOSE, MESSAGES_MASK_BIT };
		(void) memcpy (close_frame + MESSAGES_SMALL_HEADER, messages_mask, MESSAGES_MASK_SIZE);
		(void) messages_send_all (fd, close_frame, sizeof (close_frame));
		while (recv (fd, payload, 1, 0) > 0);
		(void) close (fd);
	}

	free (frame);
	free (payload);

	return NULL;

}

static int messages_compare (const void *a, const void *b) {

	const double first = *((const double *) a);
	const double second = *((const double *) b);

	return (first > second) - (first < second);

}

static double messages_percentile (const double *sorted, const unsigned int count, const unsigned int percentile) {

	unsigned int idx = (count * percentile) / MESSAGES_PERCENTILE_BASE;
	if (idx >= count) idx = count - 1;

	return sorted[idx] * MESSAGES_SECONDS_TO_MILLISECONDS;

}

int main (int argc, char **argv) {

	if (argc < MESSAGES_ARGUMENTS) {
		(void) fprintf (stderr, "usage: messages <port> <connections> <messages per connection> <size> [path]\n");
		return 1;
	}

	MessagesContext context;
	(void) memset (&context, 0, sizeof (MessagesContext));
	context.port = (unsigned short) atoi (argv[MESSAGES_ARGUMENT_PORT]);
	context.messages = (unsigned int) atoi (argv[MESSAGES_ARGUMENT_MESSAGES]);
	context.size = (size_t) atoi (argv[MESSAGES_ARGUMENT_SIZE]);
	context.path = (argc > MESSAGES_ARGUMENT_PATH) ? argv[MESSAGES_ARGUMENT_PATH] : MESSAGES_DEFAULT_PATH;

	const unsigned int connections = (unsigned int) atoi (argv[MESSAGES_ARGUMENT_CONNECTIONS]);
	if (!connections || (connections > MESSAGES_MAX_CONNECTIONS) || !context.messages || (context.size > MESSAGES_MAX_SIZE)) {
		(void) fprintf (stderr, "invalid arguments\n");
		return 1;
	}

	const unsigned int total = connections * context.messages;
	context.latencies = (double *) calloc (total, sizeof (double));
	(void) pthread_mutex_init (&context.mutex, NULL);

	pthread_t *threads = (pthread_t *) calloc (connections, sizeof (pthread_t));
	MessagesConnection *connection = (MessagesConnection *) calloc (connections, sizeof (MessagesConnection));

	const double start = messages_now ();
	for (unsigned int i = 0; i < connections; i++) {
		connection[i].context = &context;
		connection[i].index = i;
		(void) pthread_create (&threads[i], NULL, messages_thread, &connection[i]);
	}

	for (unsigned int i = 0; i < connections; i++) (void) pthread_join (threads[i], NULL);

	const double elapsed = messages_now () - start;

	qsort (context.latencies, total, sizeof (double), messages_compare);

	(void) printf (
		"connections %u - messages %u - size %zu - %.0f msg/s - p50 %.3f ms - p95 %.3f ms - p99 %.3f ms - failed %u\n",
		connections, total, context.size, (double) (total - context.failed) / elapsed,
		messages_percentile (context.latencies, total, MESSAGES_PERCENTILE_50),
		messages_percentile (context.latencies, total, MESSAGES_PERCENTILE_95),
		messages_percentile (context.latencies, total, MESSAGES_PERCENTILE_99),
		context.failed
	);

	free (threads);
	free (connection);
	free (context.latencies);
	(void) pthread_mutex_destroy (&context.mutex);

	return context.failed ? 1 : 0;

}
