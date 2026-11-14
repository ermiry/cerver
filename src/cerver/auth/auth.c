#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

#include "cerver/admin/cerver.h"
#include "cerver/admin/poll.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/hold.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/connection/registry.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static const char *auth_missing_data = { "Missing auth data!" };
static const char *auth_internal_error = { "Internal cerver error!" };

#include "cerver/auth/internal.h"

static AuthData *auth_strip_auth_data (Packet *packet);

#pragma region errors

const char *cerver_auth_error_to_string (const CerverAuthError error) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_AUTH_ERROR_##name: return #string;
		CERVER_AUTH_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_auth_error_to_string (CERVER_AUTH_ERROR_NONE);

}

const char *cerver_auth_error_description (const CerverAuthError error) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_AUTH_ERROR_##name: return #description;
		CERVER_AUTH_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_auth_error_description (CERVER_AUTH_ERROR_NONE);

}

#pragma endregion

#pragma region data

static AuthData *auth_data_new (void) {

	AuthData *auth_data = (AuthData *) malloc (sizeof (AuthData));
	if (auth_data) {
		auth_data->token = NULL;

		auth_data->auth_data = NULL;
		auth_data->auth_data_size = 0;

		auth_data->data = NULL;
		auth_data->delete_data = NULL;
	}

	return auth_data;

}

static AuthData *auth_data_create (
	const char *token, void *data, size_t auth_data_size
) {

	AuthData *auth_data = auth_data_new ();
	if (auth_data) {
		auth_data->token = token ? str_new (token) : NULL;
		if (data) {
			auth_data->auth_data = malloc (auth_data_size);
			if (auth_data->auth_data) {
				memcpy (auth_data->auth_data, data, auth_data_size);
				auth_data->auth_data_size = auth_data_size;
			}

			else {
				free (auth_data);
				auth_data = NULL;
			}
		}
	}

	return auth_data;

}

static void auth_data_delete (AuthData *auth_data) {

	if (auth_data) {
		str_delete (auth_data->token);
		if (auth_data->auth_data) free (auth_data->auth_data);

		// the data that the authenticate method recovered and no client took
		if (auth_data->data && auth_data->delete_data) {
			auth_data->delete_data (auth_data->data);
		}

		free (auth_data);
	}

}

#pragma endregion

#pragma region method

static AuthMethod *auth_method_new (void) {

	AuthMethod *auth_method = (AuthMethod *) malloc (sizeof (AuthMethod));
	if (auth_method) {
		auth_method->packet = NULL;
		auth_method->auth_data = NULL;

		auth_method->error_message = NULL;
	}

	return auth_method;

}

static AuthMethod *auth_method_create (
	Packet *packet, AuthData *auth_data
) {

	AuthMethod *auth_method = auth_method_new ();
	if (auth_method) {
		auth_method->packet = packet;
		auth_method->auth_data = auth_data;
	}

	return auth_method;

}

static void auth_method_delete (AuthMethod *auth_method) {

	if (auth_method) {
		str_delete (auth_method->error_message);

		free (auth_method);
	}

}

#pragma endregion

#pragma region auth

// the success of a cerver with sessions has the session token of the client,
// its session id is read with the client's session lock
static Packet *auth_success_packet_create (const Client *client, const bool with_token) {

	Packet *packet = NULL;

	if (with_token) {
		SessionToken token = { { 0 } };
		(void) client_get_session_id (client, token.token, TOKEN_SIZE);

		packet = packet_create_serialized (
			PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_SUCCESS,
			&session_token_schema, &token
		);
	}

	else {
		packet = packet_generate_request (
			PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_SUCCESS, NULL, 0
		);
	}

	return packet;

}

static void auth_send_success_packet (
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	const bool with_token
) {

	if (cerver && connection) {
		Packet *success_packet = auth_success_packet_create (client, with_token);

		if (success_packet) {
			packet_set_network_values (
				success_packet,
				(Cerver *) cerver,
				(Client *) client, (Connection *) connection,
				NULL
			);

			(void) packet_send (success_packet, 0, NULL, false);

			packet_delete (success_packet);
		}

		else {
			#ifdef AUTH_DEBUG
			cerver_log_error (
				"Failed to create success auth packet in cerver %s",
				cerver->info->name
			);
			#endif
		}
	}

}

// generates the session id of a new client with the session id generator of the cerver,
// any new connection that authenticates with the session id (token) is added to the client
// returns 0 on success, 1 on error
u8 auth_session_create (
	Packet *packet, AuthData *auth_data, Client *client
) {

	u8 retval = 1;

	SessionData *session_data = session_data_new (packet, auth_data, client);
	if (session_data) {
		char *session_id = (char *) packet->cerver->session_id_generator (session_data);
		if (session_id) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
				"Generated client <%lu> session id: <%s>",
				client->id, session_id
			);
			#endif

			retval = client_set_session_id (client, session_id);

			// the client keeps its own copy
			free (session_id);
		}

		session_data_delete (session_data);
	}

	if (retval) {
		cerver_log_error (
			"auth_session_create () - failed to generate client's <%lu> session id!",
			client->id
		);
	}

	return retval;

}

// creates a new client and registers the connection to it
static Client *auth_create_new_client (
	Packet *packet, AuthData *auth_data,
	CerverAuthError *error
) {

	Client *client = client_create ();
	if (client) {
		connection_register_to_client (client, packet->connection);

		// the data that the authenticate method recovered belongs to the new client
		if (auth_data->data) {
			client_set_data (client, auth_data->data, auth_data->delete_data);

			auth_data->data = NULL;
			auth_data->delete_data = NULL;
		}

		// if the cerver is configured to use sessions,
		// this client has the first connection associated with the id & auth data
		if (
			packet->cerver->use_sessions
			&& auth_session_create (packet, auth_data, client)
		) {
			client_connection_remove (client, packet->connection);

			// remove the connection from on hold structures to stop receiving data
			on_hold_connection_drop (packet->cerver, packet->connection);

			*error = CERVER_AUTH_ERROR_DROPPED;

			client_delete (client);
			client = NULL;
		}
	}

	// without a client the connection can not go on, it is told why and dropped
	else {
		(void) error_packet_generate_and_send (
			CERVER_ERROR_CERVER_ERROR, auth_internal_error,
			packet->cerver, NULL, packet->connection
		);

		on_hold_connection_drop (packet->cerver, packet->connection);

		*error = CERVER_AUTH_ERROR_DROPPED;
	}

	return client;

}

// sends an ERR_FAILED_AUTH with the message to the connection
void auth_send_failed_packet (
	Cerver *cerver, Connection *connection, const char *error_message
) {

	Packet *error_packet = error_packet_generate (
		CERVER_ERROR_FAILED_AUTH,
		error_message
	);

	if (error_packet) {
		packet_set_network_values (error_packet, cerver, NULL, connection, NULL);
		(void) packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

}

// send an ERR_FAILED_AUTH to the connection and update connection stats
// if the connection's max auth tries has been reached, it will be dropped
static CerverAuthError auth_failed (
	Cerver *cerver, Connection *connection,
	const char *error_message
) {

	CerverAuthError error = CERVER_AUTH_ERROR_FAILED;

	if (cerver && connection) {
		auth_send_failed_packet (cerver, connection, error_message);

		connection->auth_tries--;
		if (connection->auth_tries <= 0) {
			#ifdef AUTH_DEBUG
			cerver_log_debug ("Connection reached max auth tries, dropping now...");
			#endif
			on_hold_connection_drop (cerver, connection);

			error = CERVER_AUTH_ERROR_DROPPED;
		}
	}

	return error;

}

// the on hold thread gives the authenticated connection to the handler of the cerver,
// a connection that no handler could take is dropped without its client
static void auth_client_connection_start (
	Cerver *cerver, Client *client, Connection *connection
) {

	if (cerver_register_new_connection_normal_default_select_handler (cerver, client, connection)) {
		cerver_new_connection_release (cerver, client, connection);
		connection_drop (cerver, connection);
	}

}

// the failed auth event of the connection, or without it when the failure dropped it,
// a connection that reached its max auth tries was deleted
static CerverAuthError auth_failed_with_event (
	const Packet *packet, const bool admin, const char *error_message
) {

	Connection *connection = packet->connection;

	const CerverAuthError error = auth_failed (packet->cerver, connection, error_message);
	if (error == CERVER_AUTH_ERROR_DROPPED) connection = NULL;

	cerver_event_trigger (
		admin ? CERVER_EVENT_ADMIN_FAILED_AUTH : CERVER_EVENT_CLIENT_FAILED_AUTH,
		packet->cerver,
		NULL, connection
	);

	return error;

}

static CerverAuthError auth_with_token_admin (
	const Packet *packet, const AuthData *auth_data
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	// if we get a token, we search for an admin with the same token
	Admin *admin = admin_get_with_session_id (
		packet->cerver->admin, auth_data->token->str
	);

	// if we found a client, register the new connection to him
	if (admin) {
		#ifdef AUTH_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
			"Found an ADMIN with session id <%s> in cerver %s.",
			auth_data->token->str, packet->cerver->info->name
		);
		#endif

		(void) connection_register_to_client (
			admin->client, packet->connection
		);
	}

	else {
		cerver_log_error (
			"Failed to get ADMIN with matching session id <%s> in cerver %s",
			auth_data->token->str, packet->cerver->info->name
		);

		// if not, the token is invalid!
		error = auth_failed (
			packet->cerver, packet->connection,
			"Session id is invalid!"
		);

		cerver_event_trigger (
			CERVER_EVENT_ADMIN_FAILED_AUTH,
			packet->cerver,
			NULL, packet->connection
		);
	}

	return error;

}

static CerverAuthError auth_with_token_normal (
	const Packet *packet, const AuthData *auth_data
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	if (packet && auth_data) {
		// if we get a token, search for a client with the same token
		Client *client = client_get_with_session_id (
			packet->cerver, auth_data->token->str
		);

		// if we found a client, register the new connection to him
		if (client) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
				"Found a CLIENT with session id <%s> in cerver %s.",
				auth_data->token->str, packet->cerver->info->name
			);
			#endif

			if (!connection_register_to_client (client, packet->connection)) {
				(void) connection_register_to_cerver (
					packet->cerver, client, packet->connection
				);
			}
		}

		else {
			cerver_log_error (
				"Failed to get CLIENT with matching session id <%s> in cerver %s",
				auth_data->token->str, packet->cerver->info->name
			);

			// if not, the token is invalid!
			error = auth_failed (
				packet->cerver, packet->connection,
				"Session id is invalid!"
			);

			cerver_event_trigger (
				CERVER_EVENT_CLIENT_FAILED_AUTH,
				packet->cerver,
				client, packet->connection
			);
		}
	}

	return error;

}

// authenticate a new connection using a session token
// if we find a client with that token, we register the connection to him;
// if we don't find a client, the token is invalid
// returns 0 on success, 1 on error
static inline CerverAuthError auth_with_token (
	const Packet *packet, const AuthData *auth_data, bool admin
) {

	return admin ?
		auth_with_token_admin (packet, auth_data) :
		auth_with_token_normal (packet, auth_data);

}

// runs the authenticate method with the auth data, without the on hold structures,
// when it fails, its error message (if it has one) is moved to error_message
// returns 0 when the method accepted the auth data, 1 when it did not
u8 auth_authenticate (
	Packet *packet, delegate authenticate, AuthData *auth_data,
	String **error_message
) {

	u8 retval = 1;

	AuthMethod *auth_method = auth_method_create (packet, auth_data);
	if (auth_method) {
		retval = authenticate (auth_method) ? 1 : 0;

		if (retval && error_message) {
			*error_message = auth_method->error_message;
			auth_method->error_message = NULL;
		}

		auth_method_delete (auth_method);
	}

	return retval;

}

// the auth data of an auth packet: a session token, or the credentials of the authenticate method
// returns NULL if the packet does not have data
AuthData *auth_data_get (Packet *packet) {

	return packet ? auth_strip_auth_data (packet) : NULL;

}

void auth_data_release (AuthData *auth_data) {

	auth_data_delete (auth_data);

}

// calls the user defined method passing the auth data and creates a new client on success
// returns 0 on success, 1 on error
static CerverAuthError auth_with_defined_method (
	Packet *packet,
	delegate authenticate, AuthData *auth_data,
	Client **client, bool admin
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	AuthMethod *auth_method = auth_method_create (packet, auth_data);
	if (auth_method) {
		if (!authenticate (auth_method)) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CLIENT,
				"Client authenticated successfully to cerver %s",
				packet->cerver->info->name
			);
			#endif

			*client = auth_create_new_client (packet, auth_data, &error);
		}

		else {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
				"Client failed to authenticate to cerver %s",
				packet->cerver->info->name
			);
			#endif

			error = auth_failed_with_event (
				packet, admin,
				auth_method->error_message ? auth_method->error_message->str : NULL
			);
		}

		auth_method_delete (auth_method);
	}

	return error;

}

// a session token is sent with its own request type
static bool auth_packet_has_token (const Packet *packet) {

	return (packet->header.request_type == AUTH_PACKET_TYPE_CLIENT_TOKEN)
		|| (packet->header.request_type == AUTH_PACKET_TYPE_ADMIN_TOKEN);

}

// the session token is a serialized SessionToken, a packet that is not one has no auth data
static AuthData *auth_strip_token (const Packet *packet) {

	AuthData *auth_data = NULL;

	SessionToken token;
	if (packet_get_object (packet, &session_token_schema, NULL, &token) == SERIALIZER_RESULT_NONE) {
		auth_data = auth_data_create (token.token, NULL, 0);
	}

	return auth_data;

}

// with an auth schema, the credentials are an object of it, decoded and validated,
// so the authenticate method gets the object in the auth data
// the fields of an auth schema can not take memory from an arena
static AuthData *auth_strip_object (const Packet *packet, const SerializerSchema *schema) {

	AuthData *auth_data = NULL;

	void *object = malloc (schema->size);
	if (object && (packet_get_object (packet, schema, NULL, object) == SERIALIZER_RESULT_NONE)) {
		auth_data = auth_data_create (NULL, object, schema->size);
	}

	free (object);

	return auth_data;

}

// strip out the auth data from the packet:
// a session token, an object of the auth schema of the cerver, or the bytes of the credentials
static AuthData *auth_strip_auth_data (Packet *packet) {

	AuthData *auth_data = NULL;

	if (packet->data_size > 0) {
		if (auth_packet_has_token (packet)) {
			auth_data = auth_strip_token (packet);
		}

		else if (packet->cerver && packet->cerver->auth_schema) {
			auth_data = auth_strip_object (packet, packet->cerver->auth_schema);
		}

		else {
			auth_data = auth_data_create (NULL, packet->data, packet->data_size);
		}
	}

	return auth_data;

}

static CerverAuthError auth_try_common (
	Packet *packet,
	delegate authenticate,
	Client **client,
	bool admin
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	// strip out the auth data from the packet
	AuthData *auth_data = auth_strip_auth_data (packet);
	if (auth_data) {
		// check that the cerver supports sessions
		if (packet->cerver->use_sessions) {
			if (auth_data->token) {
				error = auth_with_token (packet, auth_data, admin);
			}

			else {
				// if not, we authenticate using the user defined method
				error = auth_with_defined_method (
					packet, authenticate, auth_data, client, admin
				);
			}
		}

		else {
			// if not, we authenticate using the user defined method
			error = auth_with_defined_method (
				packet, authenticate, auth_data, client, admin
			);
		}

		auth_data_delete (auth_data);
	}

	// failed to get auth data form packet
	else {
		#ifdef AUTH_DEBUG
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to get auth data from packet in cerver %s",
			packet->cerver->info->name
		);
		#endif

		error = auth_failed_with_event (packet, admin, auth_missing_data);
	}

	return error;

}

// try to authenticate a connection using the values he sent to use
CerverAuthError auth_try (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	if (packet->cerver->authenticate) {
		Client *client = NULL;
		if (!auth_try_common (
			packet, packet->cerver->authenticate, &client, false
		)) {
			// remove from on hold structures & poll array
			(void) on_hold_connection_remove (
				packet->cerver, packet->connection
			);

			// reset connection's bad packets counter
			packet->connection->bad_packets = 0;

			if (client) {
				// register the new client & its connection to the cerver main structures & poll
				if (!client_register_to_cerver (packet->cerver, client)) {
					// if we are successfull, send success packet
					auth_send_success_packet (
						packet->cerver,
						client, packet->connection,
						packet->cerver->use_sessions
					);

					// the events run before the handler owns the connection,
					// its receive thread can delete it as soon as it starts
					cerver_event_trigger (
						CERVER_EVENT_CLIENT_SUCCESS_AUTH,
						packet->cerver,
						client, packet->connection
					);

					cerver_event_trigger (
						CERVER_EVENT_CLIENT_CONNECTED,
						packet->cerver,
						client, packet->connection
					);

					auth_client_connection_start (packet->cerver, client, packet->connection);
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
						"admin_auth_try () - failed to register a new admin to cerver %s",
						packet->cerver->info->name
					);

					// send an error packet to the client
					error_packet_generate_and_send (
						CERVER_ERROR_CERVER_ERROR, "Internal cerver error!",
						packet->cerver, client, packet->connection
					);

					// close the client's only connection and delete the client
					client_connection_drop (
						packet->cerver,
						client,
						(Connection *) client->connections->start->data
					);

					client_delete (client);
				}
			}

			// added connection to client with matching id (token)
			else {
				Client *match = client_get_with_sock_fd (
					packet->cerver, packet->connection->socket->sock_fd
				);

				if (match) {
					// send success auth packet to client
					auth_send_success_packet (
						packet->cerver,
						match, packet->connection,
						false
					);

					// the event runs before the handler owns the connection
					cerver_event_trigger (
						CERVER_EVENT_CLIENT_NEW_CONNECTION,
						packet->cerver,
						match, packet->connection
					);

					// select how client connection will be handled
					// based on cerver's handler type
					(void) cerver_handler_register_connection (
						packet->cerver, match, packet->connection
					);
				}
			}
		}
	}

	// no authentication method -- clients are not able to authenticate with to the cerver!
	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Cerver %s does not have an authenticate method!",
			packet->cerver->info->name
		);

		// close the on hold connection assocaited with sock fd
		// and remove it from the cerver
		on_hold_connection_drop (packet->cerver, packet->connection);

		error = CERVER_AUTH_ERROR_NO_HANDLER;
	}

	return error;

}

CerverAuthError admin_auth_try (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	if (packet->cerver->admin) {
		if (packet->cerver->admin->authenticate) {
			Client *client = NULL;
			if (!auth_try_common (
				packet, packet->cerver->admin->authenticate, &client, true)
			) {
				// remove from on hold structures & poll array
				(void) on_hold_connection_remove (packet->cerver, packet->connection);

				// reset connection's bad packets counter
				packet->connection->bad_packets = 0;

				if (client) {
					// create a new admin with client and register it to the admin
					Admin *admin = admin_create_with_client (client);
					if (!admin_cerver_register_admin (packet->cerver->admin, admin)) {
						// if we are successfull, send success packet
						auth_send_success_packet (
							packet->cerver,
							client, packet->connection,
							packet->cerver->use_sessions
						);

						cerver_event_trigger (
							CERVER_EVENT_ADMIN_CONNECTED,
							packet->cerver,
							client, packet->connection
						);
					}

					else {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
							"admin_auth_try () - failed to register a new admin to cerver %s",
							packet->cerver->info->name
						);

						// send an error packet to the client
						(void) error_packet_generate_and_send (
							CERVER_ERROR_CERVER_ERROR, "Internal cerver error!",
							packet->cerver, client, packet->connection
						);

						// close the admin's client only connection and delete the admin
						client_connection_drop (
							packet->cerver,
							admin->client,
							(Connection *) admin->client->connections->start->data
						);

						admin_delete (admin);
					}
				}

				// added connection to client with matching id (token)
				else {
					Admin *admin = admin_get_with_sock_fd (
						packet->cerver->admin, packet->connection->socket->sock_fd
					);

					if (admin) {
						admin_cerver_poll_register_connection (
							packet->cerver->admin, packet->connection
						);

						// send success auth packet to client
						auth_send_success_packet (
							packet->cerver,
							admin->client, packet->connection,
							false
						);

						cerver_event_trigger (
							CERVER_EVENT_ADMIN_NEW_CONNECTION,
							packet->cerver,
							client, packet->connection
						);
					}
				}
			}
		}

		// no authentication method -- clients are not able to authenticate with the cerver!
		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver %s ADMIN does not have an authenticate method!",
				packet->cerver->info->name
			);

			// close the on hold connection assocaited with sock fd
			// and remove it from the cerver
			on_hold_connection_drop (packet->cerver, packet->connection);

			error = CERVER_AUTH_ERROR_NO_HANDLER;
		}
	}

	else {
		cerver_log_warning (
			"admin_auth_try () - Cerver %s does NOT support ADMINS!",
			packet->cerver->info->name
		);

		on_hold_connection_drop (packet->cerver, packet->connection);

		error = CERVER_AUTH_ERROR_DROPPED;
	}

	return error;

}

#pragma endregion
