#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <linux/sockios.h>

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
			(void) pthread_mutex_init (&socket->close_mutex, NULL);
		}
	}

	return socket;

}

static void socket_destroy (void *socket_ptr) {

	Socket *socket = (Socket *) socket_ptr;

	(void) pthread_mutex_destroy (&socket->read_mutex_storage);
	(void) pthread_mutex_destroy (&socket->write_mutex_storage);
	(void) pthread_mutex_destroy (&socket->close_mutex);

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

// a send that another thread is doing on the fd must end before it is closed,
// or it could write to another connection that took the same fd,
// the shutdown wakes up a send that waits for room, so the wait for the write mutex is short
static void socket_close_wait_sends (Socket *socket) {

	(void) socket_shutdown (socket);

	if (socket->write_mutex) (void) pthread_mutex_lock (socket->write_mutex);

}

int socket_close (Socket *socket) {

	int retval = 0;

	if (socket) {
		socket_close_wait_sends (socket);

		(void) pthread_mutex_lock (&socket->close_mutex);

		if (socket->sock_fd >= 0) {
			retval = close (socket->sock_fd);
			socket->sock_fd = -1;
		}

		(void) pthread_mutex_unlock (&socket->close_mutex);

		if (socket->write_mutex) (void) pthread_mutex_unlock (socket->write_mutex);
	}

	return retval;

}

void socket_replace (Socket *socket, const int fd) {

	if (socket) {
		(void) pthread_mutex_lock (&socket->close_mutex);

		if (socket->sock_fd >= 0) {
			(void) close (socket->sock_fd);
		}

		socket->sock_fd = fd;

		(void) pthread_mutex_unlock (&socket->close_mutex);
	}

}

u8 socket_shutdown (Socket *socket) {

	u8 retval = 1;

	if (socket) {
		(void) pthread_mutex_lock (&socket->close_mutex);

		if (socket->sock_fd >= 0) {
			retval = (u8) (shutdown (socket->sock_fd, SHUT_RDWR) != 0);
		}

		(void) pthread_mutex_unlock (&socket->close_mutex);
	}

	return retval;

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

// sets the deadline of a send to timeout milliseconds from now (CLOCK_MONOTONIC)
void socket_deadline_create_milliseconds (
	struct timespec *deadline, const u32 timeout
) {

	if (!timeout) {
		socket_deadline_create (deadline, 0);
	}

	else {
		(void) clock_gettime (CLOCK_MONOTONIC, deadline);

		deadline->tv_sec += (time_t) (timeout / SOCKET_MILLISECONDS_PER_SECOND);
		deadline->tv_nsec += (long) (timeout % SOCKET_MILLISECONDS_PER_SECOND)
			* SOCKET_NANOSECONDS_PER_MILLISECOND;

		if (deadline->tv_nsec >= SOCKET_NANOSECONDS_PER_SECOND) {
			deadline->tv_sec += 1;
			deadline->tv_nsec -= SOCKET_NANOSECONDS_PER_SECOND;
		}
	}

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

// a send with MSG_DONTWAIT never blocks, so a full socket (EAGAIN) waits for space
// until the deadline in poll (), in blocking and in non blocking sockets
static bool socket_send_until_can_retry (
	const int sock_fd, const int error, const struct timespec *deadline
) {

	bool retry = false;

	if (error == EINTR) {
		retry = true;
	}

	else if ((error == EAGAIN) || (error == EWOULDBLOCK)) {
		retry = socket_wait_writable (sock_fd, deadline);
	}

	return retry;

}

// sends all the parts with as few system calls as the socket takes,
// sharing the deadline with any other send of the caller
// returns 0 on success, 1 on error
u8 socket_send_vector_until (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const struct timespec *deadline,
	size_t *sent, int *error
) {

	u8 retval = 0;

	size_t total = 0;
	int failure = 0;

	while (n_parts > 0) {
		struct msghdr message;
		(void) memset (&message, 0, sizeof (struct msghdr));
		message.msg_iov = parts;
		message.msg_iovlen = (size_t) n_parts;

		const ssize_t result = sendmsg (
			socket->sock_fd, &message, flags | MSG_NOSIGNAL | MSG_DONTWAIT
		);

		if (result < 0) {
			failure = errno;
			if (socket_send_until_can_retry (socket->sock_fd, failure, deadline)) continue;

			retval = 1;
			break;
		}

		total += (size_t) result;
		socket_send_parts_advance (&parts, &n_parts, (size_t) result);
	}

	socket_send_result (sent, total, error, retval ? failure : 0);

	return retval;

}

// sends all the parts with as few system calls as the socket takes
// returns 0 on success, 1 on error
u8 socket_send_vector (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const time_t timeout,
	size_t *sent, int *error
) {

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, timeout);

	return socket_send_vector_until (
		socket, parts, n_parts, flags, &deadline, sent, error
	);

}

// works as socket_send_vector_until () with a single buffer
u8 socket_send_buffer_until (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const struct timespec *deadline,
	size_t *sent, int *error
) {

	struct iovec part = { .iov_base = (void *) buffer, .iov_len = size };

	return socket_send_vector_until (
		socket, &part, size ? 1 : 0, flags, deadline, sent, error
	);

}

// works as socket_send_vector () with a single buffer
u8 socket_send_buffer (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const time_t timeout,
	size_t *sent, int *error
) {

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, timeout);

	return socket_send_buffer_until (
		socket, buffer, size, flags, &deadline, sent, error
	);

}

// sendfile () has no MSG_DONTWAIT, so in a blocking socket
// SO_SNDTIMEO is limited to what is left of the deadline before each call
// returns false if the deadline has expired
static bool socket_send_file_limit (
	const int sock_fd, const bool blocking, const struct timespec *deadline
) {

	const int remaining = socket_deadline_remaining (deadline);

	if (blocking && (remaining > 0)) {
		struct timeval limit = CERVER_ZERO_INITIALIZER;
		limit.tv_sec = remaining / SOCKET_MILLISECONDS_PER_SECOND;
		limit.tv_usec = (remaining % SOCKET_MILLISECONDS_PER_SECOND) * SOCKET_MICROSECONDS_PER_MILLISECOND;

		(void) setsockopt (sock_fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof (struct timeval));
	}

	return remaining > 0;

}

// the bytes of the pending ones that fit in the send buffer of the socket,
// half of its free space because the buffer also counts the overhead of the kernel,
// and at least SOCKET_SEND_FILE_MIN_ROOM when the space is unknown or almost full
static size_t socket_send_file_room (const int sock_fd, const size_t pending) {

	size_t room = SOCKET_SEND_FILE_MIN_ROOM;

	int buffer = 0;
	socklen_t buffer_size = sizeof (int);
	int queued = 0;
	if (
		!getsockopt (sock_fd, SOL_SOCKET, SO_SNDBUF, &buffer, &buffer_size)
		&& !ioctl (sock_fd, SIOCOUTQ, &queued)
		&& (buffer > queued)
	) {
		const size_t free_space = (size_t) (buffer - queued) / SOCKET_SEND_FILE_ROOM_DIVISOR;
		if (free_space > room) room = free_space;
	}

	return (pending < room) ? pending : room;

}

// in a blocking socket the SO_SNDTIMEO of sendfile () applies to each piece that the kernel sends,
// so sendfile () can block longer than what is left of the deadline:
// it waits until the socket can be written before the deadline, and sends only what fits in its buffer
// returns the bytes to send, 0 if the deadline expired
static size_t socket_send_file_ready (
	const int sock_fd, const bool blocking,
	const struct timespec *deadline, const size_t pending
) {

	size_t count = 0;

	if (socket_send_file_limit (sock_fd, blocking, deadline)) {
		if (!blocking) count = pending;
		else if (socket_wait_writable (sock_fd, deadline)) count = socket_send_file_room (sock_fd, pending);
	}

	return count;

}

// sends size bytes of a file with sendfile (), from offset (NULL for its current offset),
// sharing the deadline with any other send of the caller
// returns 0 on success, 1 on error
u8 socket_send_file_until (
	const Socket *socket,
	const int file_fd, off_t *offset, const size_t size,
	const struct timespec *deadline,
	size_t *sent, int *error
) {

	u8 retval = 0;

	size_t total = 0;
	int failure = 0;

	const int sock_fd = socket->sock_fd;
	const bool blocking = socket_is_blocking (sock_fd);

	// the send timeout of the socket is restored after the file
	struct timeval original = CERVER_ZERO_INITIALIZER;
	socklen_t original_size = sizeof (struct timeval);
	const bool restore = blocking && !getsockopt (
		sock_fd, SOL_SOCKET, SO_SNDTIMEO, &original, &original_size
	);

	while (total < size) {
		const size_t count = socket_send_file_ready (sock_fd, blocking, deadline, size - total);
		if (!count) {
			failure = EAGAIN;
			retval = 1;
			break;
		}

		const ssize_t result = sendfile (sock_fd, file_fd, offset, count);
		if (result < 0) {
			failure = errno;
			if (socket_send_until_can_retry (sock_fd, failure, deadline)) continue;

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

	if (restore) {
		(void) setsockopt (sock_fd, SOL_SOCKET, SO_SNDTIMEO, &original, sizeof (struct timeval));
	}

	socket_send_result (sent, total, error, retval ? failure : 0);

	return retval;

}

// sends size bytes of a file with sendfile (), from its current offset
// returns 0 on success, 1 on error
u8 socket_send_file (
	const Socket *socket,
	const int file_fd, const size_t size,
	const time_t timeout,
	size_t *sent, int *error
) {

	struct timespec deadline = { 0, 0 };
	socket_deadline_create (&deadline, timeout);

	return socket_send_file_until (
		socket, file_fd, NULL, size, &deadline, sent, error
	);

}

#pragma endregion
