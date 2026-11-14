#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/client/connect.h"
#include "cerver/client/events.h"
#include "cerver/client/handler.h"
#include "cerver/client/internal.h"
#include "cerver/client/requests.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/data.h"
#include "cerver/connection/end.h"
#include "cerver/connection/update.h"
#include "cerver/core/cerver.h"
#include "cerver/core/report.h"
#include "cerver/core/serialization.h"
#include "cerver/files/files.h"
#include "cerver/files/send.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region handler

const char *client_handler_error_to_string (
	const ClientHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CLIENT_HANDLER_ERROR_##name: return #string;
		CLIENT_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return client_handler_error_to_string (CLIENT_HANDLER_ERROR_NONE);

}

const char *client_handler_error_description (
	const ClientHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CLIENT_HANDLER_ERROR_##name: return #description;
		CLIENT_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return client_handler_error_description (CLIENT_HANDLER_ERROR_NONE);

}

// the info is an object of the summary schema, validated when it is decoded
static void client_cerver_packet_handle_info (Packet *packet) {

	CerverSummary summary;
	if (packet_get_object (packet, &cerver_summary_schema, NULL, &summary) == SERIALIZER_RESULT_NONE) {
		#ifdef CLIENT_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Received a cerver info packet.");
		#endif

		CerverReport *cerver_report = cerver_deserialize (&summary);
		if (cerver_report_check_info (
			cerver_report, packet->client, packet->connection
		)) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_NONE,
				"Failed to correctly check cerver info!"
			);
		}

		// the info is in the cerver report of the connection
		else {
			client_event_trigger (CLIENT_EVENT_CERVER_INFO, packet->client, packet->connection);
		}
	}

}

// handles cerver type packets
static ClientHandlerError client_cerver_packet_handler (Packet *packet) {

	ClientHandlerError error = CLIENT_HANDLER_ERROR_NONE;

	switch (packet->header.request_type) {
		case CERVER_PACKET_TYPE_INFO:
			client_cerver_packet_handle_info (packet);
			break;

		// the cerver is going to teardown and closes the connection right after it,
		// the receive loop ends or reconnects the connection like with any close,
		// and the other connections of the client are not affected
		case CERVER_PACKET_TYPE_TEARDOWN:
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"---> Cerver teardown <---"
			);
			#endif

			client_event_trigger (CLIENT_EVENT_CERVER_TEARDOWN, packet->client, packet->connection);
			break;

		default:
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Unknown cerver type packet"
			);
			break;
	}

	return error;

}

// handles a client type packet
static ClientHandlerError client_client_packet_handler (Packet *packet) {

	ClientHandlerError error = CLIENT_HANDLER_ERROR_NONE;

	switch (packet->header.request_type) {
		// the cerver close our connection
		case CLIENT_PACKET_TYPE_CLOSE_CONNECTION:
			client_connection_end (packet->client, packet->connection);
			error = CLIENT_HANDLER_ERROR_CLOSED;
			break;

		// the cerver has disconneted us
		case CLIENT_PACKET_TYPE_DISCONNECT:
			client_got_disconnected (packet->client);
			client_event_trigger (CLIENT_EVENT_DISCONNECTED, packet->client, NULL);
			error = CLIENT_HANDLER_ERROR_CLOSED;
			break;

		default:
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Unknown client packet type"
			);
			break;
	}

	return error;

}

// handles a request from a cerver to get a file
static void client_request_get_file (Packet *packet) {

	Client *client = packet->client;

	client->file_stats->n_files_requests += 1;

	// get the necessary information to fulfil the request
	FileHeader requested = { { 0 }, 0 };
	if (file_header_get (packet, &requested)) {
		const FileHeader *file_header = &requested;

		// search for the requested file in the configured paths
		String *actual_filename = client_files_search_file (
			client, file_header->filename
		);

		if (actual_filename) {
			#ifdef CLIENT_DEBUG
			cerver_log_debug (
				"client_request_get_file () - Sending %s...\n",
				actual_filename->str
			);
			#endif

			// if found, pipe the file contents to the client's socket fd
			// the socket should be blocked during the entire operation
			ssize_t sent = file_send (
				NULL, client, packet->connection,
				actual_filename->str
			);

			if (sent > 0) {
				client->file_stats->n_success_files_requests += 1;
				client->file_stats->n_files_sent += 1;
				client->file_stats->n_bytes_sent += sent;

				cerver_log_success ("Sent file %s", actual_filename->str);
			}

			else {
				cerver_log_error ("Failed to send file %s", actual_filename->str);

				client->file_stats->n_bad_files_sent += 1;
			}

			str_delete (actual_filename);
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_warning (
				"client_request_get_file () - "
				"file not found"
			);
			#endif

			// if not found, return an error to the client
			(void) client_error_packet_generate_and_send (
				CLIENT_ERROR_FILE_NOT_FOUND, "File not found",
				packet->client, packet->connection
			);

			client->file_stats->n_bad_files_requests += 1;
		}

	}

	else {
		#ifdef CLIENT_DEBUG
		cerver_log_warning (
			"client_request_get_file () - "
			"missing file header"
		);
		#endif

		// return a bad request error packet
		(void) client_error_packet_generate_and_send (
			CLIENT_ERROR_GET_FILE, "Missing or invalid file header",
			packet->client, packet->connection
		);

		client->file_stats->n_bad_files_requests += 1;
	}

}

static void client_request_send_file_actual (Packet *packet) {

	Client *client = packet->client;

	client->file_stats->n_files_upload_requests += 1;

	// get the necessary information to fulfil the request
	FileHeader received = { { 0 }, 0 };
	if (file_header_get (packet, &received)) {
		FileHeader *file_header = &received;

		const char *file_data = NULL;
		size_t file_data_len = 0;

		// the file is sent after its packet, so its first bytes can be in the same buffer
		if (packet->connection) {
			file_data_len = receive_handle_take_trailing (
				&packet->connection->receive_handle, &file_data, (size_t) file_header->len
			);
		}

		char *saved_filename = NULL;
		if (!client->file_upload_handler (
			client, packet->connection,
			file_header,
			file_data, file_data_len,
			&saved_filename
		)) {
			client->file_stats->n_success_files_uploaded += 1;

			client->file_stats->n_bytes_received += file_header->len;

			if (client->file_upload_cb) {
				client->file_upload_cb (
					packet->client, packet->connection,
					saved_filename
				);
			}

			if (saved_filename) free (saved_filename);
		}

		else {
			cerver_log_error (
				"client_request_send_file () - "
				"Failed to receive file"
			);

			client->file_stats->n_bad_files_received += 1;
		}
	}

	else {
		#ifdef CLIENT_DEBUG
		cerver_log_warning (
			"client_request_send_file () - "
			"missing file header"
		);
		#endif

		// return a bad request error packet
		(void) client_error_packet_generate_and_send (
			CLIENT_ERROR_SEND_FILE, "Missing or invalid file header",
			client, packet->connection
		);

		client->file_stats->n_bad_files_upload_requests += 1;
	}

}

// request from a cerver to receive a file
static void client_request_send_file (Packet *packet) {

	// check if the client is able to process the request
	if (packet->client->file_upload_handler && packet->client->uploads_path) {
		client_request_send_file_actual (packet);
	}

	else {
		// return a bad request error packet
		(void) client_error_packet_generate_and_send (
			CLIENT_ERROR_SEND_FILE, "Unable to process request",
			packet->client, packet->connection
		);

		#ifdef CLIENT_DEBUG
		cerver_log_warning (
			"Client %s is unable to handle REQUEST_PACKET_TYPE_SEND_FILE packets!",
			packet->client->name
		);
		#endif
	}

}

// handles a request made from the cerver
static void client_request_packet_handler (Packet *packet) {

	switch (packet->header.request_type) {
		// request from a cerver to get a file
		case REQUEST_PACKET_TYPE_GET_FILE:
			client_request_get_file (packet);
			break;

		// request from a cerver to receive a file
		case REQUEST_PACKET_TYPE_SEND_FILE:
			client_request_send_file (packet);
			break;

		default:
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
				"Unknown request from cerver"
			);
			break;
	}

}

// get the token from the packet data
// returns 0 on succes, 1 on error
static u8 auth_strip_token (Packet *packet, Client *client) {

	u8 retval = 1;

	SessionToken token;
	if (packet_get_object (packet, &session_token_schema, NULL, &token) == SERIALIZER_RESULT_NONE) {
		retval = client_set_session_id (client, token.token);
	}

	return retval;

}

// the success of an UDP connection has the session token (when the cerver uses sessions)
// followed by the datagram token, that its datagrams have from now on
// returns 0 on success, 1 if the packet does not have them
static u8 client_auth_success_datagram (Packet *packet) {

	u8 retval = 1;

	const bool valid = packet->data && (packet->data_size >= CONNECTION_DATAGRAM_TOKEN_SIZE);
	const size_t session_size = valid ? (packet->data_size - CONNECTION_DATAGRAM_TOKEN_SIZE) : 0;

	SessionToken token;
	const bool valid_session = !session_size || (serializer_decode (
		&session_token_schema, packet->data, session_size, NULL, &token
	) == SERIALIZER_RESULT_NONE);

	if (valid && valid_session) {
		const u8 *data = (const u8 *) packet->data;

		if (session_size) (void) client_set_session_id (packet->client, token.token);

		connection_set_datagram_token (packet->connection, data + session_size);

		retval = 0;
	}

	return retval;

}

static void client_auth_success_session (Packet *packet) {

	if (packet->connection->cerver_report) {
		if (packet->connection->cerver_report->uses_sessions) {
			if (!auth_strip_token (packet, packet->client)) {
				#ifdef AUTH_DEBUG
				cerver_log_debug (
					"Got client's <%s> session id <%s>",
					packet->client->name,
					packet->client->session_id->str
				);
				#endif
			}
		}
	}

}

static void client_auth_success_handler (Packet *packet) {

	if (packet->connection->protocol == PROTOCOL_UDP) {
		if (client_auth_success_datagram (packet)) return;
	}

	else {
		client_auth_success_session (packet);
	}

	packet->connection->authenticated = true;

	client_event_trigger (
		CLIENT_EVENT_SUCCESS_AUTH,
		packet->client, packet->connection
	);

}

static void client_auth_packet_handler (Packet *packet) {

	switch (packet->header.request_type) {
		// cerver requested authentication, if not, we will be disconnected
		case AUTH_PACKET_TYPE_REQUEST_AUTH:
			break;

		// we recieve a token from the cerver to use in sessions
		case AUTH_PACKET_TYPE_CLIENT_AUTH:
			break;

		// we have successfully authenticated with the server
		case AUTH_PACKET_TYPE_SUCCESS:
			client_auth_success_handler (packet);
			break;

		default:
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Unknown auth packet type"
			);
			break;
	}

}

// handles a PACKET_TYPE_APP packet type
static void client_app_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->client->app_packet_handler, packet, true,
		packet->client->name, "app_packet_handler"
	);

}

// handles a PACKET_TYPE_APP_ERROR packet type
static void client_app_error_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->client->app_error_packet_handler, packet, true,
		packet->client->name, "app_error_packet_handler"
	);

}

// handles a PACKET_TYPE_CUSTOM packet type
static void client_custom_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->client->custom_packet_handler, packet, true,
		packet->client->name, "custom_packet_handler"
	);

}

// the client handles a packet based on its type
// a bad packet that the cerver sent: the connection is ended when it reaches the client limit,
// its socket is shut down, so its receive loop ends it like when the cerver closes it
// returns CLIENT_HANDLER_ERROR_CLOSED when the connection is being ended
static ClientHandlerError client_bad_packet (const Packet *packet) {

	CERVER_STATS_ADD (packet->client->stats->received_packets->n_bad_packets, 1);

	packet->connection->bad_packets += 1;

	const u8 max_bad_packets = packet->client->max_bad_packets;
	if (!max_bad_packets || (packet->connection->bad_packets < max_bad_packets)) {
		return CLIENT_HANDLER_ERROR_NONE;
	}

	cerver_log_warning (
		"Client %s connection %s received %u bad packets - ending it...",
		packet->client->name, packet->connection->name,
		(unsigned int) packet->connection->bad_packets
	);

	(void) connection_drop_request (packet->connection);

	return CLIENT_HANDLER_ERROR_CLOSED;

}

static ClientHandlerError client_packet_handler_actual (
	Packet *packet
) {

	ClientHandlerError error = CLIENT_HANDLER_ERROR_NONE;	

	switch (packet->header.packet_type) {
		// a packet without a type is not handled, it was leaked
		case PACKET_TYPE_NONE:
			packet_delete (packet);
			break;

		// handles cerver type packets
		case PACKET_TYPE_CERVER:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_cerver_packets, 1);
			packet->connection->stats->received_packets->n_cerver_packets += 1;
			error = client_cerver_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles a client type packet
		case PACKET_TYPE_CLIENT:
			error = client_client_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles an error from the server
		case PACKET_TYPE_ERROR:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_error_packets, 1);
			packet->connection->stats->received_packets->n_error_packets += 1;
			client_error_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles a request made from the server
		case PACKET_TYPE_REQUEST:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_request_packets, 1);
			packet->connection->stats->received_packets->n_request_packets += 1;
			client_request_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles authentication packets
		case PACKET_TYPE_AUTH:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_auth_packets, 1);
			packet->connection->stats->received_packets->n_auth_packets += 1;
			client_auth_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles a game packet sent from the server
		case PACKET_TYPE_GAME:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_game_packets, 1);
			packet->connection->stats->received_packets->n_game_packets += 1;
			game_client_packet_handler (packet);
			packet_delete (packet);
			break;

		// user set handler to handler app specific packets
		case PACKET_TYPE_APP:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_app_packets, 1);
			packet->connection->stats->received_packets->n_app_packets += 1;
			client_app_packet_handler (packet);
			break;

		// user set handler to handle app specific errors
		case PACKET_TYPE_APP_ERROR:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_app_error_packets, 1);
			packet->connection->stats->received_packets->n_app_error_packets += 1;
			client_app_error_packet_handler (packet);
			break;

		// custom packet hanlder
		case PACKET_TYPE_CUSTOM:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_custom_packets, 1);
			packet->connection->stats->received_packets->n_custom_packets += 1;
			client_custom_packet_handler (packet);
			break;

		// handles a test packet form the cerver
		case PACKET_TYPE_TEST:
			CERVER_STATS_ADD (packet->client->stats->received_packets->n_test_packets, 1);
			packet->connection->stats->received_packets->n_test_packets += 1;
			cerver_log (LOG_TYPE_TEST, LOG_TYPE_NONE, "Got a test packet from cerver");
			packet_delete (packet);
			break;

		default:
			packet->connection->stats->received_packets->n_bad_packets += 1;
			error = client_bad_packet (packet);
			packet_delete (packet);
			break;
	}

	return error;

}

static ClientHandlerError client_packet_handler_check_version (
	Packet *packet
) {

	// the version came in the header, it used to be expected in the data,
	// and the check was inverted, so every valid packet was rejected
	// a rejected packet is handled by client_bad_packet ()
	return packet_check (packet) ? CLIENT_HANDLER_ERROR_NONE : CLIENT_HANDLER_ERROR_PACKET;

}

u8 client_packet_handler (Packet *packet) {

	u8 retval = 1;

	// update general stats
	CERVER_STATS_ADD (packet->client->stats->n_packets_received, 1);

	ClientHandlerError error = CLIENT_HANDLER_ERROR_NONE;
	if (packet->client->check_packets) {
		if (!client_packet_handler_check_version (packet)) {
			error = client_packet_handler_actual (packet);
		}

		// a rejected packet is a bad one, it used to be leaked
		else {
			error = client_bad_packet (packet);
			packet_delete (packet);
		}
	}

	else {
		error = client_packet_handler_actual (packet);
	}

	switch (error) {
		case CLIENT_HANDLER_ERROR_NONE:
			retval = 0;
			break;

		default: break;
	}

	return retval;

}

static u8 client_receive_dispatch (
	ReceiveHandle *receive_handle, Packet *packet
) {

	(void) receive_handle;

	return client_packet_handler (packet);

}

// assembles packets from the received buffers, no matter where the buffers are split
void client_receive_handle_buffer (
	ReceiveHandle *receive_handle
) {

	receive_handle_buffer (
		receive_handle,
		receive_handle->client->max_received_packet_size,
		client_receive_dispatch
	);

}

// a bad datagram of the cerver: the connection is ended when it reaches the client limit,
// its socket is shut down, so its receive loop ends it like when the cerver closes it
static void client_receive_datagram_bad (Client *client, Connection *connection) {

	CERVER_STATS_ADD (client->stats->received_packets->n_bad_packets, 1);

	connection->bad_packets += 1;

	if (client->max_bad_packets && (connection->bad_packets >= client->max_bad_packets)) {
		cerver_log_warning (
			"Client %s connection %s received %u bad datagrams - ending it...",
			client->name, connection->name, (unsigned int) connection->bad_packets
		);

		(void) connection_drop_request (connection);
	}

}

// a datagram has a single packet, followed by the token of the connection once it authenticated
// returns 0 when its packet was handled, 1 when it was a bad datagram
u8 client_receive_datagram (
	Client *client, Connection *connection,
	const char *datagram, const size_t size
) {

	u8 retval = 1;

	PacketHeader header;
	const UdpDatagram kind = udp_datagram_check (
		datagram, size, client->max_received_packet_size, &header
	);

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	const bool valid = connection_get_datagram_token (connection, token)
		? ((kind == UDP_DATAGRAM_TOKEN) && udp_datagram_token_matches (datagram, &header, token))
		: (kind == UDP_DATAGRAM_PLAIN);

	Packet *packet = valid ? udp_datagram_packet_create (datagram, &header) : NULL;
	if (packet) {
		packet->client = client;
		packet->connection = connection;

		(void) client_packet_handler (packet);

		retval = 0;
	}

	else {
		client_receive_datagram_bad (client, connection);
	}

	return retval;

}

// handles a failed recive from a connection associatd with a client
// in its update thread, the end is deferred until its receive loop ends (unless another thread owns it),
// in any other thread (a direct receive), the connection is ended now
// a client without connections stops running
static void client_receive_handle_failed (
	Client *client, Connection *connection
) {

	if (connection_is_update_thread (connection)) {
		// the update thread connects again when its receive loop ends
		if (!connection->attempt_reconnect || !client_is_running (client)) {
			(void) client_connection_end_request (connection);
		}
	}

	else if (connection->active) {
		(void) client_connection_end (client, connection);
	}

}

// a recv () of 0 bytes in an UDP socket that was not shut down is an empty datagram
static bool client_receive_is_empty_datagram (const Connection *connection) {

	return (connection->protocol == PROTOCOL_UDP)
		&& !__atomic_load_n (&connection->shut_down, __ATOMIC_ACQUIRE);

}

// performs the actual recv () method on the connection's sock fd
// handles if the receive method failed
// the amount of bytes read from the socket is placed in rc
ReceiveError client_receive_actual (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t *rc
) {

	ReceiveError error = RECEIVE_ERROR_NONE;

	ssize_t received = recv (
		connection->socket->sock_fd,
		buffer, buffer_size,
		connection->receive_flags
	);

	switch (received) {
		case -1: {
			if (errno == EAGAIN) {
				#ifdef SOCKET_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
					"client_receive_internal () - connection %s sock fd: %d timed out",
					connection->name, connection->socket->sock_fd
				);
				#endif

				error = RECEIVE_ERROR_TIMEOUT;
			}

			else {
				#ifdef CONNECTION_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
					"client_receive_internal () - rc < 0 - connection %s sock fd: %d",
					connection->name, connection->socket->sock_fd
				);

				perror ("Error ");
				#endif

				client_receive_handle_failed (client, connection);

				error = RECEIVE_ERROR_FAILED;
			}
		} break;

		case 0: {
			// an empty datagram is a bad one, it does not end the connection
			if (client_receive_is_empty_datagram (connection)) break;

			#ifdef CONNECTION_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
				"client_receive_internal () - rc == 0 - connection %s sock fd: %d",
				connection->name, connection->socket->sock_fd
			);

			// perror ("Error ");
			#endif

			client_receive_handle_failed (client, connection);

			error = RECEIVE_ERROR_EMPTY;
		} break;

		default: {
			#ifdef CLIENT_RECEIVE_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
				"client_receive_actual () - received %ld from connection %s",
				received, connection->name
			);
			#endif
		} break;
	}

	*rc = (size_t) ((received > 0) ? received : 0);

	return error;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// request to read x amount of bytes from the connection's sock fd
// into the specified buffer
// this method will only return once the requested bytes
// have been received or on any error
ReceiveError client_receive_data (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t requested_data
) {

	ReceiveError error = RECEIVE_ERROR_NONE;
	size_t received = 0;

	size_t data_size = requested_data;

	char *buffer_end = buffer;
	// size_t buffer_pos = 0;

	do {
		error = client_receive_actual (
			client, connection,
			buffer_end, data_size,
			&received
		);

		if (error == RECEIVE_ERROR_NONE) {
			// we got some data
			data_size -= received;

			buffer_end += received;
		}

		else if (error == RECEIVE_ERROR_TIMEOUT) {
			// we are still waiting to get more data
		}

		else {
			// an error has ocurred or we have been disconnected
			// so end the loop
			break;
		}
	} while (data_size > 0);

	return (data_size > 0) ? RECEIVE_ERROR_FAILED : RECEIVE_ERROR_NONE;

}

#pragma GCC diagnostic pop

// receive data from connection's socket
// this method does not perform any checks and expects a valid buffer
// to handle incomming data
// returns 0 on success, 1 on error
unsigned int client_receive_internal (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size
) {

	unsigned int retval = 1;

	size_t received = 0;
	
	ReceiveError error = client_receive_actual (
		client, connection,
		buffer, buffer_size,
		&received
	);

	// the update threads of all the connections of the client update its stats
	CERVER_STATS_ADD (client->stats->n_receives_done, 1);
	CERVER_STATS_ADD (client->stats->total_bytes_received, received);

	#ifdef CONNECTION_STATS
	connection->stats->n_receives_done += 1;
	connection->stats->total_bytes_received += received;
	#endif

	switch (error) {
		// a datagram has a single packet, the bytes of a stream are assembled into packets
		case RECEIVE_ERROR_NONE: {
			if (connection->protocol == PROTOCOL_UDP) {
				(void) client_receive_datagram (client, connection, buffer, received);
			}

			else {
				connection->receive_handle.buffer = buffer;
				connection->receive_handle.buffer_size = buffer_size;
				connection->receive_handle.received_size = received;

				client_receive_handle_buffer (
					&connection->receive_handle
				);
			}

			retval = 0;
		} break;

		case RECEIVE_ERROR_TIMEOUT: {
			retval = 0;
		} break;

		default: break;
	}

	return retval;

}

// allocates a new packet buffer to receive incoming data from the connection's socket
// returns 0 on success handle
// returns 1 if any error ocurred and must likely the connection was ended
unsigned int client_receive (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		char *packet_buffer = (char *) calloc (
			connection->receive_packet_buffer_size, sizeof (char)
		);

		if (packet_buffer) {
			retval = client_receive_internal (
				client, connection,
				packet_buffer, connection->receive_packet_buffer_size
			);

			free (packet_buffer);
		}

		else {
			// #ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CONNECTION,
				"client_receive () - Failed to allocate a new packet buffer!"
			);
			// #endif
		}
	}

	return retval;

}

#pragma endregion
