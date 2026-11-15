#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/poll.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"
#include "cerver/admin/handler.h"
#include "cerver/admin/internal.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region handler

const char *admin_cerver_handler_error_to_string (
	const AdminCerverHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case ADMIN_CERVER_HANDLER_ERROR_##name: return #string;
		ADMIN_CERVER_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return admin_cerver_handler_error_to_string (ADMIN_CERVER_HANDLER_ERROR_NONE);

}

const char *admin_cerver_handler_error_description (
	const AdminCerverHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case ADMIN_CERVER_HANDLER_ERROR_##name: return #description;
		ADMIN_CERVER_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return admin_cerver_handler_error_description (ADMIN_CERVER_HANDLER_ERROR_NONE);

}

// handles a packet of type PACKET_TYPE_CLIENT
static AdminCerverHandlerError admin_cerver_client_packet_handler (
	Packet *packet
) {

	AdminCerverHandlerError error = ADMIN_CERVER_HANDLER_ERROR_NONE;

	switch (packet->header.request_type) {
		// the client is going to close its current connection
		// but will remain in the cerver if it has another connection active
		// if not, it will be dropped
		case CLIENT_PACKET_TYPE_CLOSE_CONNECTION: {
			#ifdef ADMIN_DEBUG
			cerver_log_debug (
				"Admin with client %lu requested to close the connection",
				packet->client->id
			);
			#endif

			switch (admin_remove_connection_with_sock_fd (
				packet->cerver->admin,
				admin_get_with_sock_fd (
					packet->cerver->admin,
					packet->connection->socket->sock_fd
				),
				packet->connection->socket->sock_fd
			)) {
				case ADMIN_CONNECTIONS_STATUS_DROPPED:
					error = ADMIN_CERVER_HANDLER_ERROR_DROPPED;
					break;

				default: break;
			}
		} break;

		// the client is going to disconnect and will close all of its active connections
		// so drop it from the server
		case CLIENT_PACKET_TYPE_DISCONNECT: {
			if (!admin_cerver_drop_admin (
				packet->cerver->admin,
				admin_get_with_sock_fd (
					packet->cerver->admin,
					packet->connection->socket->sock_fd
				)
			)) {
				cerver_event_trigger (
					CERVER_EVENT_ADMIN_DISCONNECTED,
					packet->cerver,
					NULL, NULL
				);

				error = ADMIN_CERVER_HANDLER_ERROR_DROPPED;
			}
		} break;

		default: {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
				"admin_cerver_client_packet_handler () - "
				"Got an unknown client packet in ADMIN cerver %s",
				packet->cerver->info->name
			);
			#endif
		} break;
	}

	return error;

}

// handles a request made from the admin
static void admin_cerver_request_packet_handler (Packet *packet) {

	switch (packet->header.request_type) {
		// request from a client to get a file
		case REQUEST_PACKET_TYPE_GET_FILE:
			cerver_request_get_file (packet);
			break;

		// request from a client to upload a file
		case REQUEST_PACKET_TYPE_SEND_FILE:
			cerver_request_send_file (packet);
			break;

		default: {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
				"Got an unknown request packet in ADMIN cerver %s",
				packet->cerver->info->name
			);
			#endif
		} break;
	}

}

// handles an PACKET_TYPE_APP packet type
static void admin_app_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->cerver->admin->app_packet_handler, packet,
		packet->cerver->admin->app_packet_handler_delete_packet,
		packet->cerver->info->name, "admin app_packet_handler"
	);

}

// handles an PACKET_TYPE_APP_ERROR packet type
static void admin_app_error_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->cerver->admin->app_error_packet_handler, packet,
		packet->cerver->admin->app_error_packet_handler_delete_packet,
		packet->cerver->info->name, "admin app_error_packet_handler"
	);

}

// handles a PACKET_TYPE_CUSTOM packet type
static void admin_custom_packet_handler (Packet *packet) {

	handler_packet_dispatch (
		packet->cerver->admin->custom_packet_handler, packet,
		packet->cerver->admin->custom_packet_handler_delete_packet,
		packet->cerver->info->name, "admin custom_packet_handler"
	);

}

static const char *admin_bad_packet_version = { "The packet has an incompatible version" };
static const char *admin_bad_packet_type = { "The packet has an unknown type" };

// a bad packet of an admin connection: its sender gets an error,
// and the connection is dropped when it reaches the admin cerver limit
// returns ADMIN_CERVER_HANDLER_ERROR_DROPPED when the connection is being dropped
static AdminCerverHandlerError admin_bad_packet (const Packet *packet, const char *reason) {

	packet->cerver->admin->stats->received_packets->n_bad_packets += 1;

	(void) error_packet_generate_and_send (
		CERVER_ERROR_PACKET_ERROR, reason,
		packet->cerver, packet->client, packet->connection
	);

	packet->connection->bad_packets += 1;

	if (packet->connection->bad_packets < packet->cerver->admin->n_bad_packets_limit) {
		return ADMIN_CERVER_HANDLER_ERROR_NONE;
	}

	cerver_log_warning (
		"Cerver %s admin connection <%d> sent %u bad packets - dropping...",
		packet->cerver->info->name, packet->connection->socket->sock_fd,
		(unsigned int) packet->connection->bad_packets
	);

	// the admin poll removes it, like when the admin closes it
	(void) connection_drop_request (packet->connection);

	return ADMIN_CERVER_HANDLER_ERROR_DROPPED;

}

static AdminCerverHandlerError admin_packet_handler_actual (
	Packet *packet
) {

	AdminCerverHandlerError error = ADMIN_CERVER_HANDLER_ERROR_NONE;

	switch (packet->header.packet_type) {
		case PACKET_TYPE_CLIENT:
			packet->cerver->admin->stats->received_packets->n_client_packets += 1;
			packet->client->stats->received_packets->n_client_packets += 1;
			packet->connection->stats->received_packets->n_client_packets += 1;
			error = admin_cerver_client_packet_handler (packet);
			packet_delete (packet);
			break;

		// handles a request made from the admin
		case PACKET_TYPE_REQUEST:
			packet->cerver->admin->stats->received_packets->n_request_packets += 1;
			packet->client->stats->received_packets->n_request_packets += 1;
			packet->connection->stats->received_packets->n_request_packets += 1;
			admin_cerver_request_packet_handler (packet);
			packet_delete (packet);
			break;

		case PACKET_TYPE_APP:
			packet->cerver->admin->stats->received_packets->n_app_packets += 1;
			packet->client->stats->received_packets->n_app_packets += 1;
			packet->connection->stats->received_packets->n_app_packets += 1;
			admin_app_packet_handler (packet);
			break;

		case PACKET_TYPE_APP_ERROR:
			packet->cerver->admin->stats->received_packets->n_app_error_packets += 1;
			packet->client->stats->received_packets->n_app_error_packets += 1;
			packet->connection->stats->received_packets->n_app_error_packets += 1;
			admin_app_error_packet_handler (packet);
			break;

		case PACKET_TYPE_CUSTOM:
			packet->cerver->admin->stats->received_packets->n_custom_packets += 1;
			packet->client->stats->received_packets->n_custom_packets += 1;
			packet->connection->stats->received_packets->n_custom_packets += 1;
			admin_custom_packet_handler (packet);
			break;

		default: {
			packet->client->stats->received_packets->n_bad_packets += 1;
			packet->connection->stats->received_packets->n_bad_packets += 1;

			error = admin_bad_packet (packet, admin_bad_packet_type);
			packet_delete (packet);
		} break;
	}

	return error;

}

static AdminCerverHandlerError admin_packet_handler_check_version (
	Packet *packet
) {

	AdminCerverHandlerError error = ADMIN_CERVER_HANDLER_ERROR_NONE;

	// the version came in the header, it used to be expected in the data,
	// and the check was inverted, so every valid packet was rejected
	// a rejected packet is handled by admin_bad_packet ()
	if (packet->cerver->check_packets && !packet_check (packet)) {
		error = ADMIN_CERVER_HANDLER_ERROR_PACKET;
	}

	return error;

}

// handles a packet from an admin
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended or removed from admin poll
u8 admin_packet_handler (Packet *packet) {

	u8 retval = 1;

	switch (admin_packet_handler_check_version (packet)) {
		case ADMIN_CERVER_HANDLER_ERROR_NONE: {
			switch (admin_packet_handler_actual (packet)) {
				case ADMIN_CERVER_HANDLER_ERROR_NONE:
					retval = 0;
					break;

				default: break;
			}
		} break;

		// a rejected packet is a bad one, it used to be leaked
		case ADMIN_CERVER_HANDLER_ERROR_PACKET:
			retval = (admin_bad_packet (packet, admin_bad_packet_version) == ADMIN_CERVER_HANDLER_ERROR_NONE) ? 0 : 1;
			packet_delete (packet);
			break;

		default: break;
	}

	return retval;

}

#pragma endregion
