/**
 * @file cerver/auth/auth.h
 * @brief The authentication of the connections of a cerver: its errors, its data and the method of the
 * application.
 */

#ifndef _CERVER_AUTH_AUTH_H_
#define _CERVER_AUTH_AUTH_H_

#include <stdlib.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/packets/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Connection;

struct _Packet;

/**
 * @defgroup auth Auth
 * @brief The authentication of the new connections of a cerver, the connections on hold and the sessions.
 *
 * A cerver enables it with cerver_set_auth (): each new connection waits on hold until it authenticates with the
 * method of the application, or with the token of its session.
 */

/**
 * @defgroup auth_errors Errors
 * @ingroup auth
 * @brief The results of an authentication.
 * @{
 */

/**
 * @brief The X-macro table of the results of an authentication: the number, the name, the string and the
 * description of each one.
 */
#define CERVER_AUTH_ERROR_MAP(XX)															\
	XX(0,	NONE,				None,				No authentication error)				\
	XX(1,	SUCCESS,			Success Auth,		Authentication has been successful)		\
	XX(2,	NO_HANDLER,			No Handler,			The cerver has no auth handler)			\
	XX(3,	MISSING_VALUES,		Missing Values,		Bad auth request due to missing values) \
	XX(4,	FAILED,				Failed Auth,		Invalid credentials)					\
	XX(5,	INVALID_SESSION,	Bad Session ID,		Session id is invalid)					\
	XX(6,	DROPPED,			Dropped Connection, The connection has been ended)

/**
 * @brief The result of an authentication, one `CERVER_AUTH_ERROR_<name>` for each row of #CERVER_AUTH_ERROR_MAP.
 */
typedef enum CerverAuthError {

	#define XX(num, name, string, description) CERVER_AUTH_ERROR_##name = num,
	CERVER_AUTH_ERROR_MAP (XX)
	#undef XX

} CerverAuthError;

/**
 * @brief Returns the string of an authentication result.
 *
 * @param[in] error the result
 * @return the string of its row in #CERVER_AUTH_ERROR_MAP, or the one of CERVER_AUTH_ERROR_NONE for a value out of
 * the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_auth_error_to_string (
	const CerverAuthError error
);

/**
 * @brief Returns the description of an authentication result.
 *
 * @param[in] error the result
 * @return the description of its row in #CERVER_AUTH_ERROR_MAP, or the one of CERVER_AUTH_ERROR_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_auth_error_description (
	const CerverAuthError error
);

/** @} */

/**
 * @defgroup auth_data Data
 * @ingroup auth
 * @brief The data of an authentication packet.
 * @{
 */

/**
 * @brief The authentication data that was taken from a packet.
 */
struct _AuthData {

	/** @brief The token of a session, NULL when the packet has credentials. */
	String *token;

	/** @brief The credentials, the bytes of the packet or the object of the auth schema of the cerver. */
	void *auth_data;
	/** @brief The size in bytes of the credentials. */
	size_t auth_data_size;

	/**
	 * @brief Data that the authenticate method can recover, like the user of the credentials.
	 *
	 * After an authentication that creates a client, it becomes the data of the client (client_set_data ());
	 * otherwise it is released with #delete_data.
	 */
	void *data;
	/** @brief Releases #data. */
	Action delete_data;

};

/** @brief The authentication data of a packet, see struct _AuthData. */
typedef struct _AuthData AuthData;

/** @} */

/**
 * @defgroup auth_method Method
 * @ingroup auth
 * @brief What the authenticate method of the application gets, and the steps of an authentication.
 * @{
 */

/**
 * @brief What the authenticate method of the application gets.
 */
struct _AuthMethod {

	/** @brief The authentication packet. */
	struct _Packet *packet;
	/** @brief The authentication data taken from the packet. */
	AuthData *auth_data;

	/**
	 * @brief A message of the application that is sent to the connection, in a CERVER_ERROR_FAILED_AUTH packet,
	 * when the authentication fails.
	 */
	String *error_message;

};

/** @brief What the authenticate method gets, see struct _AuthMethod. */
typedef struct _AuthMethod AuthMethod;

/**
 * @brief Takes the authentication data of an authentication packet: the token of a session, or the credentials
 * for the authenticate method.
 *
 * @param[in] packet the packet
 * @return the new data, released with auth_data_release (); NULL for a NULL packet, one without data, a token that
 * is not a SessionToken or credentials that are not an object of the auth schema of the cerver
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE AuthData *auth_data_get (struct _Packet *packet);

/**
 * @brief Releases the authentication data.
 *
 * @param[in] auth_data the data, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void auth_data_release (AuthData *auth_data);

/**
 * @brief Runs the authenticate method with the authentication data, without the on hold structures.
 *
 * @param[in] packet the authentication packet
 * @param[in] authenticate the method, it returns 0 when it accepts the data
 * @param[in] auth_data the authentication data
 * @param[out] error_message when the method fails, its error message (if it has one) is moved here, and the
 * caller releases it; it can be NULL
 * @retval 0 the method accepted the authentication data
 * @retval 1 the method did not accept it, or its argument could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 auth_authenticate (
	struct _Packet *packet, delegate authenticate, AuthData *auth_data,
	String **error_message
);

/**
 * @brief Generates the session id of a new client with the session id generator of the cerver.
 *
 * Any new connection that authenticates with the session id (the token) is added to the client.
 *
 * @param[in] packet the authentication packet, with its cerver
 * @param[in] auth_data the authentication data
 * @param[in,out] client the client, it keeps its own copy of the session id
 * @retval 0 the session id was set
 * @retval 1 it could not be generated or set
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 auth_session_create (
	struct _Packet *packet, AuthData *auth_data, struct _Client *client
);

/**
 * @brief Sends a CERVER_ERROR_FAILED_AUTH packet with a message to a connection.
 *
 * @param[in] cerver the cerver
 * @param[in] connection the connection
 * @param[in] error_message the message
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void auth_send_failed_packet (
	struct _Cerver *cerver, struct _Connection *connection, const char *error_message
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
