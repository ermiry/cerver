/**
 * @file cerver/auth/hold.h
 * @brief The connections on hold: the new connections of a cerver that requires authentication, until they
 * authenticate.
 */

#ifndef _CERVER_AUTH_HOLD_H_
#define _CERVER_AUTH_HOLD_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/auth/auth.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Connection;
struct _Packet;

/**
 * @defgroup auth_hold_handler Handler
 * @ingroup auth
 * @brief The packets of the connections on hold.
 * @{
 */

/**
 * @brief Handles a packet of an on hold connection.
 *
 * @param[in] packet the packet, it is always deleted
 * @retval 0 the connection is still on hold and more packets can be handled (a test packet, a bad packet below the
 * max, missing values, a failed authentication or an invalid session)
 * @retval 1 the connection was ended or removed from on hold
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 on_hold_packet_handler (
	struct _Packet *packet
);

/** @} */

/**
 * @defgroup auth_hold_connections Connections
 * @ingroup auth
 * @brief Putting a connection on hold and taking it out.
 * @{
 */

/**
 * @brief Puts a new connection on hold until it authenticates, or fails to.
 *
 * @param[in,out] cerver the cerver, it requires authentication
 * @param[in,out] connection the new connection
 * @retval 0 the connection is on hold
 * @retval 1 a NULL argument, a cerver without on hold connections, or the connection could not be registered (the
 * on hold poll is full)
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 on_hold_connection (
	struct _Cerver *cerver, struct _Connection *connection
);

/**
 * @brief Closes an on hold connection and removes it from the cerver; it triggers CERVER_EVENT_ON_HOLD_DROPPED.
 *
 * @param[in] cerver the cerver
 * @param[in] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void on_hold_connection_drop (
	const struct _Cerver *cerver, struct _Connection *connection
);

/**
 * @brief Closes an on hold connection that its client closed or lost; it triggers
 * CERVER_EVENT_ON_HOLD_DISCONNECTED.
 *
 * @param[in] cerver the cerver
 * @param[in] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void on_hold_connection_disconnected (
	const struct _Cerver *cerver, struct _Connection *connection
);

/** @} */

/**
 * @defgroup auth_hold_poll Poll
 * @ingroup auth
 * @brief The poll of the connections on hold.
 * @{
 */

/**
 * @brief Removes a socket from the on hold poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] sock_fd the socket
 * @retval 0 the socket was removed
 * @retval 1 a NULL cerver, or the socket is not in the poll
 * @threadsafety It takes the lock of the on hold poll.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 on_hold_poll_unregister_sock_fd (
	struct _Cerver *cerver, const i32 sock_fd
);

/**
 * @brief The thread of the on hold poll: handles the packets of the on hold connections until they authenticate
 * and the cerver stops.
 *
 * @param[in] cerver_ptr the cerver
 * @return NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void *on_hold_poll (void *cerver_ptr);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
