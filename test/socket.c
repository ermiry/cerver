#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/socket.h>
#include <sys/uio.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cerver/network.h>
#include <cerver/socket.h>

#include "test.h"

// more than any socket buffer of a socketpair
#define SOCKET_TEST_LARGE_SIZE				(1 << 23)

#define SOCKET_TEST_TIMEOUT					8
#define SOCKET_TEST_SHORT_TIMEOUT			1

// the milliseconds that a deadline can end earlier than its secs
#define SOCKET_TEST_TIMEOUT_TOLERANCE		16

// the milliseconds that a send can end after its deadline, also under valgrind
#define SOCKET_TEST_DEADLINE_TOLERANCE		512

// the SO_SNDTIMEO of a blocking socket, longer than the deadlines of its sends
#define SOCKET_TEST_BLOCKING_TIMEOUT		4

#define SOCKET_TEST_SHARED_DEADLINE			300

// the reader waits before reading, so the writer finds a full socket
#define SOCKET_TEST_READER_DELAY			(1L << 26)
#define SOCKET_TEST_READ_BUFFER_SIZE		(1 << 16)

#define SOCKET_TEST_PARTS					3

#define SOCKET_TEST_FILE_TEMPLATE			"/tmp/cerver-socket-XXXXXX"
#define SOCKET_TEST_FILE_PATH_SIZE			64
#define SOCKET_TEST_FILE_SIZE				(1 << 20)
#define SOCKET_TEST_FILE_EXTRA				1

#define SOCKET_TEST_PATTERN_MODULE			251

// a TCP connection in the loopback whose reader never reads, with a small receive buffer,
// where sendfile () blocks longer than the SO_SNDTIMEO of the socket
#define SOCKET_TEST_TCP_ADDRESS				"127.0.0.1"
#define SOCKET_TEST_TCP_BACKLOG				1
#define SOCKET_TEST_TCP_RECEIVE_BUFFER		(1 << 12)
#define SOCKET_TEST_TCP_FILE_SIZE			(1 << 24)
#define SOCKET_TEST_TCP_DEADLINE			500

// the milliseconds that a file to a TCP socket can end after its deadline
#define SOCKET_TEST_TCP_TOLERANCE			256

typedef struct SocketTestReader {

	int fd;

	char *data;
	size_t expected;
	size_t received;

} SocketTestReader;

static unsigned char socket_test_pattern (const size_t index) {

	return (unsigned char) (index % SOCKET_TEST_PATTERN_MODULE);

}

static char *socket_test_buffer_create (const size_t size) {

	char *buffer = (char *) malloc (size);
	test_check_ptr (buffer);

	for (size_t i = 0; i < size; i++) buffer[i] = (char) socket_test_pattern (i);

	return buffer;

}

static void socket_test_pair (int fds[2]) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	// the writer is non blocking, like the connections of a cerver
	test_check_true (sock_set_blocking (fds[0], false));

}

static void socket_test_sleep (const long nanoseconds) {

	struct timespec interval = { 0, nanoseconds };
	(void) nanosleep (&interval, NULL);

}

// reads every byte that the writer sends after a delay
static void *socket_test_reader (void *reader_ptr) {

	SocketTestReader *reader = (SocketTestReader *) reader_ptr;

	socket_test_sleep (SOCKET_TEST_READER_DELAY);

	char buffer[SOCKET_TEST_READ_BUFFER_SIZE];
	while (reader->received < reader->expected) {
		const ssize_t result = recv (reader->fd, buffer, sizeof (buffer), 0);
		if (result <= 0) break;

		(void) memcpy (reader->data + reader->received, buffer, (size_t) result);
		reader->received += (size_t) result;
	}

	return NULL;

}

static void socket_test_reader_start (
	pthread_t *thread, SocketTestReader *reader,
	const int fd, const size_t expected
) {

	reader->fd = fd;
	reader->data = (char *) malloc (expected);
	test_check_ptr (reader->data);
	reader->expected = expected;
	reader->received = 0;

	test_check_int_eq (pthread_create (thread, NULL, socket_test_reader, reader), 0, NULL);

}

static void socket_test_reader_check (
	pthread_t thread, SocketTestReader *reader, const char *expected
) {

	(void) pthread_join (thread, NULL);

	test_check_unsigned_eq ((unsigned int) reader->received, (unsigned int) reader->expected, NULL);
	test_check_int_eq (memcmp (reader->data, expected, reader->expected), 0, NULL);

	free (reader->data);

}

static int socket_test_file_create (char *path, const char *buffer, const size_t size);

static void test_socket_deadline (void) {

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, SOCKET_TEST_TIMEOUT);

	const int remaining = socket_deadline_remaining (&deadline);
	test_check (remaining > 0, NULL);
	test_check (remaining <= (SOCKET_TEST_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND), NULL);

	struct timespec expired = { 0, 0 };
	test_check_int_eq (socket_deadline_remaining (&expired), 0, NULL);

	// 0 uses the default send timeout
	socket_deadline_create (&deadline, 0);
	test_check (socket_deadline_remaining (&deadline) > (SOCKET_TEST_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND), NULL);

	socket_deadline_create_milliseconds (&deadline, SOCKET_TEST_SHARED_DEADLINE);
	const int milliseconds = socket_deadline_remaining (&deadline);
	test_check (milliseconds > 0, NULL);
	test_check (milliseconds <= SOCKET_TEST_SHARED_DEADLINE, NULL);
	test_check (deadline.tv_nsec < SOCKET_NANOSECONDS_PER_SECOND, NULL);

	socket_deadline_create_milliseconds (&deadline, 0);
	test_check (socket_deadline_remaining (&deadline) > (SOCKET_TEST_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND), NULL);

}

static long long socket_test_elapsed (const struct timespec *start) {

	struct timespec end = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &end);

	return ((long long) (end.tv_sec - start->tv_sec) * SOCKET_MILLISECONDS_PER_SECOND)
		+ ((end.tv_nsec - start->tv_nsec) / SOCKET_NANOSECONDS_PER_MILLISECOND);

}

// a blocking writer, like the accepted sockets of a cerver with a send timeout,
// with an SO_SNDTIMEO longer than the deadlines of the tests
static void socket_test_blocking_pair (int fds[2]) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);
	test_check_int_eq (sock_set_send_timeout (fds[0], SOCKET_TEST_BLOCKING_TIMEOUT), 0, NULL);

}

// the deadline is the limit of the send, not the SO_SNDTIMEO of the blocking socket
static void test_socket_send_blocking_timeout (void) {

	int fds[2];
	socket_test_blocking_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_LARGE_SIZE);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	size_t sent = 0;
	int error = 0;
	test_check_unsigned_eq (
		socket_send_buffer (socket, buffer, SOCKET_TEST_LARGE_SIZE, 0, SOCKET_TEST_SHORT_TIMEOUT, &sent, &error),
		1, NULL
	);

	const long long elapsed = socket_test_elapsed (&start);

	test_check_int_eq (error, EAGAIN, NULL);
	test_check (sent < SOCKET_TEST_LARGE_SIZE, NULL);
	test_check (
		elapsed <= ((SOCKET_TEST_SHORT_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND) + SOCKET_TEST_DEADLINE_TOLERANCE),
		NULL
	);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// a file to a blocking socket ends with its deadline, and the SO_SNDTIMEO of the socket is restored
static void test_socket_send_file_blocking_timeout (void) {

	int fds[2];
	socket_test_blocking_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_FILE_SIZE);

	char path[SOCKET_TEST_FILE_PATH_SIZE] = { 0 };
	const int file_fd = socket_test_file_create (path, buffer, SOCKET_TEST_FILE_SIZE);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	size_t sent = 0;
	int error = 0;
	test_check_unsigned_eq (
		socket_send_file (socket, file_fd, SOCKET_TEST_FILE_SIZE, SOCKET_TEST_SHORT_TIMEOUT, &sent, &error),
		1, NULL
	);

	const long long elapsed = socket_test_elapsed (&start);

	test_check_int_eq (error, EAGAIN, NULL);
	test_check (sent < SOCKET_TEST_FILE_SIZE, NULL);
	test_check (
		elapsed <= ((SOCKET_TEST_SHORT_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND) + SOCKET_TEST_DEADLINE_TOLERANCE),
		NULL
	);

	struct timeval timeout = { 0, 0 };
	socklen_t timeout_size = sizeof (struct timeval);
	test_check_int_eq (getsockopt (fds[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, &timeout_size), 0, NULL);
	test_check_int_eq ((int) timeout.tv_sec, SOCKET_TEST_BLOCKING_TIMEOUT, NULL);

	(void) close (file_fd);
	(void) unlink (path);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// the sends that share a deadline end with it in total, not each one with its own timeout
static void test_socket_send_until_shared (void) {

	int fds[2];
	socket_test_blocking_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_LARGE_SIZE);

	char path[SOCKET_TEST_FILE_PATH_SIZE] = { 0 };
	const int file_fd = socket_test_file_create (path, buffer, SOCKET_TEST_FILE_SIZE);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	struct timespec deadline = { 0, 0 };
	socket_deadline_create_milliseconds (&deadline, SOCKET_TEST_SHARED_DEADLINE);

	int error = 0;
	test_check_unsigned_eq (
		socket_send_buffer_until (socket, buffer, SOCKET_TEST_LARGE_SIZE, 0, &deadline, NULL, &error),
		1, NULL
	);

	test_check_int_eq (error, EAGAIN, NULL);

	struct iovec parts[SOCKET_TEST_PARTS];
	for (unsigned int i = 0; i < SOCKET_TEST_PARTS; i++) {
		parts[i].iov_base = buffer;
		parts[i].iov_len = SOCKET_TEST_READ_BUFFER_SIZE;
	}

	error = 0;
	test_check_unsigned_eq (
		socket_send_vector_until (socket, parts, SOCKET_TEST_PARTS, 0, &deadline, NULL, &error),
		1, NULL
	);

	test_check_int_eq (error, EAGAIN, NULL);

	error = 0;
	test_check_unsigned_eq (
		socket_send_file_until (socket, file_fd, NULL, SOCKET_TEST_FILE_SIZE, &deadline, NULL, &error),
		1, NULL
	);

	test_check_int_eq (error, EAGAIN, NULL);

	const long long elapsed = socket_test_elapsed (&start);

	test_check (elapsed >= (SOCKET_TEST_SHARED_DEADLINE - SOCKET_TEST_TIMEOUT_TOLERANCE), NULL);
	test_check (elapsed <= (SOCKET_TEST_SHARED_DEADLINE + SOCKET_TEST_DEADLINE_TOLERANCE), NULL);

	(void) close (file_fd);
	(void) unlink (path);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// a full socket waits until the other side reads
static void test_socket_send_full (void) {

	int fds[2];
	socket_test_pair (fds);

	Socket *socket = socket_create (fds[0]);
	test_check_ptr (socket);

	char *buffer = socket_test_buffer_create (SOCKET_TEST_LARGE_SIZE);

	pthread_t thread;
	SocketTestReader reader;
	socket_test_reader_start (&thread, &reader, fds[1], SOCKET_TEST_LARGE_SIZE);

	size_t sent = 0;
	int error = 0;
	test_check_unsigned_eq (
		socket_send_buffer (socket, buffer, SOCKET_TEST_LARGE_SIZE, 0, SOCKET_TEST_TIMEOUT, &sent, &error),
		0, NULL
	);

	test_check_unsigned_eq ((unsigned int) sent, SOCKET_TEST_LARGE_SIZE, NULL);
	test_check_int_eq (error, 0, NULL);

	socket_test_reader_check (thread, &reader, buffer);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// without a reader the send fails when the deadline expires
static void test_socket_send_timeout (void) {

	int fds[2];
	socket_test_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_LARGE_SIZE);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	size_t sent = 0;
	int error = 0;
	test_check_unsigned_eq (
		socket_send_buffer (socket, buffer, SOCKET_TEST_LARGE_SIZE, 0, SOCKET_TEST_SHORT_TIMEOUT, &sent, &error),
		1, NULL
	);

	struct timespec end = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &end);

	const long long elapsed =
		((long long) (end.tv_sec - start.tv_sec) * SOCKET_MILLISECONDS_PER_SECOND)
		+ ((end.tv_nsec - start.tv_nsec) / SOCKET_NANOSECONDS_PER_MILLISECOND);

	test_check_int_eq (error, EAGAIN, NULL);
	test_check (sent < SOCKET_TEST_LARGE_SIZE, NULL);
	test_check (
		elapsed >= ((SOCKET_TEST_SHORT_TIMEOUT * SOCKET_MILLISECONDS_PER_SECOND) - SOCKET_TEST_TIMEOUT_TOLERANCE),
		NULL
	);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// a closed peer returns EPIPE instead of raising SIGPIPE
static void test_socket_send_closed (void) {

	int fds[2];
	socket_test_pair (fds);
	(void) close (fds[1]);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_READ_BUFFER_SIZE);

	int error = 0;
	test_check_unsigned_eq (
		socket_send_buffer (socket, buffer, SOCKET_TEST_READ_BUFFER_SIZE, 0, SOCKET_TEST_SHORT_TIMEOUT, NULL, &error),
		1, NULL
	);

	test_check_int_eq (error, EPIPE, NULL);

	free (buffer);
	(void) close (fds[0]);
	socket_delete (socket);

}

// the parts arrive in order, even when a part is split between sends
static void test_socket_send_vector (void) {

	int fds[2];
	socket_test_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_LARGE_SIZE);

	const size_t part_size = SOCKET_TEST_LARGE_SIZE / SOCKET_TEST_PARTS;
	struct iovec parts[SOCKET_TEST_PARTS];
	for (unsigned int i = 0; i < SOCKET_TEST_PARTS; i++) {
		parts[i].iov_base = buffer + (i * part_size);
		parts[i].iov_len = part_size;
	}

	const size_t total = part_size * SOCKET_TEST_PARTS;

	pthread_t thread;
	SocketTestReader reader;
	socket_test_reader_start (&thread, &reader, fds[1], total);

	size_t sent = 0;
	test_check_unsigned_eq (
		socket_send_vector (socket, parts, SOCKET_TEST_PARTS, 0, SOCKET_TEST_TIMEOUT, &sent, NULL),
		0, NULL
	);

	test_check_unsigned_eq ((unsigned int) sent, (unsigned int) total, NULL);

	socket_test_reader_check (thread, &reader, buffer);

	// nothing to send is a success
	test_check_unsigned_eq (socket_send_buffer (socket, buffer, 0, 0, SOCKET_TEST_TIMEOUT, &sent, NULL), 0, NULL);
	test_check_unsigned_eq ((unsigned int) sent, 0, NULL);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

static int socket_test_file_create (char *path, const char *buffer, const size_t size) {

	(void) strncpy (path, SOCKET_TEST_FILE_TEMPLATE, SOCKET_TEST_FILE_PATH_SIZE - 1);

	const int fd = mkstemp (path);
	test_check (fd > -1, NULL);

	test_check (write (fd, buffer, size) == (ssize_t) size, NULL);
	test_check (lseek (fd, 0, SEEK_SET) == 0, NULL);

	return fd;

}

// every byte of the file is sent, and a file shorter than the size fails
static void test_socket_send_file (void) {

	int fds[2];
	socket_test_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_FILE_SIZE);

	char path[SOCKET_TEST_FILE_PATH_SIZE] = { 0 };
	const int file_fd = socket_test_file_create (path, buffer, SOCKET_TEST_FILE_SIZE);

	pthread_t thread;
	SocketTestReader reader;
	socket_test_reader_start (&thread, &reader, fds[1], SOCKET_TEST_FILE_SIZE);

	size_t sent = 0;
	test_check_unsigned_eq (
		socket_send_file (socket, file_fd, SOCKET_TEST_FILE_SIZE, SOCKET_TEST_TIMEOUT, &sent, NULL),
		0, NULL
	);

	test_check_unsigned_eq ((unsigned int) sent, SOCKET_TEST_FILE_SIZE, NULL);

	socket_test_reader_check (thread, &reader, buffer);

	// the file has ended
	test_check_unsigned_eq (
		socket_send_file (socket, file_fd, SOCKET_TEST_FILE_EXTRA, SOCKET_TEST_TIMEOUT, &sent, NULL),
		1, NULL
	);

	test_check_unsigned_eq ((unsigned int) sent, 0, NULL);

	(void) close (file_fd);
	(void) unlink (path);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

// a blocking writer accepted in the loopback, and a reader with a small receive buffer that never reads
static void socket_test_tcp_pair (int fds[2]) {

	const int listener = socket (AF_INET, SOCK_STREAM, 0);
	test_check (listener > -1, NULL);

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	address.sin_port = 0;
	test_check_int_eq (inet_pton (AF_INET, SOCKET_TEST_TCP_ADDRESS, &address.sin_addr), 1, NULL);

	socklen_t address_size = sizeof (struct sockaddr_in);
	test_check_int_eq (bind (listener, (struct sockaddr *) &address, address_size), 0, NULL);
	test_check_int_eq (listen (listener, SOCKET_TEST_TCP_BACKLOG), 0, NULL);
	test_check_int_eq (getsockname (listener, (struct sockaddr *) &address, &address_size), 0, NULL);

	fds[1] = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fds[1] > -1, NULL);

	const int receive_buffer = SOCKET_TEST_TCP_RECEIVE_BUFFER;
	test_check_int_eq (setsockopt (fds[1], SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof (int)), 0, NULL);
	test_check_int_eq (connect (fds[1], (struct sockaddr *) &address, address_size), 0, NULL);

	fds[0] = accept (listener, NULL, NULL);
	test_check (fds[0] > -1, NULL);
	test_check_int_eq (sock_set_send_timeout (fds[0], SOCKET_TEST_BLOCKING_TIMEOUT), 0, NULL);

	(void) close (listener);

}

// a file to a blocking TCP socket ends with its deadline,
// sendfile () can block longer than the SO_SNDTIMEO that limits it
static void test_socket_send_file_tcp_deadline (void) {

	int fds[2];
	socket_test_tcp_pair (fds);

	Socket *socket = socket_create (fds[0]);
	char *buffer = socket_test_buffer_create (SOCKET_TEST_TCP_FILE_SIZE);

	char path[SOCKET_TEST_FILE_PATH_SIZE] = { 0 };
	const int file_fd = socket_test_file_create (path, buffer, SOCKET_TEST_TCP_FILE_SIZE);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	struct timespec deadline = { 0, 0 };
	socket_deadline_create_milliseconds (&deadline, SOCKET_TEST_TCP_DEADLINE);

	size_t sent = 0;
	int error = 0;
	test_check_unsigned_eq (
		socket_send_file_until (socket, file_fd, NULL, SOCKET_TEST_TCP_FILE_SIZE, &deadline, &sent, &error),
		1, NULL
	);

	const long long elapsed = socket_test_elapsed (&start);

	test_check_int_eq (error, EAGAIN, NULL);
	test_check (sent < SOCKET_TEST_TCP_FILE_SIZE, NULL);
	test_check (elapsed >= (SOCKET_TEST_TCP_DEADLINE - SOCKET_TEST_TIMEOUT_TOLERANCE), NULL);
	test_check (elapsed <= (SOCKET_TEST_TCP_DEADLINE + SOCKET_TEST_TCP_TOLERANCE), "the file waited longer than its deadline");

	(void) close (file_fd);
	(void) unlink (path);

	free (buffer);
	(void) close (fds[0]);
	(void) close (fds[1]);
	socket_delete (socket);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing SOCKET...\n");

	test_socket_deadline ();

	test_socket_send_full ();

	test_socket_send_timeout ();

	test_socket_send_closed ();

	test_socket_send_vector ();

	test_socket_send_file ();

	test_socket_send_blocking_timeout ();

	test_socket_send_file_blocking_timeout ();

	test_socket_send_until_shared ();

	test_socket_send_file_tcp_deadline ();

	(void) printf ("\nDone with SOCKET tests!\n\n");

	return 0;

}
