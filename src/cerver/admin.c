#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/config.h"

#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <sys/poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/admin.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/events.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/bsem.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

u8 admin_cerver_poll_register_connection (
	AdminCerver *admin_cerver, Connection *connection
);

static u8 admin_cerver_poll_unregister_connection (
	AdminCerver *admin_cerver, Connection *connection
);

#pragma region stats

static AdminCerverStats *admin_cerver_stats_new (void) {

	AdminCerverStats *admin_cerver_stats = (AdminCerverStats *) malloc (sizeof (AdminCerverStats));
	if (admin_cerver_stats) {
		(void) memset (admin_cerver_stats, 0, sizeof (AdminCerverStats));
		admin_cerver_stats->received_packets = packets_per_type_new ();
		admin_cerver_stats->sent_packets = packets_per_type_new ();
	}

	return admin_cerver_stats;

}

static void admin_cerver_stats_delete (AdminCerverStats *admin_cerver_stats) {

	if (admin_cerver_stats) {
		packets_per_type_delete (admin_cerver_stats->received_packets);
		packets_per_type_delete (admin_cerver_stats->sent_packets);

		free (admin_cerver_stats);
	}

}

void admin_cerver_stats_print (AdminCerverStats *stats) {

	if (stats) {
		cerver_log_msg ("\nAdmin stats:");
		cerver_log_msg ("Threshold_time: %ld\n", stats->threshold_time);

		cerver_log_msg ("Total n receives done:                  %ld", stats->total_n_receives_done);
		cerver_log_msg ("Total n packets received:               %ld", stats->total_n_packets_received);
		cerver_log_msg ("Total bytes received:                   %ld\n", stats->total_bytes_received);

		cerver_log_msg ("Total n packets sent:                   %ld", stats->total_n_packets_sent);
		cerver_log_msg ("Total bytes sent:                       %ld\n", stats->total_bytes_sent);

		cerver_log_msg ("Current connections:                    %ld", stats->current_connections);
		cerver_log_msg ("Current connected admins:               %ld\n", stats->current_connected_admins);

		cerver_log_msg ("Total admin connections:                %ld", stats->total_admin_connections);
		cerver_log_msg ("Total n admins:                         %ld", stats->total_n_admins);

		cerver_log_msg ("\nReceived packets:");
		packets_per_type_print (stats->received_packets);

		cerver_log_msg ("\nSent packets:");
		packets_per_type_print (stats->sent_packets);
	}

}

static void admin_cerver_packet_send_update_stats (
	AdminCerverStats *stats,
	PacketType packet_type, size_t sent
) {

	stats->total_n_packets_sent += 1;
	stats->total_bytes_sent += sent;

	switch (packet_type) {
		case PACKET_TYPE_NONE: break;

		case PACKET_TYPE_CERVER: stats->sent_packets->n_cerver_packets += 1; break;

		case PACKET_TYPE_CLIENT: break;

		case PACKET_TYPE_ERROR: stats->sent_packets->n_error_packets += 1; break;

		case PACKET_TYPE_REQUEST: stats->sent_packets->n_request_packets += 1; break;

		case PACKET_TYPE_AUTH: stats->sent_packets->n_auth_packets += 1; break;

		case PACKET_TYPE_GAME: stats->sent_packets->n_game_packets += 1; break;

		case PACKET_TYPE_APP: stats->sent_packets->n_app_packets += 1; break;

		case PACKET_TYPE_APP_ERROR: stats->sent_packets->n_app_error_packets += 1; break;

		case PACKET_TYPE_CUSTOM: stats->sent_packets->n_custom_packets += 1; break;

		case PACKET_TYPE_TEST: stats->sent_packets->n_test_packets += 1; break;

		default: stats->sent_packets->n_unknown_packets += 1; break;
	}

}

#pragma endregion

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

int admin_comparator_by_id (const void *a, const void *b) {

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

static Connection *admin_connection_get_by_sock_fd (
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
Admin *admin_get_by_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
) {

	Admin *retval = NULL;

	if (admin_cerver) {
		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			if (admin_connection_get_by_sock_fd (
				(Admin *) le->data, sock_fd)
			) {
				retval = (Admin *) le->data;
				break;
			}
		}
	}

	return retval;

}

// gets an admin by a matching client's session id
Admin *admin_get_by_session_id (
	AdminCerver *admin_cerver, const char *session_id
) {

	Admin *retval = NULL;

	if (admin_cerver) {
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
	}

	return retval;

}

// removes the connection from the admin referred to by the sock fd
// checks if there is another active connection in the admin, if not it will be dropped
// returns the resulting status after the operation
AdminConnectionsStatus admin_remove_connection_by_sock_fd (
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
					"admin_remove_connection_by_sock_fd () - Admin client with id "
					"%ld does not have ANY connection - removing him from cerver...",
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
				connection = admin_connection_get_by_sock_fd (admin, sock_fd);
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
						"admin_remove_connection_by_sock_fd () - Admin client with id "
						"%ld does not have a connection related to sock fd %d",
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

#pragma region main

AdminCerver *admin_cerver_new (void) {

	AdminCerver *admin_cerver = (AdminCerver *) malloc (sizeof (AdminCerver));
	if (admin_cerver) {
		admin_cerver->cerver = NULL;

		admin_cerver->admins = NULL;

		admin_cerver->authenticate = NULL;

		admin_cerver->max_admins = ADMIN_CERVER_DEFAULT_MAX_ADMINS;
		admin_cerver->max_admin_connections = ADMIN_CERVER_DEFAULT_MAX_ADMIN_CONNECTIONS;

		admin_cerver->n_bad_packets_limit = ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS;

		admin_cerver->receive_buffer_size = ADMIN_CERVER_DEFAULT_RECEIVE_BUFFER_SIZE;

		admin_cerver->fds = NULL;
		admin_cerver->max_n_fds = ADMIN_CERVER_DEFAULT_POLL_FDS;
		admin_cerver->current_n_fds = 0;
		admin_cerver->poll_timeout = ADMIN_CERVER_DEFAULT_POLL_TIMEOUT;
		admin_cerver->poll_lock = NULL;

		admin_cerver->app_packet_handler = NULL;
		admin_cerver->app_error_packet_handler = NULL;
		admin_cerver->custom_packet_handler = NULL;

		admin_cerver->num_handlers_alive = 0;
		admin_cerver->num_handlers_working = 0;
		admin_cerver->handlers_lock = NULL;

		admin_cerver->app_packet_handler_delete_packet = true;
		admin_cerver->app_error_packet_handler_delete_packet = true;
		admin_cerver->custom_packet_handler_delete_packet = true;

		admin_cerver->check_packets = ADMIN_CERVER_DEFAULT_CHECK_PACKETS;

		admin_cerver->update_thread_id = 0;
		admin_cerver->update = NULL;
		admin_cerver->update_args = NULL;
		admin_cerver->delete_update_args = NULL;
		admin_cerver->update_ticks = ADMIN_CERVER_DEFAULT_UPDATE_TICKS;

		admin_cerver->update_interval_thread_id = 0;
		admin_cerver->update_interval = NULL;
		admin_cerver->update_interval_args = NULL;
		admin_cerver->delete_update_interval_args = NULL;
		admin_cerver->update_interval_secs = ADMIN_CERVER_DEFAULT_UPDATE_INTERVAL_SECS;

		admin_cerver->stats = NULL;
	}

	return admin_cerver;

}

void admin_cerver_delete (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		dlist_delete (admin_cerver->admins);

		if (admin_cerver->fds) free (admin_cerver->fds);

		if (admin_cerver->poll_lock) {
			pthread_mutex_destroy (admin_cerver->poll_lock);
			free (admin_cerver->poll_lock);
		}

		handler_delete (admin_cerver->app_packet_handler);
		handler_delete (admin_cerver->app_error_packet_handler);
		handler_delete (admin_cerver->custom_packet_handler);

		if (admin_cerver->handlers_lock) {
			pthread_mutex_destroy (admin_cerver->handlers_lock);
			free (admin_cerver->handlers_lock);
		}

		admin_cerver_stats_delete (admin_cerver->stats);

		free (admin_cerver);
	}

}

AdminCerver *admin_cerver_create (void) {

	AdminCerver *admin_cerver = admin_cerver_new ();
	if (admin_cerver) {
		admin_cerver->admins = dlist_init (admin_delete, admin_comparator_by_id);

		admin_cerver->stats = admin_cerver_stats_new ();
	}

	return admin_cerver;

}

// sets the authentication methods to be used to successfully authenticate admin credentials
// must return 0 on success, 1 on error
void admin_cerver_set_authenticate (
	AdminCerver *admin_cerver, delegate authenticate
) {

	if (admin_cerver) admin_cerver->authenticate = authenticate;

}

// sets the max numbers of admins allowed at any given time
void admin_cerver_set_max_admins (
	AdminCerver *admin_cerver, u8 max_admins
) {

	if (admin_cerver) admin_cerver->max_admins = max_admins;

}

// sets the max number of connections allowed per admin
void admin_cerver_set_max_admin_connections (
	AdminCerver *admin_cerver, u8 max_admin_connections
) {

	if (admin_cerver) admin_cerver->max_admin_connections = max_admin_connections;

}

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
void admin_cerver_set_bad_packets_limit (
	AdminCerver *admin_cerver, i32 n_bad_packets_limit
) {

	if (admin_cerver) {
		admin_cerver->n_bad_packets_limit = n_bad_packets_limit > 0 ?
			n_bad_packets_limit : ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS;
	}

}

// sets the admin cerver's receive buffer size used for recv ()
void admin_cerver_set_receive_buffer_size (
	AdminCerver *admin_cerver, const size_t buffer_size
) {

	if (admin_cerver) {
		admin_cerver->receive_buffer_size = buffer_size;
	}

}

// sets the max number of poll fds for the admin cerver
void admin_cerver_set_max_fds (
	AdminCerver *admin_cerver, u32 max_n_fds
) {

	if (admin_cerver) admin_cerver->max_n_fds = max_n_fds;

}

// sets a custom poll time out to use for admins
void admin_cerver_set_poll_timeout (
	AdminCerver *admin_cerver, u32 poll_timeout
) {

	if (admin_cerver) admin_cerver->poll_timeout = poll_timeout;

}

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
void admin_cerver_set_app_handlers (
	AdminCerver *admin_cerver, Handler *app_handler, Handler *app_error_handler
) {

	if (admin_cerver) {
		admin_cerver->app_packet_handler = app_handler;
		if (admin_cerver->app_packet_handler) {
			admin_cerver->app_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->app_packet_handler->cerver = admin_cerver->cerver;
		}

		admin_cerver->app_error_packet_handler = app_error_handler;
		if (admin_cerver->app_error_packet_handler) {
			admin_cerver->app_error_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->app_error_packet_handler->cerver = admin_cerver->cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_APP packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_app_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->app_packet_handler_delete_packet = delete_packet;

}

// sets option to automatically delete PACKET_TYPE_APP_ERROR packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_app_error_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->app_error_packet_handler_delete_packet = delete_packet;

}

// sets a PACKET_TYPE_CUSTOM packet type handler
void admin_cerver_set_custom_handler (
	AdminCerver *admin_cerver, Handler *custom_handler
) {

	if (admin_cerver) {
		admin_cerver->custom_packet_handler = custom_handler;
		if (admin_cerver->custom_packet_handler) {
			admin_cerver->custom_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->custom_packet_handler->cerver = admin_cerver->cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_CUSTOM packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_custom_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->custom_packet_handler_delete_packet = delete_packet;

}

// returns the total number of handlers currently alive (ready to handle packets)
unsigned int admin_cerver_get_n_handlers_alive (
	AdminCerver *admin_cerver
) {

	unsigned int retval = 0;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->handlers_lock);
		retval = admin_cerver->num_handlers_alive;
		(void) pthread_mutex_unlock (admin_cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently working (handling a packet)
unsigned int admin_cerver_get_n_handlers_working (
	AdminCerver *admin_cerver
) {

	unsigned int retval = 0;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->handlers_lock);
		retval = admin_cerver->num_handlers_working;
		(void) pthread_mutex_unlock (admin_cerver->handlers_lock);
	}

	return retval;

}

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
void admin_cerver_set_check_packets (
	AdminCerver *admin_cerver, bool check_packets
) {

	if (admin_cerver) {
		admin_cerver->check_packets = check_packets;
	}

}

// sets a custom update function to be executed every n ticks
// a new thread will be created that will call your method each tick
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void admin_cerver_set_update (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u8 fps
) {

	if (admin_cerver) {
		admin_cerver->update = update;
		admin_cerver->update_args = update_args;
		admin_cerver->delete_update_args = delete_update_args;
		admin_cerver->update_ticks = fps;
	}

}

// sets a custom update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void admin_cerver_set_update_interval (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u32 interval
) {

	if (admin_cerver) {
		admin_cerver->update_interval = update;
		admin_cerver->update_interval_args = update_args;
		admin_cerver->delete_update_interval_args = delete_update_args;
		admin_cerver->update_interval_secs = interval;
	}

}

// returns the current number of connected admins
u8 admin_cerver_get_current_admins (AdminCerver *admin_cerver) {

	return admin_cerver ? (u8) dlist_size (admin_cerver->admins) : 0;

}

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins (
	AdminCerver *admin_cerver, Packet *packet
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		u8 errors = 0;
		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			errors |= admin_send_packet ((Admin *) le->data, packet);
		}

		retval = errors;
	}

	return retval;

}

// broadcasts a packet to all connected admins in an admin cerver using packet_send_to_split ()
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins_split (
	AdminCerver *admin_cerver, Packet *packet
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		u8 errors = 0;

		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			errors |= admin_send_packet_split ((Admin *) le->data, packet);
		}

		retval = errors;
	}

	return retval;

}

// broadcasts a packet to all connected admins in an admin cerver using packet_send_pieces ()
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins_pieces (
	AdminCerver *admin_cerver, Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		u8 errors = 0;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			errors |= admin_send_packet_pieces (
				(Admin *) le->data, packet, pieces, sizes, n_pieces
			);
		}

		retval = errors;
	}

	return retval;

}

// registers a newly created admin to the admin cerver structures (internal & poll)
// this will allow the admin cerver to start handling admin's packets
// returns 0 on success, 1 on error
u8 admin_cerver_register_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		if (!admin_cerver_poll_register_connection (
			admin_cerver,
			(Connection *) dlist_start (admin->client->connections)->data
		)) {
			(void) dlist_insert_after (
				admin_cerver->admins, dlist_end (admin_cerver->admins), admin
			);

			admin->admin_cerver = admin_cerver;

			admin_cerver->stats->current_connected_admins += 1;
			admin_cerver->stats->total_n_admins += 1;

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connected admins: %ld",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connected_admins
			);
			#endif

			retval = 0;     // success
		}
	}

	return retval;

}

// unregisters an existing admin from the admin cerver structures (internal & poll)
// returns 0 on success, 1 on error
u8 admin_cerver_unregister_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		if (dlist_remove (admin_cerver->admins, admin, NULL)) {
			// unregister all his active connections from the poll array
			for (
				ListElement *le = dlist_start (admin->client->connections);
				le; le = le->next
			) {
				admin_cerver_poll_unregister_connection (
					admin_cerver, (Connection *) le->data
				);
			}

			admin->admin_cerver = NULL;

			admin_cerver->stats->current_connected_admins -= 1;

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connected admins: %ld",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connected_admins
			);
			#endif

			retval = 0;
		}
	}

	return retval;

}

// unregisters an admin from the cerver & then deletes it
// returns 0 on success, 1 on error
u8 admin_cerver_drop_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		retval = admin_cerver_unregister_admin (admin_cerver, admin);
		admin_delete (admin);
	}

	return retval;

}

#pragma endregion

#pragma region start

static void *admin_poll (void *cerver_ptr);

// called in a dedicated thread only if a user method was set
// executes methods every tick
static void *admin_cerver_update (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update () has started!",
			admin_cerver->cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (admin_cerver->cerver, admin_cerver->update_args);

		u32 time_per_frame = 1000000 / admin_cerver->update_ticks;
		// printf ("time per frame: %d\n", time_per_frame);
		u32 temp = 0;
		i32 sleep_time = 0;
		u64 delta_time = 0;

		u64 delta_ticks = 0;
		u32 fps = 0;
		struct timespec start = { 0 }, middle = { 0 }, end = { 0 };

		while (admin_cerver->cerver->isRunning) {
			(void) clock_gettime (CLOCK_MONOTONIC_RAW, &start);

			// do stuff
			if (admin_cerver->update) admin_cerver->update (cu);

			// limit the fps
			(void) clock_gettime (CLOCK_MONOTONIC_RAW, &middle);
			temp = (middle.tv_nsec - start.tv_nsec) / 1000;
			// printf ("temp: %d\n", temp);
			sleep_time = time_per_frame - temp;
			// printf ("sleep time: %d\n", sleep_time);
			if (sleep_time > 0) {
				(void) usleep (sleep_time);
			}

			// count fps
			(void) clock_gettime (CLOCK_MONOTONIC_RAW, &end);
			delta_time = (end.tv_nsec - start.tv_nsec) / 1000000;
			delta_ticks += delta_time;
			fps++;
			// printf ("delta ticks: %ld\n", delta_ticks);
			if (delta_ticks >= 1000) {
				// printf ("cerver %s update fps: %i\n", cerver->info->name, fps);
				delta_ticks = 0;
				fps = 0;
			}
		}

		cerver_update_delete (cu);

		if (admin_cerver->update_args) {
			if (admin_cerver->delete_update_args) {
				admin_cerver->delete_update_args (admin_cerver->update_args);
			}
		}

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update () has ended!",
			admin_cerver->cerver->info->name
		);
		#endif
	}

	return NULL;

}

// called in a dedicated thread only if a user method was set
// executes methods every x seconds
static void *admin_cerver_update_interval (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update_interval () has started!",
			admin_cerver->cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (
			admin_cerver->cerver, admin_cerver->update_interval_args
		);

		while (admin_cerver->cerver->isRunning) {
			if (admin_cerver->update_interval)
				admin_cerver->update_interval (cu);

			(void) sleep (admin_cerver->update_interval_secs);
		}

		cerver_update_delete (cu);

		if (admin_cerver->update_interval_args) {
			if (admin_cerver->delete_update_interval_args) {
				admin_cerver->delete_update_interval_args (
					admin_cerver->update_interval_args
				);
			}
		}

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update_interval () has ended!",
			admin_cerver->cerver->info->name
		);
		#endif
	}

	return NULL;

}

// inits admin cerver's internal structures & values
static u8 admin_cerver_start_internal (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		admin_cerver->fds = (struct pollfd *) calloc (
			admin_cerver->max_n_fds, sizeof (struct pollfd)
		);

		if (admin_cerver->fds) {
			(void) memset (
				admin_cerver->fds, 0, sizeof (struct pollfd) * admin_cerver->max_n_fds
			);

			for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
				admin_cerver->fds[i].fd = -1;

			admin_cerver->current_n_fds = 0;

			admin_cerver->poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			(void) pthread_mutex_init (admin_cerver->poll_lock, NULL);

			retval = 0;
		}
	}

	return retval;

}

static u8 admin_cerver_app_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->app_packet_handler) {
			if (!admin_cerver->app_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->app_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s app_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s app_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			#ifdef ADMIN_DEBUG
			cerver_log_warning (
				"Admin cerver %s does not have an app_packet_handler",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

static u8 admin_cerver_app_error_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->app_error_packet_handler) {
			if (!admin_cerver->app_error_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->app_error_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s app_error_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s app_error_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			#ifdef ADMIN_DEBUG
			cerver_log_warning (
				"Admin cerver %s does not have an app_error_packet_handler",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

static u8 admin_cerver_custom_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->custom_packet_handler) {
			if (!admin_cerver->custom_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->custom_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s custom_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s custom_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			// #ifdef ADMIN_DEBUG
			// cerver_log_warning (
			//     "Cerver %s does not have a custom_packet_handler",
			// 	admin_cerver->cerver->info->name
			// );
			// #endif
		}
	}

	return retval;

}

static u8 admin_cerver_handlers_start (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Initializing cerver %s admin handlers...",
			admin_cerver->cerver->info->name
		);
		#endif

		admin_cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		(void) pthread_mutex_init (admin_cerver->handlers_lock, NULL);

		errors |= admin_cerver_app_handler_start (admin_cerver);

		errors |= admin_cerver_app_error_handler_start (admin_cerver);

		errors |= admin_cerver_custom_handler_start (admin_cerver);

		if (!errors) {
			#ifdef ADMIN_DEBUG
			cerver_log_debug (
				"Done initializing cerver %s admin handlers!",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return errors;

}

static u8 admin_cerver_start_poll (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		if (!thread_create_detachable (
			&cerver->admin_thread_id,
			admin_poll,
			cerver
		)) {
			retval = 0;		// success
		}

		else {
			cerver_log_error (
				"Failed to create admin_poll () thread in cerver %s!",
				cerver->info->name
			);
		}
	}

	return retval;

}

u8 admin_cerver_start (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		if (!admin_cerver_start_internal (admin_cerver)) {
			if (admin_cerver->update) {
				if (thread_create_detachable (
					&admin_cerver->update_thread_id,
					admin_cerver_update,
					admin_cerver
				)) {
					cerver_log_error (
						"Failed to create cerver %s ADMIN UPDATE thread!",
						admin_cerver->cerver->info->name
					);
				}
			}

			if (admin_cerver->update_interval) {
				if (thread_create_detachable (
					&admin_cerver->update_interval_thread_id,
					admin_cerver_update_interval,
					admin_cerver
				)) {
					cerver_log_error (
						"Failed to create cerver %s ADMIN UPDATE INTERVAL thread!",
						admin_cerver->cerver->info->name
					);
				}
			}

			if (!admin_cerver_handlers_start (admin_cerver)) {
				if (!admin_cerver_start_poll (admin_cerver->cerver)) {
					retval = 0;
				}
			}

			else {
				cerver_log_error (
					"admin_cerver_start () - failed to start cerver %s admin handlers!",
					admin_cerver->cerver->info->name
				);
			}
		}

		else {
			cerver_log_error (
				"admin_cerver_start () - failed to start cerver %s admin internal!",
				admin_cerver->cerver->info->name
			);
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

static u8 admin_cerver_app_handler_destroy (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (admin_cerver->app_packet_handler) {
			if (!admin_cerver->app_packet_handler->direct_handle) {
				// stop app handler
				bsem_post_all (
					admin_cerver->app_packet_handler->job_queue->has_jobs
				);
			}
		}
	}

	return errors;

}

static u8 admin_cerver_app_error_handler_destroy (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (admin_cerver->app_error_packet_handler) {
			if (!admin_cerver->app_error_packet_handler->direct_handle) {
				// stop app error handler
				bsem_post_all (
					admin_cerver->app_error_packet_handler->job_queue->has_jobs
				);
			}
		}
	}

	return errors;

}

static u8 admin_cerver_custom_handler_destroy (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (admin_cerver->custom_packet_handler) {
			if (!admin_cerver->custom_packet_handler->direct_handle) {
				// stop custom handler
				bsem_post_all (
					admin_cerver->custom_packet_handler->job_queue->has_jobs
				);
			}
		}
	}

	return errors;

}

// correctly destroy admin cerver's custom handlers
static u8 admin_cerver_handlers_end (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Stopping handlers in cerver %s admin...",
			admin_cerver->cerver->info->name
		);
		#endif

		errors |= admin_cerver_app_handler_destroy (admin_cerver);

		errors |= admin_cerver_app_error_handler_destroy (admin_cerver);

		errors |= admin_cerver_custom_handler_destroy (admin_cerver);

		// poll remaining handlers
		while (admin_cerver->num_handlers_alive) {
			if (admin_cerver->app_packet_handler)
				bsem_post_all (admin_cerver->app_packet_handler->job_queue->has_jobs);

			if (admin_cerver->app_error_packet_handler)
				bsem_post_all (admin_cerver->app_error_packet_handler->job_queue->has_jobs);

			if (admin_cerver->custom_packet_handler)
				bsem_post_all (admin_cerver->custom_packet_handler->job_queue->has_jobs);

			sleep (1);
		}
	}

	return errors;

}

static u8 admin_cerver_disconnect_admins (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (dlist_size (admin_cerver->admins)) {
			// send a cerver teardown packet to all clients connected to cerver
			Packet *packet = packet_generate_request (PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_TEARDOWN, NULL, 0);
			if (packet) {
				errors |= admin_cerver_broadcast_to_admins (admin_cerver, packet);
				packet_delete (packet);
			}
		}
	}

	return errors;

}

u8 admin_cerver_end (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Staring cerver %s admin teardown...",
			admin_cerver->cerver->info->name
		);
		#endif

		errors |= admin_cerver_handlers_end (admin_cerver);

		errors |= admin_cerver_disconnect_admins (admin_cerver);

		cerver_log_success (
			"Cerver %s admin teardown was successful!",
			admin_cerver->cerver->info->name
		);
	}

	return errors;

}

#pragma endregion

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
				"Admin with client %ld requested to close the connection",
				packet->client->id
			);
			#endif

			switch (admin_remove_connection_by_sock_fd (
				packet->cerver->admin,
				admin_get_by_sock_fd (
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
				admin_get_by_sock_fd (
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

	if (packet->cerver->admin->app_packet_handler) {
		if (packet->cerver->admin->app_packet_handler->direct_handle) {
			// printf ("app_packet_handler - direct handle!\n");
			packet->cerver->admin->app_packet_handler->handler (packet);
			if (packet->cerver->admin->app_packet_handler_delete_packet)
				packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->cerver->admin->app_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to cerver's %s ADMIN app_packet_handler!",
					packet->cerver->info->name
				);
			}
		}
	}

	else {
		#ifdef ADMIN_DEBUG
		cerver_log_warning (
			"Cerver %s ADMIN does not have an app_packet_handler!",
			packet->cerver->info->name
		);
		#endif
	}

}

// handles an PACKET_TYPE_APP_ERROR packet type
static void admin_app_error_packet_handler (Packet *packet) {

	if (packet->cerver->admin->app_error_packet_handler) {
		if (packet->cerver->admin->app_error_packet_handler->direct_handle) {
			// printf ("app_error_packet_handler - direct handle!\n");
			packet->cerver->admin->app_error_packet_handler->handler (packet);
			if (packet->cerver->admin->app_error_packet_handler_delete_packet)
				packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->cerver->admin->app_error_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to cerver's %s ADMIN app_error_packet_handler!",
					packet->cerver->info->name
				);
			}
		}
	}

	else {
		#ifdef ADMIN_DEBUG
		cerver_log_warning (
			"Cerver %s ADMIN does not have an app_error_packet_handler!",
			packet->cerver->info->name
		);
		#endif
	}

}

// handles a PACKET_TYPE_CUSTOM packet type
static void admin_custom_packet_handler (Packet *packet) {

	if (packet->cerver->admin->custom_packet_handler) {
		if (packet->cerver->admin->custom_packet_handler->direct_handle) {
			// printf ("custom_packet_handler - direct handle!\n");
			packet->cerver->admin->custom_packet_handler->handler (packet);
			if (packet->cerver->admin->custom_packet_handler_delete_packet)
				packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->cerver->admin->custom_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to cerver's %s ADMIN custom_packet_handler!",
					packet->cerver->info->name
				);
			}
		}
	}

	else {
		#ifdef ADMIN_DEBUG
		cerver_log_warning (
			"Cerver %s ADMIN does not have an custom_packet_handler!",
			packet->cerver->info->name
		);
		#endif
	}

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
			packet->cerver->admin->stats->received_packets->n_bad_packets += 1;
			packet->client->stats->received_packets->n_bad_packets += 1;
			packet->connection->stats->received_packets->n_bad_packets += 1;
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PACKET,
				"Got a packet of unknown type in cerver %s admin handler",
				packet->cerver->info->name
			);
			#endif
			packet_delete (packet);
		} break;
	}

	return error;

}

static AdminCerverHandlerError admin_packet_handler_check_version (
	Packet *packet
) {

	AdminCerverHandlerError error = ADMIN_CERVER_HANDLER_ERROR_NONE;

	if (packet->cerver->check_packets) {
		// we expect the packet version in the packet's data
		if (packet->data) {
			(void) memcpy (&packet->version, packet->data_ptr, sizeof (PacketVersion));
			packet->data_ptr += sizeof (PacketVersion);

			// TODO: return errors to client
			// TODO: drop client on max bad packets
			if (packet_check (packet)) {
				error = ADMIN_CERVER_HANDLER_ERROR_PACKET;
			}
		}

		else {
			cerver_log_error (
				"admin_packet_handler () - No packet version to check!"
			);
			
			// TODO: add to bad packets count

			error = ADMIN_CERVER_HANDLER_ERROR_PACKET;
		}
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

		case ADMIN_CERVER_HANDLER_ERROR_PACKET:
			retval = 0;
			break;

		default: break;
	}

	return retval;

}

#pragma endregion

#pragma region poll

// get a free index in the admin cerver poll fds array
static i32 admin_cerver_poll_get_free_idx (
	AdminCerver *admin_cerver
) {

	if (admin_cerver) {
		for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
			if (admin_cerver->fds[i].fd == -1) return i;

	}

	return -1;

}

// get the idx of the connection sock fd in the admin cerver poll fds
static i32 admin_cerver_poll_get_idx_by_sock_fd (
	AdminCerver *admin_cerver, i32 sock_fd
) {

	if (admin_cerver) {
		for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
			if (admin_cerver->fds[i].fd == sock_fd) return i;
	}

	return -1;

}

// regsiters a client connection to the cerver's admin poll array
// returns 0 on success, 1 on error
u8 admin_cerver_poll_register_connection (
	AdminCerver *admin_cerver, Connection *connection
) {

	u8 retval = 1;

	if (admin_cerver && connection) {
		(void) pthread_mutex_lock (admin_cerver->poll_lock);

		i32 idx = admin_cerver_poll_get_free_idx (admin_cerver);
		if (idx >= 0) {
			admin_cerver->fds[idx].fd = connection->socket->sock_fd;
			admin_cerver->fds[idx].events = POLLIN;
			admin_cerver->current_n_fds++;

			admin_cerver->stats->current_connections++;
			admin_cerver->stats->total_admin_connections++;

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Added sock fd <%d> to cerver %s ADMIN poll, idx: %i",
				connection->socket->sock_fd,
				admin_cerver->cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connections: %ld",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connections
			);
			#endif

			retval = 0;
		}

		else if (idx < 0) {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN poll is full!",
				admin_cerver->cerver->info->name
			);
			#endif
		}

		(void) pthread_mutex_unlock (admin_cerver->poll_lock);
	}

	return retval;

}

// unregsiters a sock fd connection from the cerver's admin poll array
// returns 0 on success, 1 on error
u8 admin_cerver_poll_unregister_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
) {

	u8 retval = 1;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->poll_lock);

		i32 idx = admin_cerver_poll_get_idx_by_sock_fd (admin_cerver, sock_fd);
		if (idx >= 0) {
			admin_cerver->fds[idx].fd = -1;
			admin_cerver->fds[idx].events = -1;
			admin_cerver->current_n_fds--;

			admin_cerver->stats->current_connections--;

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Removed sock fd <%d> from cerver %s ADMIN poll, idx: %d",
				sock_fd, admin_cerver->cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connections: %ld",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connections
			);
			#endif

			retval = 0;
		}

		else {
			// #ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
				"Sock fd <%d> was NOT found in cerver %s ADMIN poll!",
				sock_fd, admin_cerver->cerver->info->name
			);
			// #endif
		}

		(void) pthread_mutex_unlock (admin_cerver->poll_lock);
	}

	return retval;

}

// unregsiters a connection from the cerver's admin hold poll array
// returns 0 on success, 1 on error
static u8 admin_cerver_poll_unregister_connection (
	AdminCerver *admin_cerver, Connection *connection
) {

	return (admin_cerver && connection) ?
		admin_cerver_poll_unregister_sock_fd (
			admin_cerver, connection->socket->sock_fd
		) : 1;

}

static inline void admin_poll_handle (
	AdminCerver *admin_cerver, char *packet_buffer
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < admin_cerver->max_n_fds; idx++) {
		if (admin_cerver->fds[idx].fd > -1) {
			CerverReceive *cr = cerver_receive_create (
				RECEIVE_TYPE_ADMIN, admin_cerver->cerver, admin_cerver->fds[idx].fd
			);

			if (cr) {
				switch (admin_cerver->fds[idx].revents) {
					case POLLIN: {
						cerver_receive_internal (
							cr,
							packet_buffer,
							admin_cerver->receive_buffer_size
						);
					} break;

					default: {
						if (admin_cerver->fds[idx].revents != 0) {
							cerver_receive_handle_failed (cr);
						}
					} break;
				}

				cerver_receive_delete (cr);
			}
		}
	}

}

// handles packets from the admin clients
static void *admin_poll (void *cerver_ptr) {

	if (cerver_ptr) {
		Cerver *cerver = (Cerver *) cerver_ptr;
		AdminCerver *admin_cerver = cerver->admin;

		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_ADMIN,
			"Cerver %s ADMIN poll has started!",
			cerver->info->name
		);

		(void) thread_set_name ("%s-admin", cerver->info->alias);

		char *packet_buffer = (char *) calloc (
			admin_cerver->receive_buffer_size, sizeof (char)
		);

		if (packet_buffer) {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Waiting for admin connections..."
			);
			#endif

			int poll_retval = 0;
			while (cerver->isRunning) {
				poll_retval = poll (
					admin_cerver->fds,
					admin_cerver->max_n_fds,
					admin_cerver->poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
							"Cerver %s ADMIN poll has failed!",
							cerver->info->name
						);

						perror ("Error");
						cerver->isRunning = false;
					} break;

					case 0: {
						// #ifdef ADMIN_DEBUG
						// cerver_log (
						//     LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
						//     "Cerver %s ADMIN poll timeout",
						// 	cerver->info->name
						// );
						// #endif
					} break;

					default: {
						admin_poll_handle (
							admin_cerver, packet_buffer
						);
					} break;
				}
			}

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN poll has stopped!", cerver->info->name
			);
			#endif
			
			free (packet_buffer);
		}

		else {
			cerver_log_error (
				"Failed to allocate ADMIN cerver poll's packet buffer!"
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
			"Can't handle admins on a NULL cerver!"
		);
	}

	return NULL;

}

#pragma endregion