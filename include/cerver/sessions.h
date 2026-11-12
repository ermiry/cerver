#ifndef _CERVER_SESSIONS_H_
#define _CERVER_SESSIONS_H_

#include "cerver/client.h"
#include "cerver/packets.h"
#include "cerver/serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _AuthData;

// auxiliary struct that is passed to cerver session id generator
typedef struct SessionData {

    Packet *packet;
    struct _AuthData *auth_data;
    Client *client;

} SessionData;

CERVER_PRIVATE SessionData *session_data_new (
    Packet *packet, struct _AuthData *auth_data, Client *client
);

CERVER_PRIVATE void session_data_delete (void *ptr);

// the random bytes of a default session id, and its length in hex characters
#define SESSION_ID_RANDOM_BYTES			32
#define SESSION_ID_LENGTH				(SESSION_ID_RANDOM_BYTES * 2)

// creates a session id of random bytes of the kernel, in hex characters,
// that can not be guessed from the time it was created
CERVER_PRIVATE void *session_default_generate_id (
    const void *session_data
);

#pragma region serialization

#define TOKEN_SIZE         256

// the bytes of the map, the version and the key and the length of the token
#define SESSION_TOKEN_ENCODING_OVERHEAD		16

// the biggest encoded session token
#define SESSION_TOKEN_MAX_ENCODED_SIZE		(TOKEN_SIZE + SESSION_TOKEN_ENCODING_OVERHEAD)

#define SESSION_TOKEN_VERSION				1

// the session id that a cerver gives to a client, and that the client sends to authenticate again
#define SESSION_TOKEN_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	STRING(T, 1, TEXT, token, TOKEN_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (SessionToken, session_token, SESSION_TOKEN_FIELDS)

// encodes the session token of the session id in the buffer
// written takes the size of the encoded token
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 session_token_encode (
	const char *session_id,
	u8 buffer[SESSION_TOKEN_MAX_ENCODED_SIZE], size_t *written
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
