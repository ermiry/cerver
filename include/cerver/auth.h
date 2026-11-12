#ifndef _CERVER_AUTH_H_
#define _CERVER_AUTH_H_

#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/client.h"
#include "cerver/packets.h"

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

	// recover data used for authentication
	// after a success auth, it will be added to the client
	// if not, it should be dispossed by user defined auth method
	// and set to NULL
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

#pragma region handler

// handles a packet from an on hold connection
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended or removed from on hold
CERVER_PRIVATE u8 on_hold_packet_handler (
	struct _Packet *packet
);

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 on_hold_connection (
	struct _Cerver *cerver, struct _Connection *connection
);

// closes the on hold connection and removes it from the cerver,
// it triggers CERVER_EVENT_ON_HOLD_DROPPED
CERVER_PRIVATE void on_hold_connection_drop (
	const struct _Cerver *cerver, struct _Connection *connection
);

// closes the on hold connection that its client closed or lost,
// it triggers CERVER_EVENT_ON_HOLD_DISCONNECTED
CERVER_PRIVATE void on_hold_connection_disconnected (
	const struct _Cerver *cerver, struct _Connection *connection
);

#pragma endregion

#pragma region poll

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 on_hold_poll_unregister_sock_fd (
	struct _Cerver *cerver, const i32 sock_fd
);

// handles packets from the on hold clients until they authenticate
CERVER_PRIVATE void *on_hold_poll (void *cerver_ptr);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
