/**
 * @file handler/internal.h
 * @internal
 * @brief The methods of the handler that other parts of cerver use, never exported by libcerver.so.
 */

#ifndef _CERVER_HANDLER_INTERNAL_H_
#define _CERVER_HANDLER_INTERNAL_H_

#include <stdbool.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Opens the fd that the process keeps in reserve, when it is not open: with it a connection can still be
 * accepted and closed when the process runs out of fds, instead of staying in the listen backlog.
 *
 * @threadsafety Safe from any thread, the reserve is one for the process.
 */
CERVER_HIDDEN void cerver_accept_reserve_fd_open (void);

/**
 * @internal
 * @brief Rejects the connection that waits in a listening socket of a process without fds: it closes the reserve
 * fd, accepts the connection with the fd that it leaves, closes it and opens the reserve again.
 *
 * @param[in] listen_fd the listening socket, that never waits when no connection does
 * @return true when a connection waited and it was rejected; a process without fds gets EMFILE from accept ()
 * also when no connection waits
 * @threadsafety Safe from any thread.
 */
CERVER_HIDDEN bool cerver_accept_reserve_fd_reject (const int listen_fd);

#ifdef __cplusplus
}
#endif

#endif
