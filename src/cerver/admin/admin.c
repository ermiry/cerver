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
#include "cerver/admin/internal.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region admin

const char *admin_connections_status_to_string (
	const AdminConnectionsStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case ADMIN_CONNECTIONS_STATUS_##name: return #string;
		ADMIN_CONNECTIONS_STATUS_MAP(XX)
		#undef XX
	}

	return admin_connections_status_to_string (ADMIN_CONNECTIONS_STATUS_NONE);

}

const char *admin_connections_status_description (
	const AdminConnectionsStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case ADMIN_CONNECTIONS_STATUS_##name: return #description;
		ADMIN_CONNECTIONS_STATUS_MAP(XX)
		#undef XX
	}

	return admin_connections_status_description (ADMIN_CONNECTIONS_STATUS_NONE);

}

Admin *admin_new (void) {

	Admin *admin = (Admin *) malloc (sizeof (Admin));
	if (admin) {
		admin->admin_cerver = NULL;

		admin->id = NULL;

		admin->client = NULL;

		admin->data = NULL;
		admin->delete_data = NULL;

		admin->bad_packets = 0;
	}

	return admin;

}

void admin_delete (void *admin_ptr) {

	if (admin_ptr) {
		Admin *admin = (Admin *) admin_ptr;

		str_delete (admin->id);

		client_delete (admin->client);

		if (admin->data) {
			if (admin->delete_data) admin->delete_data (admin->data);
		}

		free (admin);
	}

}

Admin *admin_create (void) {

	Admin *admin = admin_new ();
	if (admin) {
		time_t rawtime = 0;
		(void) time (&rawtime);
		admin->id = str_create ("%ld-%d", rawtime, random_int_in_range (0, 100));
	}

	return admin;

}

Admin *admin_create_with_client (Client *client) {

	Admin *admin = NULL;

	if (client) {
		admin = admin_create ();
		if (admin) admin->client = client;
	}

	return admin;

}

int admin_comparator_with_id (const void *a, const void *b) {

	if (a && b) return strcmp (((Admin *) a)->id->str, ((Admin *) b)->id->str);

	else if (a && !b) return -1;
	else if (!a && b) return 1;
	return 0;

}

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
void admin_set_data (
	Admin *admin, void *data, Action delete_data
) {

	if (admin) {
		admin->data = data;
		admin->delete_data = delete_data;
	}

}

static Connection *admin_connection_get_with_sock_fd (
	Admin *admin, const i32 sock_fd
) {

	Connection *retval = NULL;

	if (admin) {
		Connection *connection = NULL;
		for (
			ListElement *le_sub = dlist_start (admin->client->connections);
			le_sub; le_sub = le_sub->next
		) {
			connection = (Connection *) le_sub->data;
			if (connection->socket->sock_fd == sock_fd) {
				retval = connection;
				break;
			}
		}
	}

	return retval;

}

// gets an admin by a matching connection in its client with the specified sock fd
Admin *admin_get_with_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
) {

	Admin *retval = NULL;

	if (admin_cerver) {
		// the on hold poll registers the admins while the admin poll searches them
		(void) pthread_mutex_lock (admin_cerver->admins->mutex);

		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			if (admin_connection_get_with_sock_fd (
				(Admin *) le->data, sock_fd)
			) {
				retval = (Admin *) le->data;
				break;
			}
		}

		(void) pthread_mutex_unlock (admin_cerver->admins->mutex);
	}

	return retval;

}

// gets an admin by a matching client's session id
Admin *admin_get_with_session_id (
	AdminCerver *admin_cerver, const char *session_id
) {

	Admin *retval = NULL;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->admins->mutex);

		Admin *admin = NULL;
		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			admin = (Admin *) le->data;
			if (admin->client->session_id) {
				if (!strcmp (admin->client->session_id->str, session_id)) {
					retval = (Admin *) le->data;
					break;
				}
			}
		}

		(void) pthread_mutex_unlock (admin_cerver->admins->mutex);
	}

	return retval;

}

// removes the connection from the admin referred to by the sock fd
// checks if there is another active connection in the admin, if not it will be dropped
// returns the resulting status after the operation
AdminConnectionsStatus admin_remove_connection_with_sock_fd (
	AdminCerver *admin_cerver,
	Admin *admin, const i32 sock_fd
) {

	AdminConnectionsStatus status = ADMIN_CONNECTIONS_STATUS_ERROR;

	if (admin_cerver && admin) {
		Connection *connection = NULL;
		switch (admin->client->connections->size) {
			case 0: {
				#ifdef ADMIN_DEBUG
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
					"admin_remove_connection_with_sock_fd () - Admin client with id "
					"%lu does not have ANY connection - removing him from cerver...",
					admin->client->id
				);
				#endif

				(void) admin_cerver_unregister_admin (admin_cerver, admin);
				admin_delete (admin);

				status = ADMIN_CONNECTIONS_STATUS_DROPPED;
			} break;

			case 1: {
				connection = (Connection *) admin->client->connections->start->data;
				if (!admin_cerver_poll_unregister_connection (
					admin_cerver, connection
				)) {
					// remove, close & delete the connection
					if (!client_connection_drop (
						admin_cerver->cerver,
						admin->client,
						connection
					)) {
						cerver_event_trigger (
							CERVER_EVENT_ADMIN_CLOSE_CONNECTION,
							admin_cerver->cerver,
							NULL, NULL
						);

						// no connections left in admin, just remove and delete
						(void) admin_cerver_unregister_admin (admin_cerver, admin);
						admin_delete (admin);

						cerver_event_trigger (
							CERVER_EVENT_ADMIN_DROPPED,
							admin_cerver->cerver,
							NULL, NULL
						);

						status = ADMIN_CONNECTIONS_STATUS_DROPPED;
					}
				}
			} break;

			default: {
				connection = admin_connection_get_with_sock_fd (admin, sock_fd);
				if (connection) {
					if (!admin_cerver_poll_unregister_connection (
						admin_cerver, connection
					)) {
						if (!client_connection_drop (
							admin_cerver->cerver,
							admin->client,
							connection
						)) {
							cerver_event_trigger (
								CERVER_EVENT_ADMIN_CLOSE_CONNECTION,
								admin_cerver->cerver,
								NULL, NULL
							);

							status = ADMIN_CONNECTIONS_STATUS_NONE;
						}
					}
				}

				else {
					#ifdef ADMIN_DEBUG
					cerver_log (
						LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
						"admin_remove_connection_with_sock_fd () - Admin client with id "
						"%lu does not have a connection related to sock fd %d",
						admin->client->id, sock_fd
					);
					#endif
				}
			} break;
		}
	}

	return status;

}

// sends a packet to the first connection of the specified admin
// returns 0 on success, 1 on error
u8 admin_send_packet (Admin *admin, Packet *packet) {

	u8 retval = 1;

	if (admin && packet) {
		// printf (
		//     "admin client: %ld -- sock fd: %d\n",
		//     admin->client->id,
		//     ((Connection *) dlist_start (admin->client->connections))->socket->sock_fd
		// );

		packet_set_network_values (
			packet,
			NULL,
			admin->client,
			(Connection *) dlist_start (admin->client->connections)->data,
			NULL
		);

		size_t sent = 0;
		if (!packet_send (packet, 0, &sent, false)) {
			// printf ("admin_send_packet () - Sent to admin: %ld\n", sent);

			admin_cerver_packet_send_update_stats (
				admin->admin_cerver->stats, packet->packet_type, sent
			);

			retval = 0;
		}

		else {
			cerver_log_error ("Failed to send packet to admin!");
		}
	}

	return retval;

}

// sends a packet to the first connection of the specified admin using packet_send_to_split ()
// returns 0 on success, 1 on error
u8 admin_send_packet_split (Admin *admin, Packet *packet) {

	u8 retval = 1;

	if (admin && packet) {
		size_t sent = 0;
		if (!packet_send_to_split (
			packet, &sent,
			NULL,
			admin->client,
			(Connection *) dlist_start (admin->client->connections)->data,
			NULL
		)) {
			// (void) printf (
			// 	"admin_send_packet_split () - Sent to admin: %ld\n",
			// 	sent
			// );

			admin_cerver_packet_send_update_stats (
				admin->admin_cerver->stats, packet->packet_type, sent
			);

			retval = 0;
		}

		else {
			cerver_log_error (
				"admin_send_packet_split () - Failed to send packet!"
			);
		}
	}

	return retval;

}

// sends a packet in pieces to the first connection of the specified admin
// returns 0 on success, 1 on error
u8 admin_send_packet_pieces (
	Admin *admin, Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
) {

	u8 retval = 1;

	if (admin && packet) {
		packet_set_network_values (
			packet,
			NULL,
			admin->client,
			(Connection *) dlist_start (admin->client->connections)->data,
			NULL
		);

		size_t sent = 0;
		if (!packet_send_pieces (
			packet,
			pieces, sizes, n_pieces,
			0, &sent
		)) {
			// (void) printf (
			// 	"admin_send_packet_pieces () - Sent to admin: %lu\n",
			// 	sent
			// );

			admin_cerver_packet_send_update_stats (
				admin->admin_cerver->stats, packet->packet_type, sent
			);

			retval = 0;
		}

		else {
			cerver_log_error ("admin_send_packet_in_pieces () - Failed to send packet!");
		}
	}

	return retval;

}

#pragma endregion
