#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>

#include <pthread.h>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/socket.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/handler.h"

#include "cerver/collections/freelist.h"

#pragma region recycle

static FreeList socket_recycled = FREELIST_INITIALIZER (
	Socket, recycle_next, SOCKET_RECYCLE_LIMIT
);

// a new socket initializes its mutexes once, a recycled one keeps them
static Socket *socket_allocate (void) {

	Socket *socket = (Socket *) freelist_pop (&socket_recycled);
	if (!socket) {
		socket = (Socket *) malloc (sizeof (Socket));
		if (socket) {
			(void) pthread_mutex_init (&socket->read_mutex_storage, NULL);
			(void) pthread_mutex_init (&socket->write_mutex_storage, NULL);
		}
	}

	return socket;

}

static void socket_destroy (void *socket_ptr) {

	Socket *socket = (Socket *) socket_ptr;

	(void) pthread_mutex_destroy (&socket->read_mutex_storage);
	(void) pthread_mutex_destroy (&socket->write_mutex_storage);

	free (socket);

}

// keeps the socket to be reused, or releases it if there are enough
static void socket_recycle (Socket *socket) {

	if (freelist_push (&socket_recycled, socket)) {
		socket_destroy (socket);
	}

}

// the recycled sockets are released when the library is unloaded
__attribute__ ((destructor)) static void socket_recycled_drain (void) {

	freelist_drain (&socket_recycled, socket_destroy);

}

#pragma endregion

#pragma region main

Socket *socket_new (void) {

	Socket *socket = socket_allocate ();
	if (socket) {
		socket->sock_fd = -1;

		socket->packet_buffer = NULL;
		socket->packet_buffer_size = 0;

		socket->read_mutex = NULL;
		socket->write_mutex = NULL;

		socket->references = 1;

		socket->recycle_next = NULL;
	}

	return socket;

}

// takes a new reference to the socket
Socket *socket_reference (Socket *socket) {

	if (socket) {
		(void) __atomic_add_fetch (&socket->references, 1, __ATOMIC_ACQ_REL);
	}

	return socket;

}

// releases a reference to the socket, it is deleted with the last one
void socket_delete (void *socket_ptr) {

	if (socket_ptr) {
		Socket *socket = (Socket *) socket_ptr;

		if (__atomic_sub_fetch (&socket->references, 1, __ATOMIC_ACQ_REL)) return;

		if (socket->packet_buffer) free (socket->packet_buffer);

		// a send that another thread started finishes before the socket is reused
		(void) pthread_mutex_lock (&socket->write_mutex_storage);
		(void) pthread_mutex_unlock (&socket->write_mutex_storage);

		socket_recycle (socket);
	}

}

void *socket_create_empty (void) {

	Socket *socket = socket_new ();
	if (socket) {
		socket->read_mutex = &socket->read_mutex_storage;
		socket->write_mutex = &socket->write_mutex_storage;
	}

	return socket;

}

Socket *socket_create (int fd) {

	Socket *socket = (Socket *) socket_create_empty ();
	if (socket) {
		socket->sock_fd = fd;
	}

	return socket;

}

#pragma endregion

#pragma region send

// sets the deadline of a send to timeout secs from now (CLOCK_MONOTONIC)
void socket_deadline_create (
	struct timespec *deadline, const time_t timeout
) {

	(void) clock_gettime (CLOCK_MONOTONIC, deadline);
	deadline->tv_sec += timeout ? timeout : (time_t) CERVER_DEFAULT_SEND_TIMEOUT;

}

// returns the milliseconds that are left until the deadline, 0 if it has expired
int socket_deadline_remaining (const struct timespec *deadline) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	const long long remaining =
		((long long) (deadline->tv_sec - now.tv_sec) * SOCKET_MILLISECONDS_PER_SECOND)
		+ ((deadline->tv_nsec - now.tv_nsec) / SOCKET_NANOSECONDS_PER_MILLISECOND);

	return (remaining > 0) ? (int) remaining : 0;

}

static bool socket_is_blocking (const int sock_fd) {

	const int flags = fcntl (sock_fd, F_GETFL, 0);

	return (flags > -1) && !(flags & O_NONBLOCK);

}

// waits until a full non blocking socket can be written again
// returns true if it can be written before the deadline
bool socket_wait_writable (
	const int sock_fd, const struct timespec *deadline
) {

	bool writable = false;

	int remaining = socket_deadline_remaining (deadline);
	while (remaining > 0) {
		struct pollfd poll_fd = { .fd = sock_fd, .events = POLLOUT, .revents = 0 };

		const int ready = poll (&poll_fd, 1, remaining);
		if (ready > 0) {
			writable = true;
			break;
		}

		if ((ready < 0) && (errno != EINTR)) break;

		remaining = socket_deadline_remaining (deadline);
	}

	return writable;

}

// an interrupted send is always retried,
// a full non blocking socket buffer (EAGAIN) is retried when it can be written before the deadline
bool socket_send_can_retry (
	const int sock_fd, const int error, const struct timespec *deadline
) {

	bool retry = false;

	if (error == EINTR) {
		retry = true;
	}

	else if (
		((error == EAGAIN) || (error == EWOULDBLOCK))
		&& !socket_is_blocking (sock_fd)
	) {
		retry = socket_wait_writable (sock_fd, deadline);
	}

	return retry;

}

// skips the parts that were sent completely and moves the start of a partial one
static void socket_send_parts_advance (
	struct iovec **parts, int *n_parts, size_t sent
) {

	while ((*n_parts > 0) && (sent >= (*parts)->iov_len)) {
		sent -= (*parts)->iov_len;
		*parts += 1;
		*n_parts -= 1;
	}

	if ((*n_parts > 0) && sent) {
		(*parts)->iov_base = ((char *) (*parts)->iov_base) + sent;
		(*parts)->iov_len -= sent;
	}

}

static void socket_send_result (
	size_t *sent, const size_t total, int *error, const int value
) {

	if (sent) *sent = total;
	if (error) *error = value;

}

// sends all the parts with as few system calls as the socket takes
// returns 0 on success, 1 on error
u8 socket_send_vector (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const time_t timeout,
	size_t *sent, int *error
) {

	u8 retval = 0;

	size_t total = 0;
	int failure = 0;

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, timeout);

	while (n_parts > 0) {
		struct msghdr message;
		(void) memset (&message, 0, sizeof (struct msghdr));
		message.msg_iov = parts;
		message.msg_iovlen = (size_t) n_parts;

		const ssize_t result = sendmsg (socket->sock_fd, &message, flags | MSG_NOSIGNAL);
		if (result < 0) {
			failure = errno;
			if (socket_send_can_retry (socket->sock_fd, failure, &deadline)) continue;

			retval = 1;
			break;
		}

		total += (size_t) result;
		socket_send_parts_advance (&parts, &n_parts, (size_t) result);
	}

	socket_send_result (sent, total, error, retval ? failure : 0);

	return retval;

}

// works as socket_send_vector () with a single buffer
u8 socket_send_buffer (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const time_t timeout,
	size_t *sent, int *error
) {

	struct iovec part = { .iov_base = (void *) buffer, .iov_len = size };

	return socket_send_vector (
		socket, &part, size ? 1 : 0, flags, timeout, sent, error
	);

}

// sends size bytes of a file with sendfile (), from its current offset
// returns 0 on success, 1 on error
u8 socket_send_file (
	const Socket *socket,
	const int file_fd, const size_t size,
	const time_t timeout,
	size_t *sent, int *error
) {

	u8 retval = 0;

	size_t total = 0;
	int failure = 0;

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, timeout);

	while (total < size) {
		const ssize_t result = sendfile (socket->sock_fd, file_fd, NULL, size - total);
		if (result < 0) {
			failure = errno;
			if (socket_send_can_retry (socket->sock_fd, failure, &deadline)) continue;

			retval = 1;
			break;
		}

		// the file ended before size bytes
		if (!result) {
			retval = 1;
			break;
		}

		total += (size_t) result;
	}

	socket_send_result (sent, total, error, retval ? failure : 0);

	return retval;

}

#pragma endregion
