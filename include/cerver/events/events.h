/**
 * @file cerver/events/events.h
 * @brief The event loops of a cerver with CERVER_HANDLER_TYPE_EVENTS: the backend of their I/O and their threads.
 */

#ifndef _CERVER_EVENTS_EVENTS_H_
#define _CERVER_EVENTS_EVENTS_H_

#include <stdbool.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

/**
 * @defgroup events Events
 * @brief The event loops of a web cerver (CERVER_HANDLER_TYPE_EVENTS): each one is an I/O thread that accepts
 * connections, reads their requests and sends their responses, and only it uses the sockets of its connections.
 *
 * A loop asks its backend for operations that complete: with io_uring the kernel makes them, and the loop gives
 * and takes many of them in one system call; with epoll the loop waits until a socket is ready and makes the
 * system call of each operation. Both give the same results to the loop.
 *
 * @since 3.0b-11
 * @{
 */

/**
 * @brief The I/O threads that cerver_set_io_threads () takes for one loop for each processor that the process
 * can use.
 *
 * @since 3.0b-11
 */
#define CERVER_IO_THREADS_PER_CORE				(~0u)

/**
 * @brief The X-macro table of the backends of the event loops: the number, the name, the string and the
 * description of each one.
 *
 * @since 3.0b-11
 */
#define CERVER_EVENTS_BACKEND_MAP(XX)																				\
	XX(0,	AUTO, 		Auto, 		Use io_uring where the kernel and the process allow it and epoll where they do not)	\
	XX(1,	URING, 		io_uring, 	Give the operations of the connections to the kernel through io_uring)			\
	XX(2,	EPOLL, 		epoll, 		Wait for the connections with epoll and make their system calls)

/**
 * @brief The backend of the event loops of a cerver, one `CERVER_EVENTS_BACKEND_<name>` for each row of
 * #CERVER_EVENTS_BACKEND_MAP.
 *
 * @since 3.0b-11
 */
typedef enum CerverEventsBackend {

	#define XX(num, name, string, description) CERVER_EVENTS_BACKEND_##name = num,
	CERVER_EVENTS_BACKEND_MAP (XX)
	#undef XX

} CerverEventsBackend;

/**
 * @brief Returns the string of a backend of the event loops.
 *
 * @param[in] backend the backend
 * @return the string of its row in #CERVER_EVENTS_BACKEND_MAP, or the one of CERVER_EVENTS_BACKEND_AUTO for a
 * value out of the table; a static string that must not be freed
 * @since 3.0b-11
 */
CERVER_EXPORT const char *cerver_events_backend_to_string (
	CerverEventsBackend backend
);

/**
 * @brief Returns the description of a backend of the event loops.
 *
 * @param[in] backend the backend
 * @return the description of its row in #CERVER_EVENTS_BACKEND_MAP, or the one of CERVER_EVENTS_BACKEND_AUTO for
 * a value out of the table; a static string that must not be freed
 * @since 3.0b-11
 */
CERVER_EXPORT const char *cerver_events_backend_description (
	CerverEventsBackend backend
);

/**
 * @brief Sets the backend of the event loops of a cerver.
 *
 * With CERVER_EVENTS_BACKEND_AUTO (the default) the loops use io_uring, and epoll where io_uring is not
 * available: a kernel older than Linux 6.1, or a process that is not allowed to use it (the default seccomp
 * profile of a container blocks its system calls). The cerver logs a warning with the reason when it uses epoll.
 *
 * @param[in,out] cerver the cerver
 * @param[in] backend the backend; a value out of #CERVER_EVENTS_BACKEND_MAP is ignored
 * @note Call it before cerver_start (). The other handler types ignore it.
 * @warning With CERVER_EVENTS_BACKEND_URING, cerver_start () fails where io_uring is not available.
 * @since 3.0b-11
 */
CERVER_EXPORT void cerver_set_events_backend (
	struct _Cerver *cerver, CerverEventsBackend backend
);

/**
 * @brief Returns the backend of the event loops of a cerver.
 *
 * @param[in] cerver the cerver
 * @return the backend that its loops use once the cerver started with CERVER_HANDLER_TYPE_EVENTS
 * (CERVER_EVENTS_BACKEND_URING or CERVER_EVENTS_BACKEND_EPOLL); before that, the one that was set;
 * CERVER_EVENTS_BACKEND_AUTO for a NULL cerver
 * @since 3.0b-11
 */
CERVER_EXPORT CerverEventsBackend cerver_get_events_backend (
	const struct _Cerver *cerver
);

/**
 * @brief Sets whether each I/O thread of a cerver stays in one processor: the thread of the loop N runs in the
 * processor N of the ones that the process can use, so a loop and the memory of its connections never move.
 *
 * @param[in,out] cerver the cerver
 * @param[in] affinity true to set the processor of each I/O thread; false (the default) leaves them to the
 * scheduler
 * @note Call it before cerver_start (). The other handler types ignore it.
 * @since 3.0b-11
 */
CERVER_EXPORT void cerver_set_io_threads_affinity (
	struct _Cerver *cerver, bool affinity
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
