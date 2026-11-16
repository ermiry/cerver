/**
 * @file cerver/connection/update.h
 * @brief The update thread of a connection, that receives its packets.
 */

#ifndef _CERVER_CONNECTION_UPDATE_H_
#define _CERVER_CONNECTION_UPDATE_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_update Update
 * @ingroup connection
 * @brief Receiving the packets of a connection in its own thread.
 * @{
 */

/**
 * @brief The update thread of a connection: it receives and handles the data of its socket until the connection
 * ends, and connects again when it has attempt reconnect.
 *
 * @param[in] ptr a ClientConnection with the client and the connection, released by the thread
 * @return NULL
 * @note For the internal methods of cerver: the connection's updating value must be set before the thread is
 * created.
 */
CERVER_PRIVATE void *connection_update (void *ptr);

/**
 * @brief Returns whether the current thread is the update thread of the connection, like a packet handler with
 * direct handle that runs in it.
 *
 * @param[in] connection the connection
 * @return true in its update thread, false for a NULL connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool connection_is_update_thread (
	const Connection *connection
);

/**
 * @brief Wakes up the update thread of the connection that is blocked in recv (), or that waits to reconnect.
 *
 * The socket is shut down but it is only closed by connection_end (), so its fd can not be reused while the
 * update thread uses it.
 *
 * @param[in,out] connection the connection, its mutex must be held
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_wake_update (Connection *connection);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
