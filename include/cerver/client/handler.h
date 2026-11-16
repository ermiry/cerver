/**
 * @file cerver/client/handler.h
 * @brief How a client receives and handles the packets of its connections.
 */

#ifndef _CERVER_CLIENT_HANDLER_H_
#define _CERVER_CLIENT_HANDLER_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup client_handler Handler
 * @ingroup client
 * @brief Receiving the data of a connection of a client and handling its packets.
 * @{
 */

/**
 * @brief The X-macro table of the results of handling a packet in a client: the number, the name, the string and
 * the description of each one.
 */
#define CLIENT_HANDLER_ERROR_MAP(XX)										\
	XX(0,	NONE,		None,				No handler error)				\
	XX(1,	PACKET,		Bad Packet,			Packet check failed)			\
	XX(2,	CLOSED,		Closed Connection, 	The connection has been ended)

/**
 * @brief The result of handling a packet in a client, one `CLIENT_HANDLER_ERROR_<name>` for each row of
 * #CLIENT_HANDLER_ERROR_MAP.
 */
typedef enum ClientHandlerError {

	#define XX(num, name, string, description) CLIENT_HANDLER_ERROR_##name = num,
	CLIENT_HANDLER_ERROR_MAP (XX)
	#undef XX

} ClientHandlerError;

/**
 * @brief Returns the string of a handler result.
 *
 * @param[in] error the result
 * @return the string of its row in #CLIENT_HANDLER_ERROR_MAP, or the one of CLIENT_HANDLER_ERROR_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *client_handler_error_to_string (
	const ClientHandlerError error
);

/**
 * @brief Returns the description of a handler result.
 *
 * @param[in] error the result
 * @return the description of its row in #CLIENT_HANDLER_ERROR_MAP, or the one of CLIENT_HANDLER_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *client_handler_error_description (
	const ClientHandlerError error
);

/**
 * @brief Receives once from the socket of the connection into a buffer and handles the data: the packets of a TCP
 * connection, the datagram of an UDP one.
 *
 * It does not check its arguments.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @param[in] buffer the buffer
 * @param[in] buffer_size the size of the buffer
 * @retval 0 the data was handled, or the receive timed out
 * @retval 1 the receive failed or the connection was closed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int client_receive_internal (
	Client *client, struct _Connection *connection,
	char *buffer, const size_t buffer_size
);

/**
 * @brief Allocates a buffer of the receive buffer size of the connection and receives once into it, like
 * client_receive_internal ().
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the data was handled, or the receive timed out
 * @retval 1 a NULL argument, the buffer could not be allocated, or the receive failed (most likely the connection
 * was ended)
 */
CERVER_PUBLIC unsigned int client_receive (
	Client *client, struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
