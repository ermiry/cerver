#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <poll.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dllist.h"

#include "cerver/threads/thread.h"

#include "cerver/admin.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/network.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

#pragma region aux

static AdminAppHandler *admin_app_handler_new (AdminCerver *admin_cerver, Packet *packet,
	Admin *admin) {

	AdminAppHandler *admin_app_handler = (AdminAppHandler *) malloc (sizeof (AdminAppHandler));
	if (admin_app_handler) {
		admin_app_handler->admin_cerver = admin_cerver;
		admin_app_handler->packet = packet;
		admin_app_handler->admin = admin;
	}

	return admin_app_handler;

}

static void admin_app_handler_delete (void *ptr) { if (ptr) free (ptr); }

#pragma endregion

#pragma region Admin Credentials

static AdminCredentials *admin_credentials_new (void) {

	AdminCredentials *credentials = (AdminCredentials *) malloc (sizeof (AdminCredentials));
	if (credentials) {
		credentials->username = NULL;
		credentials->password = NULL;
		credentials->logged_in = false;
	}

	return credentials;

}

static void admin_credentials_delete (void *credentials_ptr) {

	if (credentials_ptr) {
		AdminCredentials *credentials = (AdminCredentials *) credentials_ptr;

		estring_delete (credentials->username);
		estring_delete (credentials->password);

		free (credentials);
	}

}

#pragma endregion

#pragma region Admin

static Admin *admin_new (void) {

	Admin *admin = (Admin *) malloc (sizeof (Admin));
	if (admin) {
		admin->id = NULL;

		admin->client = NULL;

		admin->data = NULL;
		admin->delete_data = NULL;

		admin->authenticated = false;
		admin->credentials = NULL;
	}

	return admin;

}

static void admin_delete (void *admin_ptr) {

	if (admin_ptr) {
		Admin *admin = (Admin *) admin_ptr;

		estring_delete (admin->id);

		client_delete (admin->client);

		if (admin->data) {
			if (admin->delete_data) admin->delete_data (admin->data);
		}

		free (admin);
	}

}

// create a new admin structures with the newly connected client data
// this does not means that it has been authenticated as an admin
static Admin *admin_create (Client *client) {

	Admin *admin = NULL;

	if (client) {
		admin = admin_new ();
		if (admin) {
			admin->client = client;
			admin->authenticated = false;
		}
	}

	return admin;

}

// compares admins by their id
int admin_comparator_by_id (const void *a, const void *b) {

	if (a && b) return estring_comparator (((Admin *) a)->id, ((Admin *) b)->id);

	else if (a && !b) return -1;
	else if (!a && b) return 1;
	return 0;

}

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
void admin_set_data (Admin *admin, void *data, Action delete_data) {

	if (admin) {
		admin->data = data;
		admin->delete_data = delete_data;
	}

}

static Admin *admin_get_by_sock_fd (AdminCerver *admin_cerver, i32 sock_fd) {

	Admin *retval = NULL;

	if (admin_cerver) {
		// search the admin by sockfd
		Admin *admin = NULL;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			admin = (Admin *) le->data;

			// search in all the admin's connections
			Connection *con = NULL;
			for (ListElement *le_sub = dlist_start (admin->client->connections); le_sub; le_sub = le_sub->next) {
				con = (Connection *) le_sub->data;
				if (con->socket->sock_fd == sock_fd) {
					retval = admin;
					break;
				}
			}

			if (retval) break;
		}
	}

	return retval;

}

static Connection *admin_connection_get_by_sock_fd (AdminCerver *admin_cerver, i32 sock_fd) {

	Connection *retval = NULL;

	if (admin_cerver) {
		// search the admin by sockfd
		Admin *admin = NULL;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			admin = (Admin *) le->data;

			// search in all the admin's connections
			Connection *con = NULL;
			for (ListElement *le_sub = dlist_start (admin->client->connections); le_sub; le_sub = le_sub->next) {
				con = (Connection *) le_sub->data;
				if (con->socket->sock_fd == sock_fd) {
					retval = con;
					return retval;
				}
			}
		}
	}

	return retval;

}

// sends a packet to the specified admin
// returns 0 on success, 1 on error
u8 admin_send_packet (Admin *admin, Packet *packet) {

	u8 retval = 1;

	if (admin && packet) {
		if (admin->authenticated) {
			// printf ("admin client: %ld -- sock fd: %d\n", admin->client->id, ((Connection *) dlist_start (admin->client->connections))->sock_fd);

			packet_set_network_values (packet, 
				NULL, 
				admin->client, 
				(Connection *) dlist_start (admin->client->connections)->data, 
				NULL);
			retval = packet_send (packet, 0, NULL, false);
			if (retval)
				cerver_log_error ("Failed to send packet to admin!");
		}
	}


	return retval;

}

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
u8 admin_broadcast_to_all (AdminCerver *admin_cerver, Packet *packet) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		// char *status = c_string_create ("Total admins connected: %ld", dlist_size (admin_cerver->admins));
		// if (status) {
		// 	cerver_log_debug (status);
		// 	free (status);
		// }

		Admin *admin = NULL;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			admin = (Admin *) le->data;

			admin_send_packet (admin, packet);
		}

		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region Admin Cerver Stats

static AdminCerverStats *admin_cerver_stats_new (void) {

    AdminCerverStats *admin_cerver_stats = (AdminCerverStats *) malloc (sizeof (AdminCerverStats));
    if (admin_cerver_stats) {
        memset (admin_cerver_stats, 0, sizeof (AdminCerverStats));
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

#pragma endregion

#pragma region Admin Cerver

static AdminCerver *admin_cerver_new (void) {

	AdminCerver *admin = (AdminCerver *) malloc (sizeof (AdminCerver));
	if (admin) {
		memset (admin, 0, sizeof (AdminCerver));

		admin->cerver = NULL;

		admin->credentials = NULL;

		admin->admins = NULL;

		admin->fds = NULL;

		admin->on_admin_fail_connection = NULL;
		admin->on_admin_success_connection = NULL;

		admin->admin_packet_handler = NULL;
		admin->admin_error_packet_handler = NULL;

		admin->stats = NULL;
	}

	return admin;

}

void admin_cerver_delete (void *admin_cerver_ptr) {

	if (admin_cerver_ptr) {
		AdminCerver *admin = (AdminCerver *) admin_cerver_ptr;

		dlist_delete (admin->credentials);

		dlist_delete (admin->admins);

		if (admin->fds) free (admin->fds);

		admin_cerver_stats_delete (admin->stats);

		free (admin);
	}

}

// inits the admin cerver networking capabilities
static u8 admin_cerver_network_init (AdminCerver *admin_cerver, u16 port, bool use_ipv6) {

	if (admin_cerver) {
		admin_cerver->port = port;
		admin_cerver->use_ipv6 = use_ipv6;

		// only TCP is supported
		admin_cerver->sock = socket ((admin_cerver->use_ipv6 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
		if (admin_cerver->sock < 0) {
			cerver_log_error ("Failed to create admin cerver socket!");
			return 1;			// error
		}

		// set the socket to non blocking mode
		if (!sock_set_blocking (admin_cerver->sock, true)) {
			cerver_log_error ("Failed to set admin cerver socket to non blocking mode!");
			close (admin_cerver->sock);
			return 1;			// error
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Admin cerver socket set to non blocking mode.");
			#endif
		}

		memset (&admin_cerver->address, 0, sizeof (struct sockaddr_storage));

		if (admin_cerver->use_ipv6) {
			struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &admin_cerver->address;
			addr->sin6_family = AF_INET6;
			addr->sin6_addr = in6addr_any;
			addr->sin6_port = htons (admin_cerver->port);
		} 

		else {
			struct sockaddr_in *addr = (struct sockaddr_in *) &admin_cerver->address;
			addr->sin_family = AF_INET;
			addr->sin_addr.s_addr = INADDR_ANY;
			addr->sin_port = htons (admin_cerver->port);
		}

		if ((bind (admin_cerver->sock, (const struct sockaddr *) &admin_cerver->address, sizeof (struct sockaddr_storage))) < 0) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Failed to bind admin cerver socket!");
			close (admin_cerver->sock);
			return 1;			// error
		}  

		return 0;       // success!!
	}

	return 1;			// error

}

static u8 admin_cerver_init_data_structures (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		admin_cerver->credentials = dlist_init (admin_credentials_delete, NULL);
		if (!admin_cerver->credentials) {
			#ifdef CERVER_DEBUG
			cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Failed to init admins credentials dlist in admin cerver");
			#endif

			return 1;		// error
		}

		admin_cerver->admins = dlist_init (admin_delete, admin_comparator_by_id);
		if (!admin_cerver->admins) {
			#ifdef CERVER_DEBUG
			cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Failed to init admins dlist in admin cerver");
			#endif

			return 1;		// error
		}

		return 0;			// success
	}

	return 1;		// error

}

AdminCerver *admin_cerver_create (u16 port, bool use_ipv6) {

	AdminCerver *admin_cerver = admin_cerver_new ();
	if (admin_cerver) {
		if (!admin_cerver_network_init (admin_cerver, port, use_ipv6)) {
			if (!admin_cerver_init_data_structures (admin_cerver)) {
				// set default values
				admin_cerver->receive_buffer_size = RECEIVE_PACKET_BUFFER_SIZE;

				admin_cerver->max_admins = DEFAULT_MAX_ADMINS;
				admin_cerver->max_admin_connections = DEFAULT_MAX_ADMIN_CONNECTIONS;

				admin_cerver->n_bad_packets_limit = DEFAULT_N_BAD_PACKETS_LIMIT;
				admin_cerver->n_bad_packets_limit_auth = DEFAULT_N_BAD_PACKETS_LIMIT_AUTH;

				admin_cerver->poll_timeout = DEFAULT_POLL_TIMEOUT;

				admin_cerver->stats = admin_cerver_stats_new ();

				return admin_cerver;
			}
		}

		admin_cerver_delete (admin_cerver);
		return NULL;		// error
	}

	return NULL;			// error

}

// sets a custom receive buffer size to use for admin handler
void admin_cerver_set_receive_buffer_size (AdminCerver *admin_cerver, u32 receive_buffer_size) {

	if (admin_cerver) admin_cerver->receive_buffer_size = receive_buffer_size;

}

// 24/01/2020 -- 11:20 -- register new admin credentials
// returns 0 on success, 1 on error
u8 admin_cerver_register_admin_credentials (AdminCerver *admin_cerver,
	const char *username, const char *password) {

	u8 retval = 1;

	if (admin_cerver && username && password) {
		AdminCredentials *credentials = admin_credentials_new ();
		if (credentials) {
			credentials->username = estring_new (username);
			credentials->password = estring_new (password);

			retval = dlist_insert_after (admin_cerver->credentials, dlist_end (admin_cerver->credentials), credentials);
		}
	}

	return retval;

}

// sets the max numbers of admins allowed concurrently
void admin_cerver_set_max_admins (AdminCerver *admin_cerver, u8 max_admins) {

	if (admin_cerver) admin_cerver->max_admins = max_admins;

}

// sets the max number of connections allowed per admin
void admin_cerver_set_max_admin_connections (AdminCerver *admin_cerver, u8 max_admin_connections) {

	if (admin_cerver) admin_cerver->max_admin_connections = max_admin_connections;

}

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
void admin_cerver_set_bad_packets_limit (AdminCerver *admin_cerver, 
	i32 n_bad_packets_limit, i32 n_bad_packets_limit_auth) {

	if (admin_cerver) {
		admin_cerver->n_bad_packets_limit = n_bad_packets_limit > 0 ? n_bad_packets_limit : DEFAULT_N_BAD_PACKETS_LIMIT;
		admin_cerver->n_bad_packets_limit_auth = n_bad_packets_limit_auth > 0 ? n_bad_packets_limit : DEFAULT_N_BAD_PACKETS_LIMIT_AUTH;
	}

}

// sets a custom poll time out to use for admins
void admin_cerver_set_poll_timeout (AdminCerver *admin_cerver, u32 poll_timeout) {

	if (admin_cerver) admin_cerver->poll_timeout = poll_timeout;

}

static OnAdminConnection *on_admin_connection_new (AdminCerver *admin_cerver, Admin *admin) {

	OnAdminConnection *on_admin_connection = (OnAdminConnection *) malloc (sizeof (OnAdminConnection));
	if (on_admin_connection) {
		on_admin_connection->admin_cerver = admin_cerver;
		on_admin_connection->admin = admin;
	}

	return on_admin_connection;

}

static inline void on_admin_connection_delete (void *ptr) { if (ptr) free (ptr); }

// sets and action to be performed when a new admin fail to authenticate
void admin_cerver_set_on_fail_connection (AdminCerver *admin_cerver, Action on_fail_connection) {

	if (admin_cerver) admin_cerver->on_admin_fail_connection = on_fail_connection;

}

// sets an action to be performed when a new admin authenticated successfully
// a struct _OnAdminConnection will be passed as the argument
void admin_cerver_set_on_success_connection (AdminCerver *admin_cerver, Action on_success_connection) {

	if (admin_cerver) admin_cerver->on_admin_success_connection = on_success_connection;

}

// sets customa admin packet handlers
void admin_cerver_set_handlers (AdminCerver *admin_cerver, 
	Action admin_packet_handler, Action admin_error_packet_handler) {

	if (admin_cerver) {
		admin_cerver->admin_packet_handler = admin_packet_handler;
		admin_cerver->admin_error_packet_handler = admin_error_packet_handler;
	}

}

// quick way to get the number of current authenticated admins
u32 admin_cerver_get_current_auth_admins (AdminCerver *admin_cerver) {

	return admin_cerver ? admin_cerver->stats->current_auth_n_connected_admins : 0;

}

#pragma endregion

#pragma region main

static void admin_cerver_receive_handle_failed (AdminCerver *admin_cerver, i32 sock_fd);

static void admin_cerver_poll (AdminCerver *admin_cerver);

static u8 admin_cerver_before_start (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		// initialize main pollfd structures
		// 21/01/2020 -- plus ten as a healthy buffer for when we accept a new client and we are waiting for authentication
		unsigned int admin_poll_n_fds = (admin_cerver->max_admins * admin_cerver->max_admin_connections) + 10;
		admin_cerver->fds = (struct pollfd *) calloc (poll_n_fds, sizeof (struct pollfd));
		if (admin_cerver->fds) {
			memset (admin_cerver->fds, 0, sizeof (admin_cerver->fds));
			// set all fds as available spaces
			for (u32 i = 0; i < admin_poll_n_fds; i++) admin_cerver->fds[i].fd = -1;

			admin_cerver->max_n_fds = admin_poll_n_fds;
			admin_cerver->current_n_fds = 0;

			retval = 0;     // success!!
		}
	}

	return retval;

}

static void admin_cerver_update (void *);

// start the admin cerver, this is ment to be called in a dedicated thread internally
void *admin_cerver_start (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		if (!admin_cerver_before_start (admin_cerver)) {
			if (!listen (admin_cerver->sock, ADMIN_CERVER_CONNECTION_QUEUE)) {
				// set up the initial listening socket     
				admin_cerver->fds[admin_cerver->current_n_fds].fd = admin_cerver->sock;
				admin_cerver->fds[admin_cerver->current_n_fds].events = POLLIN;
				admin_cerver->current_n_fds++;

				admin_cerver->running = true;

				// create a dedicated thread for admin cerver update method
				if (thread_create_detachable (
					&admin_cerver->update_thread_id,
					(void *(*) (void *)) admin_cerver_update,
					admin_cerver
				)) {
					cerver_log_error ("Failed to create admin_cerver_update () thread!");
				}

				admin_cerver_poll (admin_cerver);
			}

			else {
				cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Failed to listen in admin cerver socket!");
				close (admin_cerver->sock);
			}
		}

		else {
			cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "admin_cerver_before_start () failed!");
			close (admin_cerver->sock);
		}
	}

	return NULL;

}

static u8 admin_cerver_poll_register_connection (AdminCerver *admin_cerver, Connection *connection);

// 22/01/2020 -- 09:18 
// register a newly accepted and created client (inside a possible admin structure)
// to the admin cerver's structures
// we have not yet correctly checked for authentication, 
// but we need this in order to get its packets
// returns 0 on success, 1 on error
static u8 admin_register_to_admin_cerver (AdminCerver *admin_cerver, Client *client) {

	u8 retval = 1;

	if (admin_cerver && client) {
		// add the newly connected client as a possible admin
		Admin *admin = admin_create (client);

		// register to admin cerver poll
		Connection *connection = (Connection *) dlist_start (client->connections)->data;
		if (!admin_cerver_poll_register_connection (admin_cerver,
				connection)) {

			dlist_insert_after (admin_cerver->admins, dlist_end (admin_cerver->admins), admin);

			// update stats general admin stats
			admin_cerver->stats->current_active_admin_connections += 1;
			admin_cerver->stats->current_n_connected_admins += 1;

			// only if all operations have succeeded, we can move on
			retval = 0;
		}

		else admin_delete (admin);
	}

	return retval;

}

static void admin_cerver_register_new_connection (AdminCerver *admin_cerver,
	const i32 new_fd, const struct sockaddr_storage client_address) {

	if (admin_cerver) {
		bool done = false;

		Client *client = NULL;
		Connection *connection = connection_create (new_fd, client_address, PROTOCOL_TCP);
		if (connection) {
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("New connection to admin from IP address: %s -- Port: %d", 
                connection->ip->str, connection->port);
			if (status) {
            	cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, status);
				free (status);
			}
        	#endif

			if (!connection->sock_receive) connection->sock_receive = sock_receive_new ();

			client = client_create ();
			if (client) {
				connection_register_to_client (client, connection);

				// TODO: 21/01/2020 -- 14:37 -- add support sessions for admins
				if (!admin_register_to_admin_cerver (admin_cerver, client)) {
					// everything has succeeded
					// we can now listen for auth packets of possible admin, and handle them
					time (&client->connected_timestamp);
					done = true;
					printf ("admin_cerver_register_new_connection () -- done!\n");
				}
			}
		}

		if (done) {
			// send cerver info packet
			packet_set_network_values (admin_cerver->cerver->info->cerver_info_packet, 
				admin_cerver->cerver, client, connection, NULL);
			if (packet_send (admin_cerver->cerver->info->cerver_info_packet, 0, NULL, false)) {
				char *error = c_string_create ("Failed to send cerver %s info packet!", admin_cerver->cerver->info->name->str);
				if (error) {
					cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, error);
					free (error);
				}
			}
		}

		else {
			// send an error packet bad on any error
			Packet *packet = error_packet_generate (ERR_CERVER_ERROR, "Internal cerver error");
			if (packet) {
				packet_send_to_sock_fd (packet, new_fd, 0, NULL, false);
				packet_delete (packet);
			}

			// disconnect directly from the admin cerver
			close (new_fd);
		}
	}

}

static void admin_cerver_accept (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		// accept the new connection
        struct sockaddr_storage client_address;
        memset (&client_address, 0, sizeof (struct sockaddr_storage));
        socklen_t socklen = sizeof (struct sockaddr_storage);

		i32 new_fd = accept (admin_cerver->sock, (struct sockaddr *) &client_address, &socklen);
        if (new_fd > 0) {
            printf ("Accepted fd: %d in cerver %s admin\n", new_fd, admin_cerver->cerver->info->name->str);
			admin_cerver_register_new_connection (admin_cerver,
				new_fd, client_address);
        } 

        else {
            // if we get EWOULDBLOCK, we have accepted all connections
            if (errno != EWOULDBLOCK) {
				char *status = c_string_create ("Accept failed in cerver %s admin poll", 
					admin_cerver->cerver->info->name->str);
				if (status) {
					cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
					free (status);
				}
                perror ("Error");
            } 
        }
	}

}

// an admin is trying to authenticate
static void admin_auth_packet_handler (AdminCerver *admin_cerver, Admin *admin, Packet *packet) {

	if (admin_cerver && admin && packet) {
		bool success = false;
		if (packet->data_size >= sizeof (SAdminCredentials)) {
			char *end = (char *) packet->data;
			SAdminCredentials *possible_credentials = (SAdminCredentials *) end;

			// check that there are valid credentials
			AdminCredentials *credentials;
			for (ListElement *le = dlist_start (admin_cerver->credentials); le; le = le->next) {
				credentials = (AdminCredentials *) le->data;

				if (!strcmp (credentials->username->str, possible_credentials->username.str)) {
					if (!strcmp (credentials->password->str, possible_credentials->password.str)) {
						if (!credentials->logged_in) {
							// handle success auth
							credentials->logged_in = true;
							admin->authenticated = true;
							admin->credentials = credentials;
							success = true;

							// send auth succes packet
							Packet *success_packet = packet_generate_request (AUTH_PACKET, SUCCESS_AUTH, NULL, 0);
							if (success_packet) {
								packet_set_network_values (success_packet, NULL, admin->client, packet->connection, NULL);
								packet_send (success_packet, 0, NULL, false);
								packet_delete (success_packet);
							}

							char *status = c_string_create ("Admin %s authenticated in cerver %s",
								credentials->username->str, admin_cerver->cerver->info->name->str);
							if (status) {
								cerver_log_success (status);
								free (status);
							}

							if (admin_cerver->on_admin_success_connection) {
								OnAdminConnection *on_admin_connection = on_admin_connection_new (admin_cerver, admin);
								admin_cerver->on_admin_success_connection (on_admin_connection);
								on_admin_connection_delete (on_admin_connection);
							}

							// TODO: 27/01/2020 -- 12:23 -- update remaining stats values
							admin_cerver->stats->current_auth_n_connected_admins += 1;
							admin_cerver->stats->current_auth_active_admin_connections += 1;
						}

						else {
							// FIXME: handle this!
						}
					}
				}
			}
		}

		// handle failed auth
		if (!success) {
			// TODO: save his ip for the blacklist
				
			// send a cerver disconnect packet
			Packet *disconnect_packet = packet_generate_request (CLIENT_PACKET, CLIENT_DISCONNET, NULL, 0);
			if (disconnect_packet) {
				packet_set_network_values (disconnect_packet, 
					NULL, 
					admin->client, 
					packet->connection,
					NULL);
				packet_send (disconnect_packet, 0, NULL, false);
				packet_delete (disconnect_packet);
			}

			// drop the admin connection
			admin_cerver_receive_handle_failed (admin_cerver, packet->connection->socket->sock_fd);

			if (admin_cerver->on_admin_fail_connection)
				admin_cerver->on_admin_fail_connection (NULL);
		}
	}

}

// handles a packet of bad type
static void admin_bad_type_packet_handler (AdminCerver *admin_cerver, Packet *packet,
	Admin *admin, bool auth) {

	if (admin_cerver && packet) {
		if (auth) {
			#ifdef CERVER_DEBUG
				char *status = c_string_create ("Got a packet of unknown type in cerver %s admin handler.",
					admin_cerver->cerver->info->name->str);
				if (status) {
					cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, status);
					free (status);
				}
			#endif
		}

		else {
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Got a packet of unknown type in cerver %s admin handler from UNAUTHORIZED admin.",
				admin_cerver->cerver->info->name->str);
			if (status) {
				cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, status);
				free (status);
			}
			#endif
		}

		u32 limit = auth ? admin_cerver->n_bad_packets_limit_auth : admin_cerver->n_bad_packets_limit;

		// 23/01/2020 -- 17:41 -- after a number of bad packets, disconnect
		admin->bad_packets += 1;
		if (admin->bad_packets >= limit) {
			// send a cerver disconnect packet
			Packet *packet = packet_generate_request (CLIENT_PACKET, CLIENT_DISCONNET, NULL, 0);
			if (packet) {
				packet_set_network_values (packet, 
					NULL, 
					admin->client, 
					packet->connection,
					NULL);
				packet_send (packet, 0, NULL, false);
				packet_delete (packet);
			}

			admin_cerver_receive_handle_failed (admin_cerver, packet->connection->socket->sock_fd);
		}
	}

}

// handles admin packet based on type
static void admin_packet_handler (AdminCerver *admin_cerver, Packet *packet,
	Admin *admin) {

	if (admin_cerver && packet) {
		switch (packet->header->packet_type) {
			// handles an error from the client
			case ERROR_PACKET: /*** TODO: ***/ break;

			// handles authentication packets
			case AUTH_PACKET: /*** TODO: ***/ break;

			// handles a request made from the client
			case REQUEST_PACKET: /*** TODO: ***/ break;

			case APP_PACKET: 
				if (admin_cerver->admin_packet_handler) {
					AdminAppHandler *admin_app_handler = admin_app_handler_new (admin_cerver, packet, admin);
					admin_cerver->admin_packet_handler (
						admin_app_handler
					);
					admin_app_handler_delete (admin_app_handler);
				}
				break;
			
			case APP_ERROR_PACKET: 
				if (admin_cerver->admin_error_packet_handler) {
					AdminAppHandler *admin_app_handler = admin_app_handler_new (admin_cerver, packet, admin);
					admin_cerver->admin_error_packet_handler (
						admin_app_handler
					);
					admin_app_handler_delete (admin_app_handler);
				}
					
				break;

			// acknowledge the client we have received his test packet
			case TEST_PACKET: {
				#ifdef CERVER_DEBUG
				char *status = c_string_create ("Got a test packet in cerver %s admin handler.", packet->cerver->info->name->str);
				if (status) {
					cerver_log_msg (stdout, LOG_DEBUG, LOG_PACKET, status);
					free (status);
				}
				#endif

				Packet *test_packet = packet_new ();
				if (test_packet) {
					packet_set_network_values (test_packet, 
						NULL,
						admin->client, 
						packet->connection, 
						packet->lobby);
					test_packet->packet_type = TEST_PACKET;
					packet_generate (test_packet);
					if (packet_send (test_packet, 0, NULL, false)) {
						char *status = c_string_create ("Failed to send error packet from cerver %s.", 
							packet->cerver->info->name->str);
						if (status) {
							cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, status);
							free (status);
						}
					}

					packet_delete (test_packet);
				}
			} break;

			default: {
				admin_bad_type_packet_handler (admin_cerver, packet,
					admin, true);
			} break;
		}
	}

}

// select the packet handler based on admin auth status
static void admin_select_packet_handler (AdminCerver *admin_cerver, i32 sock_fd, Packet *packet) {

	if (admin_cerver && packet) {
		packet->connection = admin_connection_get_by_sock_fd (admin_cerver, sock_fd);
		// if (packet->connection) printf ("\nFound connection!\n");

		Admin *admin = admin_get_by_sock_fd (admin_cerver, sock_fd);
		if (admin->authenticated) {
			admin_packet_handler (admin_cerver, packet,
				admin);
		}

		else {
			// we only want to handle specific packets from non auth admins
			switch (packet->header->packet_type) {
				// handles authentication packets
				case AUTH_PACKET: 
					admin_auth_packet_handler (admin_cerver, admin, packet);
					break;

				default: {
					admin_bad_type_packet_handler (admin_cerver, packet,
						admin, false);
				} break;
			}
		}
	}

}

static SockReceive *admin_cerver_receive_handle_spare_packet (AdminCerver *admin_cerver, i32 sock_fd,
    size_t buffer_size, char **end, size_t *buffer_pos) {

	SockReceive *sock_receive = NULL;

	// get the sock receive from the correct admin
	Connection *connection = admin_connection_get_by_sock_fd (admin_cerver, sock_fd);
	if (connection) {
		sock_receive = connection->sock_receive;

		if (sock_receive) {
			if (sock_receive->header) {
				// copy the remaining header size
				memcpy (sock_receive->header_end, (void *) *end, sock_receive->remaining_header);
				sock_receive->complete_header = true;
			}

			else if (sock_receive->spare_packet) {
				size_t copy_to_spare = 0;
				if (sock_receive->missing_packet < buffer_size) 
					copy_to_spare = sock_receive->missing_packet;

				else copy_to_spare = buffer_size;

				// append new data from buffer to the spare packet
				if (copy_to_spare > 0) {
					packet_append_data (sock_receive->spare_packet, (void *) *end, copy_to_spare);

					// check if we can handle the packet 
					size_t curr_packet_size = sock_receive->spare_packet->data_size + sizeof (PacketHeader);
					if (sock_receive->spare_packet->header->packet_size == curr_packet_size) {
						// admin_packet_handler (admin_cerver, sock_fd, sock_receive->spare_packet);
						admin_select_packet_handler (admin_cerver, sock_fd, sock_receive->spare_packet);
						sock_receive->spare_packet = NULL;
						sock_receive->missing_packet = 0;
					}

					else sock_receive->missing_packet -= copy_to_spare;

					// offset for the buffer
					if (copy_to_spare < buffer_size) *end += copy_to_spare;
					*buffer_pos += copy_to_spare;
				}
			}
		}

		// drop bad connection
		else {
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Sock fd: %d does not have an associated receive buffer in admin cerver %s.",
				sock_fd, admin_cerver->cerver->info->name->str);
			if (status) {
				cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
				free (status);
			}
			#endif

			admin_cerver_receive_handle_failed (admin_cerver, sock_fd);
    	}
	}

	else {
		char *status = c_string_create ("No connection associeted with socket %d in admin cerver %s!", 
			sock_fd, admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_error (status);
			free (status);
		}

		admin_cerver_receive_handle_failed (admin_cerver, sock_fd);
	}

    return sock_receive;

}

// FIXME: 23/01/2018 -- 18:07 -- check packet header for correct values, to prevent an attack of packets, that wont pass to the handler
static void admin_cerver_receive_handle_buffer (AdminCerver *admin_cerver, i32 sock_fd, char *buffer, size_t buffer_size) {

	if (admin_cerver && buffer) {
		if (buffer && (buffer_size > 0)) {
            char *end = buffer;
            size_t buffer_pos = 0;

            SockReceive *sock_receive = admin_cerver_receive_handle_spare_packet (admin_cerver, sock_fd,
                buffer_size, &end, &buffer_pos);

			if (!sock_receive) return;

            PacketHeader *header = NULL;
            size_t packet_size = 0;
            char *packet_data = NULL;

            size_t remaining_buffer_size = 0;
            size_t packet_real_size = 0;
            size_t to_copy_size = 0;

            bool spare_header = false;

            while (buffer_pos < buffer_size) {
                remaining_buffer_size = buffer_size - buffer_pos;

                if (sock_receive->complete_header) {
                    packet_header_copy (&header, (PacketHeader *) sock_receive->header);

                    end += sock_receive->remaining_header;
                    buffer_pos += sock_receive->remaining_header;

                    // reset sock header values
                    free (sock_receive->header);
                    sock_receive->header = NULL;
                    sock_receive->header_end = NULL;
                    sock_receive->complete_header = false;

                    spare_header = true;
                }

                else if (remaining_buffer_size >= sizeof (PacketHeader)) {
                    header = (PacketHeader *) end;
                    end += sizeof (PacketHeader);
                    buffer_pos += sizeof (PacketHeader);

                    spare_header = false;
                }

                if (header) {
                    // check the packet size
                    packet_size = header->packet_size;
                    if ((packet_size > 0) /* && (packet_size < 65536) */) {
                        Packet *packet = packet_new ();
                        if (packet) {
                            packet_header_copy (&packet->header, header);
                            packet->packet_size = header->packet_size;
                            packet->cerver = admin_cerver->cerver;
                            packet->lobby = NULL;

                            if (spare_header) {
                                free (header);
                                header = NULL;
                            }

                            // check for packet size and only copy what is in the current buffer
                            packet_real_size = packet->header->packet_size - sizeof (PacketHeader);
                            to_copy_size = 0;
                            if ((remaining_buffer_size - sizeof (PacketHeader)) < packet_real_size) {
                                sock_receive->spare_packet = packet;

                                if (spare_header) to_copy_size = buffer_size - sock_receive->remaining_header;
                                else to_copy_size = remaining_buffer_size - sizeof (PacketHeader);
                                
                                sock_receive->missing_packet = packet_real_size - to_copy_size;
                            }

                            else {
                                to_copy_size = packet_real_size;
                                packet_delete (sock_receive->spare_packet);
                                sock_receive->spare_packet = NULL;
                            } 

                            packet_set_data (packet, (void *) end, to_copy_size);

                            end += to_copy_size;
                            buffer_pos += to_copy_size;

                            if (!sock_receive->spare_packet) {
								// admin_packet_handler (admin_cerver, sock_fd, packet);
								admin_select_packet_handler (admin_cerver, sock_fd, packet);
                            }
                                
                        }

                        else {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                                "Failed to create a new packet in cerver_handle_receive_buffer ()");
                        }
                    }

                    else {
                        char *status = c_string_create ("Got a packet of invalid size: %ld", packet_size);
                        if (status) {
                            cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, status); 
                            free (status);
                        }
                        
                        break;
                    }
                }

                else {
                    if (sock_receive->spare_packet) packet_append_data (sock_receive->spare_packet, (void *) end, remaining_buffer_size);

                    else {
                        // copy the piece of possible header that was cut of between recv ()
                        sock_receive->header = malloc (sizeof (PacketHeader));
                        memcpy (sock_receive->header, (void *) end, remaining_buffer_size);

                        sock_receive->header_end = (char *) sock_receive->header;
                        sock_receive->header_end += remaining_buffer_size;

                        // sock_receive->curr_header_pos = remaining_buffer_size;
                        sock_receive->remaining_header = sizeof (PacketHeader) - remaining_buffer_size;

                        buffer_pos += remaining_buffer_size;
                    }
                }
            }
        }
	}

}

static u8 admin_cerver_poll_unregister_connection (AdminCerver *admin_cerver, Connection *connection);

// handles a failed recive from a connection associatd with a client
// end the connection to prevent seg faults or signals for bad sock fd
static void admin_cerver_receive_handle_failed (AdminCerver *admin_cerver, i32 sock_fd) {

	if (admin_cerver) {
		#ifdef CERVER_DEBUG
		printf ("\nadmin_cerver_receive_handle_failed ()\n");
		#endif

		Admin *admin = admin_get_by_sock_fd (admin_cerver, sock_fd);
		if (admin) {
			Connection *connection = connection_get_by_sock_fd_from_client (admin->client, sock_fd);

			// remove from admin cerver poll
			admin_cerver_poll_unregister_connection (admin_cerver, connection);

			// end the connection
			connection_end (connection);

			// remove the connection from the admin client
			dlist_remove (admin->client->connections, connection, NULL);
			connection_delete (connection);

			admin_cerver->stats->current_active_admin_connections -= 1;

			// if the admin does not have any active connection, drop it
            if (admin->client->connections->size <= 0) {
				// remove the admin from the admin cerver
				dlist_remove (admin_cerver->admins, admin, NULL);

				if (admin->credentials) {
					admin->credentials->logged_in = false;
					char *status = c_string_create ("Admin %s logged out from cerver %s",
						admin->credentials->username->str, admin_cerver->cerver->info->name->str);
					if (status) {
						cerver_log_success (status);
						free (status);
					}
				}

				admin_delete (admin);

				admin_cerver->stats->current_n_connected_admins -= 1;

				char *status = c_string_create ("Remaining connected admins in cerver %s: %ld", 
					admin_cerver->cerver->info->name->str, dlist_size (admin_cerver->admins));
				if (status) {
					cerver_log_debug (status);
					free (status);
				}
			}
		}

		else {
			// for whatever reason we have a rogue connection, so drop it
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Sock fd: %d is not registered to a client in cerver %s admin",
				sock_fd, admin_cerver->cerver->info->name->str);
			if (status) {
            	cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
				free (status);
			}
            #endif
            close (sock_fd);        // just close the socket
		}
	}

}

// receive all incoming data from the socket
static void amdin_cerver_receive (AdminCerver *admin_cerver, i32 sock_fd) {

	if (admin_cerver && (sock_fd > -1)) {
		char *buffer = (char *) calloc (admin_cerver->receive_buffer_size, sizeof (char));
		if (buffer) {
			ssize_t rc = recv (sock_fd, buffer, admin_cerver->receive_buffer_size, 0);
			// ssize_t rc = read (sock_fd, buffer, admin_cerver->receive_buffer_size);

			if (rc < 0) {
				#ifdef CERVER_DEBUG
				printf ("\n");
				perror ("Error");
				printf ("\n");
				#endif

				if (errno != EWOULDBLOCK) {     // no more data to read 
					#ifdef CERVER_DEBUG 
					char *status = c_string_create ("amdin_cerver_receive () - rc < 0 - sock fd: %d", sock_fd);
					if (status) {
						cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
						free (status);
					}
					perror ("Error ");
					#endif

					admin_cerver_receive_handle_failed (admin_cerver, sock_fd);
				}
			}

			else if (rc == 0) {
				// man recv -> steam socket perfomed an orderly shutdown
				// but in dgram it might mean something?
				// #ifdef CERVER_DEBUG
				// char *status = c_string_create ("amdin_cerver_receive () - rc == 0 - sock fd: %d", sock_fd);
				// if (status) {
				// 	cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
				// 	free (status);
				// }
				
				// printf ("\n");
				// perror ("Error");
				// printf ("\n");
				// #endif

				admin_cerver_receive_handle_failed (admin_cerver, sock_fd);
			}

			else {
				// char *status = c_string_create ("Admin Cerver %s rc: %ld for sock fd: %d",
				// 	admin_cerver->cerver->info->name->str, rc, sock_fd);
				// if (status) {
				// 	cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
				// 	free (status);
				// }

				// TODO: dedicated stats for authenticated admins
				admin_cerver->stats->admin_receives_done += 1;
				admin_cerver->stats->admin_bytes_received += rc;

				admin_cerver->stats->total_n_receives_done += 1;
				admin_cerver->stats->total_bytes_received += rc;

				admin_cerver_receive_handle_buffer (admin_cerver, sock_fd,
					buffer, rc);
			}

			free (buffer);
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
				"Failed to allocate a new buffer in amdin_cerver_receive ()!");
			#endif
			// break;
		}
	}

}

// get a free index in the admin cerver poll strcuture
static i32 admin_cerver_poll_get_free_idx (AdminCerver *admin_cerver) {

    if (admin_cerver) {
        for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
            if (admin_cerver->fds[i].fd == -1) return i;

        return -1;
    }

    return 0;

}

// get the idx of the connection sock fd in the cerver poll fds
static i32 admin_cerver_poll_get_idx_by_sock_fd (AdminCerver *admin_cerver, i32 sock_fd) {

    if (admin_cerver) {
        for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
            if (admin_cerver->fds[i].fd == sock_fd) return i;
    }

    return -1;

}

// regsiters a client connection to the admin cerver's poll structure
// returns 0 on success, 1 on error
static u8 admin_cerver_poll_register_connection (AdminCerver *admin_cerver, Connection *connection) {

	u8 retval = 1;

	if (admin_cerver && connection) {
		i32 idx = admin_cerver_poll_get_free_idx (admin_cerver);
		if (idx > 0) {
			admin_cerver->fds[idx].fd = connection->socket->sock_fd;
            admin_cerver->fds[idx].events = POLLIN;
            admin_cerver->current_n_fds++;

			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Added new sock fd to cerver %s admin poll, idx: %i",
				admin_cerver->cerver->info->name->str, idx);
			if (status) {
				cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, status);
				free (status);
			}
            #endif

			printf ("admin_cerver_poll_register_connection () sock fd: %d\n", connection->socket->sock_fd);

            retval = 0;
		}

		else {
			// admin cerver poll is full, no more connections allowed
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Poll from cerver %s admin is full!",
				admin_cerver->cerver->info->name->str);
			if (status) {
				cerver_log_msg (stdout, LOG_WARNING, LOG_NO_TYPE, status);
				free (status);
			}
            #endif
		}
	}

	return retval;

}

// unregsiters a client connection from the admin cerver's poll structure
// returns 0 on success, 1 on error
static u8 admin_cerver_poll_unregister_connection (AdminCerver *admin_cerver, Connection *connection) {

	u8 retval = 1;

	if (admin_cerver && connection) {
		i32 idx = admin_cerver_poll_get_idx_by_sock_fd (admin_cerver, connection->socket->sock_fd);
		if (idx > 0) {
			admin_cerver->fds[idx].fd = -1;
            admin_cerver->fds[idx].events = -1;
            admin_cerver->current_n_fds--;

			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Removed sock fd from cerver %s admin poll, idx: %d",
				admin_cerver->cerver->info->name->str, idx);
			if (status) {
				cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
				free (status);
			}
            #endif

			retval = 0;			// success
		}
	}

	return retval;

}

static void admin_cerver_poll (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		char *status = c_string_create ("Cerver %s admin handler has started!", 
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, status);
			free (status);
		}

		int poll_retval = 0;

		while (admin_cerver->cerver->isRunning && admin_cerver->running) {
			poll_retval = poll (admin_cerver->fds, admin_cerver->max_n_fds, admin_cerver->poll_timeout);

			// poll failed
			if (poll_retval < 0) {
				char *status = c_string_create ("Cerver %s admin poll has failed!", 
					admin_cerver->cerver->info->name->str);
				if (status) {
					cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
					free (status);
				}
				
				perror ("Error");
				admin_cerver->running = false;
				break;
			}

			// if poll has timed out, just continue to the next loop... 
			if (poll_retval == 0) {
				// #ifdef CERVER_DEBUG
				// char *status = c_string_create ("Cerver %s admin poll timeout", 
				// 	admin_cerver->cerver->info->name->str);
				// if (status) {
				// 	cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
				// 	free (status);
				// }
				// #endif
				continue;
			}

			// one or more fd(s) are readable, need to determine which ones they are
			for (u32 i = 0; i < admin_cerver->max_n_fds; i++) {
				if (admin_cerver->fds[i].fd != -1) {
					switch (admin_cerver->fds[i].revents) {
						// A connection setup has been completed or new data arrived
						case POLLIN: {
							// accept incoming connections that are queued
							if (i == 0) admin_cerver_accept (admin_cerver);

							// not the cerver socket, so a connection fd must be readable
							else amdin_cerver_receive (admin_cerver, admin_cerver->fds[i].fd);
						} break;

						// A disconnection request has been initiated by the other end
						// or a connection is broken (SIGPIPE is also set when we try to write to it)
						// or The other end has shut down one direction
						case POLLHUP: 
							admin_cerver_receive_handle_failed (admin_cerver, admin_cerver->fds[i].fd); 
							break;

						// An asynchronous error occurred
						case POLLERR: 
							admin_cerver_receive_handle_failed (admin_cerver, admin_cerver->fds[i].fd); 
							break;

						// Urgent data arrived. SIGURG is sent then.
						case POLLPRI: {
							/*** TODO: ***/
						} break;

						// 23/01/2020 -- 15:48 -- trying this
						default: 
							// admin_cerver_receive_handle_failed (admin_cerver, admin_cerver->fds[i].fd); 
							break;
					}
				}
			}
		}
	}

}

static void admin_cerver_update (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		// #ifdef CERVER_DEBUG
		char *status = c_string_create ("The cerver %s admin update has started!",
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_success (status);
			free (status);
		}
		// #endif

		while (admin_cerver->cerver->isRunning && admin_cerver->running) {
			// FIXME:
			// check for timeouts in authentication
			// TODO: create an ip blacklist to prevent attacks -- maybe read from a file
			// check for inactivity timeout

			sleep (10);
		}
	}

}

#pragma endregion

#pragma region teardown 

// disable socket I/O in both ways and stop any ongoing job
static u8 admin_cerver_shutdown (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		if (admin_cerver->running) {
			admin_cerver->running = false;

			// close the admin cerver socket
			if (!close (admin_cerver->sock)) {
				#ifdef CERVER_DEBUG
				char *status = c_string_create ("The cerver %s admin socket has been closed.",
					admin_cerver->cerver->info->name->str);
                if (status) {
					cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
					free (status);
				}
            	#endif

				retval = 0;
			}

			else {
				char *status = c_string_create ("Failed to close cerver %s admin socket!",
					admin_cerver->cerver->info->name->str);
				if (status) {
					cerver_log_msg (stdout, LOG_ERROR, LOG_CERVER, status);
					free (status);
				}
			}
		}
	}

	return retval;

}

// send a final package to all admins, and then disconnect them from the cerver
static u8 admin_cerver_disconnect_admins (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		// check if we have conencted admins
		if (admin_cerver->stats->current_auth_n_connected_admins > 0) {
			// FIXME: prepare to send a final report on cerver stats

			// send cerver teardown packet
			Packet *report_packet = packet_generate_request (CERVER_PACKET, CERVER_INFO_STATS, NULL, 0);
			if (report_packet) {
				// send to admins
				Admin *admin = NULL;
				for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
					admin = (Admin *) le->data;

					if (admin->authenticated) {
						// send to their first connection
						packet_set_network_values (report_packet, 
							NULL,
							admin->client,
							(Connection *) dlist_start (admin->client->connections),
							NULL);
						packet_send (report_packet, 0, NULL, false);
					}
				}

				packet_delete (report_packet);
			}
		}

		// disconnect all the admins -- send a teardown packet and end connections
		Packet *teardown_packet = packet_generate_request (CERVER_PACKET, CERVER_TEARDOWN, NULL, 0);
		if (teardown_packet) {
			// send to admins
			Admin *admin = NULL;
			for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
				admin = (Admin *) le->data;

				// send to their first connection
				packet_set_network_values (teardown_packet, 
					NULL,
					admin->client,
					(Connection *) dlist_start (admin->client->connections),
					NULL);
				packet_send (teardown_packet, 0, NULL, false);

				// end connections
				Connection *connection = NULL;
				for (ListElement *le_sub = dlist_start (admin->client->connections); le_sub; le_sub = le_sub->next) {
					connection = (Connection *) le->data;
					connection_end (connection);
				}
			}

			packet_delete (teardown_packet);
		}
	}

	return retval;

}

// correctly stops the admin cerver, disconnects admins and free any memory used by admin cerver
// returns 0 on success, 1 on any error
u8 admin_cerver_teardown (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		u8 errors = 0;
		char *status = NULL;

		#ifdef CERVER_DEBUG
		status = c_string_create ("Starting cerver %s admin teardown...", 
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
			free (status);
		}
        #endif

		// manage connected admins
		errors |= admin_cerver_disconnect_admins (admin_cerver);

		// shutdown admin network
		errors |= admin_cerver_shutdown (admin_cerver);

		status = c_string_create ("Cerver %s admin teardown was successfull!",
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_success (status);
			free (status);
		}

		retval = errors;
	}

	return retval;

}

#pragma endregion