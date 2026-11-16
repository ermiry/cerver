/**
 * @file auth/internal.h
 * @internal
 * @brief The methods that the authentication and the connections on hold share, never exported by libcerver.so.
 */

#ifndef _CERVER_AUTH_INTERNAL_H_
#define _CERVER_AUTH_INTERNAL_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/auth/auth.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Removes a connection from the on hold poll, the on hold tree and the on hold map, without closing it.
 *
 * @param[in] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was removed
 * @retval 1 a NULL argument, or it was not in the on hold poll
 */
CERVER_HIDDEN u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection);

/**
 * @internal
 * @brief Adds the socket of a connection to a free slot of the on hold poll.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the socket was added
 * @retval 1 a NULL argument, or the on hold poll is full
 * @threadsafety It takes the lock of the on hold poll.
 */
CERVER_HIDDEN u8 on_hold_poll_register_connection (Cerver *cerver, Connection *connection);

/**
 * @internal
 * @brief Removes the socket of a connection from the on hold poll, see on_hold_poll_unregister_sock_fd ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the socket was removed
 * @retval 1 a NULL argument, or the socket is not in the poll
 */
CERVER_HIDDEN u8 on_hold_poll_unregister_connection (Cerver *cerver, Connection *connection);

/**
 * @internal
 * @brief Authenticates an on hold connection as a client with the authenticate method of the cerver.
 *
 * On success, the connection leaves on hold, its client is registered to the cerver, it gets the success packet
 * and CERVER_EVENT_CLIENT_SUCCESS_AUTH is triggered.
 *
 * @param[in] packet the authentication packet, with its cerver and its connection
 * @return the result of the authentication
 */
CERVER_HIDDEN CerverAuthError auth_try (Packet *packet);

/**
 * @internal
 * @brief Authenticates an on hold connection as an admin with the authenticate method of the admin cerver.
 *
 * On success, the connection leaves on hold, its admin is registered to the admin cerver, it gets the success
 * packet and CERVER_EVENT_ADMIN_CONNECTED is triggered.
 *
 * @param[in] packet the authentication packet, with its cerver and its connection
 * @return the result of the authentication
 */
CERVER_HIDDEN CerverAuthError admin_auth_try (Packet *packet);

#ifdef __cplusplus
}
#endif

#endif
