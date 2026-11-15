/**
 * @file cerver/connection/registry.h
 * @brief The lookups of the connections, and how they are registered to their client and to the cerver.
 */

#ifndef _CERVER_CONNECTION_REGISTRY_H_
#define _CERVER_CONNECTION_REGISTRY_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_registry Registry
 * @ingroup connection
 * @brief Where the connections of a cerver are found and registered.
 * @{
 */

/**
 * @brief Gets an on hold connection of the cerver by its socket.
 *
 * @param[in] cerver the cerver
 * @param[in] sock_fd the fd of the socket
 * @return the connection, NULL if the cerver does not have it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_on_hold (
	struct _Cerver *cerver, const i32 sock_fd
);

/**
 * @brief Gets a connection of the client by its socket.
 *
 * @param[in] client the client
 * @param[in] sock_fd the fd of the socket
 * @return the connection, NULL if the client does not have it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_client (
	struct _Client *client, const i32 sock_fd
);

/**
 * @brief Gets a connection of the admins of the admin cerver by its socket.
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in] sock_fd the fd of the socket
 * @return the connection, NULL if no admin has it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_admin (
	struct _AdminCerver *admin_cerver, const i32 sock_fd
);

/**
 * @brief Checks if the connection belongs to the client, by the fd of its socket.
 *
 * @param[in] client the client
 * @param[in] connection the connection
 * @return true if a connection of the client has the same fd, false for a NULL argument
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool connection_check_owner (
	struct _Client *client, Connection *connection
);

/**
 * @brief Registers a connection to a client, without adding it to the poll of the cerver.
 *
 * @param[in,out] client the client, it owns the connection from now on
 * @param[in,out] connection the connection
 * @retval 0 the connection was registered
 * @retval 1 a NULL argument or it could not be added to the list of the client
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_register_to_client (
	struct _Client *client, Connection *connection
);

/**
 * @brief Maps the socket of the connection to its client in the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @retval 0 the connection was registered
 * @retval 1 a NULL argument or it could not be mapped
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_register_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client, Connection *connection
);

/**
 * @brief Removes the socket of the connection from the map of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was unregistered
 * @retval 1 a NULL argument or the cerver did not have it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_unregister_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

/**
 * @brief Adds the socket of the connection to the poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was added
 * @retval 1 a NULL argument or it could not be added
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_register_to_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

/**
 * @brief Removes the socket of the connection from the poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was removed
 * @retval 1 a NULL argument or it could not be removed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_unregister_from_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

/**
 * @brief Adds the connection to the poll of the cerver and then, on success, to its map: like
 * connection_register_to_cerver_poll () and connection_register_to_cerver ().
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @retval 0 the connection was added
 * @retval 1 a NULL argument or one of the steps failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_add_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client,
	Connection *connection
);

/**
 * @brief Removes the connection from the poll of the cerver (only with CERVER_HANDLER_TYPE_POLL) and from its
 * map.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was removed, or an argument is NULL
 * @retval 1 one of the steps failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 connection_remove_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
