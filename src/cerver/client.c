#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/auth.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/sessions.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static void client_event_delete (void *ptr);
static void client_error_delete (void *client_error_ptr);

static u8 client_file_receive (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

static ReceiveError client_receive_actual (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t *rc
);

// request to read x amount of bytes from the connection's sock fd
// into the specified buffer
// this method will only return once the requested bytes
// have been received or on any error
static ReceiveError client_receive_data (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t requested_data
);

unsigned int client_receive (
	Client *client, Connection *connection
);

static u8 client_packet_handler (Packet *packet);

static bool client_connection_end_request (Connection *connection);

static u64 next_client_id = 0;

#pragma region aux

static ClientConnection *client_connection_aux_new (
	Client *client, Connection *connection
) {

	ClientConnection *cc = (ClientConnection *) malloc (sizeof (ClientConnection));
	if (cc) {
		cc->connection_thread_id = 0;
		cc->client = client;
		cc->connection = connection;
	}

	return cc;

}

void client_connection_aux_delete (ClientConnection *cc) { if (cc) free (cc); }

#pragma endregion

#pragma region stats

static ClientStats *client_stats_new (void) {

	ClientStats *client_stats = (ClientStats *) malloc (sizeof (ClientStats));
	if (client_stats) {
		(void) memset (client_stats, 0, sizeof (ClientStats));
		client_stats->received_packets = packets_per_type_new ();
		client_stats->sent_packets = packets_per_type_new ();
	}

	return client_stats;

}

static inline void client_stats_delete (ClientStats *client_stats) {

	if (client_stats) {
		packets_per_type_delete (client_stats->received_packets);
		packets_per_type_delete (client_stats->sent_packets);

		free (client_stats);
	}

}

void client_stats_print (Client *client) {

	if (client) {
		if (client->stats) {
			cerver_log_msg ("\nClient's stats:\n");
			cerver_log_msg ("Threshold time:            %ld", client->stats->threshold_time);

			cerver_log_msg ("N receives done:           %ld", client->stats->n_receives_done);

			cerver_log_msg ("Total bytes received:      %ld", client->stats->total_bytes_received);
			cerver_log_msg ("Total bytes sent:          %ld", client->stats->total_bytes_sent);

			cerver_log_msg ("N packets received:        %ld", client->stats->n_packets_received);
			cerver_log_msg ("N packets sent:            %ld", client->stats->n_packets_sent);

			cerver_log_msg ("\nReceived packets:");
			packets_per_type_print (client->stats->received_packets);

			cerver_log_msg ("\nSent packets:");
			packets_per_type_print (client->stats->sent_packets);
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Client does not have a reference to a client stats!"
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CLIENT,
			"Can't get stats of a NULL client!"
		);
	}

}

static ClientFileStats *client_file_stats_new (void) {

	ClientFileStats *file_stats = (ClientFileStats *) malloc (sizeof (ClientFileStats));
	if (file_stats) {
		(void) memset (file_stats, 0, sizeof (ClientFileStats));
	}

	return file_stats;

}

static void client_file_stats_delete (ClientFileStats *file_stats) {

	if (file_stats) free (file_stats);

}

void client_file_stats_print (Client *client) {

	if (client) {
		if (client->file_stats) {
			cerver_log_msg ("Files requests:                %ld", client->file_stats->n_files_requests);
			cerver_log_msg ("Success requests:              %ld", client->file_stats->n_success_files_requests);
			cerver_log_msg ("Bad requests:                  %ld", client->file_stats->n_bad_files_requests);
			cerver_log_msg ("Files sent:                    %ld", client->file_stats->n_files_sent);
			cerver_log_msg ("Failed files sent:             %ld", client->file_stats->n_bad_files_sent);
			cerver_log_msg ("Files bytes sent:              %ld\n", client->file_stats->n_bytes_sent);

			cerver_log_msg ("Files upload requests:         %ld", client->file_stats->n_files_upload_requests);
			cerver_log_msg ("Success uploads:               %ld", client->file_stats->n_success_files_uploaded);
			cerver_log_msg ("Bad uploads:                   %ld", client->file_stats->n_bad_files_upload_requests);
			cerver_log_msg ("Bad files received:            %ld", client->file_stats->n_bad_files_received);
			cerver_log_msg ("Files bytes received:          %ld\n", client->file_stats->n_bytes_received);
		}
	}

}

#pragma endregion

#pragma region main

const char *client_connections_status_to_string (
	const ClientConnectionsStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case CLIENT_CONNECTIONS_STATUS_##name: return #string;
		CLIENT_CONNECTIONS_STATUS_MAP(XX)
		#undef XX
	}

	return client_connections_status_to_string (CLIENT_CONNECTIONS_STATUS_NONE);

}

const char *client_connections_status_description (
	const ClientConnectionsStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case CLIENT_CONNECTIONS_STATUS_##name: return #description;
		CLIENT_CONNECTIONS_STATUS_MAP(XX)
		#undef XX
	}

	return client_connections_status_description (CLIENT_CONNECTIONS_STATUS_NONE);

}

Client *client_new (void) {

	Client *client = (Client *) malloc (sizeof (Client));
	if (client) {
		client->id = 0;
		client->session_id = NULL;

		(void) memset (client->name, 0, CLIENT_NAME_SIZE);

		client->connections = NULL;

		client->drop_client = false;

		client->data = NULL;
		client->delete_data = NULL;

		client->running = false;
		client->time_started = 0;
		client->uptime = 0;

		client->last_activity = time (NULL);

		client->num_handlers_alive = 0;
		client->num_handlers_working = 0;
		client->handlers_lock = NULL;
		client->app_packet_handler = NULL;
		client->app_error_packet_handler = NULL;
		client->custom_packet_handler = NULL;

		client->max_received_packet_size = CLIENT_DEFAULT_MAX_RECEIVED_PACKET_SIZE;

		client->check_packets = false;
		client->max_bad_packets = CLIENT_DEFAULT_MAX_BAD_PACKETS;

		client->lock = NULL;

		client->n_update_threads = 0;
		(void) pthread_cond_init (&client->updates_cond, NULL);

		client->n_async_threads = 0;

		for (unsigned int i = 0; i < CLIENT_MAX_EVENTS; i++)
			client->events[i] = NULL;

		for (unsigned int i = 0; i < CLIENT_MAX_ERRORS; i++)
			client->errors[i] = NULL;

		client->n_paths = 0;
		for (unsigned int i = 0; i < CLIENT_FILES_MAX_PATHS; i++)
			client->paths[i] = NULL;

		client->uploads_path = NULL;

		client->file_upload_handler = client_file_receive;

		client->file_upload_cb = NULL;

		client->file_stats = NULL;

		client->stats = NULL;
	}

	return client;

}

void client_delete (void *ptr) {

	if (ptr) {
		Client *client = (Client *) ptr;

		str_delete (client->session_id);

		dlist_delete (client->connections);

		if (client->data) {
			if (client->delete_data) client->delete_data (client->data);
			else free (client->data);
		}

		if (client->handlers_lock) {
			pthread_mutex_destroy (client->handlers_lock);
			free (client->handlers_lock);
		}

		handler_delete (client->app_packet_handler);
		handler_delete (client->app_error_packet_handler);
		handler_delete (client->custom_packet_handler);

		if (client->lock) {
			pthread_mutex_destroy (client->lock);
			free (client->lock);
		}

		(void) pthread_cond_destroy (&client->updates_cond);

		for (unsigned int i = 0; i < CLIENT_MAX_EVENTS; i++)
			if (client->events[i]) client_event_delete (client->events[i]);

		for (unsigned int i = 0; i < CLIENT_MAX_ERRORS; i++)
			if (client->errors[i]) client_error_delete (client->errors[i]);

		for (unsigned int i = 0; i < CLIENT_FILES_MAX_PATHS; i++)
			str_delete (client->paths[i]);

		str_delete (client->uploads_path);

		client_file_stats_delete (client->file_stats);

		client_stats_delete (client->stats);

		free (client);
	}

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

void client_delete_dummy (void *ptr) {}

#pragma GCC diagnostic pop

// creates a new client and inits its values
Client *client_create (void) {

	Client *client = client_new ();
	if (client) {
		// clients are created by the cerver threads and by the app at the same time
		client->id = __atomic_fetch_add (&next_client_id, 1, __ATOMIC_RELAXED);

		(void) strncpy (client->name, CLIENT_DEFAULT_NAME, CLIENT_NAME_SIZE - 1);

		(void) time (&client->connected_timestamp);

		client->connections = dlist_init (
			connection_delete, connection_comparator
		);

		client->lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (client->lock, NULL);

		client->file_stats = client_file_stats_new ();

		client->stats = client_stats_new ();
	}

	return client;

}

// creates a new client and registers a new connection
Client *client_create_with_connection (
	Cerver *cerver,
	const i32 sock_fd, const struct sockaddr_storage *address
) {

	Client *client = client_create ();
	if (client) {
		Connection *connection = connection_create (sock_fd, address, cerver->protocol);
		if (connection) connection_register_to_client (client, connection);
		else {
			// failed to create a new connection
			client_delete (client);
			client = NULL;
		}
	}

	return client;

}

// sets the client's name
void client_set_name (Client *client, const char *name) {

	if (client) {
		(void) strncpy (client->name, name, CLIENT_NAME_SIZE - 1);
	}

}

// sets the client's session id
// returns 0 on succes, 1 on error
u8 client_set_session_id (
	Client *client, const char *session_id
) {

	u8 retval = 1;

	if (client) {
		str_delete (client->session_id);
		client->session_id = session_id ? str_new (session_id) : NULL;

		retval = 0;
	}

	return retval;

}

// returns the client's app data
void *client_get_data (Client *client) {

	return (client ? client->data : NULL);

}

// sets client's data and a way to destroy it
// deletes the previous data of the client
void client_set_data (
	Client *client, void *data, Action delete_data
) {

	if (client) {
		if (client->data) {
			if (client->delete_data) client->delete_data (client->data);
			else free (client->data);
		}

		client->data = data;
		client->delete_data = delete_data;
	}

}

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
void client_set_app_handlers (
	Client *client,
	Handler *app_handler, Handler *app_error_handler
) {

	if (client) {
		client->app_packet_handler = app_handler;
		if (client->app_packet_handler) {
			client->app_packet_handler->type = HANDLER_TYPE_CLIENT;
			client->app_packet_handler->client = client;
		}

		client->app_error_packet_handler = app_error_handler;
		if (client->app_error_packet_handler) {
			client->app_error_packet_handler->type = HANDLER_TYPE_CLIENT;
			client->app_error_packet_handler->client = client;
		}
	}

}

// sets a PACKET_TYPE_CUSTOM packet type handler
void client_set_custom_handler (
	Client *client, Handler *custom_handler
) {

	if (client) {
		client->custom_packet_handler = custom_handler;
		if (client->custom_packet_handler) {
			client->custom_packet_handler->type = HANDLER_TYPE_CLIENT;
			client->custom_packet_handler->client = client;
		}
	}

}

// only handle packets with size <= max_received_packet_size
// if the packet is bigger it will be considered a bad packet 
void client_set_max_received_packet_size (
	Client *client, size_t max_received_packet_size
) {

	if (client) client->max_received_packet_size = max_received_packet_size;

}

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
void client_set_check_packets (
	Client *client, bool check_packets
) {

	if (client) {
		client->check_packets = check_packets;
	}

}

void client_set_max_bad_packets (
	Client *client, u8 max_bad_packets
) {

	if (client) client->max_bad_packets = max_bad_packets;

}

// compare clients based on their client ids
int client_comparator_client_id (
	const void *a, const void *b
) {

	if (a && b) {
		Client *client_a = (Client *) a;
		Client *client_b = (Client *) b;

		if (client_a->id < client_b->id) return -1;
		else if (client_a->id == client_b->id) return 0;
		else return 1;
	}

	return 0;

}

// compare clients based on their session ids
int client_comparator_session_id (
	const void *a, const void *b
) {

	if (a && b) return str_compare (
		((Client *) a)->session_id, ((Client *) b)->session_id
	);

	if (a && !b) return -1;
	if (!a && b) return 1;

	return 0;

}

// a connection with an update thread is ended by it,
// so its socket is never closed while the thread uses it
// any other connection is closed now
static void client_connections_close (Client *client) {

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
		Connection *connection = (Connection *) le->data;

		if (!client_connection_end_request (connection)) connection_end (connection);
	}

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

}

// closes all client connections
u8 client_disconnect (Client *client) {

	u8 retval = 1;

	if (client) {
		client_connections_close (client);

		retval = 0;
	}

	return retval;

}

// the client got disconnected from the cerver, so correctly clear our data
void client_got_disconnected (Client *client) {

	if (client) {
		// close any ongoing connection
		client_connections_close (client);

		// reset client
		__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);
		client->time_started = 0;
	}

}

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
void client_drop (Cerver *cerver, Client *client) {

	if (cerver && client) {
		client_unregister_from_cerver (cerver, client);
		client_delete (client);
	}

}

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
u8 client_connection_add (Client *client, Connection *connection) {

	return (client && connection) ?
		(u8) dlist_insert_at_end (client->connections, connection) : 1;

}

// removes the connection from the client
// returns 0 on success, 1 on error
u8 client_connection_remove (
	Client *client, Connection *connection
) {

	u8 retval = 1;

	if (client && connection)
		retval = dlist_remove (
			client->connections, connection, NULL
		) ? 0 : 1;

	return retval;

}

// closes the connection & then removes it from the client
// finally deletes the connection
// moves the socket to the cerver's socket pool
// returns 0 on success, 1 on error
u8 client_connection_drop (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && client && connection) {
		if (dlist_remove (client->connections, connection, NULL)) {
			connection_drop (cerver, connection);

			retval = 0;
		}
	}

	return retval;

}

// removes the connection from the client referred to by the sock fd by calling client_connection_drop ()
// and also remove the client & connection from the cerver's structures when needed
// also checks if there is another active connection in the client, if not it will be dropped
// returns the resulting status after the operation
ClientConnectionsStatus client_remove_connection_by_sock_fd (
	Cerver *cerver, Client *client, const i32 sock_fd
) {

	ClientConnectionsStatus status = CLIENT_CONNECTIONS_STATUS_ERROR;

	if (cerver && client) {
		Connection *connection = NULL;
		switch (client->connections->size) {
			case 0: {
				#ifdef CLIENT_DEBUG
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_CLIENT,
					"client_remove_connection_by_sock_fd () - "
					"Client <%ld> does not have ANY connection - removing him from cerver...",
					client->id
				);
				#endif

				(void) client_remove_from_cerver (cerver, client);
				client_delete (client);

				status = CLIENT_CONNECTIONS_STATUS_DROPPED;
			} break;

			case 1: {
				#ifdef CLIENT_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
					"client_remove_connection_by_sock_fd () - "
					"Client <%d> has only 1 connection left!",
					client->id
				);
				#endif

				connection = (Connection *) client->connections->start->data;

				// remove the connection from cerver structures & poll array
				(void) connection_remove_from_cerver (cerver, connection);

				// remove, close & delete the connection
				if (!client_connection_drop (
					cerver,
					client,
					connection
				)) {
					cerver_event_trigger (
						CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
						cerver,
						NULL, NULL
					);

					// no connections left in client, just remove and delete
					client_remove_from_cerver (cerver, client);
					client_delete (client);

					cerver_event_trigger (
						CERVER_EVENT_CLIENT_DROPPED,
						cerver,
						NULL, NULL
					);

					status = CLIENT_CONNECTIONS_STATUS_DROPPED;
				}
			} break;

			default: {
				#ifdef CLIENT_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
					"client_remove_connection_by_sock_fd () - "
					"Client <%d> has %ld connections left!",
					client->id, dlist_size (client->connections)
				);
				#endif

				// search the connection in the client
				connection = connection_get_by_sock_fd_from_client (
					client, sock_fd
				);

				if (connection) {
					// remove the connection from cerver structures & poll array
					(void) connection_remove_from_cerver (cerver, connection);

					if (!client_connection_drop (
						cerver,
						client,
						connection
					)) {
						cerver_event_trigger (
							CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
							cerver,
							NULL, NULL
						);

						status = CLIENT_CONNECTIONS_STATUS_DROPPED;
					}
				}

				else {
					// the connection may not belong to this client
					#ifdef CLIENT_DEBUG
					cerver_log (
						LOG_TYPE_WARNING, LOG_TYPE_CLIENT,
						"client_remove_connection_by_sock_fd () - Client with id "
						"%ld does not have a connection related to sock fd %d",
						client->id, sock_fd
					);
					#endif
				}
			} break;
		}
	}

	return status;

}

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
u8 client_register_connections_to_cerver (
	Cerver *cerver, Client *client
) {

	u8 retval = 1;

	if (cerver && client) {
		u8 n_failed = 0;          // n connections that failed to be registered

		Connection *connection = NULL;
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			connection = (Connection *) le->data;
			if (connection_register_to_cerver (cerver, client, connection))
				n_failed++;
		}

		 // check how many connections have failed
		if (n_failed == client->connections->size) {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Failed to register all the connections for client %ld (id) to cerver %s",
				client->id, cerver->info->name
			);
			#endif

			client_drop (cerver, client);       // drop the client ---> no active connections
		}

		else retval = 0;        // at least one connection is active
	}

	return retval;

}

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
u8 client_unregister_connections_from_cerver (
	Cerver *cerver, Client *client
) {

	u8 retval = 1;

	if (cerver && client) {
		u8 n_failed = 0;        // n connections that failed to unregister

		Connection *connection = NULL;
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			connection = (Connection *) le->data;
			if (connection_unregister_from_cerver (cerver, connection))
				n_failed++;
		}

		// check how many connections have failed
		if ((n_failed > 0) && (n_failed == client->connections->size)) {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Failed to unregister all the connections for client %ld (id) from cerver %s",
				client->id, cerver->info->name
			);
			#endif

			// client_drop (cerver, client);       // drop the client ---> no active connections
		}

		else retval = 0;        // at least one connection is active
	}

	return retval;

}

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
u8 client_register_connections_to_cerver_poll (
	Cerver *cerver, Client *client
) {

	u8 retval = 1;

	if (cerver && client) {
		u8 n_failed = 0;          // n connections that failed to be registered

		// register all the client connections to the cerver poll
		Connection *connection = NULL;
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			connection = (Connection *) le->data;
			if (connection_register_to_cerver_poll (cerver, connection))
				n_failed++;
		}

		// check how many connections have failed
		if (n_failed == client->connections->size) {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Failed to register all the connections for client %ld (id) to cerver %s poll",
				client->id, cerver->info->name
			);
			#endif

			client_drop (cerver, client);       // drop the client ---> no active connections
		}

		else retval = 0;        // at least one connection is active
	}

	return retval;

}

// unregisters all the active connections from a client from the cerver's poll
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
u8 client_unregister_connections_from_cerver_poll (
	Cerver *cerver, Client *client
) {

	u8 retval = 1;

	if (cerver && client) {
		u8 n_failed = 0;        // n connections that failed to unregister

		// unregister all the client connections from the cerver poll
		Connection *connection = NULL;
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			connection = (Connection *) le->data;
			if (connection_unregister_from_cerver_poll (cerver, connection))
				n_failed++;
		}

		// check how many connections have failed
		if (n_failed == client->connections->size) {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Failed to unregister all the connections for client %ld (id) from cerver %s poll",
				client->id, cerver->info->name
			);
			#endif

			client_drop (cerver, client);       // drop the client ---> no active connections
		}

		else retval = 0;        // at least one connection is active
	}

	return retval;

}

// removes the client from cerver data structures, not taking into account its connections
Client *client_remove_from_cerver (
	Cerver *cerver, Client *client
) {

	Client *retval = NULL;

	if (cerver && client) {
		void *client_data = avl_remove_node (cerver->clients, client);
		if (client_data) {
			retval = (Client *) client_data;

			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CLIENT,
				"Unregistered a client from cerver %s.", cerver->info->name
			);
			#endif

			CERVER_STATS_SUBTRACT (cerver->stats->current_n_connected_clients, 1);
			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Connected clients to cerver %s: %i.",
				cerver->info->name, cerver->stats->current_n_connected_clients
			);
			#endif
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Received NULL ptr when attempting to remove a client from cerver's %s client tree.",
				cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

static void client_register_to_cerver_internal (
	Cerver *cerver, Client *client
) {

	(void) avl_insert_node (cerver->clients, client);

	#ifdef CLIENT_DEBUG
	cerver_log (
		LOG_TYPE_SUCCESS, LOG_TYPE_CLIENT,
		"Registered a new client to cerver %s.",
		cerver->info->name
	);
	#endif

	CERVER_STATS_ADD (cerver->stats->total_n_clients, 1);
	CERVER_STATS_ADD (cerver->stats->current_n_connected_clients, 1);

	#ifdef CERVER_STATS
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
		"Connected clients to cerver %s: %i.",
		cerver->info->name,
		cerver->stats->current_n_connected_clients
	);
	#endif

}

// registers a client to the cerver --> add it to cerver's structures
// registers all of the current active client connections to the cerver poll
// returns 0 on success, 1 on error
u8 client_register_to_cerver (
	Cerver *cerver, Client *client
) {

	u8 retval = 1;

	if (cerver && client) {
		if (!client_register_connections_to_cerver (cerver, client)) {
			switch (cerver->handler_type) {
				case CERVER_HANDLER_TYPE_NONE: break;

				case CERVER_HANDLER_TYPE_POLL: {
					if (!client_register_connections_to_cerver_poll (cerver, client)) {
						client_register_to_cerver_internal (cerver, client);

						retval = 0;
					}
				} break;

				case CERVER_HANDLER_TYPE_THREADS: {
					client_register_to_cerver_internal (cerver, client);

					retval = 0;
				} break;

				default: break;
			}
		}
	}

	return retval;

}

// unregisters a client from a cerver -- removes it from cerver's structures
Client *client_unregister_from_cerver (
	Cerver *cerver, Client *client
) {

	Client *retval = NULL;

	if (cerver && client) {
		if (client->connections->size > 0) {
			// unregister the connections from the cerver
			client_unregister_connections_from_cerver (cerver, client);

			// unregister all the client connections from the cerver
			// client_unregister_connections_from_cerver (cerver, client);
			Connection *connection = NULL;
			for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
				connection = (Connection *) le->data;
				connection_unregister_from_cerver_poll (cerver, connection);
			}
		}

		// remove the client from the cerver's clients
		retval = client_remove_from_cerver (cerver, client);
	}

	return retval;

}

// gets the client associated with a sock fd using the client-sock fd map
Client *client_get_by_sock_fd (Cerver *cerver, i32 sock_fd) {

	Client *client = NULL;

	if (cerver) {
		const i32 *key = &sock_fd;
		void *client_data = htab_get (
			cerver->client_sock_fd_map,
			key, sizeof (i32)
		);

		if (client_data) client = (Client *) client_data;
	}

	return client;

}

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
Client *client_get_by_session_id (
	Cerver *cerver, const char *session_id
) {

	Client *client = NULL;

	if (session_id) {
		// create our search query
		Client *client_query = client_new ();
		if (client_query) {
			client_set_session_id (client_query, session_id);

			void *data = avl_get_node_data_safe (cerver->clients, client_query, NULL);
			if (data) client = (Client *) data;     // found

			client_delete (client_query);
		}
	}

	return client;

}

// broadcast a packet to all clients inside an avl structure
void client_broadcast_to_all_avl (
	AVLNode *node,
	Cerver *cerver,
	Packet *packet
) {

	if (node && cerver && packet) {
		client_broadcast_to_all_avl (node->right, cerver, packet);

		// send the packet to current client
		if (node->id) {
			Client *client = (Client *) node->id;

			// send the packet to all of its active connections
			Connection *connection = NULL;
			for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
				connection = (Connection *) le->data;
				packet_set_network_values (packet, cerver, client, connection, NULL);
				packet_send (packet, 0, NULL, false);
			}
		}

		client_broadcast_to_all_avl (node->left, cerver, packet);
	}

}

#pragma endregion

#pragma region events

u8 client_event_unregister (Client *client, ClientEventType event_type);

// get the description for the current event type
const char *client_event_type_description (const ClientEventType type) {

	switch (type) {
		#define XX(num, name, description) case CLIENT_EVENT_##name: return #description;
		CLIENT_EVENT_MAP(XX)
		#undef XX
	}

	return client_event_type_description (CLIENT_EVENT_UNKNOWN);

}

static ClientEventData *client_event_data_new (void) {

	ClientEventData *event_data = (ClientEventData *) malloc (sizeof (ClientEventData));
	if (event_data) {
		event_data->client = NULL;
		event_data->connection = NULL;

		event_data->response_data = NULL;
		event_data->delete_response_data = NULL;

		event_data->action_args = NULL;
		event_data->delete_action_args = NULL;
	}

	return event_data;

}

void client_event_data_delete (ClientEventData *event_data) {

	if (event_data) free (event_data);

}

static ClientEventData *client_event_data_create (
	const Client *client, const Connection *connection,
	ClientEvent *event
) {

	ClientEventData *event_data = client_event_data_new ();
	if (event_data) {
		event_data->client = client;
		event_data->connection = connection;

		event_data->response_data = event->response_data;
		event_data->delete_response_data = event->delete_response_data;

		event_data->action_args = event->work_args;
		event_data->delete_action_args = event->delete_action_args;
	}

	return event_data;

}

static ClientEvent *client_event_new (void) {

	ClientEvent *event = (ClientEvent *) malloc (sizeof (ClientEvent));
	if (event) {
		event->type = CLIENT_EVENT_NONE;

		event->create_thread = false;
		event->drop_after_trigger = false;

		event->request_type = 0;
		event->response_data = NULL;
		event->delete_response_data = NULL;

		event->work = NULL;
		event->work_args = NULL;
		event->delete_action_args = NULL;
	}

	return event;

}

static void client_event_delete (void *ptr) {

	if (ptr) {
		ClientEvent *event = (ClientEvent *) ptr;

		if (event->response_data) {
			if (event->delete_response_data)
				event->delete_response_data (event->response_data);
		}

		if (event->work_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->work_args);
		}

		free (event);
	}

}

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated ClientEventData structure will be passed to your method
// that should be free using the client_event_data_delete () method
// returns 0 on success, 1 on error
u8 client_event_register (
	Client *client,
	const ClientEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (client) {
		ClientEvent *event = client_event_new ();
		if (event) {
			event->type = event_type;

			event->create_thread = create_thread;
			event->drop_after_trigger = drop_after_trigger;

			event->work = work;
			event->work_args = work_args;
			event->delete_action_args = delete_action_args;

			// search if there is an action already registred for that event and remove it
			(void) client_event_unregister (client, event_type);

			client->events[event_type] = event;

			retval = 0;
		}
	}

	return retval;

}

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if event is NOT registered
u8 client_event_unregister (
	Client *client, const ClientEventType event_type
) {

	u8 retval = 1;

	if (client) {
		if (client->events[event_type]) {
			client_event_delete (client->events[event_type]);
			client->events[event_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

void client_event_set_response (
	Client *client,
	const ClientEventType event_type,
	void *response_data, Action delete_response_data
) {

	if (client) {
		ClientEvent *event = client->events[event_type];
		if (event) {
			event->response_data = response_data;
			event->delete_response_data = delete_response_data;
		}
	}

}

// triggers all the actions that are registred to an event
void client_event_trigger (
	const ClientEventType event_type,
	const Client *client, const Connection *connection
) {

	if (client) {
		ClientEvent *event = client->events[event_type];
		if (event) {
			// trigger the action
			if (event->work) {
				if (event->create_thread) {
					pthread_t thread_id = 0;
					thread_create_detachable (
						&thread_id,
						event->work,
						client_event_data_create (
							client, connection,
							event
						)
					);
				}

				else {
					(void) event->work (client_event_data_create (
						client, connection,
						event
					));
				}

				if (event->drop_after_trigger) {
					(void) client_event_unregister ((Client *) client, event_type);
				}
			}
		}
	}

}

#pragma endregion

#pragma region errors

u8 client_error_unregister (Client *client, const ClientErrorType error_type);

// get the description for the current error type
const char *client_error_type_description (const ClientErrorType type) {

	switch (type) {
		#define XX(num, name, description) case CLIENT_ERROR_##name: return #description;
		CLIENT_ERROR_MAP(XX)
		#undef XX
	}

	return client_error_type_description (CLIENT_ERROR_UNKNOWN);

}

static ClientErrorData *client_error_data_new (void) {

	ClientErrorData *error_data = (ClientErrorData *) malloc (sizeof (ClientErrorData));
	if (error_data) {
		error_data->client = NULL;
		error_data->connection = NULL;

		error_data->action_args = NULL;

		error_data->error_message = NULL;
	}

	return error_data;

}

void client_error_data_delete (ClientErrorData *error_data) {

	if (error_data) {
		str_delete (error_data->error_message);

		free (error_data);
	}

}

static ClientErrorData *client_error_data_create (
	const Client *client, const Connection *connection,
	void *args,
	const char *error_message
) {

	ClientErrorData *error_data = client_error_data_new ();
	if (error_data) {
		error_data->client = client;
		error_data->connection = connection;

		error_data->action_args = args;

		error_data->error_message = error_message ? str_new (error_message) : NULL;
	}

	return error_data;

}

static ClientError *client_error_new (void) {

	ClientError *client_error = (ClientError *) malloc (sizeof (ClientError));
	if (client_error) {
		client_error->type = CLIENT_ERROR_NONE;

		client_error->create_thread = false;
		client_error->drop_after_trigger = false;

		client_error->work = NULL;
		client_error->work_args = NULL;
		client_error->delete_action_args = NULL;
	}

	return client_error;

}

static void client_error_delete (void *client_error_ptr) {

	if (client_error_ptr) {
		ClientError *client_error = (ClientError *) client_error_ptr;

		if (client_error->work_args) {
			if (client_error->delete_action_args)
				client_error->delete_action_args (client_error->work_args);
		}

		free (client_error_ptr);
	}

}

// registers an action to be triggered when the specified error occurs
// if there is an existing action registered to an error, it will be overrided
// a newly allocated ClientErrorData structure will be passed to your method
// that should be free using the client_error_data_delete () method
// returns 0 on success, 1 on error
u8 client_error_register (
	Client *client,
	const ClientErrorType error_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (client) {
		ClientError *error = client_error_new ();
		if (error) {
			error->type = error_type;

			error->create_thread = create_thread;
			error->drop_after_trigger = drop_after_trigger;

			error->work = work;
			error->work_args = work_args;
			error->delete_action_args = delete_action_args;

			// search if there is an action already registred for that error and remove it
			(void) client_error_unregister (client, error_type);

			client->errors[error_type] = error;

			retval = 0;
		}
	}

	return retval;

}

// unregisters the action associated with the error types
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if error is NOT registered
u8 client_error_unregister (Client *client, const ClientErrorType error_type) {

	u8 retval = 1;

	if (client) {
		if (client->errors[error_type]) {
			client_error_delete (client->errors[error_type]);
			client->errors[error_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

// triggers all the actions that are registred to an error
// returns 0 on success, 1 on error
u8 client_error_trigger (
	const ClientErrorType error_type,
	const Client *client, const Connection *connection,
	const char *error_message
) {

	u8 retval = 1;

	if (client) {
		ClientError *error = client->errors[error_type];
		if (error) {
			// trigger the action
			if (error->work) {
				if (error->create_thread) {
					pthread_t thread_id = 0;
					retval = thread_create_detachable (
						&thread_id,
						error->work,
						client_error_data_create (
							client, connection,
							error,
							error_message
						)
					);
				}

				else {
					(void) error->work (client_error_data_create (
						client, connection,
						error,
						error_message
					));

					retval = 0;
				}

				if (error->drop_after_trigger) {
					(void) client_error_unregister ((Client *) client, error_type);
				}
			}
		}
	}

	return retval;

}

// handles error packets
static void client_error_packet_handler (Packet *packet) {

	if (packet->data_size >= sizeof (SError)) {
		char *end = (char *) packet->data;
		SError *s_error = (SError *) end;

		switch (s_error->error_type) {
			case CLIENT_ERROR_NONE: break;

			case CLIENT_ERROR_CERVER_ERROR:
				client_error_trigger (
					CLIENT_ERROR_CERVER_ERROR,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_PACKET_ERROR:
				client_error_trigger (
					CLIENT_ERROR_PACKET_ERROR,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_FAILED_AUTH: {
				if (client_error_trigger (
					CLIENT_ERROR_FAILED_AUTH,
					packet->client, packet->connection,
					s_error->msg
				)) {
					// not error action is registered to handle the error
					cerver_log_error ("Failed to authenticate - %s", s_error->msg);
				}
			} break;

			case CLIENT_ERROR_GET_FILE:
				client_error_trigger (
					CLIENT_ERROR_GET_FILE,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_SEND_FILE:
				client_error_trigger (
					CLIENT_ERROR_SEND_FILE,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_FILE_NOT_FOUND:
				client_error_trigger (
					CLIENT_ERROR_FILE_NOT_FOUND,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_CREATE_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_CREATE_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_JOIN_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_JOIN_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_LEAVE_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_LEAVE_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_FIND_LOBBY:
				client_error_trigger (
					CLIENT_ERROR_FIND_LOBBY,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			case CLIENT_ERROR_GAME_INIT:
				client_error_trigger (
					CLIENT_ERROR_GAME_INIT,
					packet->client, packet->connection,
					s_error->msg
				);
				break;
			case CLIENT_ERROR_GAME_START:
				client_error_trigger (
					CLIENT_ERROR_GAME_START,
					packet->client, packet->connection,
					s_error->msg
				);
				break;

			default: {
				client_error_trigger (
					CLIENT_ERROR_UNKNOWN,
					packet->client, packet->connection,
					s_error->msg
				);
			} break;
		}
	}

}

// creates an error packet ready to be sent
Packet *client_error_packet_generate (
	const ClientErrorType type, const char *msg
) {

	Packet *packet = packet_new ();
	if (packet) {
		size_t packet_len = sizeof (PacketHeader) + sizeof (SError);

		// zeroed, the whole buffer is sent
		packet->packet = calloc (1, packet_len);
		packet->packet_size = packet_len;

		char *end = (char *) packet->packet;
		PacketHeader *header = (PacketHeader *) end;
		header->packet_type = PACKET_TYPE_ERROR;
		header->packet_size = packet_len;

		header->request_type = REQUEST_PACKET_TYPE_NONE;
		header->version = packet_version_local ();

		end += sizeof (PacketHeader);

		SError *s_error = (SError *) end;
		s_error->error_type = type;
		s_error->timestamp = time (NULL);
		if (msg) (void) strncpy (s_error->msg, msg, ERROR_MESSAGE_LENGTH - 1);
	}

	return packet;

}

// creates and send a new error packet
// returns 0 on success, 1 on error
u8 client_error_packet_generate_and_send (
	const ClientErrorType type, const char *msg,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	Packet *error_packet = client_error_packet_generate (type, msg);
	if (error_packet) {
		packet_set_network_values (error_packet, NULL, client, connection, NULL);
		retval = packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

	return retval;

}

#pragma endregion

#pragma region client

static u8 client_app_handler_start (Client *client) {

	u8 retval = 0;

	if (client) {
		if (client->app_packet_handler) {
			if (!client->app_packet_handler->direct_handle) {
				if (!handler_start (client->app_packet_handler)) {
					#ifdef CLIENT_DEBUG
					cerver_log_success (
						"Client %s app_packet_handler has started!",
						client->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start client %s app_packet_handler!",
						client->name
					);

					retval = 1;
				}
			}
		}

		else {
			cerver_log_warning (
				"Client %s does not have an app_packet_handler",
				client->name
			);
		}
	}

	return retval;

}

static u8 client_app_error_handler_start (Client *client) {

	u8 retval = 0;

	if (client) {
		if (client->app_error_packet_handler) {
			if (!client->app_error_packet_handler->direct_handle) {
				if (!handler_start (client->app_error_packet_handler)) {
					#ifdef CLIENT_DEBUG
					cerver_log_success (
						"Client %s app_error_packet_handler has started!",
						client->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start client %s app_error_packet_handler!",
						client->name
					);

					retval = 1;
				}
			}
		}

		else {
			cerver_log_warning (
				"Client %s does not have an app_error_packet_handler",
				client->name
			);
		}
	}

	return retval;

}

static u8 client_custom_handler_start (Client *client) {

	u8 retval = 0;

	if (client) {
		if (client->custom_packet_handler) {
			if (!client->custom_packet_handler->direct_handle) {
				if (!handler_start (client->custom_packet_handler)) {
					#ifdef CLIENT_DEBUG
					cerver_log_success (
						"Client %s custom_packet_handler has started!",
						client->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start client %s custom_packet_handler!",
						client->name
					);

					retval = 1;
				}
			}
		}

		else {
			cerver_log_warning (
				"Client %s does not have a custom_packet_handler",
				client->name
			);
		}
	}

	return retval;

}

// starts all client's handlers
static u8 client_handlers_start (Client *client) {

	u8 errors = 0;

	if (client) {
		#ifdef CLIENT_DEBUG
		cerver_log_debug (
			"Initializing %s handlers...", client->name
		);
		#endif

		// a client that starts again (after its connections ended) keeps its lock
		if (!client->handlers_lock) {
			client->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			pthread_mutex_init (client->handlers_lock, NULL);
		}

		errors |= client_app_handler_start (client);

		errors |= client_app_error_handler_start (client);

		errors |= client_custom_handler_start (client);

		if (!errors) {
			#ifdef CLIENT_DEBUG
			cerver_log_success (
				"Done initializing client %s handlers!", client->name
			);
			#endif
		}
	}

	return errors;

}

static u8 client_start (Client *client) {

	u8 retval = 1;

	if (client) {
		// check if we walready have the client poll running
		// the update threads stop the client when the cerver closes its last connection
		if (!client_is_running (client)) {
			time (&client->time_started);
			__atomic_store_n (&client->running, true, __ATOMIC_SEQ_CST);

			if (!client_handlers_start (client)) {
				retval = 0;
			}

			else {
				__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);
			}
		}

		else {
			// client is already running because of an active connection
			retval = 0;
		}
	}

	return retval;

}

// creates a new connection that is ready to connect and registers it to the client
Connection *client_connection_create (
	Client *client,
	const char *ip_address, u16 port,
	Protocol protocol, bool use_ipv6
) {

	Connection *connection = NULL;

	if (client) {
		connection = connection_create_empty ();
		if (connection) {
			connection_set_values (connection, ip_address, port, protocol, use_ipv6);
			connection_init (connection);
			connection_register_to_client (client, connection);

			connection_set_synchronization (connection);
		}
	}

	return connection;

}

// registers an existing connection to a client
// retuns 0 on success, 1 on error
int client_connection_register (
	Client *client, Connection *connection
) {

	int retval = 1;

	if (client && connection) {
		retval = dlist_insert_at_end (client->connections, connection);
	}

	return retval;

}

// unregister an exitsing connection from the client
// returns 0 on success, 1 on error or if the connection does not belong to the client
int client_connection_unregister (
	Client *client, Connection *connection
) {

	int retval = 1;

	if (client && connection) {
		if (dlist_remove (client->connections, connection, NULL)) {
			retval = 0;
		}
	}

	return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static inline void client_connection_get_next_packet_handler (
	const size_t received,
	Client *client, Connection *connection,
	Packet *packet
) {

	// update stats
	CERVER_STATS_ADD (client->stats->n_receives_done, 1);
	CERVER_STATS_ADD (client->stats->total_bytes_received, received);

	#ifdef CONNECTION_STATS
	connection->stats->n_receives_done += 1;
	connection->stats->total_bytes_received += received;
	#endif

	// handle the actual packet
	(void) client_packet_handler (packet);

}

#pragma GCC diagnostic pop

static unsigned int client_connection_get_next_packet_actual (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	// TODO: use a static packet
	Packet *packet = packet_new ();
	packet->cerver = NULL;
	packet->client = client;
	packet->connection = connection;
	packet->lobby = NULL;

	// first receive the packet header
	size_t data_size = sizeof (PacketHeader);

	if (
		client_receive_data (
			client, connection,
			(char *) &packet->header, sizeof (PacketHeader),
			data_size
		) == RECEIVE_ERROR_NONE
	) {
		#ifdef CLIENT_RECEIVE_DEBUG
		packet_header_log (&packet->header);
		#endif

		// check if need more data to complete the packet
		if (packet->header.packet_size > sizeof (PacketHeader)) {
			// TODO: add ability to configure this value
			// check that the packet is not to big
			if (packet->header.packet_size <= MAX_UDP_PACKET_SIZE) {
				(void) packet_create_data (
					packet, packet->header.packet_size - sizeof (PacketHeader)
				);

				data_size = packet->data_size;

				if (
					client_receive_data (
						client, connection,
						packet->data, packet->data_size,
						data_size
					) == RECEIVE_ERROR_NONE
				) {
					// we can safely handle the packet
					client_connection_get_next_packet_handler (
						packet->packet_size,
						client, connection,
						packet
					);

					retval = 0;
				}
			}

			else {
				// we received a bad packet
				packet_delete (packet);
			}
		}

		else {
			// we can safely handle the packet
			client_connection_get_next_packet_handler (
				packet->packet_size,
				client, connection,
				packet
			);

			retval = 0;
		}
	}

	return retval;

}

// performs a receive in the connection's socket
// to get a complete packet & handle it
// returns 0 on success, 1 on error
unsigned int client_connection_get_next_packet (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		retval = client_connection_get_next_packet_actual (
			client, connection
		);
	}

	return retval;

}

#pragma endregion

#pragma region connect

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		if (!connection_connect (connection)) {
			connection->active = true;
			(void) time (&connection->connected_timestamp);

			// the event sees the connection already active
			client_event_trigger (CLIENT_EVENT_CONNECTED, client, connection);

			retval = 0;     // success - connected to cerver
		}

		else {
			client_event_trigger (
				CLIENT_EVENT_CONNECTION_FAILED,
				client, connection
			);
		}
	}

	return retval;

}

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect_to_cerver (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (!client_connect (client, connection)) {
		// we expect to handle a packet with the cerver's information
		client_connection_get_next_packet (
			client, connection
		);

		retval = 0;
	}

	return retval;

}

// counts an async thread that is about to use the client (1),
// or one that does not use it anymore (-1)
static void client_async_thread_count (Client *client, const int delta) {

	if (!client->lock) return;

	(void) pthread_mutex_lock (client->lock);

	if (delta > 0) client->n_async_threads += 1;
	else client->n_async_threads -= 1;

	(void) pthread_cond_broadcast (&client->updates_cond);
	(void) pthread_mutex_unlock (client->lock);

}

// creates a detached thread that uses the client,
// and releases its data if the thread could not be created
// returns 0 on success, 1 on error
static unsigned int client_async_thread_create (
	ClientConnection *cc, void *(*work) (void *)
) {

	Client *client = cc->client;

	client_async_thread_count (client, 1);

	pthread_t thread_id = 0;
	const unsigned int retval = thread_create_detachable (&thread_id, work, cc);
	if (retval) {
		client_connection_aux_delete (cc);
		client_async_thread_count (client, -1);
	}

	return retval;

}

static void *client_connect_thread (void *client_connection_ptr) {

	if (client_connection_ptr) {
		ClientConnection *cc = (ClientConnection *) client_connection_ptr;
		Client *client = cc->client;

		// triggers CLIENT_EVENT_CONNECTED or CLIENT_EVENT_CONNECTION_FAILED
		if (!client_connect (cc->client, cc->connection)) {
			client_start (cc->client);
		}

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// open a success connection, EVENT_CONNECTED will be triggered, otherwise, EVENT_CONNECTION_FAILED will be triggered
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
unsigned int client_connect_async (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		ClientConnection *cc = client_connection_aux_new (client, connection);
		if (cc) {
			retval = client_async_thread_create (cc, client_connect_thread);

			#ifdef CLIENT_DEBUG
			if (retval) cerver_log_error ("Failed to create client_connect_thread () detachable thread!");
			#endif
		}
	}

	return retval;

}

static void *client_connect_to_cerver_thread (void *client_connection_ptr) {

	if (client_connection_ptr) {
		ClientConnection *cc = (ClientConnection *) client_connection_ptr;
		Client *client = cc->client;

		// handles the packet with the cerver's information
		(void) client_connect_to_cerver (cc->client, cc->connection);

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// works like client_connect_to_cerver ()
// this is NOT a blocking method, a new thread will be created,
// client_requests_wait () and client_teardown () wait for it
// returns 0 on success creating connection thread, 1 on error
unsigned int client_connect_to_cerver_async (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		ClientConnection *cc = client_connection_aux_new (client, connection);
		if (cc) {
			retval = client_async_thread_create (cc, client_connect_to_cerver_thread);

			#ifdef CLIENT_DEBUG
			if (retval) cerver_log_error ("Failed to create client_connect_to_cerver_thread () detachable thread!");
			#endif
		}
	}

	return retval;

}

#pragma endregion

#pragma region start

// after a client connection successfully connects to a server,
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
int client_connection_start (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		if (connection->active) {
			if (!client_start (client) && !connection_start (connection)) {
				// set before the thread exists, so an end that arrives first waits for it
				client_update_thread_started (client, connection, true);

				if (!thread_create_detachable (
					&connection->update_thread_id,
					connection_update,
					client_connection_aux_new (client, connection)
				)) {
					retval = 0;         // success
				}

				else {
					client_update_thread_started (client, connection, false);

					cerver_log_error (
						"client_connection_start () - Failed to create update thread for client %s",
						client->name
					);
				}
			}

			else {
				cerver_log_error (
					"client_connection_start () - Failed to start client %s",
					client->name
				);
			}
		}
	}

	return retval;

}

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
int client_connect_and_start (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		if (!client_connect (client, connection)) {
			if (!client_connection_start (client, connection)) {
				retval = 0;
			}
		}

		else {
			cerver_log_error (
				"client_connect_and_start () - Client %s failed to connect",
				client->name
			);
		}
	}

	return retval;

}

static void *client_connection_start_wrapper (void *data_ptr) {

	if (data_ptr) {
		ClientConnection *cc = (ClientConnection *) data_ptr;
		Client *client = cc->client;

		(void) client_connect_and_start (cc->client, cc->connection);
		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
u8 client_connect_and_start_async (Client *client, Connection *connection) {

	u8 retval = 1;

	ClientConnection *cc = (client && connection) ?
		client_connection_aux_new (client, connection) : NULL;

	if (cc) {
		retval = (u8) client_async_thread_create (cc, client_connection_start_wrapper);
	}

	return retval;

}

#pragma endregion

#pragma region requests

// when a client is already connected to the cerver, a request can be made to the cerver
// and the result will be returned
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
unsigned int client_request_to_cerver (
	Client *client, Connection *connection, Packet *request
) {

	unsigned int retval = 1;

	if (client && connection && request) {
		// send the request to the cerver
		packet_set_network_values (request, NULL, client, connection, NULL);

		size_t sent = 0;
		if (!packet_send (request, 0, &sent, false)) {
			// printf ("Request to cerver: %ld\n", sent);

			// receive the data directly
			client_connection_get_next_packet (client, connection);

			retval = 0;
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_error (
				"client_request_to_cerver () - "
				"failed to send request packet!"
			);
			#endif
		}
	}

	return retval;

}

static void *client_request_to_cerver_thread (void *cc_ptr) {

	if (cc_ptr) {
		ClientConnection *cc = (ClientConnection *) cc_ptr;
		Client *client = cc->client;

		(void) client_connection_get_next_packet (
			cc->client, cc->connection
		);

		// the response was handled, the app can end the connection
		client_event_trigger (CLIENT_EVENT_CONNECTION_DATA, cc->client, cc->connection);

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// when a client is already connected to the cerver, a request can be made to the cerver
// the result will be placed inside the connection
// this method will NOT block and the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
unsigned int client_request_to_cerver_async (
	Client *client, Connection *connection, Packet *request
) {

	unsigned int retval = 1;

	if (client && connection && request) {
		// send the request to the cerver
		packet_set_network_values (request, NULL, client, connection, NULL);
		if (!packet_send (request, 0, NULL, false)) {
			ClientConnection *cc = client_connection_aux_new (client, connection);
			if (cc) {
				// create a new thread to receive & handle the response
				retval = client_async_thread_create (cc, client_request_to_cerver_thread);

				#ifdef CLIENT_DEBUG
				if (retval) cerver_log_error ("Failed to create client_request_to_cerver_thread () detachable thread!");
				#endif
			}
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_error (
				"client_request_to_cerver_async () - "
				"failed to send request packet!"
			);
			#endif
		}
	}

	return retval;

}

#pragma endregion

#pragma region files

static u8 client_file_receive (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	// generate a custom filename taking into account the uploads path
	*saved_filename = c_string_create (
		"%s/%ld-%s",
		client->uploads_path->str,
		time (NULL), file_header->filename
	);

	if (*saved_filename) {
		retval = file_receive_actual (
			client, connection,
			file_header,
			file_data, file_data_len,
			saved_filename
		);
	}

	return retval;

}

// adds a new file path to take into account when getting a request for a file
// returns 0 on success, 1 on error
u8 client_files_add_path (Client *client, const char *path) {

	u8 retval = 1;

	if (client && path) {
		if (client->n_paths < CLIENT_FILES_MAX_PATHS) {
			client->paths[client->n_paths] = str_new (path);
			client->n_paths += 1;

			// the path was added, it always returned an error before
			retval = 0;
		}
	}

	return retval;

}

// sets the default uploads path to be used when receiving a file
void client_files_set_uploads_path (
	Client *client, const char *uploads_path
) {

	if (client && uploads_path) {
		str_delete (client->uploads_path);
		client->uploads_path = str_new (uploads_path);
	}

}

// sets a custom method to be used to handle a file upload (receive)
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
void client_files_set_file_upload_handler (
	Client *client,
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct _FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
) {

	if (client) {
		client->file_upload_handler = file_upload_handler;
	}

}

// sets a callback to be executed after a file has been successfully received
void client_files_set_file_upload_cb (
	Client *client,
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	)
) {

	if (client) {
		client->file_upload_cb = file_upload_cb;
	}

}

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
String *client_files_search_file (
	Client *client, const char *filename
) {

	String *retval = NULL;

	if (client && filename) {
		char filename_query[FILENAME_DEFAULT_SIZE * 2] = { 0 };
		for (unsigned int i = 0; i < client->n_paths; i++) {
			(void) snprintf (
				filename_query, FILENAME_DEFAULT_SIZE * 2,
				"%s/%s",
				client->paths[i]->str, filename
			);

			if (file_exists (filename_query)) {
				retval = str_new (filename_query);
				break;
			}
		}
	}

	return retval;

}

// requests a file from the cerver
// the client's uploads_path should have been configured before calling this method
// returns 0 on success sending request, 1 on failed to send request
u8 client_file_get (
	Client *client, Connection *connection,
	const char *filename
) {

	u8 retval = 1;

	if (client && connection && filename) {
		if (client->uploads_path) {
			Packet *packet = packet_new ();
			if (packet) {
				size_t packet_len = sizeof (PacketHeader) + sizeof (FileHeader);

				// zeroed, the whole buffer is sent
				packet->packet = calloc (1, packet_len);
				packet->packet_size = packet_len;

				char *end = (char *) packet->packet;
				PacketHeader *header = (PacketHeader *) end;
				header->packet_type = PACKET_TYPE_REQUEST;
				header->packet_size = packet_len;

				header->request_type = REQUEST_PACKET_TYPE_GET_FILE;
				header->version = packet_version_local ();

				end += sizeof (PacketHeader);

				FileHeader *file_header = (FileHeader *) end;
				(void) strncpy (file_header->filename, filename, FILENAME_DEFAULT_SIZE - 1);
				file_header->len = 0;

				packet_set_network_values (packet, NULL, client, connection, NULL);

				retval = packet_send (packet, 0, NULL, false);

				packet_delete (packet);
			}
		}
	}

	return retval;

}

// sends a file to the cerver
// returns 0 on success sending request, 1 on failed to send request
u8 client_file_send (
	Client *client, Connection *connection,
	const char *filename
) {

	u8 retval = 1;

	if (client && connection && filename) {
		char *last = strrchr ((char *) filename, '/');
		const char *actual_filename = last ? last + 1 : NULL;
		if (actual_filename) {
			// try to open the file
			struct stat filestatus = { 0 };
			int file_fd = file_open_as_fd (filename, &filestatus, O_RDONLY);
			if (file_fd >= 0) {
				size_t sent = file_send_by_fd (
					NULL, client, connection,
					file_fd, actual_filename, filestatus.st_size
				);

				client->file_stats->n_files_sent += 1;
				client->file_stats->n_bytes_sent += sent;

				if (sent == (size_t) filestatus.st_size) retval = 0;

				close (file_fd);
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_FILE,
					"client_file_send () - Failed to open file %s", filename
				);
			}
		}

		else {
			cerver_log_error ("client_file_send () - failed to get actual filename");
		}
	}

	return retval;

}

#pragma endregion

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

static void client_cerver_packet_handle_info (Packet *packet) {

	if (packet->data && (packet->data_size > 0)) {
		char *end = (char *) packet->data;

		#ifdef CLIENT_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Received a cerver info packet.");
		#endif

		CerverReport *cerver_report = cerver_deserialize ((SCerver *) end);
		if (cerver_report_check_info (
			cerver_report, packet->client, packet->connection
		)) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_NONE,
				"Failed to correctly check cerver info!"
			);
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

		// the cerves is going to be teardown, we have to disconnect
		case CERVER_PACKET_TYPE_TEARDOWN:
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"---> Cerver teardown <---"
			);
			#endif

			client_got_disconnected (packet->client);
			client_event_trigger (CLIENT_EVENT_DISCONNECTED, packet->client, NULL);

			error = CLIENT_HANDLER_ERROR_CLOSED;
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
	if (packet->data_size >= sizeof (FileHeader)) {
		char *end = (char *) packet->data;
		FileHeader *file_header = (FileHeader *) end;

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
			CLIENT_ERROR_GET_FILE, "Missing file header",
			packet->client, packet->connection
		);

		client->file_stats->n_bad_files_requests += 1;
	}

}

static void client_request_send_file_actual (Packet *packet) {

	Client *client = packet->client;

	client->file_stats->n_files_upload_requests += 1;

	// get the necessary information to fulfil the request
	if (packet->data_size >= sizeof (FileHeader)) {
		char *end = (char *) packet->data;
		FileHeader *file_header = (FileHeader *) end;

		const char *file_data = NULL;
		size_t file_data_len = 0;
		// printf (
		// 	"\n\npacket->data_size %ld > sizeof (FileHeader) %ld\n\n",
		// 	packet->data_size, sizeof (FileHeader)
		// );
		if (packet->data_size > sizeof (FileHeader)) {
			file_data = end += sizeof (FileHeader);
			file_data_len = packet->data_size - sizeof (FileHeader);
		}

		// the file is sent after its packet, so its first bytes can be in the same buffer
		else if (packet->connection) {
			file_data_len = receive_handle_take_trailing (
				&packet->connection->receive_handle, &file_data, file_header->len
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
			CLIENT_ERROR_SEND_FILE, "Missing file header",
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

	// check we have a big enough packet
	if (packet->data_size > 0) {
		char *end = (char *) packet->data;

		// check if we have a token
		if (packet->data_size == (sizeof (SToken))) {
			SToken *s_token = (SToken *) (end);
			retval = client_set_session_id (client, s_token->token);
		}
	}

	return retval;

}

static void client_auth_success_handler (Packet *packet) {

	packet->connection->authenticated = true;

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

	if (packet->client->app_packet_handler) {
		if (packet->client->app_packet_handler->direct_handle) {
			// printf ("app_packet_handler - direct handle!\n");
			packet->client->app_packet_handler->handler (packet);
			packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->client->app_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to client's %s app_packet_handler!",
					packet->client->name
				);
			}
		}
	}

	else {
		cerver_log_warning (
			"Client %s does not have a app_packet_handler!",
			packet->client->name
		);
	}

}

// handles a PACKET_TYPE_APP_ERROR packet type
static void client_app_error_packet_handler (Packet *packet) {

	if (packet->client->app_error_packet_handler) {
		if (packet->client->app_error_packet_handler->direct_handle) {
			// printf ("app_error_packet_handler - direct handle!\n");
			packet->client->app_error_packet_handler->handler (packet);
			packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->client->app_error_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to client's %s app_error_packet_handler!",
					packet->client->name
				);
			}
		}
	}

	else {
		cerver_log_warning (
			"Client %s does not have a app_error_packet_handler!",
			packet->client->name
		);
	}

}

// handles a PACKET_TYPE_CUSTOM packet type
static void client_custom_packet_handler (Packet *packet) {

	if (packet->client->custom_packet_handler) {
		if (packet->client->custom_packet_handler->direct_handle) {
			// printf ("custom_packet_handler - direct handle!\n");
			packet->client->custom_packet_handler->handler (packet);
			packet_delete (packet);
		}

		else {
			// add the packet to the handler's job queueu to be handled
			// as soon as the handler is available
			if (job_queue_push (
				packet->client->custom_packet_handler->job_queue,
				job_create (NULL, packet)
			)) {
				cerver_log_error (
					"Failed to push a new job to client's %s custom_packet_handler!",
					packet->client->name
				);
			}
		}
	}

	else {
		cerver_log_warning (
			"Client %s does not have a custom_packet_handler!",
			packet->client->name
		);
	}

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
		case PACKET_TYPE_NONE: break;

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

static u8 client_packet_handler (Packet *packet) {

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
static void client_receive_handle_buffer (
	ReceiveHandle *receive_handle
) {

	receive_handle_buffer (
		receive_handle,
		receive_handle->client->max_received_packet_size,
		client_receive_dispatch
	);

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

// performs the actual recv () method on the connection's sock fd
// handles if the receive method failed
// the amount of bytes read from the socket is placed in rc
static ReceiveError client_receive_actual (
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
static ReceiveError client_receive_data (
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
		case RECEIVE_ERROR_NONE: {
			connection->receive_handle.buffer = buffer;
			connection->receive_handle.buffer_size = buffer_size;
			connection->receive_handle.received_size = received;

			client_receive_handle_buffer (
				&connection->receive_handle
			);

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

#pragma region end

// ends a connection with a cerver
// by sending a disconnect packet and the closing the connection
static void client_connection_terminate (
	Client *client, Connection *connection
) {

	if (connection) {
		if (connection->active) {
			if (__atomic_load_n (&connection->cerver_report, __ATOMIC_ACQUIRE)) {
				// send a close connection packet
				Packet *packet = packet_generate_request (
					PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_CLOSE_CONNECTION, NULL, 0
				);

				if (packet) {
					packet_set_network_values (packet, NULL, client, connection, NULL);
					if (packet_send (packet, 0, NULL, false)) {
						cerver_log_error ("Failed to send CLIENT_CLOSE_CONNECTION!");
					}
					packet_delete (packet);
				}
			}
		}
	}

}

// closes the connection's socket & set it to be inactive
// does not send a close connection packet to the cerver
// returns 0 on success, 1 on error
int client_connection_stop (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		client_event_trigger (CLIENT_EVENT_CONNECTION_CLOSE, client, connection);
		connection_end (connection);

		retval = 0;
	}

	return retval;

}

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
int client_connection_close (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		client_connection_terminate (client, connection);
		retval = client_connection_stop (client, connection);
	}

	return retval;

}

// returns true while the client is running, it is read by its update threads
bool client_is_running (const Client *client) {

	return client ? __atomic_load_n (&client->running, __ATOMIC_SEQ_CST) : false;

}

// the connections are removed by their address, the comparator of the list uses their fd
static int client_connection_same (const void *a, const void *b) {

	return (a == b) ? 0 : 1;

}

// returns true if the connection is in the client's list, the client's lock must be held
static bool client_connection_is_registered (
	const Client *client, const Connection *connection
) {

	bool registered = false;

	for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
		if (le->data == connection) {
			registered = true;
			break;
		}
	}

	return registered;

}

// an update thread is about to be created (started is true) or it failed to be created
void client_update_thread_started (
	Client *client, Connection *connection, const bool started
) {

	(void) pthread_mutex_lock (connection->mutex);
	connection->updating = started;
	(void) pthread_mutex_unlock (connection->mutex);

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);
		if (started) client->n_update_threads += 1;
		else client->n_update_threads -= 1;
		(void) pthread_mutex_unlock (client->lock);
	}

}

// an update thread of the client has ended and it does not use the client anymore
void client_update_thread_ended (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);
		client->n_update_threads -= 1;
		(void) pthread_cond_broadcast (&client->updates_cond);
		(void) pthread_mutex_unlock (client->lock);
	}

}

// closes the connection, removes it from the client and deletes it
// it runs only in the thread that owns the end of the connection,
// when its update thread (if any) does not use it anymore
// a client that the cerver left without connections (remote) stops running, like when it was disconnected
static void client_connection_destroy (
	Client *client, Connection *connection, const bool remote
) {

	(void) client_connection_stop (client, connection);

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	(void) dlist_remove (client->connections, connection, client_connection_same);

	if (remote && !dlist_size (client->connections)) {
		__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);
	}

	(void) pthread_cond_broadcast (&client->updates_cond);

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	connection_delete (connection);

}

// the ownership of the end of a connection
typedef enum ClientConnectionEnd {

	CLIENT_CONNECTION_END_NONE			= 0,
	CLIENT_CONNECTION_END_OWNER			= 1,	// this thread ends the connection
	CLIENT_CONNECTION_END_TAKEN			= 2,	// the end deferred to the update thread is taken by this thread
	CLIENT_CONNECTION_END_OTHER			= 3,	// another thread ends the connection

	CLIENT_CONNECTION_END_MAX			= 4

} ClientConnectionEnd;

// claims the end of the connection, the connection's mutex must be held
static ClientConnectionEnd client_connection_claim_end (
	Connection *connection, const bool deferred
) {

	ClientConnectionEnd end = CLIENT_CONNECTION_END_OTHER;

	if (!connection->ending) {
		connection->ending = true;
		connection->end_deferred = deferred;
		end = CLIENT_CONNECTION_END_OWNER;

		connection_set_state (connection, CONNECTION_STATE_DISCONNECTING);
	}

	// the update thread has not finished its loop yet, so its deferred end can be taken
	else if (!deferred && connection->end_deferred && connection->updating) {
		connection->end_deferred = false;
		end = CLIENT_CONNECTION_END_TAKEN;
	}

	return end;

}

// claims the end of a connection of the client, the client's lock is held
// so the connection can not be destroyed by its update thread meanwhile
static ClientConnectionEnd client_connection_claim_registered (
	Client *client, Connection *connection
) {

	ClientConnectionEnd end = CLIENT_CONNECTION_END_NONE;

	if (client_connection_is_registered (client, connection)) {
		(void) pthread_mutex_lock (connection->mutex);
		end = client_connection_claim_end (connection, false);
		(void) pthread_mutex_unlock (connection->mutex);
	}

	return end;

}

// waits until the update thread of the connection that ends in another thread removes it,
// the client's lock is held
static void client_connection_wait_removed (Client *client, Connection *connection) {

	while (client_connection_is_registered (client, connection)) {
		(void) pthread_cond_wait (&client->updates_cond, client->lock);
	}

}

// wakes up the update thread of the connection and waits until its receive loop ends
static void client_connection_wait_update (Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);

	if (connection->updating) {
		connection_wake_update (connection);

		while (connection->updating) {
			(void) pthread_cond_wait (connection->cond, connection->mutex);
		}
	}

	(void) pthread_mutex_unlock (connection->mutex);

}

// a connection that ends from its own update thread (a packet handler with direct handle):
// its loop is woken up and it completes the end when it finishes
static int client_connection_end_from_update (Client *client, Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);
	const ClientConnectionEnd end = client_connection_claim_end (connection, true);
	(void) pthread_mutex_unlock (connection->mutex);

	if (end == CLIENT_CONNECTION_END_OWNER) {
		client_connection_terminate (client, connection);

		(void) pthread_mutex_lock (connection->mutex);
		connection_wake_update (connection);
		(void) pthread_mutex_unlock (connection->mutex);
	}

	return 0;

}

// a connection that ends from any other thread: its update thread is woken up and joined
static int client_connection_end_actual (Client *client, Connection *connection) {

	int retval = 0;

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	const ClientConnectionEnd end = client->lock ?
		client_connection_claim_registered (client, connection) : CLIENT_CONNECTION_END_OWNER;

	// its update thread is ending it, it returns once the connection was removed
	if (end == CLIENT_CONNECTION_END_OTHER) client_connection_wait_removed (client, connection);

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	switch (end) {
		case CLIENT_CONNECTION_END_OWNER:
			client_connection_terminate (client, connection);
			client_connection_wait_update (connection);
			client_connection_destroy (client, connection, false);
			break;

		// the cerver already closed it, so the close packet is not sent
		case CLIENT_CONNECTION_END_TAKEN:
			client_connection_wait_update (connection);
			client_connection_destroy (client, connection, false);
			break;

		case CLIENT_CONNECTION_END_OTHER: break;

		default: retval = 1; break;
	}

	return retval;

}

// terminates and destroy a connection registered to a client
// that is connected to a cerver
// the first thread that ends a connection owns its end, any other one waits for it
// a packet handler that runs in the connection's update thread only requests the end,
// the update thread completes it when its receive loop ends
// returns 0 on success, 1 on error
int client_connection_end (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		retval = connection_is_update_thread (connection) ?
			client_connection_end_from_update (client, connection) :
			client_connection_end_actual (client, connection);
	}

	return retval;

}

// completes the end of a connection that its update thread owns,
// called by the update thread when its receive loop has ended
void client_connection_end_deferred (Client *client, Connection *connection) {

	if (client && connection) {
		client_connection_destroy (client, connection, true);
	}

}

// requests the end of a connection without waiting for it:
// a connection with an update thread is ended by it when its receive loop ends
// returns true if its update thread ends it, false if it has no update thread
static bool client_connection_end_request (Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);

	const bool updating = connection->updating;
	if (updating) {
		(void) client_connection_claim_end (connection, true);
		connection_wake_update (connection);
	}

	(void) pthread_mutex_unlock (connection->mutex);

	return updating;

}

static void client_app_handler_destroy (Client *client) {

	if (client) {
		if (client->app_packet_handler) {
			if (!client->app_packet_handler->direct_handle) {
				// stop app handler
				bsem_post_all (client->app_packet_handler->job_queue->has_jobs);
			}
		}
	}

}

static void client_app_error_handler_destroy (Client *client) {

	if (client) {
		if (client->app_error_packet_handler) {
			if (!client->app_error_packet_handler->direct_handle) {
				// stop app error handler
				bsem_post_all (client->app_error_packet_handler->job_queue->has_jobs);
			}
		}
	}

}

static void client_custom_handler_destroy (Client *client) {

	if (client) {
		if (client->custom_packet_handler) {
			if (!client->custom_packet_handler->direct_handle) {
				// stop custom handler
				bsem_post_all (client->custom_packet_handler->job_queue->has_jobs);
			}
		}
	}

}

static void client_handlers_destroy (Client *client) {

	if (client) {
		cerver_log_debug (
			"Client %s num_handlers_alive: %d",
			client->name, client->num_handlers_alive
		);

		client_app_handler_destroy (client);

		client_app_error_handler_destroy (client);

		client_custom_handler_destroy (client);

		// poll remaining handlers
		while (client->num_handlers_alive) {
			if (client->app_packet_handler)
				bsem_post_all (client->app_packet_handler->job_queue->has_jobs);

			if (client->app_error_packet_handler)
				bsem_post_all (client->app_error_packet_handler->job_queue->has_jobs);

			if (client->custom_packet_handler)
				bsem_post_all (client->custom_packet_handler->job_queue->has_jobs);

			sleep (1);
		}
	}

}

// returns the first connection of the client, NULL if it has none
static Connection *client_connections_first (Client *client) {

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	ListElement *le = dlist_start (client->connections);
	Connection *connection = le ? (Connection *) le->data : NULL;

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	return connection;

}

// ends every connection, each end returns once its connection was removed
// returns 0 on success, 1 if it was called from an update thread of the client
static u8 client_teardown_connections (Client *client) {

	u8 retval = 0;

	Connection *connection = NULL;
	while ((connection = client_connections_first (client))) {
		// an update thread can not wait for itself
		if (connection_is_update_thread (connection)) {
			cerver_log_error (
				"client_teardown () - client %s can not be destroyed from the update thread of one of its connections",
				client->name
			);

			retval = 1;
			break;
		}

		(void) client_connection_end (client, connection);
	}

	return retval;

}

// waits until every update thread has stopped using the client
static void client_teardown_wait_updates (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);

		while (client->n_update_threads) {
			(void) pthread_cond_wait (&client->updates_cond, client->lock);
		}

		(void) pthread_mutex_unlock (client->lock);
	}

}

// waits until the async connect and request threads do not use the client,
// an async connect can still add and start a connection
static void client_teardown_wait_async (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);

		while (client->n_async_threads) {
			(void) pthread_cond_wait (&client->updates_cond, client->lock);
		}

		(void) pthread_mutex_unlock (client->lock);
	}

}

void client_requests_wait (Client *client) {

	if (client) client_teardown_wait_async (client);

}

// returns 0 on success, 1 on error
static u8 client_teardown_actual (Client *client) {

	u8 retval = 1;

	client_teardown_wait_async (client);

	if (!client_teardown_connections (client)) {
		client_teardown_wait_updates (client);

		client_handlers_destroy (client);

		// the connections were already deleted
		dlist_delete (client->connections);
		client->connections = NULL;

		client_delete (client);

		retval = 0;
	}

	return retval;

}

static void *client_teardown_internal (void *client_ptr) {

	if (client_ptr) {
		(void) client_teardown_actual ((Client *) client_ptr);
	}

	return NULL;

}

// stop any on going connection and process and destroys the client
// every connection is ended (their update threads are woken up and joined)
// returns 0 on success, 1 on error
u8 client_teardown (Client *client) {

	u8 retval = 1;

	if (client) {
		__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);

		retval = client_teardown_actual (client);
	}

	return retval;

}

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
u8 client_teardown_async (Client *client) {

	pthread_t thread_id = 0;
	return client ? thread_create_detachable (
		&thread_id,
		client_teardown_internal,
		client
	) : 1;

}

#pragma endregion