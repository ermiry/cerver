#include <stdlib.h>
#include <string.h>

#include <time.h>

#include "cerver/types/types.h"

#include "cerver/auth.h"
#include "cerver/client.h"
#include "cerver/packets.h"
#include "cerver/sessions.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/sha256.h"

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

// create a unique session id for each client based on the current time
void *session_default_generate_id (const void *session_data) {

	char *retval = NULL;

	if (session_data) {
		// SessionData *data = (SessionData *) session_data;

		// get current time
		time_t now = time (NULL);
		char buf[128] = { 0 };
		strftime (buf, 128, "%Y-%m-%d.%X", localtime (&now));

		char *temp = c_string_create ("%s-%d", buf, random_int_in_range (0, 8096));
		if (temp) {
			uint8_t hash[32] = { 0 };
			char hash_string[65] = { 0 };
			sha256_calc (hash, temp, strlen (temp));
			sha256_hash_to_string (hash_string, hash);

			retval = c_string_create ("%s", hash_string);

			free (temp);
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
