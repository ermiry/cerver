/**
 * @file connection/internal.h
 * @internal
 * @brief The methods that the parts of the connections share, never exported by libcerver.so.
 */

#ifndef _CERVER_CONNECTION_INTERNAL_H_
#define _CERVER_CONNECTION_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Sets the stats in zero, with their packets per type in their own storage.
 *
 * @param[out] stats the stats
 */
CERVER_HIDDEN void connection_stats_init (ConnectionStats *stats);

/**
 * @internal
 * @brief Releases stats that were allocated, not the ones embedded in the connection.
 *
 * @param[in] connection the connection
 * @param[in] stats the stats, they can be NULL
 */
CERVER_HIDDEN void connection_stats_delete (
	Connection *connection, ConnectionStats *stats
);

/**
 * @internal
 * @brief Takes a deleted connection to reuse it, or allocates a new one with its embedded mutexes and condition.
 *
 * @return the connection, NULL if it could not be allocated
 */
CERVER_HIDDEN Connection *connection_allocate (void);

/**
 * @internal
 * @brief Keeps a deleted connection to be reused, or destroys it when there are #CONNECTION_RECYCLE_LIMIT.
 *
 * @param[in] connection the connection
 */
CERVER_HIDDEN void connection_recycle (Connection *connection);

/**
 * @internal
 * @brief Deletes the mutexes and the condition of the connection that are not the embedded ones.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void connection_synchronization_delete (Connection *connection);

/**
 * @internal
 * @brief Tries once every resolved address of the cerver until one connects.
 *
 * @param[in,out] connection the connection
 * @param[in] fresh true in the first round, when the socket of connection_init () can be used
 * @retval 0 an address connected
 * @retval 1 every address failed, or the connection was ended
 */
CERVER_HIDDEN u8 connection_connect_round (Connection *connection, const bool fresh);

/**
 * @internal
 * @brief The send thread of the connection: it sends and deletes the packets of the queue until it is stopped.
 *
 * @param[in] connection_ptr the connection
 * @return NULL
 */
CERVER_HIDDEN void *connection_send_thread (void *connection_ptr);

/**
 * @internal
 * @brief Returns whether the connection is active, read with __atomic.
 *
 * @param[in] connection the connection
 * @return true while the connection is active
 * @threadsafety Safe from any thread: a client teardown and the reconnection thread read it at the same time.
 */
CERVER_HIDDEN bool connection_is_active (const Connection *connection);

/**
 * @internal
 * @brief Sets whether the connection is active, written with __atomic.
 *
 * @param[in,out] connection the connection
 * @param[in] active the new value
 */
CERVER_HIDDEN void connection_set_active (Connection *connection, const bool active);

/**
 * @internal
 * @brief Takes the end of an active connection: it sets it inactive and returns whether it was active.
 *
 * @param[in,out] connection the connection
 * @return true only for the one thread that took it, the one that ends the connection
 * @threadsafety Safe from any thread: two threads that end the same connection never both end it.
 */
CERVER_HIDDEN bool connection_take_end (Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
