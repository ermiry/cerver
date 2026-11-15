#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/internal.h"
#include "cerver/core/start.h"
#include "cerver/core/update.h"
#include "cerver/files/cerver.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/http/core/http.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region main

Cerver *cerver_new (void) {

	Cerver *cerver = (Cerver *) malloc (sizeof (Cerver));
	if (cerver) {
		cerver->type = CERVER_TYPE_NONE;

		cerver->sock = -1;
		(void) memset (&cerver->address, 0, sizeof (struct sockaddr_storage));

		cerver->port = CERVER_DEFAULT_PORT;
		cerver->protocol = CERVER_DEFAULT_PROTOCOL;         // default protocol
		cerver->use_ipv6 = CERVER_DEFAULT_USE_IPV6;
		cerver->connection_queue = CERVER_DEFAULT_CONNECTION_QUEUE;

		(void) memset (cerver->bind_address, 0, CERVER_BIND_ADDRESS_SIZE);
		cerver->ipv6_only = CERVER_DEFAULT_IPV6_ONLY;

		cerver->running = false;
		cerver->stop_requested = false;
		cerver->blocking = true;
		cerver->reusable = CERVER_DEFAULT_REUSABLE_FLAGS;
		cerver->reusable_port = CERVER_DEFAULT_REUSABLE_PORT;
		cerver->ignore_sigpipe = CERVER_DEFAULT_IGNORE_SIGPIPE;

		cerver->cerver_data = NULL;
		cerver->delete_cerver_data = NULL;

		cerver->n_thpool_threads = CERVER_DEFAULT_POOL_THREADS;
		cerver->thpool_queue_size = CERVER_DEFAULT_THPOOL_QUEUE_SIZE;
		cerver->thpool = NULL;

		cerver->overload_policy = CERVER_DEFAULT_OVERLOAD_POLICY;

		cerver->drain_policy = CERVER_DEFAULT_DRAIN_POLICY;
		cerver->drain_timeout = CERVER_DEFAULT_DRAIN_TIMEOUT;
		cerver->drained = true;

		cerver->send_timeout = CERVER_DEFAULT_SEND_TIMEOUT;

		cerver->stop_mutex = thread_mutex_new ();
		cerver->stop_cond = thread_cond_new ();

		cerver->clients = NULL;
		cerver->client_sock_fd_map = NULL;

		cerver->inactive_clients = false;
		cerver->max_inactive_time = CERVER_DEFAULT_MAX_INACTIVE_TIME;
		cerver->check_inactive_interval = CERVER_DEFAULT_CHECK_INACTIVE_INTERVAL;
		cerver->inactive_thread_id = 0;

		cerver->handler_type = CERVER_HANDLER_TYPE_NONE;

		cerver->handle_detachable_threads = false;

		thread_group_init (&cerver->connection_threads);
		thread_group_init (&cerver->event_threads);

		cerver->udp = NULL;
		cerver->udp_peer_timeout = CERVER_UDP_DEFAULT_PEER_TIMEOUT;
		cerver->udp_max_peers = CERVER_UDP_DEFAULT_MAX_PEERS;
		cerver->udp_peers_count = 0;

		cerver->fds = NULL;
		cerver->max_n_fds = CERVER_DEFAULT_POLL_FDS;
		cerver->current_n_fds = 0;
		cerver->poll_max_connections = CERVER_DEFAULT_POLL_MAX_CONNECTIONS;
		cerver->poll_timeout = CERVER_DEFAULT_POLL_TIMEOUT;
		cerver->poll_lock = NULL;

		cerver->auth_required = CERVER_DEFAULT_AUTH_REQUIRED;
		cerver->auth_packet = NULL;
		cerver->max_auth_tries = CERVER_DEFAULT_MAX_AUTH_TRIES;
		cerver->authenticate = NULL;
		cerver->auth_schema = NULL;

		cerver->on_hold_connections = NULL;
		cerver->on_hold_connection_sock_fd_map = NULL;
		cerver->hold_fds = NULL;
		cerver->on_hold_poll_timeout = CERVER_DEFAULT_ON_HOLD_TIMEOUT;
		cerver->max_on_hold_connections = CERVER_DEFAULT_ON_HOLD_POLL_FDS;
		cerver->current_on_hold_nfds = 0;
		cerver->on_hold_poll_thread_id = 0;
		cerver->on_hold_poll_lock = NULL;
		cerver->on_hold_max_bad_packets = CERVER_DEFAULT_ON_HOLD_MAX_BAD_PACKETS;
		cerver->on_hold_check_packets = CERVER_DEFAULT_ON_HOLD_CHECK_PACKETS;
		cerver->on_hold_timeout = CERVER_DEFAULT_ON_HOLD_SECONDS;
		cerver->on_hold_receive_buffer_size = CERVER_DEFAULT_ON_HOLD_RECEIVE_BUFFER_SIZE;

		cerver->use_sessions = CERVER_DEFAULT_USE_SESSIONS;
		cerver->session_id_generator = NULL;

		cerver->handle_received_buffer = NULL;

		cerver->receive_buffer_size = CERVER_DEFAULT_RECEIVE_BUFFER_SIZE;
		cerver->max_received_packet_size = CERVER_DEFAULT_MAX_RECEIVED_PACKET_SIZE;

		cerver->app_packet_handler = NULL;
		cerver->app_error_packet_handler = NULL;
		cerver->custom_packet_handler = NULL;

		cerver->app_packet_handler_delete_packet = true;
		cerver->app_error_packet_handler_delete_packet = true;
		cerver->custom_packet_handler_delete_packet = true;

		cerver->multiple_handlers = CERVER_DEFAULT_MULTIPLE_HANDLERS;
		cerver->handlers = NULL;
		cerver->n_handlers = 0;

		for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
			cerver->request_handlers[type] = NULL;
			cerver->request_handlers_size[type] = 0;
		}

		cerver->num_handlers_alive = 0;
		cerver->num_handlers_working = 0;
		cerver->handlers_lock = NULL;
		cerver->handlers_alive_cond = NULL;

		cerver->check_packets = CERVER_DEFAULT_CHECK_PACKETS;
		cerver->max_bad_packets = CERVER_DEFAULT_MAX_BAD_PACKETS;

		cerver->update_thread_id = 0;
		cerver->update = NULL;
		cerver->update_args = NULL;
		cerver->delete_update_args = NULL;
		cerver->update_ticks = CERVER_DEFAULT_UPDATE_TICKS;

		cerver->update_interval_thread_id = 0;
		cerver->update_interval = NULL;
		cerver->update_interval_args = NULL;
		cerver->delete_update_interval_args = NULL;
		cerver->update_interval_secs = CERVER_DEFAULT_UPDATE_INTERVAL_SECONDS;

		cerver->admin = NULL;
		cerver->admin_thread_id = 0;

		for (unsigned int i = 0; i < CERVER_MAX_EVENTS; i++)
			cerver->events[i] = NULL;

		for (unsigned int i = 0; i < CERVER_MAX_ERRORS; i++)
			cerver->errors[i] = NULL;

		cerver->info = NULL;
		cerver->stats = NULL;
		cerver->request_stats = NULL;

		cerver->balancer = NULL;
	}

	return cerver;

}

void cerver_delete (void *ptr) {

	if (ptr) {
		Cerver *cerver = (Cerver *) ptr;

		if (cerver->cerver_data) {
			if (cerver->delete_cerver_data) cerver->delete_cerver_data (cerver->cerver_data);
			else free (cerver->cerver_data);
		}

		thread_mutex_delete (cerver->stop_mutex);
		thread_cond_delete (cerver->stop_cond);

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

		cerver_request_handlers_delete (cerver);

		if (cerver->handlers_lock) {
			pthread_mutex_destroy (cerver->handlers_lock);
			free (cerver->handlers_lock);
		}

		thread_cond_delete (cerver->handlers_alive_cond);

		admin_cerver_delete (cerver->admin);

		for (unsigned int i = 0; i < CERVER_MAX_EVENTS; i++)
			if (cerver->events[i]) cerver_event_delete (cerver->events[i]);

		for (unsigned int i = 0; i < CERVER_MAX_ERRORS; i++)
			if (cerver->errors[i]) cerver_error_event_delete (cerver->errors[i]);

		cerver_info_delete (cerver->info);
		cerver_stats_delete (cerver->stats);
		free (cerver->request_stats);

		thread_group_end (&cerver->connection_threads);
		thread_group_end (&cerver->event_threads);

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
	Cerver *cerver, const size_t size
) {

	if (cerver && size) cerver->receive_buffer_size = size;

}

// sets the cerver's ability to use reusable flags in sock fd
// if TRUE, this can prevent failing when trying to bind address
// the default value is CERVER_DEFAULT_REUSABLE_FLAGS
void cerver_set_reusable_address_flags (
	Cerver *cerver, bool value
) {

	if (cerver) cerver->reusable = value;

}

// sets the cerver's ability to share its port with other sockets (SO_REUSEPORT)
// the default value is CERVER_DEFAULT_REUSABLE_PORT
void cerver_set_reusable_port (
	Cerver *cerver, bool value
) {

	if (cerver) cerver->reusable_port = value;

}

// sets the literal IPv4 or IPv6 address that the cerver binds
// returns 0 on success, 1 if it is not a valid ip
unsigned int cerver_set_bind_address (
	Cerver *cerver, const char *address
) {

	unsigned int retval = 1;

	if (cerver) {
		struct sockaddr_storage parsed;

		if (!address) {
			(void) memset (cerver->bind_address, 0, CERVER_BIND_ADDRESS_SIZE);
			retval = 0;
		}

		else if (!network_parse_address (address, cerver->port, &parsed)) {
			(void) snprintf (cerver->bind_address, CERVER_BIND_ADDRESS_SIZE, "%s", address);
			cerver->use_ipv6 = (parsed.ss_family == AF_INET6);
			retval = 0;
		}

		else {
			cerver_log_error (
				"cerver_set_bind_address () - %s is not a valid ip for cerver %s",
				address, cerver->info->name
			);
		}
	}

	return retval;

}

// sets IPV6_V6ONLY in the socket of an IPv6 cerver
void cerver_set_ipv6_only (
	Cerver *cerver, bool ipv6_only
) {

	if (cerver) cerver->ipv6_only = ipv6_only;

}

// returns true while the cerver is accepting connections
bool cerver_is_running (const Cerver *cerver) {

	return cerver ? __atomic_load_n (&cerver->running, __ATOMIC_SEQ_CST) : false;

}

void cerver_set_running (Cerver *cerver, bool value) {

	__atomic_store_n (&cerver->running, value, __ATOMIC_SEQ_CST);

}

// sets whether cerver_start () ignores SIGPIPE when the process has not set a handler for it,
// writing to a connection that the client reset raises SIGPIPE, that by default kills the process
// the default value is CERVER_DEFAULT_IGNORE_SIGPIPE
void cerver_set_ignore_sigpipe (
	Cerver *cerver, bool value
) {

	if (cerver) cerver->ignore_sigpipe = value;

}

// a handler that the application installed is never replaced
void cerver_start_ignore_sigpipe (const Cerver *cerver) {

	struct sigaction current;
	(void) memset (&current, 0, sizeof (struct sigaction));

	if (
		cerver->ignore_sigpipe
		&& !sigaction (SIGPIPE, NULL, &current)
		&& (current.sa_handler == SIG_DFL)
	) {
		(void) signal (SIGPIPE, SIG_IGN);
	}

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

// sets how many accepted connections can wait in the thpool queue
// the default value is CERVER_DEFAULT_THPOOL_QUEUE_SIZE
void cerver_set_thpool_queue_size (
	Cerver *cerver, unsigned int queue_size
) {

	if (cerver) cerver->thpool_queue_size = queue_size;

}

// sets what happens with a new connection when the thpool queue is full
// the default value is CERVER_DEFAULT_OVERLOAD_POLICY
void cerver_set_overload_policy (
	Cerver *cerver, CerverOverloadPolicy policy
) {

	if (cerver && (policy > CERVER_OVERLOAD_POLICY_NONE) && (policy < CERVER_OVERLOAD_POLICY_MAX)) {
		cerver->overload_policy = policy;
	}

}

// sets what happens with the queued connections when the cerver is drained
// the default value is CERVER_DEFAULT_DRAIN_POLICY
void cerver_set_drain_policy (
	Cerver *cerver, CerverDrainPolicy policy
) {

	if (cerver && (policy > CERVER_DRAIN_POLICY_NONE) && (policy < CERVER_DRAIN_POLICY_MAX)) {
		cerver->drain_policy = policy;
	}

}

// sets how many secs the cerver waits for its connections when it is drained
// the default value is CERVER_DEFAULT_DRAIN_TIMEOUT
void cerver_set_drain_timeout (
	Cerver *cerver, u32 drain_timeout
) {

	if (cerver) cerver->drain_timeout = drain_timeout;

}

// sets the SO_SNDTIMEO in secs for every accepted connection
// the default value is CERVER_DEFAULT_SEND_TIMEOUT
void cerver_set_send_timeout (
	Cerver *cerver, u32 send_timeout
) {

	if (cerver) cerver->send_timeout = send_timeout;

}

// DEPRECATED: the sockets pool was removed,
// every connection now owns its socket
void cerver_set_sockets_pool_init (
	Cerver *cerver, unsigned int n_sockets
) {

	(void) cerver;
	(void) n_sockets;

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
		cerver->max_inactive_time = max_inactive_time ? max_inactive_time : CERVER_DEFAULT_MAX_INACTIVE_TIME;
		cerver->check_inactive_interval = check_inactive_interval ? check_inactive_interval : CERVER_DEFAULT_CHECK_INACTIVE_INTERVAL;
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

// sets, before the cerver starts, the initial size of the main poll array
// and the max connections that it handles, 0 is no limit
// returns 0 on success, 1 on error
u8 cerver_set_poll_fds (
	Cerver *cerver, const u32 initial, const u32 max_connections
) {

	u8 retval = 1;

	if (cerver && !cerver->fds && (initial >= CERVER_POLL_MIN_FDS)) {
		cerver->max_n_fds = initial;
		cerver->poll_max_connections = max_connections;

		retval = 0;
	}

	return retval;

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

// sets the schema of the credentials of the clients,
// the credentials are decoded and validated with it before the authenticate method gets them,
// and its auth data is the object; a packet that is not an object of the schema fails the auth
// the fields of the schema can not take memory from an arena
void cerver_set_auth_schema (
	Cerver *cerver, const SerializerSchema *schema
) {

	if (cerver) cerver->auth_schema = schema;

}

// sets the cerver on poll timeout in ms
void cerver_set_on_hold_poll_timeout (
	Cerver *cerver, u32 on_hold_poll_timeout
) {

	if (cerver) cerver->on_hold_poll_timeout = on_hold_poll_timeout;

}

// sets, before the cerver starts, the connections that the on hold poll handles at the same time
// returns 0 on success, 1 on error
u8 cerver_set_on_hold_poll_fds (
	Cerver *cerver, const u32 max_connections
) {

	u8 retval = 1;

	if (cerver && !cerver->hold_fds && max_connections) {
		cerver->max_on_hold_connections = max_connections;

		retval = 0;
	}

	return retval;

}

void cerver_set_on_hold_timeout (
	Cerver *cerver, u32 on_hold_timeout
) {

	if (cerver) cerver->on_hold_timeout = on_hold_timeout;

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
void cerver_set_on_hold_check_packets (
	Cerver *cerver, bool check
) {

	if (cerver) cerver->on_hold_check_packets = check;

}

// sets the size of the buffer to be allocated
// to receive packets from on hold connections
void cerver_set_on_hold_receive_buffer_size (
	Cerver *cerver, const size_t on_hold_receive_buffer_size
) {

	if (cerver && on_hold_receive_buffer_size) {
		cerver->on_hold_receive_buffer_size = on_hold_receive_buffer_size;
	}

}

// configures the cerver to use client sessions
// retuns 0 on success, 1 on error
u8 cerver_set_sessions (
	Cerver *cerver, void *(*session_id_generator) (const void *)
) {

	u8 retval = 1;

	// without a generator, the default one gives 32 random bytes of the kernel in hex characters
	if (cerver) {
		cerver->session_id_generator = session_id_generator ?
			session_id_generator : session_default_generate_id;

		cerver->use_sessions = true;

		retval = 0;
	}

	return retval;

}

// sets a custom method to handle the raw received buffer from the socker
void cerver_set_handle_recieved_buffer (
	Cerver *cerver, Action handle_received_buffer
) {

	if (cerver) cerver->handle_received_buffer = handle_received_buffer;

}

// only handle packets with size <= max_received_packet_size
// if the packet is bigger it will be considered a bad packet 
void cerver_set_max_received_packet_size (
	Cerver *cerver, size_t max_received_packet_size
) {

	if (cerver && max_received_packet_size) cerver->max_received_packet_size = max_received_packet_size;

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

	if (cerver && n_handlers && !cerver->handlers) {
		cerver->handlers = (Handler **) calloc (n_handlers, sizeof (Handler *));
		if (cerver->handlers) {
			cerver->multiple_handlers = true;
			cerver->n_handlers = n_handlers;

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

void cerver_set_max_bad_packets (
	Cerver *cerver, u8 max_bad_packets
) {

	if (cerver) cerver->max_bad_packets = max_bad_packets;

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


#pragma region create

// returns a new cerver with the specified parameters
Cerver *cerver_create (
	const CerverType type, const char *name,
	const u16 port, const Protocol protocol, bool use_ipv6,
	const u16 connection_queue
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
					cerver->cerver_data = http_cerver_create (cerver);
					cerver->delete_cerver_data = http_cerver_delete;
				} break;

				case CERVER_TYPE_FILES: {
					cerver->cerver_data = file_cerver_create (cerver);
					cerver->delete_cerver_data = file_cerver_delete;
				} break;

				default: break;
			}

			cerver_set_handle_recieved_buffer (cerver, cerver_receive_handle_buffer);

			cerver->info = cerver_info_new ();
			cerver_set_name (cerver, name);

			cerver->stats = cerver_stats_new ();
			cerver->request_stats = cerver_request_stats_new ();
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

// creates a new cerver of type CERVER_TYPE_WEB
Cerver *cerver_create_web (
	const char *name, const u16 port, const u16 connection_queue
) {

	return cerver_create (
		CERVER_TYPE_WEB,
		name, port, PROTOCOL_TCP, false,
		connection_queue
	);

}

#pragma endregion
