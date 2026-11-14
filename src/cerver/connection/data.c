#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/data.h"
#include "cerver/connection/internal.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region data

// sets the connection auth data to send whenever the cerver requires authentication
// and a method to destroy it once the connection has ended,
// if delete_auth_data is NULL, the auth data won't be deleted
void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
	bool admin_auth
) {

	if (connection && auth_data) {
		connection_remove_auth_data (connection);

		connection->auth_data = auth_data;
		connection->auth_data_size = auth_data_size;
		connection->delete_auth_data = delete_auth_data;
		connection->admin_auth = admin_auth;
	}

}

// encodes the object as the auth data of the connection
// returns 0 on success, 1 on error
static u8 connection_set_auth_encoded (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	const bool admin_auth, const bool token
) {

	u8 retval = 1;

	const size_t size = serializer_encoded_size (schema, object);
	void *buffer = size ? malloc (size) : NULL;

	if (buffer && (serializer_encode (schema, object, buffer, size, NULL) == SERIALIZER_RESULT_NONE)) {
		connection_set_auth_data (connection, buffer, size, free, admin_auth);
		connection->auth_serialized = true;
		connection->auth_token = token;

		retval = 0;
	}

	else {
		free (buffer);
	}

	return retval;

}

// sets the credentials as an object of the auth schema of the cerver (cerver_set_auth_schema ()),
// the object is encoded now, so it can be released after this
// returns 0 on success, 1 if the object can not be encoded
u8 connection_set_auth_object (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	bool admin_auth
) {

	return (connection && schema && object)
		? connection_set_auth_encoded (connection, schema, object, admin_auth, false)
		: 1;

}

// sets the session token that a cerver with sessions gave to the client,
// to authenticate the connection with it instead of the credentials
// returns 0 on success, 1 on error
u8 connection_set_auth_token (
	Connection *connection, const char *token,
	bool admin_auth
) {

	u8 retval = 1;

	if (connection && token && (strlen (token) < TOKEN_SIZE)) {
		SessionToken session_token = { { 0 } };
		(void) strncpy (session_token.token, token, TOKEN_SIZE - 1);

		retval = connection_set_auth_encoded (
			connection, &session_token_schema, &session_token, admin_auth, true
		);
	}

	return retval;

}

// the request type of the auth packet: the credentials or a session token, of a client or an admin
static u32 connection_auth_request_type (const Connection *connection) {

	u32 request_type = connection->admin_auth ? AUTH_PACKET_TYPE_ADMIN_AUTH : AUTH_PACKET_TYPE_CLIENT_AUTH;

	if (connection->auth_token) {
		request_type = connection->admin_auth ? AUTH_PACKET_TYPE_ADMIN_TOKEN : AUTH_PACKET_TYPE_CLIENT_TOKEN;
	}

	return request_type;

}

// removes the connection auth data using the connection's delete_auth_data method
// if not such method, the data won't be deleted
// the connection's auth data & delete method will be equal to NULL
void connection_remove_auth_data (Connection *connection) {

	if (connection) {
		if (connection->auth_data) {
			if (connection->delete_auth_data)
				connection->delete_auth_data (connection->auth_data);
		}

		if (connection->auth_packet) {
			packet_delete (connection->auth_packet);
			connection->auth_packet = NULL;
		}

		connection->auth_data = NULL;
		connection->auth_data_size = 0;
		connection->delete_auth_data = NULL;
		connection->auth_serialized = false;
		connection->auth_token = false;
	}

}

// generates the connection auth packet to be send to the server
// this is also generated automatically whenever the cerver ask for authentication
// returns 0 on success, 1 on error
u8 connection_generate_auth_packet (Connection *connection) {

	u8 retval = 1;

	if (connection) {
		if (connection->auth_data) {
			connection->auth_packet = packet_generate_request (
				PACKET_TYPE_AUTH,
				connection_auth_request_type (connection),
				connection->auth_data, connection->auth_data_size
			);

			// an encoded object is sent with its flag
			if (connection->auth_packet && connection->auth_serialized) {
				connection->auth_packet->header.flags |= PACKET_FLAG_SERIALIZED;
				if (packet_generate (connection->auth_packet)) {
					packet_delete (connection->auth_packet);
					connection->auth_packet = NULL;
				}
			}

			if (connection->auth_packet) retval = 0;
		}
	}

	return retval;

}

// sets the token that an authenticated UDP connection sends after the packet in every datagram
// the token is written before the flag, a sender that sees the flag reads it complete
void connection_set_datagram_token (
	Connection *connection, const u8 *token
) {

	if (connection && token) {
		(void) memcpy (connection->datagram_token, token, CONNECTION_DATAGRAM_TOKEN_SIZE);
		__atomic_store_n (&connection->datagram_token_set, true, __ATOMIC_RELEASE);
	}

}

// copies the datagram token of the connection into token
// returns true if the connection has one
bool connection_get_datagram_token (
	const Connection *connection, u8 *token
) {

	bool has_token = false;

	if (connection && __atomic_load_n (&connection->datagram_token_set, __ATOMIC_ACQUIRE)) {
		(void) memcpy (token, connection->datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE);
		has_token = true;
	}

	return has_token;

}

// the connection sends its datagrams without a token again
void connection_clear_datagram_token (Connection *connection) {

	if (connection) {
		__atomic_store_n (&connection->datagram_token_set, false, __ATOMIC_RELEASE);
	}

}

// returns true if the connection sends a token in its datagrams (it authenticated)
bool connection_has_datagram_token (const Connection *connection) {

	return connection && __atomic_load_n (&connection->datagram_token_set, __ATOMIC_ACQUIRE);

}

#pragma endregion
