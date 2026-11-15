#ifndef _CERVER_CONNECTION_DATA_H_
#define _CERVER_CONNECTION_DATA_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// sets the connection auth data to send whenever the cerver requires authentication
// and a method to destroy it once the connection has ended,
// if delete_auth_data is NULL, the auth data won't be deleted
CERVER_PUBLIC void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
	bool admin_auth
);

// sets the credentials as an object of the auth schema of the cerver (cerver_set_auth_schema ()),
// the object is encoded now, so it can be released after this
// returns 0 on success, 1 if the object can not be encoded
CERVER_PUBLIC u8 connection_set_auth_object (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	bool admin_auth
);

// sets the session token that a cerver with sessions gave to the client,
// to authenticate the connection with it instead of the credentials
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 connection_set_auth_token (
	Connection *connection, const char *token,
	bool admin_auth
);

// removes the connection auth data using the connection's delete_auth_data method
// if not such method, the data won't be deleted
// the connection's auth data & delete method will be equal to NULL
CERVER_PUBLIC void connection_remove_auth_data (
	Connection *connection
);

// generates the connection auth packet to be send to the server
// this is also generated automatically whenever the cerver ask for authentication
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 connection_generate_auth_packet (
	Connection *connection
);

// sets the token that an authenticated UDP connection sends after the packet in every datagram
CERVER_PRIVATE void connection_set_datagram_token (
	Connection *connection, const u8 *token
);

// copies the datagram token of the connection into token
// returns true if the connection has one
CERVER_PRIVATE bool connection_get_datagram_token (
	const Connection *connection, u8 *token
);

// the connection sends its datagrams without a token again
CERVER_PRIVATE void connection_clear_datagram_token (Connection *connection);

// returns true if the connection sends a token in its datagrams (it authenticated)
CERVER_PRIVATE bool connection_has_datagram_token (const Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
