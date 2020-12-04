#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <poll.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/pool.h"

#include "cerver/admin.h"
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

#include "cerver/threads/thread.h"
#include "cerver/threads/thpool.h"

#include "cerver/http/http.h"

#include "cerver/game/game.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region global

// initializes global cerver values
// should be called only once at the start of the program
void cerver_init (void) {

	cerver_log_init ();

}

// correctly disposes global values
// should be called only once at the very end of the program
void cerver_end (void) {

	cerver_log_end ();

}

#pragma endregion

#pragma region types

const char *cerver_type_to_string (CerverType type) {

	switch (type) {
		#define XX(num, name, string) case CERVER_TYPE_##name: return #string;
		CERVER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_type_to_string (CERVER_TYPE_NONE);

}

const char *cerver_handler_type_to_string (CerverHandlerType type) {

	switch (type) {
		#define XX(num, name, string, description) case CERVER_HANDLER_TYPE_##name: return #string;
		CERVER_HANDLER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_handler_type_to_string (CERVER_HANDLER_TYPE_NONE);

}

const char *cerver_handler_type_description (CerverHandlerType type) {

	switch (type) {
		#define XX(num, name, string, description) case CERVER_HANDLER_TYPE_##name: return #description;
		CERVER_HANDLER_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_handler_type_description (CERVER_HANDLER_TYPE_NONE);
	
}

#pragma endregion

#pragma region info

static CerverInfo *cerver_info_new (void) {

	CerverInfo *cerver_info = (CerverInfo *) malloc (sizeof (CerverInfo));
	if (cerver_info) {
		memset (cerver_info, 0, sizeof (CerverInfo));
		cerver_info->name = NULL;
		cerver_info->welcome_msg = NULL;
		cerver_info->cerver_info_packet = NULL;
	}

	return cerver_info;

}

static void cerver_info_delete (CerverInfo *cerver_info) {

	if (cerver_info) {
		str_delete (cerver_info->name);
		str_delete (cerver_info->welcome_msg);
		packet_delete (cerver_info->cerver_info_packet);

		free (cerver_info);
	}

}

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg) {

	u8 retval = 1;

	if (cerver) {
		if (cerver->info) {
			str_delete (cerver->info->welcome_msg);
			cerver->info->welcome_msg = msg ? str_new (msg) : NULL;
			retval = 0;
		}
	}

	return retval;

}

// sends the cerver info packet
// retuns 0 on success, 1 on error
u8 cerver_info_send_info_packet (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		packet_set_network_values (
			cerver->info->cerver_info_packet,
			cerver,
			client,
			connection,
			NULL
		);

		if (!packet_send (cerver->info->cerver_info_packet, 0, NULL, false)) {
			retval = 0;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_PACKET,
				"Failed to send cerver %s info packet!",
				cerver->info->name->str
			);
		}
	}

	return retval;

}

#pragma endregion

#pragma region stats

static CerverStats *cerver_stats_new (void) {

	CerverStats *cerver_stats = (CerverStats *) malloc (sizeof (CerverStats));
	if (cerver_stats) {
		memset (cerver_stats, 0, sizeof (CerverStats));
		cerver_stats->received_packets = packets_per_type_new ();
		cerver_stats->sent_packets = packets_per_type_new ();
	}

	return cerver_stats;

}

static void cerver_stats_delete (CerverStats *cerver_stats) {

	if (cerver_stats) {
		packets_per_type_delete (cerver_stats->received_packets);
		packets_per_type_delete (cerver_stats->sent_packets);

		free (cerver_stats);
	}

}

// sets the cerver stats threshold time (how often the stats get reset)
void cerver_stats_set_threshold_time (
	Cerver *cerver, time_t threshold_time
) {

	if (cerver) {
		if (cerver->stats) cerver->stats->threshold_time = threshold_time;
	}

}

void cerver_stats_print (Cerver *cerver, bool received, bool sent) {

	if (cerver) {
		if (cerver->stats) {
			cerver_log_msg ("\nCerver's %s stats:\n", cerver->info->name->str);
			cerver_log_msg ("Threshold time:                %ld\n", cerver->stats->threshold_time);

			if (cerver->auth_required) {
				cerver_log_msg ("Client packets received:       %ld", cerver->stats->client_n_packets_received);
				cerver_log_msg ("Client receives done:          %ld", cerver->stats->client_receives_done);
				cerver_log_msg ("Client bytes received:         %ld\n", cerver->stats->client_bytes_received);

				cerver_log_msg ("On hold packets received:      %ld", cerver->stats->on_hold_n_packets_received);
				cerver_log_msg ("On hold receives done:         %ld", cerver->stats->on_hold_receives_done);
				cerver_log_msg ("On hold bytes received:        %ld\n", cerver->stats->on_hold_bytes_received);
			}

			cerver_log_msg ("Total packets received:        %ld", cerver->stats->total_n_packets_received);
			cerver_log_msg ("Total receives done:           %ld", cerver->stats->total_n_receives_done);
			cerver_log_msg ("Total bytes received:          %ld\n", cerver->stats->total_bytes_received);

			cerver_log_msg ("N packets sent:                %ld", cerver->stats->n_packets_sent);
			cerver_log_msg ("Total bytes sent:              %ld\n", cerver->stats->total_bytes_sent);

			cerver_log_msg ("Current active client connections:         %ld", cerver->stats->current_active_client_connections);
			cerver_log_msg ("Current connected clients:                 %ld", cerver->stats->current_n_connected_clients);
			cerver_log_msg ("Current on hold connections:               %ld", cerver->stats->current_n_hold_connections);
			cerver_log_msg ("Total on hold connections:                 %ld", cerver->stats->total_on_hold_connections);
			cerver_log_msg ("Total clients:                             %ld", cerver->stats->total_n_clients);
			cerver_log_msg ("Unique clients:                            %ld", cerver->stats->unique_clients);
			cerver_log_msg ("Total client connections:                  %ld", cerver->stats->total_client_connections);

			if (received) {
				cerver_log_msg ("\nReceived packets:");
				packets_per_type_print (cerver->stats->received_packets);
			}

			if (sent) {
				cerver_log_msg ("\nSent packets:");
				packets_per_type_print (cerver->stats->sent_packets);
			}

			cerver_log_msg ("\n");
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver %s does not have a reference to cerver stats!",
				cerver->info->name->str
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"Cant print stats of a NULL cerver!"
		);
	}

}

#pragma endregion

#pragma region main

Cerver *cerver_new (void) {

	Cerver *c = (Cerver *) malloc (sizeof (Cerver));
	if (c) {
		memset (c, 0, sizeof (Cerver));

		c->sock = -1;

		c->protocol = PROTOCOL_TCP;         // default protocol
		c->use_ipv6 = false;
		c->connection_queue = DEFAULT_CONNECTION_QUEUE;
		c->receive_buffer_size = RECEIVE_PACKET_BUFFER_SIZE;

		c->isRunning = false;
		c->blocking = true;

		c->cerver_data = NULL;
		c->delete_cerver_data = NULL;

		c->n_thpool_threads = 0;
		c->thpool = NULL;

		c->sockets_pool_init = DEFAULT_SOCKETS_INIT;
		c->sockets_pool = NULL;

		c->clients = NULL;
		c->client_sock_fd_map = NULL;

		c->inactive_clients = false;

		c->handler_type = CERVER_HANDLER_TYPE_NONE;

		c->handle_detachable_threads = false;

		c->fds = NULL;
		c->poll_timeout = DEFAULT_POLL_TIMEOUT;
		c->poll_lock = NULL;

		c->auth_required = false;
		c->auth_packet = NULL;
		c->max_auth_tries = DEFAULT_AUTH_TRIES;
		c->authenticate = NULL;

		c->on_hold_connections = NULL;
		c->on_hold_connection_sock_fd_map = NULL;
		c->hold_fds = NULL;
		c->on_hold_poll_timeout = DEFAULT_POLL_TIMEOUT;
		c->on_hold_poll_lock = NULL;
		c->on_hold_max_bad_packets = DEFAULT_ON_HOLD_MAX_BAD_PACKETS;
		c->on_hold_check_packets = false;

		c->use_sessions = false;
		c->session_id_generator = NULL;

		c->handle_received_buffer = NULL;

		c->app_packet_handler = NULL;
		c->app_error_packet_handler = NULL;
		c->custom_packet_handler = NULL;

		c->app_packet_handler_delete_packet = true;
		c->app_error_packet_handler_delete_packet = true;
		c->custom_packet_handler_delete_packet = true;

		c->handlers = NULL;
		c->handlers_lock = NULL;

		c->check_packets = false;

		c->update = NULL;
		c->update_args = NULL;
		c->delete_update_args = NULL;
		c->update_ticks = DEFAULT_UPDATE_TICKS;

		c->update_interval = NULL;
		c->update_interval_args = NULL;
		c->delete_update_interval_args = NULL;
		c->update_interval_secs = DEFAULT_UPDATE_INTERVAL_SECS;

		c->admin = NULL;

		for (unsigned int i = 0; i < CERVER_MAX_EVENTS; i++)
			c->events[i] = NULL;

		for (unsigned int i = 0; i < CERVER_MAX_ERRORS; i++)
			c->errors[i] = NULL;

		c->info = NULL;
		c->stats = NULL;
	}

	return c;

}

void cerver_delete (void *ptr) {

	if (ptr) {
		Cerver *cerver = (Cerver *) ptr;

		if (cerver->cerver_data) {
			if (cerver->delete_cerver_data) cerver->delete_cerver_data (cerver->cerver_data);
			else free (cerver->cerver_data);
		}

		pool_delete (cerver->sockets_pool);

		if (cerver->clients) avl_delete (cerver->clients);
		if (cerver->client_sock_fd_map) htab_destroy (cerver->client_sock_fd_map);

		if (cerver->fds) free (cerver->fds);

		// 28/05/2020
		if (cerver->poll_lock) {
			pthread_mutex_destroy (cerver->poll_lock);
			free (cerver->poll_lock);
		}

		packet_delete (cerver->auth_packet);

		if (cerver->on_hold_connections) avl_delete (cerver->on_hold_connections);
		if (cerver->on_hold_connection_sock_fd_map) htab_destroy (cerver->on_hold_connection_sock_fd_map);
		if (cerver->hold_fds) free (cerver->hold_fds);

		if (cerver->on_hold_poll_lock) {
			pthread_mutex_destroy (cerver->on_hold_poll_lock);
			free (cerver->on_hold_poll_lock);
		}

		// 27/05/2020
		handler_delete (cerver->app_packet_handler);
		handler_delete (cerver->app_error_packet_handler);
		handler_delete (cerver->custom_packet_handler);

		// 10/05/2020
		if (cerver->handlers) {
			for (unsigned int idx = 0; idx < cerver->n_handlers; idx++) {
				handler_delete (cerver->handlers[idx]);
			}

			free (cerver->handlers);
		}

		if (cerver->handlers_lock) {
			pthread_mutex_destroy (cerver->handlers_lock);
			free (cerver->handlers_lock);
		}

		admin_cerver_delete (cerver->admin);

		for (unsigned int i = 0; i < CERVER_MAX_EVENTS; i++)
			if (cerver->events[i]) cerver_event_delete (cerver->events[i]);

		for (unsigned int i = 0; i < CERVER_MAX_ERRORS; i++)
			if (cerver->errors[i]) cerver_error_event_delete (cerver->errors[i]);

		cerver_info_delete (cerver->info);
		cerver_stats_delete (cerver->stats);

		free (cerver);
	}

}

// sets the cerver main network values
void cerver_set_network_values (
	Cerver *cerver,
	const u16 port, const Protocol protocol,
	bool use_ipv6, const u16 connection_queue
) {

	if (cerver) {
		cerver->port = port;
		cerver->protocol = protocol;
		cerver->use_ipv6 = use_ipv6;
		cerver->connection_queue = connection_queue;
	}

}

// sets the cerver connection queue (how many connections to queue for accept)
void cerver_set_connection_queue (
	Cerver *cerver, const u16 connection_queue
) {

	if (cerver) cerver->connection_queue = connection_queue;

}

// sets the cerver's receive buffer size used in recv method
void cerver_set_receive_buffer_size (
	Cerver *cerver, const u32 size
) {

	if (cerver) cerver->receive_buffer_size = size;

}

// sets the cerver's data and a way to free it
void cerver_set_cerver_data (
	Cerver *cerver, void *data, Action delete_data
) {

	if (cerver) {
		cerver->cerver_data = data;
		cerver->delete_cerver_data = delete_data;
	}

}

// sets the cerver's thpool number of threads
// this will enable the cerver's ability to handle received packets using multiple threads
// usefull if you want the best concurrency and effiency
// but we aware that you need to make your structures and data thread safe, as they might be accessed
// from multiple threads at the same time
// by default, all received packets will be handle only in one thread
void cerver_set_thpool_n_threads (
	Cerver *cerver, u16 n_threads
) {

	if (cerver) cerver->n_thpool_threads = n_threads;

}

// sets the initial number of sockets to be created in the cerver's sockets pool
// the defauult value is 10
void cerver_set_sockets_pool_init (
	Cerver *cerver, unsigned int n_sockets
) {

	if (cerver) cerver->sockets_pool_init = n_sockets;

}

// 17/06/2020
// enables the ability to check for inactive clients - clients that have not been sent or received from a packet in x time
// will be automatically dropped from the cerver
// max_inactive_time - max secs allowed for a client to be inactive, 0 for default
// check_inactive_interval - how often to check for inactive clients in secs, 0 for default
void cerver_set_inactive_clients (
	Cerver *cerver,
	u32 max_inactive_time, u32 check_inactive_interval
) {

	if (cerver) {
		cerver->inactive_clients = true;
		cerver->max_inactive_time = max_inactive_time ? max_inactive_time : DEFAULT_MAX_INACTIVE_TIME;
		cerver->check_inactive_interval = check_inactive_interval ? check_inactive_interval : DEFAULT_CHECK_INACTIVE_INTERVAL;
	}

}

// sets the cerver handler type
// the default type is to handle connections using the poll () which requires only one thread
// if threads type is selected, a new thread will be created for each new connection
void cerver_set_handler_type (
	Cerver *cerver, CerverHandlerType handler_type
) {

	if (cerver) cerver->handler_type = handler_type;

}

// set the ability to handle new connections if cerver handler type is CERVER_HANDLER_TYPE_THREADS
// by only creating new detachable threads for each connection
// by default, this option is turned off to also use the thpool
// if cerver is of type CERVER_TYPE_WEB, the thpool will be used more often as connections have a shorter life
void cerver_set_handle_detachable_threads (
	Cerver *cerver, bool active
) {

	if (cerver) cerver->handle_detachable_threads = active;

}

// sets the cerver poll timeout
void cerver_set_poll_time_out (
	Cerver *cerver, const u32 poll_timeout
) {

	if (cerver) cerver->poll_timeout = poll_timeout;

}

// enables cerver's built in authentication methods
// cerver requires client authentication upon new client connections
// retuns 0 on success, 1 on error
u8 cerver_set_auth (
	Cerver *cerver, u8 max_auth_tries, delegate authenticate
) {

	u8 retval = 1;

	if (cerver) {
		cerver->auth_required = true;
		cerver->max_auth_tries = max_auth_tries;
		cerver->authenticate = authenticate;

		retval = 0;
	}

	return retval;

}

// sets the max auth tries a new connection is allowed to have
// before it is dropped due to failure
void cerver_set_auth_max_tries (
	Cerver *cerver, u8 max_auth_tries
) {

	if (cerver) cerver->max_auth_tries = max_auth_tries;

}

// sets the method to be used for client authentication
// must return 0 on success authentication
void cerver_set_auth_method (
	Cerver *cerver, delegate authenticate
) {

	if (cerver && authenticate) cerver->authenticate = authenticate;

}

// sets the cerver on poll timeout in ms
void cerver_set_on_hold_poll_timeout (
	Cerver *cerver, u32 on_hold_poll_timeout
) {

	if (cerver) cerver->on_hold_poll_timeout = on_hold_poll_timeout;

}

// sets the max number of bad packets to tolerate from an ON HOLD connection before being dropped,
// the default is DEFAULT_ON_HOLD_MAX_BAD_PACKETS
// a bad packet is anyone which can't be handle by the cerver because there is no handle,
// or because it failed the packet_check () method
void cerver_set_on_hold_max_bad_packets (
	Cerver *cerver, u8 on_hold_max_bad_packets
) {

	if (cerver) cerver->on_hold_max_bad_packets = on_hold_max_bad_packets;

}

// sets whether to check for packets using the packet_check () method in ON HOLD handler or NOT
// any packet that fails the check will be considered as a bad packet
void cerver_set_on_hold_check_packets (Cerver *cerver, bool check) {

	if (cerver) cerver->on_hold_check_packets = check;

}

// configures the cerver to use client sessions
// retuns 0 on success, 1 on error
u8 cerver_set_sessions (
	Cerver *cerver, void *(*session_id_generator) (const void *)
) {

	u8 retval = 1;

	if (cerver) {
		if (session_id_generator) {
			cerver->session_id_generator = session_id_generator;
			cerver->use_sessions = true;

			retval = 0;
		}
	}

	return retval;

}

// sets a custom method to handle the raw received buffer from the socker
void cerver_set_handle_recieved_buffer (
	Cerver *cerver, Action handle_received_buffer
) {

	if (cerver) cerver->handle_received_buffer = handle_received_buffer;

}

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
void cerver_set_app_handlers (
	Cerver *cerver, Handler *app_handler, Handler *app_error_handler
) {

	if (cerver) {
		cerver->app_packet_handler = app_handler;
		if (cerver->app_packet_handler) {
			cerver->app_packet_handler->type = HANDLER_TYPE_CERVER;
			cerver->app_packet_handler->cerver = cerver;
		}

		cerver->app_error_packet_handler = app_error_handler;
		if (cerver->app_error_packet_handler) {
			cerver->app_error_packet_handler->type = HANDLER_TYPE_CERVER;
			cerver->app_error_packet_handler->cerver = cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_APP packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void cerver_set_app_handler_delete (
	Cerver *cerver, bool delete_packet
) {

	if (cerver) cerver->app_packet_handler_delete_packet = delete_packet;

}

// sets option to automatically delete PACKET_TYPE_APP_ERROR packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void cerver_set_app_error_handler_delete (
	Cerver *cerver, bool delete_packet
) {

	if (cerver) cerver->app_error_packet_handler_delete_packet = delete_packet;

}

// sets a PACKET_TYPE_CUSTOM packet type handler
void cerver_set_custom_handler (
	Cerver *cerver, Handler *custom_handler
) {

	if (cerver) {
		cerver->custom_packet_handler = custom_handler;
		if (cerver->custom_packet_handler) {
			cerver->custom_packet_handler->type = HANDLER_TYPE_CERVER;
			cerver->custom_packet_handler->cerver = cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_CUSTOM packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void cerver_set_custom_handler_delete (
	Cerver *cerver, bool delete_packet
) {

	if (cerver) cerver->custom_packet_handler_delete_packet = delete_packet;

}

// enables the ability of the cerver to have multiple app handlers
// returns 0 on success, 1 on error
int cerver_set_multiple_handlers (
	Cerver *cerver, unsigned int n_handlers
) {

	int retval = 1;

	if (cerver) {
		cerver->multiple_handlers = true;
		cerver->n_handlers = n_handlers;

		cerver->handlers = (Handler **) calloc (cerver->n_handlers, sizeof (Handler *));
		if (cerver->handlers) {
			for (unsigned int idx = 0; idx < cerver->n_handlers; idx++)
				cerver->handlers[idx] = NULL;

			// 27/05/2020 -- moved to cerver_handlers_start ()
			// cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			// pthread_mutex_init (cerver->handlers_lock, NULL);

			retval = 0;
		}
	}

	return retval;

}

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
void cerver_set_check_packets (
	Cerver *cerver, bool check_packets
) {

	if (cerver) {
		cerver->check_packets = check_packets;
	}

}

// sets a custom cerver update function to be executed every n ticks
// a new thread will be created that will call your method each tick
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void cerver_set_update (
	Cerver *cerver,
	Action update,
	void *update_args, void (*delete_update_args)(void *),
	const u8 fps
) {

	if (cerver) {
		cerver->update = update;
		cerver->update_args = update_args;
		cerver->delete_update_args = delete_update_args;
		cerver->update_ticks = fps;
	}

}

// sets a custom cerver update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void cerver_set_update_interval (
	Cerver *cerver,
	Action update,
	void *update_args, void (*delete_update_args)(void *),
	const u32 interval
) {

	if (cerver) {
		cerver->update_interval = update;
		cerver->update_interval_args = update_args;
		cerver->delete_update_interval_args = delete_update_args;
		cerver->update_interval_secs = interval;
	}

}

// enables admin connections to cerver
// returns 0 on success, 1 on error
u8 cerver_set_admin_enable (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->admin = admin_cerver_create ();
		if (cerver->admin) {
			cerver->admin->cerver = cerver;
			retval = 0;
		}
	}

	return retval;

}

#pragma endregion

#pragma region sockets

static int cerver_sockets_pool_init (Cerver *cerver) {

	int retval = 1;

	if (cerver) {
		cerver->sockets_pool = pool_create (socket_delete);
		if (cerver->sockets_pool) {
			retval = pool_init (
				cerver->sockets_pool,
				socket_create_empty,
				cerver->sockets_pool_init
			);
		}
	}

	return retval;

}

int cerver_sockets_pool_push (Cerver *cerver, Socket *socket) {

	int retval = 1;

	if (cerver && socket) {
		retval = pool_push (cerver->sockets_pool, socket);
		// printf ("push!\n");
	}

	return retval;

}

Socket *cerver_sockets_pool_pop (Cerver *cerver) {

	Socket *retval = NULL;

	if (cerver) {
		void *value = pool_pop (cerver->sockets_pool);
		if (value) retval = (Socket *) value;
		// printf ("pop!\n");
	}

	return retval;

}

static void cerver_sockets_pool_end (Cerver *cerver) {

	if (cerver) {
		pool_delete (cerver->sockets_pool);
		cerver->sockets_pool = NULL;
	}

}

#pragma endregion

#pragma region handlers

// prints info about current handlers
void cerver_handlers_print_info (Cerver *cerver) {

	if (cerver) {
		cerver_log_debug (
			"%d - current ACTIVE handlers in cerver %s",
			cerver->num_handlers_alive, cerver->info->name->str
		);

		cerver_log_debug (
			"%d - current WORKING handlers in cerver %s",
			cerver->num_handlers_working, cerver->info->name->str
		);
	}

}

// adds a new handler to the cerver handlers array
// is the responsability of the user to provide a unique handler id, which must be < cerver->n_handlers
// returns 0 on success, 1 on error
u8 cerver_handlers_add (Cerver *cerver, Handler *handler) {

	u8 retval = 1;

	if (cerver && handler) {
		if (cerver->handlers) {
			cerver->handlers[handler->id] = handler;

			handler->type = HANDLER_TYPE_CERVER;
			handler->cerver = cerver;

			retval = 0;
		}
	}

	return retval;

}

// returns the current number of app handlers (multiple handlers option)
unsigned int cerver_get_n_handlers (Cerver *cerver) {

	unsigned int retval = 0;

	if (cerver) {
		pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->n_handlers;
		pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently alive (ready to handle packets)
unsigned int cerver_get_n_handlers_alive (Cerver *cerver) {

	unsigned int retval = 0;

	if (cerver) {
		pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->num_handlers_alive;
		pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently working (handling a packet)
unsigned int cerver_get_n_handlers_working (Cerver *cerver) {

	unsigned int retval = 0;

	if (cerver) {
		pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->num_handlers_working;
		pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// correctly destroys multiple app handlers, if any
static u8 cerver_multiple_app_handlers_destroy (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		if (cerver->handlers && (cerver->num_handlers_alive > 0)) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Stopping multiple app handlers in cerver %s...",
				cerver->info->name->str
			);
			#endif

			#ifdef CERVER_DEBUG
			cerver_handlers_print_info (cerver);
			#endif

			// give x timeout to kill idle handlers
			double timeout = 1.0;
			time_t start = 0, end = 0;
			double time_passed = 0.0;
			time (&start);
			while (time_passed < timeout && cerver->num_handlers_alive) {
				for (unsigned int i = 0; i < cerver->n_handlers; i++) {
					bsem_post_all (cerver->handlers[i]->job_queue->has_jobs);
					time (&end);
					time_passed = difftime (end, start);
				}
			}

			// poll remaining handlers
			while (cerver->num_handlers_alive) {
				for (unsigned int i = 0; i < cerver->n_handlers; i++) {
					bsem_post_all (cerver->handlers[i]->job_queue->has_jobs);
					sleep (1);
				}
			}

			retval = 0;
		}
	}

	return retval;

}

static u8 cerver_app_handlers_destroy (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->multiple_handlers) {
			if (cerver->handlers && (cerver->num_handlers_alive > 0)) {
				if (!cerver_multiple_app_handlers_destroy (cerver)) {
					#ifdef CERVER_DEBUG
					cerver_log_success (
						"Done destroying multiple app handlers in cerver %s!",
						cerver->info->name->str
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to destroy multiple app handlers in cerver %s!",
						cerver->info->name->str
					);
				}
			}
		}

		else {
			if (cerver->app_packet_handler) {
				if (!cerver->app_packet_handler->direct_handle) {
					// stop app handler
					bsem_post_all (cerver->app_packet_handler->job_queue->has_jobs);
				}
			}
		}
	}

	return errors;

}

static u8 cerver_app_error_handler_destroy (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->app_error_packet_handler) {
			if (!cerver->app_error_packet_handler->direct_handle) {
				// stop app error handler
				bsem_post_all (cerver->app_error_packet_handler->job_queue->has_jobs);
			}
		}
	}

	return errors;

}

static u8 cerver_custom_handler_destroy (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->custom_packet_handler) {
			if (!cerver->custom_packet_handler->direct_handle) {
				// stop custom handler
				bsem_post_all (cerver->custom_packet_handler->job_queue->has_jobs);
			}
		}
	}

	return errors;

}

// 27/05/2020
// correctly destroy cerver's custom handlers
static u8 cerver_handlers_destroy (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Stopping handlers in cerver %s...",
			cerver->info->name->str
		);
		#endif

		errors |= cerver_app_handlers_destroy (cerver);

		errors |= cerver_app_error_handler_destroy (cerver);

		errors |= cerver_custom_handler_destroy (cerver);

		// poll remaining handlers
		while (cerver->num_handlers_alive) {
			if (cerver->app_packet_handler)
				bsem_post_all (cerver->app_packet_handler->job_queue->has_jobs);

			if (cerver->app_error_packet_handler)
				bsem_post_all (cerver->app_error_packet_handler->job_queue->has_jobs);

			if (cerver->custom_packet_handler)
				bsem_post_all (cerver->custom_packet_handler->job_queue->has_jobs);

			sleep (1);
		}
	}

	return errors;

}

#pragma endregion

#pragma region create

// returns a new cerver with the specified parameters
Cerver *cerver_create (
	const CerverType type, const char *name,
	const u16 port, const Protocol protocol, bool use_ipv6,
	u16 connection_queue
) {

	Cerver *cerver = NULL;

	if (name) {
		cerver = cerver_new ();
		if (cerver) {
			cerver->type = type;

			cerver_set_network_values (cerver, port, protocol, use_ipv6, connection_queue);

			// init cerver type based values
			switch (cerver->type) {
				case CERVER_TYPE_CUSTOM: break;

				case CERVER_TYPE_GAME: {
					cerver->cerver_data = game_new ();
					cerver->delete_cerver_data = game_delete;
				} break;

				case CERVER_TYPE_WEB: {
					// cerver->cerver_data = http_cerver_create (cerver);
					// cerver->delete_cerver_data = http_cerver_delete;
				} break;

				case CERVER_TYPE_FILES: {
					cerver->cerver_data = file_cerver_create (cerver);
					cerver->delete_cerver_data = file_cerver_delete;
				} break;

				default: break;
			}

			cerver_set_handle_recieved_buffer (cerver, cerver_receive_handle_buffer);

			cerver->info = cerver_info_new ();
			cerver->info->name = str_new (name);

			cerver->stats = cerver_stats_new ();
		}
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_NONE,
			"A name is required to create a new cerver!"
		);
	}

	return cerver;

}

#pragma endregion

#pragma region init

// inits the cerver's address & binds the socket to it
static u8 cerver_network_init_address (Cerver *cerver) {

	u8 retval = 1;

	memset (&cerver->address, 0, sizeof (struct sockaddr_storage));

	if (cerver->use_ipv6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &cerver->address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (cerver->port);
	}

	else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &cerver->address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (cerver->port);
	}

	if (!bind (cerver->sock, (const struct sockaddr *) &cerver->address, sizeof (struct sockaddr_storage))) {
		retval = 0;       // success!!
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to bind cerver %s socket!", cerver->info->name->str
		);

		close (cerver->sock);
	}

	return retval;

}

// set cerver's socket blocking property based on cerver handler type
static u8 cerver_network_init_block_socket (Cerver *cerver) {

	u8 retval = 0;

	switch (cerver->handler_type) {
		case CERVER_HANDLER_TYPE_NONE: break;

		case CERVER_HANDLER_TYPE_POLL: {
			// set the socket to non blocking mode
			if (sock_set_blocking (cerver->sock, cerver->blocking)) {
				cerver->blocking = false;
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
					"Cerver %s socket set to non blocking mode.",
					cerver->info->name->str
				);
				#endif
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to set cerver %s socket to non blocking mode!",
					cerver->info->name->str
				);

				close (cerver->sock);

				retval = 1;     // error
			}
		} break;

		case CERVER_HANDLER_TYPE_THREADS: break;

		default: break;
	}

	return retval;

}

// inits the cerver networking capabilities
static u8 cerver_network_init (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		// init the cerver with the selected protocol
		switch (cerver->protocol) {
			case IPPROTO_TCP:
				cerver->sock = socket ((cerver->use_ipv6 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
				break;

			case IPPROTO_UDP:
				cerver->sock = socket ((cerver->use_ipv6 ? AF_INET6 : AF_INET), SOCK_DGRAM, 0);
				break;

			default: cerver_log (LOG_TYPE_ERROR, LOG_TYPE_CERVER, "Unknown protocol type!"); break;
		}

		if (cerver->sock >= 0) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Created cerver %s socket - <%d>!",
				cerver->info->name->str,
				cerver->sock
			);
			#endif

			if (!cerver_network_init_block_socket (cerver)) {
				retval = cerver_network_init_address (cerver);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to create cerver %s socket!", cerver->info->name->str
			);
		}
	}

	return retval;

}

static u8 cerver_init_poll_fds (Cerver *cerver) {

	u8 retval = 1;

	cerver->fds = (struct pollfd *) calloc (poll_n_fds, sizeof (struct pollfd));
	if (cerver->fds) {
		memset (cerver->fds, 0, sizeof (struct pollfd) * poll_n_fds);
		// set all fds as available spaces
		for (u32 i = 0; i < poll_n_fds; i++) cerver->fds[i].fd = -1;

		cerver->max_n_fds = poll_n_fds;
		cerver->current_n_fds = 0;

		retval = 0;     // success!!
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to allocate cerver %s main fds!", cerver->info->name->str
		);
		#endif
	}

	return retval;

}

static u8 cerver_init_data_structures (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->clients = avl_init (
			cerver->use_sessions ? client_comparator_session_id : client_comparator_client_id,
			client_delete
		);

		if (cerver->clients) {
			cerver->client_sock_fd_map = htab_create (poll_n_fds, NULL, NULL);
			if (cerver->client_sock_fd_map) {
				u8 errors = 0;

				// init cerver handler type based values
				switch (cerver->handler_type) {
					case CERVER_HANDLER_TYPE_NONE: break;

					case CERVER_HANDLER_TYPE_POLL: {
						// initialize main pollfd structures
						errors |= cerver_init_poll_fds (cerver);
					} break;

					case CERVER_HANDLER_TYPE_THREADS: break;

					default: break;
				}

				retval = errors;
			}

			else {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to init clients sock fd map in cerver %s",
					cerver->info->name->str
				);
				#endif
			}
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init clients avl in cerver %s",
				cerver->info->name->str
			);
			#endif
		}
	}

	return retval;

}

// inits a cerver of a given type
static u8 cerver_init_internal (Cerver *cerver) {

	int retval = 1;

	if (cerver) {
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Initializing cerver %s...", cerver->info->name->str
		);

		cerver_log_msg ("Cerver type: %s", cerver_type_to_string (cerver->type));
		cerver_log_msg ("Cerver handler type: %s", cerver_handler_type_to_string (cerver->handler_type));

		if (!cerver_network_init (cerver)) {
			if (!cerver_init_data_structures (cerver)) {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
					"Done initializing cerver %s network values & data structures!",
					cerver->info->name->str
				);
				#endif

				retval = 0;     // success!!
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to init cerver %s data structures!",
					cerver->info->name->str
				);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s network!", cerver->info->name->str
			);
		}
	}

	return retval;

}

static u8 cerver_one_time_init_thpool (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->n_thpool_threads) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Cerver %s is configured to use a thpool with %d threads",
				cerver->info->name->str, cerver->n_thpool_threads
			);
			#endif

			cerver->thpool = thpool_create (cerver->n_thpool_threads);
			thpool_set_name (cerver->thpool, cerver->info->name->str);
			if (thpool_init (cerver->thpool)) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to init cerver %s thpool!", cerver->info->name->str
				);

				errors = 1;
			}
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Cerver %s is configured to NOT use a thpool for receive methods",
				cerver->info->name->str
			);
			#endif
		}
	}

	return errors;

}

static u8 cerver_one_time_init (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (!cerver_init_internal (cerver)) {
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Initialized cerver %s!", cerver->info->name->str
			);

			// 29/05/2020
			errors |= cerver_sockets_pool_init (cerver);

			// 28/05/2020
			cerver->poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			pthread_mutex_init (cerver->poll_lock, NULL);

			// init the cerver thpool
			errors |= cerver_one_time_init_thpool (cerver);

			// perform one time init methods by cerver type
			switch (cerver->type) {
				case CERVER_TYPE_CUSTOM: break;

				case CERVER_TYPE_GAME: {
					GameCerver *game_data = (GameCerver *) cerver->cerver_data;
					game_data->cerver = cerver;
					if (game_data && game_data->load_game_data) {
						game_data->load_game_data (NULL);
					}

					else {
						cerver_log (
							LOG_TYPE_WARNING, LOG_TYPE_GAME,
							"Game cerver %s doesn't have a reference to a game data!",
							cerver->info->name->str
						);
					}
				} break;

				case CERVER_TYPE_WEB: {
					// http_cerver_init ((HttpCerver *) cerver->cerver_data);
				} break;

				case CERVER_TYPE_FILES: break;

				default: break;
			}

			cerver->info->cerver_info_packet = cerver_packet_generate (cerver);
			if (!cerver->info->cerver_info_packet) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to generate cerver %s info packet",
					cerver->info->name->str
				);

				errors |= 1;
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s!", cerver->info->name->str
			);

			errors |= 1;
		}
	}

	return errors;

}

#pragma endregion

#pragma region start

static void cerver_update (void *args);

static void cerver_update_interval (void *args);

// inits cerver's auth capabilities
static u8 cerver_auth_start (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->auth_packet = packet_generate_request (PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_REQUEST_AUTH, NULL, 0);

		cerver->max_on_hold_connections = poll_n_fds / 2;
		cerver->on_hold_connections = avl_init (connection_comparator, connection_delete);
		cerver->on_hold_connection_sock_fd_map = htab_create (cerver->max_on_hold_connections / 4, NULL, NULL);
		if (cerver->on_hold_connections && cerver->on_hold_connection_sock_fd_map) {
			cerver->hold_fds = (struct pollfd *) calloc (cerver->max_on_hold_connections, sizeof (struct pollfd));
			if (cerver->hold_fds) {
				memset (cerver->hold_fds, 0, sizeof (struct pollfd) * cerver->max_on_hold_connections);

				for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
					cerver->hold_fds[i].fd = -1;

				cerver->current_on_hold_nfds = 0;

				cerver->on_hold_poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
				pthread_mutex_init (cerver->on_hold_poll_lock, NULL);

				if (!thread_create_detachable (&cerver->on_hold_poll_id, on_hold_poll, cerver)) {
					retval = 0;
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_NONE,
						"Failed to create cerver's %s on_hold_poll () thread!",
						cerver->info->name->str
					);
				}

				retval = 0;
			}
		}
	}

	return retval;

}

// 11/05/2020 -- start cerver's multiple app handlers
static u8 cerver_multiple_app_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		cerver->num_handlers_alive = 0;
		cerver->num_handlers_working = 0;

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Initializing cerver %s multiple app handlers...",
			cerver->info->name->str
		);
		#endif

		for (unsigned int i = 0; i < cerver->n_handlers; i++) {
			if (cerver->handlers[i]) {
				if (!cerver->handlers[i]->direct_handle) {
					errors |= handler_start (cerver->handlers[i]);
				}
			}
		}

		if (!errors) {
			// wait for all handlers to be initialized
			while (cerver->num_handlers_alive != cerver->n_handlers) {}

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Cerver %s multiple app handlers are ready!",
				cerver->info->name->str
			);
			#endif
		}

		else {
			cerver_log_error (
				"Failed to init ALL multiple app handlers in cerver %s",
				cerver->info->name->str
			);
		}
	}

	return errors;

}

static u8 cerver_app_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->multiple_handlers) {
			errors |= cerver_multiple_app_handlers_start (cerver);
		}

		else {
			if (cerver->app_packet_handler) {
				if (!cerver->app_packet_handler->direct_handle) {
					// init single app packet handler
					if (!handler_start (cerver->app_packet_handler)) {
						#ifdef CERVER_DEBUG
						cerver_log_success (
							"Cerver %s app_packet_handler has started!",
							cerver->info->name->str
						);
						#endif
					}

					else {
						cerver_log_error (
							"Failed to start cerver %s app_packet_handler!",
							cerver->info->name->str
						);

						errors = 1;
					}
				}
			}

			else {
				switch (cerver->type) {
					case CERVER_TYPE_WEB: break;
					case CERVER_TYPE_FILES: break;

					default: {
						cerver_log_warning (
							"Cerver %s does not have an app_packet_handler",
							cerver->info->name->str
						);
					} break;
				}
			}
		}
	}

	return errors;

}

static u8 cerver_app_error_handler_start (Cerver *cerver) {

	u8 retval = 0;

	if (cerver) {
		if (cerver->app_error_packet_handler) {
			if (!cerver->app_error_packet_handler->direct_handle) {
				if (!handler_start (cerver->app_error_packet_handler)) {
					#ifdef CERVER_DEBUG
					cerver_log_success (
						"Cerver %s app_error_packet_handler has started!",
						cerver->info->name->str
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s app_error_packet_handler!",
						cerver->info->name->str
					);
				}
			}
		}

		else {
			switch (cerver->type) {
				case CERVER_TYPE_WEB: break;
				case CERVER_TYPE_FILES: break;

				default: {
					cerver_log_warning (
						"Cerver %s does not have an app_error_packet_handler",
						cerver->info->name->str
					);
				} break;
			}
		}
	}

	return retval;

}

static u8 cerver_custom_handler_start (Cerver *cerver) {

	u8 retval = 0;

	if (cerver) {
		if (cerver->custom_packet_handler) {
			if (!cerver->custom_packet_handler->direct_handle) {
				if (!handler_start (cerver->custom_packet_handler)) {
					#ifdef CERVER_DEBUG
					cerver_log_success (
						"Cerver %s custom_packet_handler has started!",
						cerver->info->name->str
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s custom_packet_handler!",
						cerver->info->name->str
					);
				}
			}
		}

		else {
			switch (cerver->type) {
				case CERVER_TYPE_WEB: break;
				case CERVER_TYPE_FILES: break;

				default: {
					cerver_log_warning (
						"Cerver %s does not have a custom_packet_handler",
						cerver->info->name->str
					);
				} break;
			}
		}
	}

	return retval;

}

// 27/05/2020 -- starts all cerver's handlers
static u8 cerver_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Initializing %s handlers...",
			cerver->info->name->str
		);
		#endif

		cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (cerver->handlers_lock, NULL);

		errors |= cerver_app_handlers_start (cerver);

		errors |= cerver_app_error_handler_start (cerver);

		errors |= cerver_custom_handler_start (cerver);

		if (!errors) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Done initializing %s handlers!",
				cerver->info->name->str
			);
			#endif
		}
	}

	return errors;

}

static u8 cerver_update_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_detachable (
		&cerver->update_thread_id,
		(void *(*) (void *)) cerver_update,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE thread!",
			cerver->info->name->str
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE thread!",
			cerver->info->name->str
		);
	}

	return retval;

}

static u8 cerver_update_interval_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_detachable (
		&cerver->update_interval_thread_id,
		(void *(*) (void *)) cerver_update_interval,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE INTERVAL thread!",
			cerver->info->name->str
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE INTERVAL thread!",
			cerver->info->name->str
		);
	}

	return retval;

}

static void cerver_inactive_check (AVLNode *node, Cerver *cerver, time_t current_time) {

	cerver_inactive_check (node->right, cerver, current_time);

	// check for client inactivity
	if (node->id) {
		Client *client = (Client *) node->id;

		if ((current_time - client->last_activity) >= cerver->max_inactive_time) {
			// TODO: the client should be dropped
			cerver_log_warning (
				"Client %ld has been inactive more than %d secs and should be dropped",
				client->id, cerver->max_inactive_time
			);
		}
	}

	cerver_inactive_check (node->left, cerver, current_time);

}

// 17/06/2020 - thread to check for inactive clients
static void *cerver_inactive_thread (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		u32 count = 0;
		while (cerver->isRunning) {
			if (count == cerver->check_inactive_interval) {
				count = 0;

				cerver_log_debug (
					"Checking for inactive clients in cerver %s...",
					cerver->info->name->str
				);

				time_t current_time = time (NULL);
				cerver_inactive_check (cerver->clients->root, cerver, current_time);

				cerver_log_debug (
					"Done checking for inactive clients in cerver %s",
					cerver->info->name->str
				);
			}

			sleep (1);
			count++;
		}
	}

	return NULL;

}

static u8 cerver_start_inactive (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_log_debug (
			"Cerver %s is set to check for inactive clients with max time of <%d> secs every <%d> secs",
			cerver->info->name->str,
			cerver->max_inactive_time,
			cerver->check_inactive_interval
		);

		if (!thread_create_detachable (
			&cerver->inactive_thread_id,
			(void *(*) (void *)) cerver_inactive_thread,
			cerver
		)) {
			cerver_log_success (
				"Created cerver %s INACTIVE thread!",
				cerver->info->name->str
			);

			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to create cerver %s INACTIVE thread!",
				cerver->info->name->str
			);
		}
	}

	return retval;

}

static u8 cerver_start_tcp (Cerver *cerver) {

	u8 retval = 1;

	switch (cerver->handler_type) {
		case CERVER_HANDLER_TYPE_NONE: {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver's %s handler type has NOT been configured!",
				cerver->info->name->str
			);
		} break;

		case CERVER_HANDLER_TYPE_POLL: {
			if (!cerver->blocking) {
				if (!listen (cerver->sock, cerver->connection_queue)) {
					// register the cerver start time
					time (&cerver->info->time_started);

					// set up the initial listening socket
					cerver->fds[cerver->current_n_fds].fd = cerver->sock;
					cerver->fds[cerver->current_n_fds].events = POLLIN;
					cerver->current_n_fds++;

					cerver_event_trigger (
						CERVER_EVENT_STARTED,
						cerver,
						NULL, NULL
					);

					retval = cerver_poll (cerver);
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_CERVER,
						"Failed to listen in cerver %s socket!",
						cerver->info->name->str
					);

					close (cerver->sock);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_POLL - socket is NOT set to non blocking!",
					cerver->info->name->str
				);
			}
		} break;

		case CERVER_HANDLER_TYPE_THREADS: {
			if (cerver->blocking) {
				if (!listen (cerver->sock, cerver->connection_queue)) {
					// register the cerver start time
					time (&cerver->info->time_started);

					cerver_event_trigger (
						CERVER_EVENT_STARTED,
						cerver,
						NULL, NULL
					);

					retval = cerver_threads (cerver);
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_CERVER,
						"Failed to listen in cerver %s socket!",
						cerver->info->name->str
					);

					close (cerver->sock);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_THREADS - socket is NOT blocking!",
					cerver->info->name->str
				);
			}
		} break;

		default: {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver's %s handler type is invalid!",
				cerver->info->name->str
			);
		} break;
	}

	return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void cerver_start_udp (Cerver *cerver) { /*** TODO: ***/ }

#pragma GCC diagnostic pop

// tell the cerver to start listening for connections and packets
// initializes cerver's structures like thpool (if any)
// and any other processes that have been configured before
// returns 0 on success, 1 on error
u8 cerver_start (Cerver *cerver) {

	u8 retval = 1;

	if (!cerver->isRunning) {
		cerver->isRunning = true;

		if (!cerver_one_time_init (cerver)) {
			u8 errors = 0;

			errors |= cerver_handlers_start (cerver);

			if (cerver->auth_required) {
				cerver_log_debug (
					"Cerver %s requires authentication",
					cerver->info->name->str
				);

				errors |= cerver_auth_start (cerver);
			}

			if (cerver->use_sessions) {
				cerver_log_debug (
					"Cerver %s supports sessions",
					cerver->info->name->str
				);
			}

			// start the admin cerver
			if (cerver->admin) {
				cerver_log_debug (
					"Cerver %s can handle admins",
					cerver->info->name->str
				);

				errors |= admin_cerver_start (cerver->admin);
			}

			if (cerver->update) {
				errors |= cerver_update_start (cerver);
			}

			if (cerver->update_interval) {
				errors |= cerver_update_interval_start (cerver);
			}

			// 17/06/2020
			// check for inactive will be handled in its own thread
			// to avoid messing with other dedicated update threads timings
			if (cerver->inactive_clients) {
				errors |= cerver_start_inactive (cerver);
			}

			if (!errors) {
				switch (cerver->protocol) {
					case PROTOCOL_TCP: {
						retval = cerver_start_tcp (cerver);
					} break;

					case PROTOCOL_UDP: {
						// retval = cerver_start_udp (cerver);
						cerver_log_warning (
							"Cerver %s - udp server is not yet implemented!",
							cerver->info->name->str
						);
					} break;

					default: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cant't start cerver %s! Unknown protocol!",
							cerver->info->name->str
						);
					}
					break;
				}
			}
		}

		else {
			cerver_log_error (
				"Failed cerver_one_time_init () for cerver %s!",
				cerver->info->name->str
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"The cerver %s is already running!",
			cerver->info->name->str
		);
	}

	return retval;

}

#pragma endregion

#pragma region update

CerverUpdate *cerver_update_new (Cerver *cerver, void *args) {

	CerverUpdate *cu = (CerverUpdate *) malloc (sizeof (CerverUpdate));
	if (cu) {
		cu->cerver = cerver;
		cu->args = args;
	}

	return cu;

}

void cerver_update_delete (void *cerver_update_ptr) {

	if (cerver_update_ptr) free (cerver_update_ptr);

}

// 31/01/2020 -- called in a dedicated thread only if a user method was set
// executes methods every tick
static void cerver_update (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update () has started!",
			cerver->info->name->str
		);
		#endif

		CerverUpdate *cu = cerver_update_new (cerver, cerver->update_args);

		u32 time_per_frame = 1000000 / cerver->update_ticks;
		// printf ("time per frame: %d\n", time_per_frame);
		u32 temp = 0;
		i32 sleep_time = 0;
		u64 delta_time = 0;

		u64 delta_ticks = 0;
		u32 fps = 0;
		struct timespec start = { 0 }, middle = { 0 }, end = { 0 };

		while (cerver->isRunning) {
			clock_gettime (CLOCK_MONOTONIC_RAW, &start);

			// do stuff
			if (cerver->update) cerver->update (cu);

			// limit the fps
			clock_gettime (CLOCK_MONOTONIC_RAW, &middle);
			temp = (middle.tv_nsec - start.tv_nsec) / 1000;
			// printf ("temp: %d\n", temp);
			sleep_time = time_per_frame - temp;
			// printf ("sleep time: %d\n", sleep_time);
			if (sleep_time > 0) {
				usleep (sleep_time);
			}

			// count fps
			clock_gettime (CLOCK_MONOTONIC_RAW, &end);
			delta_time = (end.tv_nsec - start.tv_nsec) / 1000000;
			delta_ticks += delta_time;
			fps++;
			// printf ("delta ticks: %ld\n", delta_ticks);
			if (delta_ticks >= 1000) {
				// printf ("cerver %s update fps: %i\n", cerver->info->name->str, fps);
				delta_ticks = 0;
				fps = 0;
			}
		}

		cerver_update_delete (cu);

		if (cerver->update_args) {
			if (cerver->delete_update_args) {
				cerver->delete_update_args (cerver->update_args);
			}
		}

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update () has ended!",
			cerver->info->name->str
		);
		#endif
	}

}

// 31/01/2020 -- called in a dedicated thread only if a user method was set
// executes methods every x seconds
static void cerver_update_interval (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has started!",
			cerver->info->name->str
		);
		#endif

		CerverUpdate *cu = cerver_update_new (cerver, cerver->update_interval_args);

		while (cerver->isRunning) {
			if (cerver->update_interval) cerver->update_interval (cu);

			sleep (cerver->update_interval_secs);
		}

		cerver_update_delete (cu);

		if (cerver->update_interval_args) {
			if (cerver->delete_update_interval_args) {
				cerver->delete_update_interval_args (cerver->update_interval_args);
			}
		}

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has ended!",
			cerver->info->name->str
		);
		#endif
	}

}

#pragma endregion

#pragma region end

// disable socket I/O in both ways and stop any ongoing job
// returns 0 on success, 1 on error
u8 cerver_shutdown (Cerver *cerver) {

	if (cerver->isRunning) {
		cerver->isRunning = false;

		// close the cerver socket
		if (!close (cerver->sock)) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"The cerver %s socket has been closed.",
				cerver->info->name->str
			);
			#endif

			return 0;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to close cerver %s socket!",
				cerver->info->name->str
			);
		}
	}

	return 1;

}

// get rid of any on hold connections
static void cerver_destroy_on_hold_connections (Cerver *cerver) {

	if (cerver) {
		if (cerver->auth_required) {
			// ends and deletes any on hold connection
			avl_delete (cerver->on_hold_connections);
			cerver->on_hold_connections = NULL;

			if (cerver->hold_fds) {
				free (cerver->hold_fds);
				cerver->hold_fds = NULL;
			}
		}
	}

}

// cleans up the client's structures in the current cerver
static void cerver_destroy_clients (Cerver *cerver) {

	if (cerver) {
		if (cerver->stats->current_n_connected_clients > 0) {
			// send a cerver teardown packet to all clients connected to cerver
			Packet *packet = packet_generate_request (PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_TEARDOWN, NULL, 0);
			if (packet) {
				client_broadcast_to_all_avl (cerver->clients->root, cerver, packet);
				packet_delete (packet);
			}
		}

		// destroy the sock fd client map
		htab_destroy (cerver->client_sock_fd_map);
		cerver->client_sock_fd_map = NULL;

		// this will end and delete client connections and then delete the client
		avl_delete (cerver->clients);
		cerver->clients = NULL;

		if (cerver->fds) {
			free (cerver->fds);
			cerver->fds = NULL;
		}
	}

}

// clean cerver data structures
static void cerver_clean (Cerver *cerver) {

	if (cerver) {
		switch (cerver->type) {
			case CERVER_TYPE_CUSTOM: break;

			case CERVER_TYPE_GAME: {
				if (cerver->cerver_data) {
					GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

					if (game_cerver->final_game_action)
						game_cerver->final_game_action (game_cerver->final_action_args);

					Lobby *lobby = NULL;
					for (ListElement *le = dlist_start (game_cerver->current_lobbys); le; le = le->next) {
						lobby = (Lobby *) le->data;
						lobby->running = false;
					}

					game_delete (game_cerver);      // delete game cerver data
					cerver->cerver_data = NULL;
				}
			} break;

			case CERVER_TYPE_WEB: break;

			case CERVER_TYPE_FILES: {
				if (cerver->cerver_data) {
					file_cerver_delete (cerver->cerver_data);
					cerver->cerver_data = NULL;
				}
			} break;

			default: break;
		}

		// clean up on hold connections
		cerver_destroy_on_hold_connections (cerver);

		// clean up cerver connected clients
		cerver_destroy_clients (cerver);
		#ifdef CERVER_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_CERVER, "Done cleaning up clients.");
		#endif

		// disable socket I/O in both ways and stop any ongoing job
		if (!cerver_shutdown (cerver)) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Cerver %s has been shutted down.", cerver->info->name->str
			);
			#endif
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to shutdown cerver %s!", cerver->info->name->str
			);
			#endif
		}

		// stop any app / custom handler
		if (!cerver_handlers_destroy (cerver)) {
			cerver_log_success (
				"Done destroying handlers in cerver %s",
				cerver->info->name->str
			);
		}

		if (cerver->thpool) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Cerver %s active thpool threads: %i",
				cerver->info->name->str,
				thpool_get_num_threads_working (cerver->thpool)
			);
			#endif

			thpool_destroy (cerver->thpool);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Destroyed cerver %s thpool!", cerver->info->name->str
			);
			#endif

			cerver->thpool = NULL;
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Cerver %s does NOT have a thpool to destroy",
				cerver->info->name->str
			);
			#endif
		}
	}

}

// teardown a cerver -> stop the cerver and clean all of its data
// returns 0 on success, 1 on error
u8 cerver_teardown (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Starting cerver %s teardown...", cerver->info->name->str
		);
		#endif

		cerver_event_trigger (
			CERVER_EVENT_TEARDOWN,
			cerver,
			NULL, NULL
		);

		cerver_clean (cerver);

		// correctly end admin connections & stop admin handlers
		admin_cerver_end (cerver->admin);

		// 29/05/2020
		cerver_sockets_pool_end (cerver);

		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_NONE,
			"Cerver %s teardown was successful!",
			cerver->info->name->str
		);

		cerver_delete (cerver);

		retval = 0;
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log (LOG_TYPE_ERROR, LOG_TYPE_CERVER, "Can't teardown a NULL cerver!");
		#endif
	}

	return retval;

}

#pragma endregion

#pragma region report

static CerverReport *cerver_report_new (void) {

	CerverReport *cerver_report = (CerverReport *) malloc (sizeof (Cerver));
	if (cerver_report) {
		memset (cerver_report, 0, sizeof (CerverReport));

		cerver_report->name = NULL;
	}

	return cerver_report;

}

void cerver_report_delete (void *ptr) {

	if (ptr) {
		CerverReport *cerver_report = (CerverReport *) ptr;

		str_delete (cerver_report->name);

		str_delete (cerver_report->welcome);

		free (cerver_report);
	}

}

static void cerver_report_check_info_handle_auth (
	CerverReport *cerver_report,
	Client *client, Connection *connection
) {

	if (cerver_report && connection) {
		if (cerver_report->auth_required) {
			// #ifdef CLIENT_DEBUG
			cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver requires authentication.");
			// #endif
			if (connection->auth_data) {
				#ifdef CLIENT_DEBUG
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Sending auth data to cerver...");
				#endif

				if (!connection->auth_packet) {
					if (!connection_generate_auth_packet (connection)) {
						cerver_log_success (
							"cerver_check_info () - Generated connection %s auth packet!",
							connection->name->str
						);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to generate connection %s auth packet!",
							connection->name->str
						);
					}
				}

				if (connection->auth_packet) {
					packet_set_network_values (
						connection->auth_packet,
						NULL,
						NULL,
						connection,
						NULL
					);

					if (!packet_send (connection->auth_packet, 0, NULL, false)) {
						cerver_log_success (
							"cerver_check_info () - Sent connection %s auth packet!",
							connection->name->str
						);

						client_event_trigger (CLIENT_EVENT_AUTH_SENT, client, connection);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to send connection %s auth packet!",
							connection->name->str
						);
					}
				}

				if (cerver_report->uses_sessions) {
					cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver supports sessions.");
				}
			}

			else {
				cerver_log_error (
					"Connection %s does NOT have an auth packet!",
					connection->name->str
				);
			}
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver does NOT require authentication.");
			#endif
		}
	}

}

u8 cerver_report_check_info (
	CerverReport *cerver_report,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver_report && connection) {
		connection->cerver_report = cerver_report;

		// #ifdef CLIENT_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Connected to cerver %s.", cerver_report->name->str);

		if (cerver_report->welcome) {
			cerver_log_msg ("%s", cerver_report->welcome->str);
		}

		switch (cerver_report->protocol) {
			case PROTOCOL_TCP:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver using TCP protocol.");
				break;
			case PROTOCOL_UDP:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver using UDP protocol.");
				break;

			default:
				cerver_log (LOG_TYPE_WARNING, LOG_TYPE_NONE, "Cerver using unknown protocol.");
				break;
		}
		// #endif

		if (cerver_report->use_ipv6) {
			// #ifdef CLIENT_DEBUG
			cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver is configured to use ipv6");
			// #endif
		}

		// #ifdef CLIENT_DEBUG
		switch (cerver_report->type) {
			case CERVER_TYPE_CUSTOM:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: CUSTOM");
				break;

			case CERVER_TYPE_GAME:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: GAME");
				break;
			case CERVER_TYPE_WEB:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: WEB");
				break;
			 case CERVER_TYPE_FILES:
				cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: FILES");
				break;

			default:
				cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Cerver type: UNKNOWN");
				break;
		}
		// #endif

		cerver_report_check_info_handle_auth (cerver_report, client, connection);

		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region serialization

static inline SCerver *scerver_new (void) {

	SCerver *scerver = (SCerver *) malloc (sizeof (SCerver));
	if (scerver) memset (scerver, 0, sizeof (SCerver));
	return scerver;

}

static inline void scerver_delete (void *ptr) { if (ptr) free (ptr); }

// srealizes the cerver
static SCerver *cerver_serliaze (Cerver *cerver) {

	SCerver *scerver = NULL;

	if (cerver) {
		scerver = scerver_new ();
		if (scerver) {
			scerver->type = cerver->type;

			strncpy (scerver->name, cerver->info->name->str, S_CERVER_NAME_LENGTH);

			if (cerver->info->welcome_msg)
				strncpy (scerver->welcome, cerver->info->welcome_msg->str, S_CERVER_WELCOME_LENGTH);

			scerver->use_ipv6 = cerver->use_ipv6;
			scerver->protocol = cerver->protocol;
			scerver->port = cerver->port;

			scerver->auth_required = cerver->auth_required;
			scerver->uses_sessions = cerver->use_sessions;
		}
	}

	return scerver;

}

CerverReport *cerver_deserialize (SCerver *scerver) {

	CerverReport *cerver_report = NULL;

	if (scerver) {
		cerver_report = cerver_report_new ();
		if (cerver_report) {
			cerver_report->type = scerver->type;

			cerver_report->name = str_new (scerver->name);
			if (strlen (scerver->welcome)) cerver_report->welcome = str_new (scerver->welcome);

			cerver_report->use_ipv6 = scerver->use_ipv6;
			cerver_report->protocol = scerver->protocol;
			cerver_report->port = scerver->port;

			cerver_report->auth_required = scerver->auth_required;
			cerver_report->uses_sessions = scerver->uses_sessions;
		}
	}

	return cerver_report;

}

// creates a cerver info packet ready to be sent
Packet *cerver_packet_generate (Cerver *cerver) {

	Packet *packet = NULL;

	if (cerver) {
		SCerver *scerver = cerver_serliaze (cerver);
		if (scerver) {
			packet = packet_generate_request (PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO, scerver, sizeof (SCerver));
			scerver_delete (scerver);
		}
	}

	return packet;

}

#pragma endregion