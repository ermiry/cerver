/**
 * @file cerver/connection/end.h
 * @brief How a connection stops, is reset, ends and is dropped.
 */

#ifndef _CERVER_CONNECTION_END_H_
#define _CERVER_CONNECTION_END_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_end End
 * @ingroup connection
 * @brief Ending a connection.
 * @{
 */

/**
 * @brief Stops and joins the send thread of the connection, and deletes the packets that are still queued.
 *
 * @param[in,out] connection the connection, it must not be active
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_stop_send (Connection *connection);

/**
 * @brief Resets the values of the connection so it can connect again: a new socket and the address of the
 * cerver.
 *
 * @param[in,out] connection the connection, it must not have an update thread
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_reset (Connection *connection);

/**
 * @brief Ends an active connection: closes its socket and sets it as disconnected.
 *
 * @param[in,out] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_end (Connection *connection);

/**
 * @brief Ends and deletes a connection.
 *
 * @param[in] cerver not used
 * @param[in] connection the connection, invalid after the call
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_drop (
	struct _Cerver *cerver, Connection *connection
);

/**
 * @brief Asks the cerver to drop the connection; it can be called from any packet handler.
 *
 * The socket is shut down (the packets that were already sent still arrive), and the receive loop ends and
 * removes the connection when the handler returns, like when the client closes it.
 *
 * @param[in,out] connection the connection, it must not be used after the call
 * @retval 0 the socket was shut down
 * @retval 1 a NULL connection, one without a socket, or the shutdown failed
 */
CERVER_EXPORT u8 connection_drop_request (Connection *connection);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
