/**
 * @file cerver/auth/sessions.h
 * @brief The sessions of the clients: the generator of their ids and the token that a client sends to
 * authenticate again.
 */

#ifndef _CERVER_SESSIONS_H_
#define _CERVER_SESSIONS_H_

#include "cerver/client/client.h"
#include "cerver/packets/packets.h"
#include "cerver/packets/serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _AuthData;

/**
 * @defgroup auth_sessions Sessions
 * @ingroup auth
 * @brief The ids of the sessions, enabled with cerver_set_sessions ().
 * @{
 */

/**
 * @brief What the session id generator of the cerver gets.
 */
typedef struct SessionData {

    /** @brief The authentication packet. */
    Packet *packet;
    /** @brief The authentication data taken from the packet. */
    struct _AuthData *auth_data;
    /** @brief The new client of the session. */
    Client *client;

} SessionData;

/**
 * @brief Allocates what the session id generator gets.
 *
 * @param[in] packet the authentication packet
 * @param[in] auth_data the authentication data
 * @param[in] client the new client
 * @return the new data, released with session_data_delete (); NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE SessionData *session_data_new (
    Packet *packet, struct _AuthData *auth_data, Client *client
);

/**
 * @brief Releases what the session id generator gets, not its references.
 *
 * @param[in] ptr the data, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void session_data_delete (void *ptr);

/** @brief The random bytes of a default session id. */
#define SESSION_ID_RANDOM_BYTES			32

/** @brief The length of a default session id, in hex characters. */
#define SESSION_ID_LENGTH				(SESSION_ID_RANDOM_BYTES * 2)

/**
 * @brief The default session id generator: #SESSION_ID_RANDOM_BYTES random bytes of the kernel, as
 * #SESSION_ID_LENGTH hex characters, that can not be guessed from the time it was created.
 *
 * @param[in] session_data a SessionData
 * @return the session id, a string allocated with malloc (); NULL for a NULL argument, when the kernel did not give
 * the random bytes or if it could not be allocated
 * @note For the internal methods of cerver: cerver_set_sessions () with NULL uses it.
 */
CERVER_PRIVATE void *session_default_generate_id (
    const void *session_data
);

/** @} */

/**
 * @defgroup auth_sessions_serialization Serialization
 * @ingroup auth
 * @brief The token of a session, encoded with the serializer.
 * @{
 */

/** @brief The size of the token of a session, with its terminator. */
#define TOKEN_SIZE         256

/** @brief The bytes of the map, the version and the key and the length of the token. */
#define SESSION_TOKEN_ENCODING_OVERHEAD		16

/** @brief The biggest encoded session token. */
#define SESSION_TOKEN_MAX_ENCODED_SIZE		(TOKEN_SIZE + SESSION_TOKEN_ENCODING_OVERHEAD)

/** @brief The version of the schema of the session token. */
#define SESSION_TOKEN_VERSION				1

/**
 * @brief The X-macro table of the fields of the session token: the session id that a cerver gives to a client,
 * and that the client sends to authenticate again.
 *
 * SERIALIZER_DECLARE () makes the struct SessionToken and the schema session_token_schema from it.
 */
#define SESSION_TOKEN_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	STRING(T, 1, TEXT, token, TOKEN_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (SessionToken, session_token, SESSION_TOKEN_FIELDS)

/**
 * @brief Encodes the session token of a session id.
 *
 * @param[in] session_id the session id, truncated to TOKEN_SIZE - 1 characters
 * @param[out] buffer where the token is encoded
 * @param[out] written the size of the encoded token
 * @retval 0 the token was encoded
 * @retval 1 it could not be encoded
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 session_token_encode (
	const char *session_id,
	u8 buffer[SESSION_TOKEN_MAX_ENCODED_SIZE], size_t *written
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
