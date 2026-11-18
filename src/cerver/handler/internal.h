/**
 * @file handler/internal.h
 * @internal
 * @brief What the events and the errors of the cerver share, and the methods of the handler that other parts of
 * cerver use, never exported by libcerver.so.
 */

#ifndef _CERVER_HANDLER_INTERNAL_H_
#define _CERVER_HANDLER_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief The arguments of an action, shared by its event and by the data of each trigger: the last one that releases
 * them deletes them, so an event that is replaced or unregistered while its action runs in a thread does not delete
 * the arguments that the action uses.
 */
struct _CerverActionArgs {

	/** @internal @brief The arguments of the action. */
	void *args;
	/** @internal @brief Releases the arguments, NULL to keep them. */
	Action delete_args;
	/** @internal @brief The event and the data of its triggers that point to them, read with __atomic. */
	unsigned int references;

};

/** @internal @brief The shared arguments of an action, see struct _CerverActionArgs. */
typedef struct _CerverActionArgs CerverActionArgs;

/**
 * @internal
 * @brief Creates the shared arguments of an action with one reference, the one of its event.
 *
 * @param[in] args the arguments, NULL creates nothing
 * @param[in] delete_args releases the arguments, it can be NULL
 * @return the shared arguments, NULL for NULL arguments or without memory
 */
CERVER_HIDDEN CerverActionArgs *cerver_action_args_create (void *args, Action delete_args);

/**
 * @internal
 * @brief Takes a reference to the shared arguments of an action.
 *
 * @param[in,out] action_args the shared arguments, it can be NULL
 * @return the same shared arguments
 */
CERVER_HIDDEN CerverActionArgs *cerver_action_args_reference (CerverActionArgs *action_args);

/**
 * @internal
 * @brief Releases a reference to the shared arguments of an action; the last one deletes the arguments.
 *
 * @param[in,out] action_args the shared arguments, it can be NULL
 */
CERVER_HIDDEN void cerver_action_args_release (CerverActionArgs *action_args);

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

struct _Cerver;
struct _Connection;

/**
 * @internal
 * @brief Registers a new connection that is ready, by the type of its cerver; it is dropped when it fails.
 *
 * The accept calls it for a plain connection, and the pool of the handshakes of TLS after the handshake.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the new connection
 * @since 2.1b-22
 */
CERVER_HIDDEN void cerver_register_new_connection_ready (struct _Cerver *cerver, struct _Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
