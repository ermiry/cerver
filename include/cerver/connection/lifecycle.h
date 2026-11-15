/**
 * @file cerver/connection/lifecycle.h
 * @brief How a connection is initialized, connects to a cerver, reconnects and starts.
 */

#ifndef _CERVER_CONNECTION_LIFECYCLE_H_
#define _CERVER_CONNECTION_LIFECYCLE_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_lifecycle Lifecycle
 * @ingroup connection
 * @brief Connecting a connection to a cerver.
 * @{
 */

/**
 * @brief Sets up a connection that is not active: resolves the address of the cerver and creates a new socket,
 * closing the one of an earlier init.
 *
 * @param[in,out] connection the connection
 * @retval 0 the connection was set up
 * @retval 1 a NULL or active connection, an unknown protocol, an address that can not be resolved, or the socket
 * could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_init (Connection *connection);

/**
 * @brief Connects the connection to the ip and the port of the cerver.
 *
 * Each round tries every resolved address; between the rounds it waits with an exponential backoff (see
 * connection_set_max_sleep ()) that the end of the connection or connection_wake_connect () interrupts. The state
 * is CONNECTION_STATE_READY on success, CONNECTION_STATE_UNAVAILABLE on failure.
 *
 * @param[in,out] connection the connection
 * @retval 0 the connection is connected
 * @retval 1 a NULL connection, or it could not connect
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE int connection_connect (Connection *connection);

/**
 * @brief Asks the update thread of a connection with attempt reconnect to close its socket and to connect again.
 *
 * @param[in,out] connection the connection
 * @retval 0 the update thread was woken up
 * @retval 1 a NULL connection, one without attempt reconnect or its mutex, or one without an update thread or that
 * is ending
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int connection_reconnect (Connection *connection);

/**
 * @brief Starts the threads of a connection that is connected, like the one of the send queue, and sets it as
 * CONNECTION_STATE_WORKING.
 *
 * @param[in,out] connection the connection
 * @retval 0 the connection was started
 * @retval 1 a NULL or inactive connection, or its send thread could not start
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int connection_start (Connection *connection);

/**
 * @brief Wakes up a connection that waits in the backoff of connection_connect (); it stops waiting if its client
 * was torn down.
 *
 * @param[in,out] connection the connection, one without its mutex or its condition is ignored
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_wake_connect (Connection *connection);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
