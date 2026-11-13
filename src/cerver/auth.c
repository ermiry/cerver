#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/socket.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/sessions.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/auth.h"
#include "cerver/events.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/thpool.h"

#include "cerver/collections/htab.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static const char *auth_missing_data = { "Missing auth data!" };
static const char *auth_internal_error = { "Internal cerver error!" };

static u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection);
static u8 on_hold_poll_register_connection (Cerver *cerver, Connection *connection);
u8 on_hold_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd);
static u8 on_hold_poll_unregister_connection (Cerver *cerver, Connection *connection);

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
	Admin *admin = admin_get_by_session_id (
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
		Client *client = client_get_by_session_id (
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
static CerverAuthError auth_try (Packet *packet) {

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
				Client *match = client_get_by_sock_fd (
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

static CerverAuthError admin_auth_try (Packet *packet) {

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
					Admin *admin = admin_get_by_sock_fd (
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

#pragma region handler

static const char *on_hold_bad_auth_request = { "The auth packet has an unknown request type" };
static const char *on_hold_bad_auth_data = { "The auth packet does not have data" };
static const char *on_hold_bad_type = { "The packet can not be handled before the authentication" };
static const char *on_hold_bad_version = { "The packet has an incompatible version" };

// a bad packet of an on hold connection: its sender gets an error,
// and the connection is dropped when it reaches the on hold limit
static CerverAuthError cerver_on_hold_handle_max_bad_packets (
	Cerver *cerver, Connection *connection, const char *reason
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	CERVER_STATS_ADD (cerver->stats->received_packets->n_bad_packets, 1);

	(void) error_packet_generate_and_send (
		CERVER_ERROR_PACKET_ERROR, reason, cerver, NULL, connection
	);

	connection->bad_packets += 1;

	if (connection->bad_packets >= cerver->on_hold_max_bad_packets) {
		#ifdef AUTH_DEBUG
		cerver_log_debug (
			"ON HOLD connection <%d> has reached max bad packets, dropping...",
			connection->socket->sock_fd
		);
		#endif

		on_hold_connection_drop (cerver, connection);

		error = CERVER_AUTH_ERROR_DROPPED;
	}

	return error;

}

// handle an auth packet from an on hold connection
// returns the error that was produced by the auth methods
static CerverAuthError cerver_auth_packet_handler (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	if (packet->data_size > 0) {
		switch (packet->header.request_type) {
			// the client sent use its data to authenticate itself
			case AUTH_PACKET_TYPE_CLIENT_AUTH:
			case AUTH_PACKET_TYPE_CLIENT_TOKEN:
				error = auth_try (packet);
				break;

			case AUTH_PACKET_TYPE_ADMIN_AUTH:
			case AUTH_PACKET_TYPE_ADMIN_TOKEN:
				error = admin_auth_try (packet);
				break;

			default: {
				#ifdef AUTH_DEBUG
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_PACKET,
					"cerver_auth_packet_hanlder () -- got an unknwown request type"
				);
				#endif

				error = cerver_on_hold_handle_max_bad_packets (
					packet->cerver, packet->connection, on_hold_bad_auth_request
				);
			} break;
		}
	}

	else {
		// bad packet
		error = cerver_on_hold_handle_max_bad_packets (
			packet->cerver, packet->connection, on_hold_bad_auth_data
		);
	}

	return error;

}

// returns the error that was produced by internal cerver methods
// based on the error, packets continue being handled or not
static CerverAuthError on_hold_packet_handler_actual (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	switch (packet->header.packet_type) {
		// handles authentication packets
		case PACKET_TYPE_AUTH:
			error = cerver_auth_packet_handler (packet);
			break;

		// acknowledge the client we have received his test packet
		case PACKET_TYPE_TEST:
			cerver_test_packet_handler (packet);
			break;

		default: {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PACKET,
				"Got an ON HOLD packet of unknown type in cerver %s.",
				packet->cerver->info->name
			);
			#endif

			error = cerver_on_hold_handle_max_bad_packets (
				packet->cerver, packet->connection, on_hold_bad_type
			);
		} break;
	}

	return error;

}

static CerverAuthError on_hold_packet_handler_check_version (
	Packet *packet
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	// the version came in the header, it used to be expected in the data,
	// and the check was inverted, so every valid packet was a bad one
	if (packet->cerver->on_hold_check_packets && !packet_check (packet)) {
		error = cerver_on_hold_handle_max_bad_packets (
			packet->cerver, packet->connection, on_hold_bad_version
		);
	}

	return error;

}

// handles a packet from an on hold connection
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended or removed from on hold
u8 on_hold_packet_handler (Packet *packet) {

	u8 retval = 1;

	if (!on_hold_packet_handler_check_version (packet)) {
		switch (on_hold_packet_handler_actual (packet)) {
			case CERVER_AUTH_ERROR_NONE:
			case CERVER_AUTH_ERROR_MISSING_VALUES:
			case CERVER_AUTH_ERROR_FAILED:
			case CERVER_AUTH_ERROR_INVALID_SESSION:
				retval = 0;
				break;

			default: break;
		}
	}

	packet_delete (packet);

	return retval;

}

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
// removes the connection from the on hold maps when its fd could not be polled
static void on_hold_connection_unmap (const Cerver *cerver, Connection *connection) {

	Connection *query = connection_new ();
	query->socket = socket_new ();
	if (query->socket) query->socket->sock_fd = connection->socket->sock_fd;

	(void) avl_remove_node (cerver->on_hold_connections, query);

	connection_delete (query);

	const void *key = &connection->socket->sock_fd;
	(void) htab_remove (cerver->on_hold_connection_sock_fd_map, key, sizeof (i32));

}

u8 on_hold_connection (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (cerver && connection) {
		if (cerver->on_hold_connections) {
			// set before the on hold poll can handle its auth packets
			connection->auth_tries = cerver->max_auth_tries;
			(void) time (&connection->connected_timestamp);

			// the poll thread finds the connection in the maps,
			// so its fd is registered in the poll after them
			const void *key = &connection->socket->sock_fd;
			if (!htab_insert (
				cerver->on_hold_connection_sock_fd_map,
				key, sizeof (i32),
				connection, sizeof (Connection)
			)) {
				avl_insert_node (cerver->on_hold_connections, connection);

				retval = on_hold_poll_register_connection (cerver, connection);
				if (retval) on_hold_connection_unmap (cerver, connection);
			}

			else {
				cerver_log_error (
					"on_hold_connection () - "
					"failed to insert connection in on_hold_connection_sock_fd_map htab!"
				);
			}
		}
	}

	return retval;

}

// removes the connection from the on hold structures
// returns 0 on success, 1 on error
static u8 on_hold_connection_remove (
	const Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		if (!on_hold_poll_unregister_connection ((Cerver *) cerver, connection)) {
			// remove the connection associated to the sock fd
			Connection *query = connection_new ();
			query->socket = socket_new ();
			if (query->socket) query->socket->sock_fd = connection->socket->sock_fd;

			if (avl_remove_node (cerver->on_hold_connections, query)) {
				#ifdef AUTH_DEBUG
				cerver_log_debug (
					"on_hold_connection_remove () - "
					"removed connection from on_hold_connections avl"
				);
				#endif
			}

			else {
				cerver_log_error (
					"on_hold_connection_remove () - "
					"failed to remove connection from on_hold_connections avl!"
				);
			}

			// remove connection from on hold map
			const void *key = &connection->socket->sock_fd;
			if (htab_remove (
				cerver->on_hold_connection_sock_fd_map, key, sizeof (i32))
			) {
				#ifdef AUTH_DEBUG
				cerver_log_debug (
					"on_hold_connection_remove () - "
					"removed connection from on_hold_connection_sock_fd_map htab"
				);
				#endif
			}

			else {
				cerver_log_error (
					"on_hold_connection_remove () - "
					"failed to remove connection from on_hold_connection_sock_fd_map htab!"
				);
			}

			connection_delete (query);

			retval = 0;
		}
	}

	return retval;

}

// closes the on hold connection, removes it from the cerver and triggers the event
static void on_hold_connection_close (
	const Cerver *cerver, Connection *connection,
	const CerverEventType event_type
) {

	if (cerver && connection) {
		// remove the connection from the on hold structures
		on_hold_connection_remove (cerver, connection);

		// close the connection socket
		connection_end (connection);

		// we can now safely delete the connection
		connection_delete (connection);

		cerver_event_trigger (event_type, cerver, NULL, NULL);
	}

}

// closes the on hold connection and removes it from the cerver
void on_hold_connection_drop (
	const Cerver *cerver, Connection *connection
) {

	on_hold_connection_close (cerver, connection, CERVER_EVENT_ON_HOLD_DROPPED);

}

// closes the on hold connection that its client closed or lost
void on_hold_connection_disconnected (
	const Cerver *cerver, Connection *connection
) {

	on_hold_connection_close (cerver, connection, CERVER_EVENT_ON_HOLD_DISCONNECTED);

}

#pragma endregion

#pragma region poll

static i32 on_hold_get_free_idx (Cerver *cerver) {

	if (cerver) {
		for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
			if (cerver->hold_fds[i].fd == -1) return i;
	}

	return -1;

}

static i32 on_hold_poll_get_idx_by_sock_fd (
	const Cerver *cerver, i32 sock_fd
) {

	if (cerver) {
		for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
			if (cerver->hold_fds[i].fd == sock_fd) return i;
	}

	return -1;

}

// regsiters a connection to the cerver's on hold poll array
// returns 0 on success, 1 on error
static u8 on_hold_poll_register_connection (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

		i32 idx = on_hold_get_free_idx (cerver);
		if (idx >= 0) {
			cerver->hold_fds[idx].fd = connection->socket->sock_fd;
			cerver->hold_fds[idx].events = POLLIN;
			cerver->current_on_hold_nfds++;

			CERVER_STATS_ADD (cerver->stats->current_n_hold_connections, 1);

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Added sock fd <%d> to cerver %s ON HOLD poll, idx: %i",
				connection->socket->sock_fd, cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_CERVER,
				"Cerver %s current ON HOLD connections: %lu",
				cerver->info->name, cerver->stats->current_n_hold_connections
			);
			#endif

			retval = 0;
		}

		else if (idx < 0) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Cerver %s ON HOLD poll is full!",
				cerver->info->name
			);
			#endif
		}

		(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);
	}

	return retval;

}

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
u8 on_hold_poll_unregister_sock_fd (
	Cerver *cerver, const i32 sock_fd
) {

	u8 retval = 1;

	if (cerver) {
		(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

		i32 idx = on_hold_poll_get_idx_by_sock_fd (cerver, sock_fd);
		if (idx >= 0) {
			cerver->hold_fds[idx].fd = -1;
			cerver->hold_fds[idx].events = -1;
			cerver->hold_fds[idx].revents = -1;
			cerver->current_on_hold_nfds--;

			CERVER_STATS_SUBTRACT (cerver->stats->current_n_hold_connections, 1);

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Removed sock fd <%d> from cerver %s ON HOLD poll, idx: %d",
				sock_fd, cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_CERVER,
				"Cerver %s current ON HOLD connections: %lu",
				cerver->info->name,
				cerver->stats->current_n_hold_connections
			);
			#endif

			retval = 0;
		}

		else {
			// #ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Sock fd <%d> was NOT found in cerver %s ON HOLD poll!",
				sock_fd, cerver->info->name
			);
			// #endif
		}

		(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);
	}

	return retval;

}

// unregsiters a connection from the cerver's on hold poll array
// returns 0 on success, 1 on error
static u8 on_hold_poll_unregister_connection (
	Cerver *cerver, Connection *connection
) {

	return (cerver && connection) ?
		on_hold_poll_unregister_sock_fd (cerver, connection->socket->sock_fd) : 1;

}

// the poll thread works with its own copy of the fds,
// because the accept thread registers new connections while it polls
static void on_hold_poll_copy_fds (
	Cerver *cerver, struct pollfd *polled_fds
) {

	(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

	(void) memcpy (
		polled_fds, cerver->hold_fds,
		sizeof (struct pollfd) * cerver->max_on_hold_connections
	);

	(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);

}

static void on_hold_poll_handle_fd (
	Cerver *cerver, char *packet_buffer,
	const struct pollfd *polled_fd
) {

	CerverReceive *cr = cerver_receive_create (
		RECEIVE_TYPE_ON_HOLD, cerver, polled_fd->fd
	);

	if (cr) {
		if (polled_fd->revents == POLLIN) {
			cerver_receive_internal (
				cr,
				packet_buffer,
				cerver->on_hold_receive_buffer_size
			);
		}

		else {
			cerver_receive_handle_failed (cr);
		}

		cerver_receive_delete (cr);
	}

}

static void on_hold_poll_expire_fd (
	Cerver *cerver, const i32 sock_fd, const time_t now
) {

	// a connection that was handled in this round is not on hold anymore
	Connection *connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);

	if (connection && ((now - connection->connected_timestamp) >= (time_t) cerver->on_hold_timeout)) {
		cerver_log_warning (
			"Cerver %s ON HOLD connection %d did not authenticate in %u secs - dropping...",
			cerver->info->name, sock_fd, cerver->on_hold_timeout
		);

		on_hold_connection_drop (cerver, connection);
	}

}

// drops the on hold connections that did not authenticate in time,
// only this thread handles them, so they are not in use
static void on_hold_poll_expire (
	Cerver *cerver, const struct pollfd *polled_fds
) {

	if (!cerver->on_hold_timeout) return;

	const time_t now = time (NULL);

	for (u32 idx = 0; idx < cerver->max_on_hold_connections; idx++) {
		if (polled_fds[idx].fd > -1) {
			on_hold_poll_expire_fd (cerver, polled_fds[idx].fd, now);
		}
	}

}

static inline void on_hold_poll_handle (
	Cerver *cerver, char *packet_buffer,
	const struct pollfd *polled_fds
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < cerver->max_on_hold_connections; idx++) {
		if ((polled_fds[idx].fd > -1) && polled_fds[idx].revents) {
			on_hold_poll_handle_fd (cerver, packet_buffer, &polled_fds[idx]);
		}
	}

}

// handles packets from the on hold clients until they authenticate
void *on_hold_poll (void *cerver_ptr) {

	if (cerver_ptr) {
		Cerver *cerver = (Cerver *) cerver_ptr;

		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
			"Cerver %s ON HOLD poll has started!",
			cerver->info->name
		);

		(void) thread_set_name ("%s-on-hold", cerver->info->alias);

		char *packet_buffer = (char *) calloc (
			cerver->on_hold_receive_buffer_size, sizeof (char)
		);

		struct pollfd *polled_fds = (struct pollfd *) calloc (
			cerver->max_on_hold_connections, sizeof (struct pollfd)
		);

		if (packet_buffer && polled_fds) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Waiting for connections to put on hold..."
			);
			#endif

			int poll_retval = 0;
			while (cerver_is_running (cerver)) {
				on_hold_poll_copy_fds (cerver, polled_fds);

				poll_retval = poll (
					polled_fds,
					cerver->max_on_hold_connections,
					cerver->on_hold_poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cerver %s ON HOLD poll has failed!",
							cerver->info->name
						);

						perror ("Error");
						cerver_stop_request (cerver);
					} break;

					case 0: {
						// #ifdef AUTH_DEBUG
						// cerver_log (
						//     LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
						//     "Cerver %s ON HOLD poll timeout",
						// 	cerver->info->name
						// );
						// #endif
					} break;

					default: {
						on_hold_poll_handle (
							cerver,
							packet_buffer,
							polled_fds
						);
					} break;
				}

				on_hold_poll_expire (cerver, polled_fds);
			}

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s ON HOLD poll has stopped!",
				cerver->info->name
			);
			#endif

		}

		else {
			cerver_log_error (
				"Failed to allocate cerver ON HOLD poll's packet buffer!"
			);
		}

		free (packet_buffer);
		free (polled_fds);
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Can't handle ON HOLD clients on a NULL cerver!"
		);
	}

	return NULL;

}

#pragma endregion
