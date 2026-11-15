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
#include "cerver/client/internal.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/connection/lifecycle.h"
#include "cerver/connection/registry.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
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

static u64 next_client_id = 0;

#pragma region aux

ClientConnection *client_connection_aux_new (
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

// the stats and their packets per type live inside the client
static void client_stats_init (ClientStats *client_stats) {

	(void) memset (client_stats, 0, sizeof (ClientStats));

	client_stats->received_packets = &client_stats->received_packets_storage;
	client_stats->sent_packets = &client_stats->sent_packets_storage;

}

void client_stats_print (Client *client) {

	if (client) {
		if (client->stats) {
			cerver_log_msg ("\nClient's stats:\n");
			cerver_log_msg ("Threshold time:            %ld", client->stats->threshold_time);

			cerver_log_msg ("N receives done:           %lu", client->stats->n_receives_done);

			cerver_log_msg ("Total bytes received:      %lu", client->stats->total_bytes_received);
			cerver_log_msg ("Total bytes sent:          %lu", client->stats->total_bytes_sent);

			cerver_log_msg ("N packets received:        %lu", client->stats->n_packets_received);
			cerver_log_msg ("N packets sent:            %lu", client->stats->n_packets_sent);

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

// the file stats live inside the client
static void client_file_stats_init (ClientFileStats *file_stats) {

	(void) memset (file_stats, 0, sizeof (ClientFileStats));

}

void client_file_stats_print (Client *client) {

	if (client) {
		if (client->file_stats) {
			cerver_log_msg ("Files requests:                %lu", client->file_stats->n_files_requests);
			cerver_log_msg ("Success requests:              %lu", client->file_stats->n_success_files_requests);
			cerver_log_msg ("Bad requests:                  %lu", client->file_stats->n_bad_files_requests);
			cerver_log_msg ("Files sent:                    %lu", client->file_stats->n_files_sent);
			cerver_log_msg ("Failed files sent:             %lu", client->file_stats->n_bad_files_sent);
			cerver_log_msg ("Files bytes sent:              %lu\n", client->file_stats->n_bytes_sent);

			cerver_log_msg ("Files upload requests:         %lu", client->file_stats->n_files_upload_requests);
			cerver_log_msg ("Success uploads:               %lu", client->file_stats->n_success_files_uploaded);
			cerver_log_msg ("Bad uploads:                   %lu", client->file_stats->n_bad_files_upload_requests);
			cerver_log_msg ("Bad files received:            %lu", client->file_stats->n_bad_files_received);
			cerver_log_msg ("Files bytes received:          %lu\n", client->file_stats->n_bytes_received);
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

#pragma region recycle

static FreeList client_recycled = FREELIST_INITIALIZER (
	Client, recycle_next, CLIENT_RECYCLE_LIMIT
);

// a new client initializes its synchronization values once,
// a recycled one keeps them, and also its connections list, that is empty
static Client *client_allocate (void) {

	Client *client = (Client *) freelist_pop (&client_recycled);
	if (!client) {
		client = (Client *) malloc (sizeof (Client));
		if (client) {
			client->connections = NULL;

			(void) pthread_mutex_init (&client->session_lock, NULL);
			(void) pthread_mutex_init (&client->lock_storage, NULL);
			(void) pthread_cond_init (&client->updates_cond, NULL);

			thread_group_init (&client->event_threads);
		}
	}

	return client;

}

static void client_destroy (void *client_ptr) {

	Client *client = (Client *) client_ptr;

	dlist_delete (client->connections);

	(void) pthread_mutex_destroy (&client->session_lock);
	(void) pthread_mutex_destroy (&client->lock_storage);
	(void) pthread_cond_destroy (&client->updates_cond);

	thread_group_end (&client->event_threads);

	free (client);

}

// keeps the client to be reused, or releases it if there are enough,
// its connections are deleted and the list is kept
static void client_recycle (Client *client) {

	dlist_reset (client->connections);

	if (freelist_push (&client_recycled, client)) {
		client_destroy (client);
	}

}

// the recycled clients are released when the library is unloaded
__attribute__ ((destructor)) static void client_recycled_drain (void) {

	freelist_drain (&client_recycled, client_destroy);

}

#pragma endregion

Client *client_new (void) {

	Client *client = client_allocate ();
	if (client) {
		client->id = 0;
		client->session_id = NULL;

		(void) memset (client->name, 0, CLIENT_NAME_SIZE);

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

		client->n_async_threads = 0;

		client->teardown_requested = false;

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

		client->recycle_next = NULL;
	}

	return client;

}

void client_delete (void *ptr) {

	if (ptr) {
		Client *client = (Client *) ptr;

		str_delete (client->session_id);
		client->session_id = NULL;

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

		for (unsigned int i = 0; i < CLIENT_MAX_EVENTS; i++)
			if (client->events[i]) client_event_delete (client->events[i]);

		for (unsigned int i = 0; i < CLIENT_MAX_ERRORS; i++)
			if (client->errors[i]) client_error_delete (client->errors[i]);

		for (unsigned int i = 0; i < CLIENT_FILES_MAX_PATHS; i++)
			str_delete (client->paths[i]);

		str_delete (client->uploads_path);

		// the stats, the lock and the synchronization values live inside the client
		client_recycle (client);
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

		// a recycled client keeps its empty connections list
		if (!client->connections) {
			client->connections = dlist_init (
				connection_delete, connection_comparator
			);
		}

		client->lock = &client->lock_storage;

		client_file_stats_init (&client->file_stats_storage);
		client->file_stats = &client->file_stats_storage;

		client_stats_init (&client->stats_storage);
		client->stats = &client->stats_storage;
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
		String *next = session_id ? str_new (session_id) : NULL;

		(void) pthread_mutex_lock (&client->session_lock);
		String *previous = client->session_id;
		client->session_id = next;
		(void) pthread_mutex_unlock (&client->session_lock);

		str_delete (previous);

		retval = 0;
	}

	return retval;

}

// copies the client's session id into session_id, cut to size - 1 bytes and NUL terminated,
// it can be called from any thread while the client exists
// returns the number of bytes copied, 0 if the client does not have a session id
size_t client_get_session_id (
	const Client *client, char *session_id, const size_t size
) {

	size_t copied = 0;

	if (client && session_id && size) {
		(void) pthread_mutex_lock ((pthread_mutex_t *) &client->session_lock);

		if (client->session_id) {
			copied = (client->session_id->len < (size - 1)) ? client->session_id->len : (size - 1);
			(void) memcpy (session_id, client->session_id->str, copied);
		}

		(void) pthread_mutex_unlock ((pthread_mutex_t *) &client->session_lock);

		session_id[copied] = '\0';
	}

	return copied;

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
ClientConnectionsStatus client_remove_connection_with_sock_fd (
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
					"client_remove_connection_with_sock_fd () - "
					"Client <%lu> does not have ANY connection - removing him from cerver...",
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
					"client_remove_connection_with_sock_fd () - "
					"Client <%lu> has only 1 connection left!",
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
					"client_remove_connection_with_sock_fd () - "
					"Client <%lu> has %zu connections left!",
					client->id, dlist_size (client->connections)
				);
				#endif

				// search the connection in the client
				connection = connection_get_with_sock_fd_from_client (
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
						"client_remove_connection_with_sock_fd () - Client with id "
						"%lu does not have a connection related to sock fd %d",
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
				"Failed to register all the connections for client %lu (id) to cerver %s",
				client->id, cerver->info->name
			);
			#endif
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
				"Failed to unregister all the connections for client %lu (id) from cerver %s",
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
				"Failed to register all the connections for client %lu (id) to cerver %s poll",
				client->id, cerver->info->name
			);
			#endif
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
				"Failed to unregister all the connections for client %lu (id) from cerver %s poll",
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
				"Connected clients to cerver %s: %lu.",
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
		"Connected clients to cerver %s: %lu.",
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

					// the caller releases the client and its connections
					else {
						(void) client_unregister_connections_from_cerver (cerver, client);
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
Client *client_get_with_sock_fd (Cerver *cerver, i32 sock_fd) {

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
Client *client_get_with_session_id (
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

u8 client_start (Client *client) {

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

// passes the received bytes of the next packet to the shared receive,
// that assembles the packet and dispatches it when it is complete
static void client_connection_get_next_packet_feed (
	Client *client, Connection *connection,
	char *buffer, const size_t size
) {

	CERVER_STATS_ADD (client->stats->n_receives_done, 1);
	CERVER_STATS_ADD (client->stats->total_bytes_received, size);

	#ifdef CONNECTION_STATS
	connection->stats->n_receives_done += 1;
	connection->stats->total_bytes_received += size;
	#endif

	connection->receive_handle.buffer = buffer;
	connection->receive_handle.buffer_size = size;
	connection->receive_handle.received_size = size;

	client_receive_handle_buffer (&connection->receive_handle);

}

// receives exactly size bytes of the next packet and feeds them
static ReceiveError client_connection_get_next_packet_piece (
	Client *client, Connection *connection,
	char *buffer, const size_t size
) {

	const ReceiveError error = client_receive_data (
		client, connection, buffer, size, size
	);

	if (error == RECEIVE_ERROR_NONE) {
		client_connection_get_next_packet_feed (client, connection, buffer, size);
	}

	return error;

}

// reads the header and then the body of the next packet in pieces of a fixed buffer,
// so the bytes after it stay in the socket, and the packet is assembled, checked against
// the max packet size of the client and dispatched by the same receive that the update thread uses
static unsigned int client_connection_get_next_packet_actual (
	Client *client, Connection *connection
) {

	char buffer[CLIENT_NEXT_PACKET_BUFFER_SIZE];

	connection->receive_handle.client = client;
	connection->receive_handle.connection = connection;

	const ReceiveError error = client_receive_data (
		client, connection, buffer, PACKET_HEADER_SIZE, PACKET_HEADER_SIZE
	);

	if (error != RECEIVE_ERROR_NONE) return 1;

	PacketHeader header;
	const bool valid_header = (
		packet_header_decode ((const u8 *) buffer, &header) == PACKET_HEADER_RESULT_NONE
	);

	#ifdef CLIENT_RECEIVE_DEBUG
	if (valid_header) packet_header_log (&header);
	#endif

	const bool valid_size = valid_header
		&& (header.packet_size >= PACKET_HEADER_SIZE)
		&& (header.packet_size <= (u64) client->max_received_packet_size);

	// an invalid header or size closes the connection, a packet with only its header is dispatched,
	// in both cases the connection must not be used after it
	client_connection_get_next_packet_feed (client, connection, buffer, PACKET_HEADER_SIZE);

	if (!valid_size) return 1;

	size_t remaining = (size_t) header.packet_size - PACKET_HEADER_SIZE;
	while (remaining > 0) {
		const size_t piece = (remaining < CLIENT_NEXT_PACKET_BUFFER_SIZE)
			? remaining : CLIENT_NEXT_PACKET_BUFFER_SIZE;

		if (client_connection_get_next_packet_piece (client, connection, buffer, piece) != RECEIVE_ERROR_NONE) {
			return 1;
		}

		remaining -= piece;
	}

	return 0;

}

// a datagram is received whole and its packet is handled like in the update thread
static unsigned int client_connection_get_next_datagram (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	char *buffer = (char *) malloc (MAX_UDP_PACKET_SIZE);
	if (buffer) {
		size_t received = 0;

		ReceiveError error = RECEIVE_ERROR_TIMEOUT;
		while (error == RECEIVE_ERROR_TIMEOUT) {
			error = client_receive_actual (
				client, connection, buffer, MAX_UDP_PACKET_SIZE, &received
			);
		}

		if (error == RECEIVE_ERROR_NONE) {
			CERVER_STATS_ADD (client->stats->n_receives_done, 1);
			CERVER_STATS_ADD (client->stats->total_bytes_received, received);

			retval = client_receive_datagram (client, connection, buffer, received);
		}

		free (buffer);
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
		retval = (connection->protocol == PROTOCOL_UDP)
			? client_connection_get_next_datagram (client, connection)
			: client_connection_get_next_packet_actual (client, connection);
	}

	return retval;

}

#pragma endregion
