/**
 * @file cerver/connection/data.h
 * @brief The credentials of a connection and the token of its datagrams.
 */

#ifndef _CERVER_CONNECTION_DATA_H_
#define _CERVER_CONNECTION_DATA_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_data Data
 * @ingroup connection
 * @brief The credentials that a connection sends when the cerver requires authentication, and the token of an
 * authenticated UDP connection.
 * @{
 */

/**
 * @brief Sets the credentials that the connection sends whenever the cerver requires authentication.
 *
 * The credentials that the connection had are removed first.
 *
 * @param[in,out] connection the connection
 * @param[in] auth_data the credentials, NULL keeps the current ones
 * @param[in] auth_data_size the size of the credentials
 * @param[in] delete_auth_data releases the credentials once the connection has ended, NULL to keep them
 * @param[in] admin_auth true to authenticate as an admin
 */
CERVER_PUBLIC void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
	bool admin_auth
);

/**
 * @brief Sets the credentials as an object of the auth schema of the cerver (cerver_set_auth_schema ()).
 *
 * The object is encoded now, so it can be released after the call.
 *
 * @param[in,out] connection the connection
 * @param[in] schema the schema of the object
 * @param[in] object the object
 * @param[in] admin_auth true to authenticate as an admin
 * @retval 0 the credentials were set
 * @retval 1 a NULL argument or the object can not be encoded
 */
CERVER_PUBLIC u8 connection_set_auth_object (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	bool admin_auth
);

/**
 * @brief Sets the session token that a cerver with sessions gave to the client, so the connection authenticates
 * with it instead of the credentials.
 *
 * @param[in,out] connection the connection
 * @param[in] token the token, shorter than TOKEN_SIZE
 * @param[in] admin_auth true to authenticate as an admin
 * @retval 0 the token was set
 * @retval 1 a NULL argument, a token that is too long, or it can not be encoded
 */
CERVER_PUBLIC u8 connection_set_auth_token (
	Connection *connection, const char *token,
	bool admin_auth
);

/**
 * @brief Removes the credentials of the connection and its auth packet.
 *
 * The credentials are released with the delete method of the connection, if it has one; then the auth data and
 * its delete method are NULL.
 *
 * @param[in,out] connection the connection
 */
CERVER_PUBLIC void connection_remove_auth_data (
	Connection *connection
);

/**
 * @brief Generates the auth packet that the connection sends to the cerver, from its credentials.
 *
 * It is also generated automatically whenever the cerver asks for authentication.
 *
 * @param[in,out] connection the connection
 * @retval 0 the packet was generated
 * @retval 1 a NULL connection, one without credentials, or the packet could not be generated
 */
CERVER_PUBLIC u8 connection_generate_auth_packet (
	Connection *connection
);

/**
 * @brief Sets the token that an authenticated UDP connection sends after the packet in every datagram.
 *
 * @param[in,out] connection the connection
 * @param[in] token the #CONNECTION_DATAGRAM_TOKEN_SIZE bytes of the token
 * @threadsafety The flag is set with __atomic after the token, so a sender reads it complete.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_set_datagram_token (
	Connection *connection, const u8 *token
);

/**
 * @brief Copies the datagram token of the connection.
 *
 * @param[in] connection the connection
 * @param[out] token the #CONNECTION_DATAGRAM_TOKEN_SIZE bytes where the token is copied
 * @return true if the connection has a token
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool connection_get_datagram_token (
	const Connection *connection, u8 *token
);

/**
 * @brief The connection sends its datagrams without a token again.
 *
 * @param[in,out] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void connection_clear_datagram_token (Connection *connection);

/**
 * @brief Returns whether the connection sends a token in its datagrams (it authenticated).
 *
 * @param[in] connection the connection
 * @return true if it has a token, false for a NULL connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool connection_has_datagram_token (const Connection *connection);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
