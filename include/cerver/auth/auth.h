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

#pragma region errors

#define CERVER_AUTH_ERROR_MAP(XX)															\
	XX(0,	NONE,				None,				No authentication error)				\
	XX(1,	SUCCESS,			Success Auth,		Authentication has been successful)		\
	XX(2,	NO_HANDLER,			No Handler,			The cerver has no auth handler)			\
	XX(3,	MISSING_VALUES,		Missing Values,		Bad auth request due to missing values) \
	XX(4,	FAILED,				Failed Auth,		Invalid credentials)					\
	XX(5,	INVALID_SESSION,	Bad Session ID,		Session id is invalid)					\
	XX(6,	DROPPED,			Dropped Connection, The connection has been ended)

typedef enum CerverAuthError {

	#define XX(num, name, string, description) CERVER_AUTH_ERROR_##name = num,
	CERVER_AUTH_ERROR_MAP (XX)
	#undef XX

} CerverAuthError;

CERVER_PUBLIC const char *cerver_auth_error_to_string (
	const CerverAuthError error
);

CERVER_PUBLIC const char *cerver_auth_error_description (
	const CerverAuthError error
);

#pragma endregion

#pragma region data

// the auth data stripped from the packet
struct _AuthData {

	String *token;

	void *auth_data;
	size_t auth_data_size;

	// data that the authenticate method can recover, like the user of the credentials,
	// after a success auth that creates a client, it becomes the data of the client
	// (client_set_data ()), otherwise it is released with delete_data
	void *data;
	Action delete_data;         // how to delete the data

};

typedef struct _AuthData AuthData;

#pragma endregion

#pragma region method

// auxiliary structure passed to the user defined auth method
struct _AuthMethod {

	struct _Packet *packet;         // the original packet
	AuthData *auth_data;            // the stripped auth data from the packet

	// a user message that can be sent to the connection when teh auth has failed
	// in a generated ERR_FAILED_AUTH packet
	String *error_message;

};

typedef struct _AuthMethod AuthMethod;

// the auth data of an auth packet: a session token, or the credentials of the authenticate method
// returns NULL if the packet does not have data
CERVER_PRIVATE AuthData *auth_data_get (struct _Packet *packet);

CERVER_PRIVATE void auth_data_release (AuthData *auth_data);

// runs the authenticate method with the auth data, without the on hold structures,
// when it fails, its error message (if it has one) is moved to error_message
// returns 0 when the method accepted the auth data, 1 when it did not
CERVER_PRIVATE u8 auth_authenticate (
	struct _Packet *packet, delegate authenticate, AuthData *auth_data,
	String **error_message
);

// generates the session id of a new client with the session id generator of the cerver,
// any new connection that authenticates with the session id (token) is added to the client
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 auth_session_create (
	struct _Packet *packet, AuthData *auth_data, struct _Client *client
);

// sends an ERR_FAILED_AUTH with the message to the connection
CERVER_PRIVATE void auth_send_failed_packet (
	struct _Cerver *cerver, struct _Connection *connection, const char *error_message
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
