#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/packets/packets.h"

#include "cerver/crypto/random.h"

#include "cerver/utils/sha256.h"
#include "cerver/utils/utils.h"

SessionData *session_data_new (
	Packet *packet, AuthData *auth_data, Client *client
) {

	SessionData *session_data = (SessionData *) malloc (sizeof (SessionData));
	if (session_data) {
		session_data->packet = packet;
		session_data->auth_data = auth_data;
		session_data->client = client;
	}

	return session_data;

}

void session_data_delete (void *ptr) {

	if (ptr) { free (ptr); }

}

// creates a session id of SESSION_ID_RANDOM_BYTES random bytes of the kernel,
// as SESSION_ID_LENGTH hex characters, that can not be guessed from the time
// returns a string allocated with malloc (), NULL on error
void *session_default_generate_id (const void *session_data) {

	char *retval = NULL;

	if (session_data) {
		u8 random_bytes[SESSION_ID_RANDOM_BYTES] = { 0 };
		if (cerver_crypto_random_bytes (random_bytes, SESSION_ID_RANDOM_BYTES)) {
			retval = (char *) malloc (SESSION_ID_LENGTH + 1);
			if (retval) sha256_hash_to_string (retval, random_bytes);
		}
	}

	return retval;

}

#pragma region serialization

SERIALIZER_DEFINE (SessionToken, session_token, SESSION_TOKEN_VERSION, SESSION_TOKEN_FIELDS)

// encodes the session token of the session id in the buffer
// written takes the size of the encoded token
// returns 0 on success, 1 on error
u8 session_token_encode (
	const char *session_id,
	u8 buffer[SESSION_TOKEN_MAX_ENCODED_SIZE], size_t *written
) {

	SessionToken token = { { 0 } };
	(void) strncpy (token.token, session_id, TOKEN_SIZE - 1);

	return (serializer_encode (
		&session_token_schema, &token,
		buffer, SESSION_TOKEN_MAX_ENCODED_SIZE, written
	) == SERIALIZER_RESULT_NONE) ? 0 : 1;

}

#pragma endregion
