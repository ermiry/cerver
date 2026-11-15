#ifndef _CERVER_CONNECTION_INTERNAL_H_
#define _CERVER_CONNECTION_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// the methods that the parts of the connections share, never exported

CERVER_HIDDEN void connection_stats_init (ConnectionStats *stats);

CERVER_HIDDEN void connection_stats_delete (
	Connection *connection, ConnectionStats *stats
);

CERVER_HIDDEN Connection *connection_allocate (void);

CERVER_HIDDEN void connection_recycle (Connection *connection);

CERVER_HIDDEN void connection_synchronization_delete (Connection *connection);

CERVER_HIDDEN u8 connection_connect_round (Connection *connection, const bool fresh);

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
