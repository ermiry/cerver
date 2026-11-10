#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/receive.h"

#include "cerver/collections/freelist.h"

// the deleted sockets that are kept to be reused
#define SOCKET_RECYCLE_LIMIT			FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

#define SOCKET_MILLISECONDS_PER_SECOND			1000
#define SOCKET_NANOSECONDS_PER_MILLISECOND		1000000

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Socket {

    int sock_fd;

	char *packet_buffer;
	size_t packet_buffer_size;

	// point to the storage below, so a socket is a single allocation
	pthread_mutex_t *read_mutex;
	pthread_mutex_t *write_mutex;

	pthread_mutex_t read_mutex_storage;
	pthread_mutex_t write_mutex_storage;

	// serializes the close of the fd with a shutdown from another thread,
	// so a shutdown never reaches an fd that was closed and given to another connection
	pthread_mutex_t close_mutex;

	// the connection owns one reference, and every CerverReceive another one,
	// so a receive thread can still read the socket after a handler deleted its connection
	unsigned int references;

	// the next deleted socket that waits to be reused
	struct _Socket *recycle_next;

};

typedef struct _Socket Socket;

CERVER_PUBLIC Socket *socket_new (void);

// releases a reference to the socket, it is deleted with the last one
CERVER_PUBLIC void socket_delete (void *socket_ptr);

// takes a new reference to the socket, that must be released with socket_delete ()
// returns the same socket
CERVER_PUBLIC Socket *socket_reference (Socket *socket);

CERVER_PUBLIC void *socket_create_empty (void);

CERVER_PUBLIC Socket *socket_create (int fd);

// closes the fd of the socket and sets it to -1, with the close mutex
// returns the result of close (), 0 if the socket was already closed
CERVER_PUBLIC int socket_close (Socket *socket);

// closes the fd of the socket if it is open and sets the new one, with the close mutex
CERVER_PUBLIC void socket_replace (Socket *socket, const int fd);

// shuts down both ways of the fd of the socket if it is still open, with the close mutex,
// it can be called from any thread that has a reference to the socket
// returns 0 on success, 1 if the socket is closed or shutdown () failed
CERVER_PUBLIC u8 socket_shutdown (Socket *socket);

#pragma region send

// sets the deadline of a send to timeout secs from now (CLOCK_MONOTONIC)
// a timeout of 0 uses CERVER_DEFAULT_SEND_TIMEOUT
CERVER_PUBLIC void socket_deadline_create (
	struct timespec *deadline, const time_t timeout
);

// returns the milliseconds that are left until the deadline, 0 if it has expired
CERVER_PUBLIC int socket_deadline_remaining (const struct timespec *deadline);

// waits until a full non blocking socket can be written again
// returns true if it can be written before the deadline
CERVER_PUBLIC bool socket_wait_writable (
	const int sock_fd, const struct timespec *deadline
);

// an interrupted send is always retried,
// a full non blocking socket buffer (EAGAIN) is retried when it can be written before the deadline,
// in a blocking socket EAGAIN means that SO_SNDTIMEO expired without any progress
CERVER_PUBLIC bool socket_send_can_retry (
	const int sock_fd, const int error, const struct timespec *deadline
);

// sends all the parts with as few system calls as the socket takes,
// like writev (), but with MSG_NOSIGNAL so a reset connection returns EPIPE instead of raising SIGPIPE
// the socket's write mutex must be held by the caller
// the parts are modified to skip what was sent
// sent (optional) is set to every byte that was sent, error (optional) to the errno of a failure
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 socket_send_vector (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const time_t timeout,
	size_t *sent, int *error
);

// works as socket_send_vector () with a single buffer
CERVER_PUBLIC u8 socket_send_buffer (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const time_t timeout,
	size_t *sent, int *error
);

// sends size bytes of a file with sendfile (), from its current offset,
// until every byte was sent or the deadline expires
// the socket's write mutex must be held by the caller
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 socket_send_file (
	const Socket *socket,
	const int file_fd, const size_t size,
	const time_t timeout,
	size_t *sent, int *error
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
