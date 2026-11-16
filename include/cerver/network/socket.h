/**
 * @file cerver/network/socket.h
 * @brief The socket of a connection: its references, how it is closed, and how it sends with a deadline.
 */

#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/handler/receive.h"

/**
 * @defgroup network_socket Socket
 * @ingroup network
 * @brief The socket of a connection.
 * @{
 */

/** @brief The deleted sockets that are kept to be reused. */
#define SOCKET_RECYCLE_LIMIT			FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/** @brief The milliseconds of a second. */
#define SOCKET_MILLISECONDS_PER_SECOND			1000
/** @brief The microseconds of a millisecond. */
#define SOCKET_MICROSECONDS_PER_MILLISECOND		1000
/** @brief The nanoseconds of a millisecond. */
#define SOCKET_NANOSECONDS_PER_MILLISECOND		1000000
/** @brief The nanoseconds of a second. */
#define SOCKET_NANOSECONDS_PER_SECOND			1000000000L

/** @brief The least bytes that each sendfile () of a blocking socket sends. */
#define SOCKET_SEND_FILE_MIN_ROOM				4096
/** @brief Each sendfile () of a blocking socket sends at most the free space of the send buffer divided by it. */
#define SOCKET_SEND_FILE_ROOM_DIVISOR			2

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

/**
 * @brief The socket of a connection, with its mutexes and its references.
 */
struct _Socket {

	/** @brief The fd, -1 when it is closed. */
    int sock_fd;

	/** @brief The buffer of a packet that came split in many receives. */
	char *packet_buffer;
	/** @brief The size of the packet buffer. */
	size_t packet_buffer_size;

	/** @brief Points to #read_mutex_storage, so a socket is a single allocation. */
	pthread_mutex_t *read_mutex;
	/** @brief Points to #write_mutex_storage; a sender holds it while it sends. */
	pthread_mutex_t *write_mutex;

	/** @brief The storage of the read mutex. */
	pthread_mutex_t read_mutex_storage;
	/** @brief The storage of the write mutex. */
	pthread_mutex_t write_mutex_storage;

	/**
	 * @brief Serializes the close of the fd with a shutdown from another thread, so a shutdown never reaches an fd
	 * that was closed and given to another connection.
	 */
	pthread_mutex_t close_mutex;

	/**
	 * @brief The references: the connection owns one, and every CerverReceive another one, so a receive thread can
	 * still read the socket after a handler deleted its connection.
	 */
	unsigned int references;

	/** @brief The next deleted socket that waits to be reused. */
	struct _Socket *recycle_next;

};

/** @brief The socket of a connection, see struct _Socket. */
typedef struct _Socket Socket;

/**
 * @brief Allocates (or reuses) a socket with one reference, without an fd and without its mutex pointers.
 *
 * @return the new socket, NULL if it could not be allocated
 */
CERVER_PUBLIC Socket *socket_new (void);

/**
 * @brief Releases a reference to the socket; it is deleted (and kept to be reused) with the last one.
 *
 * The last release waits for a send of another thread to end before the socket is reused.
 *
 * @param[in] socket_ptr the socket, it can be NULL
 * @threadsafety Safe from any thread that has a reference, the count is an __atomic.
 */
CERVER_PUBLIC void socket_delete (void *socket_ptr);

/**
 * @brief Takes a new reference to the socket, that must be released with socket_delete ().
 *
 * @param[in,out] socket the socket, it can be NULL
 * @return the same socket
 * @threadsafety Safe from any thread that has a reference, the count is an __atomic.
 */
CERVER_PUBLIC Socket *socket_reference (Socket *socket);

/**
 * @brief Creates a socket without an fd, with its read and write mutexes.
 *
 * @return the new socket as a void pointer, NULL if it could not be allocated
 */
CERVER_PUBLIC void *socket_create_empty (void);

/**
 * @brief Creates a socket with an fd and its read and write mutexes.
 *
 * @param[in] fd the fd, the socket owns it from now on
 * @return the new socket, NULL if it could not be allocated
 */
CERVER_PUBLIC Socket *socket_create (int fd);

/**
 * @brief Closes the fd of the socket and sets it to -1, with the close mutex.
 *
 * It shuts the fd down first and waits for a send of another thread to end (the write mutex).
 *
 * @param[in,out] socket the socket, it can be NULL
 * @return the result of close (), 0 if the socket was NULL or already closed
 * @warning The caller must not hold the write mutex.
 */
CERVER_PUBLIC int socket_close (Socket *socket);

/**
 * @brief Closes the fd of the socket if it is open and sets the new one, with the close mutex.
 *
 * @param[in,out] socket the socket, it can be NULL
 * @param[in] fd the new fd, the socket owns it from now on
 */
CERVER_PUBLIC void socket_replace (Socket *socket, const int fd);

/**
 * @brief Shuts down both ways of the fd of the socket if it is still open, with the close mutex.
 *
 * @param[in] socket the socket
 * @retval 0 the fd was shut down
 * @retval 1 the socket is NULL or closed, or shutdown () failed
 * @threadsafety Safe from any thread that has a reference to the socket.
 */
CERVER_PUBLIC u8 socket_shutdown (Socket *socket);

/** @} */

/**
 * @defgroup network_send Send
 * @ingroup network
 * @brief The sends of a socket, limited by a deadline.
 * @{
 */

/**
 * @brief Sets the deadline of a send to some seconds from now (CLOCK_MONOTONIC).
 *
 * @param[out] deadline the deadline
 * @param[in] timeout the seconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT
 */
CERVER_PUBLIC void socket_deadline_create (
	struct timespec *deadline, const time_t timeout
);

/**
 * @brief Sets the deadline of a send to some milliseconds from now (CLOCK_MONOTONIC).
 *
 * @param[out] deadline the deadline
 * @param[in] timeout the milliseconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT seconds
 */
CERVER_PUBLIC void socket_deadline_create_milliseconds (
	struct timespec *deadline, const u32 timeout
);

/**
 * @brief Returns the milliseconds that are left until the deadline.
 *
 * @param[in] deadline the deadline
 * @return the milliseconds, 0 if it has expired
 */
CERVER_PUBLIC int socket_deadline_remaining (const struct timespec *deadline);

/**
 * @brief Waits with poll () until a full non blocking socket can be written again.
 *
 * @param[in] sock_fd the socket
 * @param[in] deadline the deadline
 * @return true if it can be written before the deadline
 */
CERVER_PUBLIC bool socket_wait_writable (
	const int sock_fd, const struct timespec *deadline
);

/**
 * @brief Returns whether a send that failed can be retried.
 *
 * An interrupted send (EINTR) is always retried; a full buffer of a non blocking socket (EAGAIN) is retried when it
 * can be written before the deadline; in a blocking socket EAGAIN means that SO_SNDTIMEO expired without any
 * progress, so it is not retried.
 *
 * @param[in] sock_fd the socket
 * @param[in] error the errno of the send
 * @param[in] deadline the deadline
 * @return true to retry the send
 */
CERVER_PUBLIC bool socket_send_can_retry (
	const int sock_fd, const int error, const struct timespec *deadline
);

/**
 * @brief Sends all the parts with as few system calls as the socket takes.
 *
 * It works like writev (), but with MSG_NOSIGNAL, so a reset connection returns EPIPE instead of raising SIGPIPE,
 * and with MSG_DONTWAIT, so the deadline is the limit of every send, also in a blocking socket. Several calls can
 * share the same deadline, so it limits a whole response.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in,out] parts the parts, modified to skip what was sent
 * @param[in] n_parts the number of parts
 * @param[in] flags the flags of sendmsg (), added to MSG_NOSIGNAL and MSG_DONTWAIT
 * @param[in] deadline the deadline
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, EAGAIN when the deadline expired, 0 on success; it can be NULL
 * @retval 0 every part was sent
 * @retval 1 on error
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_vector_until (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const struct timespec *deadline,
	size_t *sent, int *error
);

/**
 * @brief Works as socket_send_vector_until () with a deadline of some seconds from now.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in,out] parts the parts, modified to skip what was sent
 * @param[in] n_parts the number of parts
 * @param[in] flags the flags of sendmsg ()
 * @param[in] timeout the seconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 every part was sent
 * @retval 1 on error
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_vector (
	const Socket *socket,
	struct iovec *parts, int n_parts,
	int flags, const time_t timeout,
	size_t *sent, int *error
);

/**
 * @brief Works as socket_send_vector_until () with a single buffer.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in] buffer the bytes
 * @param[in] size the size of the buffer, 0 sends nothing
 * @param[in] flags the flags of sendmsg ()
 * @param[in] deadline the deadline
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 the buffer was sent
 * @retval 1 on error
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_buffer_until (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const struct timespec *deadline,
	size_t *sent, int *error
);

/**
 * @brief Works as socket_send_vector () with a single buffer.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in] buffer the bytes
 * @param[in] size the size of the buffer, 0 sends nothing
 * @param[in] flags the flags of sendmsg ()
 * @param[in] timeout the seconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 the buffer was sent
 * @retval 1 on error
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_buffer (
	const Socket *socket,
	const void *buffer, const size_t size,
	int flags, const time_t timeout,
	size_t *sent, int *error
);

/**
 * @brief Sends some bytes of a file with sendfile () until every byte was sent or the deadline expires.
 *
 * In a blocking socket, SO_SNDTIMEO is limited to the deadline while the file is sent and restored after it.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in] file_fd the file
 * @param[in,out] offset where the file is read from, moved by what was sent; NULL uses the current offset of the
 * file
 * @param[in] size the bytes to send
 * @param[in] deadline the deadline
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, EAGAIN when the deadline expired, 0 when the file ended before size
 * bytes; it can be NULL
 * @retval 0 every byte was sent
 * @retval 1 on error, or the file ended before size bytes
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_file_until (
	const Socket *socket,
	const int file_fd, off_t *offset, const size_t size,
	const struct timespec *deadline,
	size_t *sent, int *error
);

/**
 * @brief Works as socket_send_file_until () from the current offset of the file, with a deadline of some seconds
 * from now.
 *
 * @param[in] socket the socket, it must not be NULL
 * @param[in] file_fd the file
 * @param[in] size the bytes to send
 * @param[in] timeout the seconds, 0 uses #CERVER_DEFAULT_SEND_TIMEOUT
 * @param[out] sent every byte that was sent, it can be NULL
 * @param[out] error the errno of a failure, it can be NULL
 * @retval 0 every byte was sent
 * @retval 1 on error, or the file ended before size bytes
 * @threadsafety The caller must hold the write mutex of the socket.
 */
CERVER_PUBLIC u8 socket_send_file (
	const Socket *socket,
	const int file_fd, const size_t size,
	const time_t timeout,
	size_t *sent, int *error
);

/**
 * @brief Shuts down both ways of the socket of a send that failed because its deadline expired.
 *
 * A send that waited for space until its deadline (EAGAIN) leaves the peer with a part of what was sent, and a peer
 * that did not read in all that time is taken as gone: the thread that receives the socket ends its connection, and
 * every send after it fails at once (EPIPE) instead of waiting for its own deadline. The sends of packets, files and
 * the balancer call it after a failure.
 *
 * @param[in,out] socket the socket, it can be NULL
 * @param[in] error the errno of the send that failed
 * @return true if the send timed out and the socket was shut down
 * @threadsafety Safe with the write mutex of the socket held: it only takes its close mutex (socket_shutdown ()).
 * @note For the internal methods of cerver.
 * @since 1.9
 */
CERVER_PRIVATE bool socket_end_on_send_timeout (Socket *socket, const int error);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
