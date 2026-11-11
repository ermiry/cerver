#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/config.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>

#include <sys/poll.h>

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
#include "cerver/udp.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/thpool.h"

#include "cerver/game/game.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the inactive clients of one check that are logged by their id, the rest only by their count
#define CERVER_INACTIVE_LOG_BATCH		256

// the first room for the sockets of a check that are shut down after the clients lock, it doubles
#define CERVER_INACTIVE_SHUTDOWN_BATCH	256

// the packet types in the request stats and handlers
#define CERVER_REQUEST_APP				0
#define CERVER_REQUEST_APP_ERROR		1
#define CERVER_REQUEST_CUSTOM			2

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

const char *cerver_overload_policy_to_string (CerverOverloadPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_OVERLOAD_POLICY_##name: return #string;
		CERVER_OVERLOAD_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_overload_policy_to_string (CERVER_OVERLOAD_POLICY_NONE);

}

const char *cerver_overload_policy_description (CerverOverloadPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_OVERLOAD_POLICY_##name: return #description;
		CERVER_OVERLOAD_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_overload_policy_description (CERVER_OVERLOAD_POLICY_NONE);

}

const char *cerver_drain_policy_to_string (CerverDrainPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_DRAIN_POLICY_##name: return #string;
		CERVER_DRAIN_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_drain_policy_to_string (CERVER_DRAIN_POLICY_NONE);

}

const char *cerver_drain_policy_description (CerverDrainPolicy policy) {

	switch (policy) {
		#define XX(num, name, string, description) case CERVER_DRAIN_POLICY_##name: return #description;
		CERVER_DRAIN_POLICY_MAP(XX)
		#undef XX

		default: break;
	}

	return cerver_drain_policy_description (CERVER_DRAIN_POLICY_NONE);

}

#pragma endregion

#pragma region info

static CerverInfo *cerver_info_new (void) {

	CerverInfo *cerver_info = (CerverInfo *) malloc (sizeof (CerverInfo));
	if (cerver_info) {
		(void) memset (cerver_info, 0, sizeof (CerverInfo));

		(void) strncpy (
			cerver_info->alias,
			CERVER_DEFAULT_ALIAS,
			CERVER_INFO_ALIAS_SIZE - 1
		);

		cerver_info->alias_len = strlen (cerver_info->alias);

		cerver_info->cerver_info_packet = NULL;
	}

	return cerver_info;

}

static void cerver_info_delete (CerverInfo *cerver_info) {

	if (cerver_info) {
		packet_delete (cerver_info->cerver_info_packet);

		free (cerver_info);
	}

}

static void cerver_set_name (Cerver *cerver, const char *name) {

	(void) strncpy (
		cerver->info->name, name, CERVER_INFO_NAME_SIZE - 1
	);

	cerver->info->name_len = strlen (cerver->info->name);

}

// sets the cerver's alias
// to be used primarily to handle cerver's related threads names
// as they must not exceed a certain size
void cerver_set_alias (Cerver *cerver, const char *alias) {

	if (cerver && alias) {
		(void) strncpy (
			cerver->info->alias, alias, CERVER_INFO_ALIAS_SIZE - 1
		);

		cerver->info->alias_len = strlen (cerver->info->alias);
	}

}

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg) {

	u8 retval = 1;

	if (cerver) {
		if (cerver->info) {
			(void) strncpy (
				cerver->info->welcome, msg, CERVER_INFO_WELCOME_SIZE - 1
			);

			cerver->info->welcome_len = strlen (cerver->info->welcome);

			retval = 0;
		}
	}

	return retval;

}

void cerver_update_uptime (Cerver *cerver) {

	if (cerver) {
		cerver->info->uptime = time (NULL) - cerver->info->time_started;
	}

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

		if (!packet_send (
			cerver->info->cerver_info_packet, 0, NULL, false
		)) {
			retval = 0;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_PACKET,
				"Failed to send cerver %s info packet!",
				cerver->info->name
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
		(void) memset (cerver_stats, 0, sizeof (CerverStats));
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

static RequestStats (*cerver_request_stats_new (void))[CERVER_REQUEST_STATS_ENTRIES] {

	RequestStats (*request_stats)[CERVER_REQUEST_STATS_ENTRIES] = (RequestStats (*)[CERVER_REQUEST_STATS_ENTRIES]) malloc (
		sizeof (RequestStats) * CERVER_REQUEST_PACKET_TYPES * CERVER_REQUEST_STATS_ENTRIES
	);

	if (request_stats) {
		for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
			for (unsigned int entry = 0; entry < CERVER_REQUEST_STATS_ENTRIES; entry++) {
				histogram_init (&request_stats[type][entry].durations);
				histogram_init (&request_stats[type][entry].sizes);
			}
		}
	}

	return request_stats;

}

// returns the index of the packet type in the request stats and handlers, or CERVER_REQUEST_PACKET_TYPES
static unsigned int cerver_request_packet_type (const PacketType packet_type) {

	unsigned int index = CERVER_REQUEST_PACKET_TYPES;

	switch (packet_type) {
		case PACKET_TYPE_APP: index = CERVER_REQUEST_APP; break;
		case PACKET_TYPE_APP_ERROR: index = CERVER_REQUEST_APP_ERROR; break;
		case PACKET_TYPE_CUSTOM: index = CERVER_REQUEST_CUSTOM; break;

		default: break;
	}

	return index;

}

static const char *cerver_request_stats_packet_type_name (const unsigned int index) {

	static const char *names[CERVER_REQUEST_PACKET_TYPES] = {
		"App", "App error", "Custom"
	};

	return names[index];

}

static unsigned int cerver_request_stats_entry (const u32 request_type) {

	return (request_type < CERVER_REQUEST_STATS_SIZE) ? request_type : CERVER_REQUEST_STATS_SIZE;

}

// adds a packet that a handler of the cerver handled to the stats of its request type
void cerver_request_stats_add (
	Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	const u64 size, const u64 microseconds
) {

	const unsigned int type = cerver_request_packet_type (packet_type);
	if (cerver && cerver->request_stats && (type < CERVER_REQUEST_PACKET_TYPES)) {
		RequestStats *stats = &cerver->request_stats[type][cerver_request_stats_entry (request_type)];

		histogram_add (&stats->durations, microseconds);
		histogram_add (&stats->sizes, size);
	}

}

// copies the stats of a request type of PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR or PACKET_TYPE_CUSTOM
// returns 0 on success, 1 on error
u8 cerver_get_request_stats (
	const Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	RequestStats *stats
) {

	u8 retval = 1;

	const unsigned int type = cerver_request_packet_type (packet_type);
	if (cerver && cerver->request_stats && stats && (type < CERVER_REQUEST_PACKET_TYPES)) {
		const RequestStats *source = &cerver->request_stats[type][cerver_request_stats_entry (request_type)];

		histogram_copy (&stats->durations, &source->durations);
		histogram_copy (&stats->sizes, &source->sizes);

		retval = 0;
	}

	return retval;

}

static void cerver_request_stats_print_entry (
	const unsigned int type, const unsigned int entry, const RequestStats *source
) {

	RequestStats stats;
	histogram_copy (&stats.durations, &source->durations);
	histogram_copy (&stats.sizes, &source->sizes);

	if (stats.durations.count) {
		if (entry < CERVER_REQUEST_STATS_SIZE) {
			cerver_log_msg ("\n%s request %u:", cerver_request_stats_packet_type_name (type), entry);
		}

		else {
			cerver_log_msg (
				"\n%s requests from %u:", cerver_request_stats_packet_type_name (type), entry
			);
		}

		histogram_print ("Durations", "us", &stats.durations);
		histogram_print ("Sizes", "bytes", &stats.sizes);
	}

}

// prints the stats of the request types that have packets
void cerver_request_stats_print (const Cerver *cerver) {

	if (cerver && cerver->request_stats) {
		for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
			for (unsigned int entry = 0; entry < CERVER_REQUEST_STATS_ENTRIES; entry++) {
				cerver_request_stats_print_entry (type, entry, &cerver->request_stats[type][entry]);
			}
		}
	}

}

static void cerver_stats_print_thpool (Cerver *cerver) {

	if (cerver->thpool) {
		ThpoolStats thpool_stats;
		thpool_get_stats (cerver->thpool, &thpool_stats);

		cerver_log_msg ("\n");
		thpool_stats_print (&thpool_stats);
	}

}

void cerver_stats_print (Cerver *cerver, bool received, bool sent) {

	if (cerver) {
		if (cerver->stats) {
			cerver_log_msg ("\nCerver's %s stats:\n", cerver->info->name);
			cerver_log_msg ("Threshold time:                %ld\n", cerver->stats->threshold_time);

			CerverStats *stats = cerver->stats;

			if (cerver->auth_required) {
				cerver_log_msg ("Client packets received:       %lu", CERVER_STATS_GET (stats->client_n_packets_received));
				cerver_log_msg ("Client receives done:          %lu", CERVER_STATS_GET (stats->client_receives_done));
				cerver_log_msg ("Client bytes received:         %lu\n", CERVER_STATS_GET (stats->client_bytes_received));

				cerver_log_msg ("On hold packets received:      %lu", CERVER_STATS_GET (stats->on_hold_n_packets_received));
				cerver_log_msg ("On hold receives done:         %lu", CERVER_STATS_GET (stats->on_hold_receives_done));
				cerver_log_msg ("On hold bytes received:        %lu\n", CERVER_STATS_GET (stats->on_hold_bytes_received));
			}

			cerver_log_msg ("Total packets received:        %lu", CERVER_STATS_GET (stats->total_n_packets_received));
			cerver_log_msg ("Total receives done:           %lu", CERVER_STATS_GET (stats->total_n_receives_done));
			cerver_log_msg ("Total bytes received:          %lu\n", CERVER_STATS_GET (stats->total_bytes_received));

			cerver_log_msg ("N packets sent:                %lu", CERVER_STATS_GET (stats->n_packets_sent));
			cerver_log_msg ("Total bytes sent:              %lu\n", CERVER_STATS_GET (stats->total_bytes_sent));

			cerver_log_msg ("Current active client connections:         %lu", CERVER_STATS_GET (stats->current_active_client_connections));
			cerver_log_msg ("Current connected clients:                 %lu", CERVER_STATS_GET (stats->current_n_connected_clients));
			cerver_log_msg ("Current on hold connections:               %lu", CERVER_STATS_GET (stats->current_n_hold_connections));
			cerver_log_msg ("Total on hold connections:                 %lu", CERVER_STATS_GET (stats->total_on_hold_connections));
			cerver_log_msg ("Total clients:                             %lu", CERVER_STATS_GET (stats->total_n_clients));
			cerver_log_msg ("Unique clients:                            %lu", CERVER_STATS_GET (stats->unique_clients));
			cerver_log_msg ("Total client connections:                  %lu", CERVER_STATS_GET (stats->total_client_connections));

			cerver_stats_print_thpool (cerver);

			cerver_request_stats_print (cerver);

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
				cerver->info->name
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

#pragma region requests

static void cerver_request_handlers_delete (Cerver *cerver) {

	for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
		for (u32 request = 0; request < cerver->request_handlers_size[type]; request++) {
			handler_delete (cerver->request_handlers[type][request]);
		}

		free (cerver->request_handlers[type]);
		cerver->request_handlers[type] = NULL;
		cerver->request_handlers_size[type] = 0;
	}

}

// grows the handlers of a packet type to have the request type, the new ones without a handler
// returns 0 on success, 1 on error
static u8 cerver_request_handlers_grow (
	Cerver *cerver, const unsigned int type, const u32 request_type
) {

	u8 retval = 1;

	const u32 size = request_type + 1;
	Handler **handlers = (Handler **) realloc (
		cerver->request_handlers[type], sizeof (Handler *) * size
	);

	if (handlers) {
		for (u32 request = cerver->request_handlers_size[type]; request < size; request++) {
			handlers[request] = NULL;
		}

		cerver->request_handlers[type] = handlers;
		cerver->request_handlers_size[type] = size;

		retval = 0;
	}

	return retval;

}

// sets, before the cerver starts, the handler of the packets of a request type
// returns 0 on success, 1 on error or if the request type already has a handler
u8 cerver_set_request_handler (
	Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	Handler *handler
) {

	u8 retval = 1;

	const unsigned int type = cerver_request_packet_type (packet_type);
	if (
		cerver && handler && !cerver_is_running (cerver)
		&& (type < CERVER_REQUEST_PACKET_TYPES) && (request_type < CERVER_REQUEST_HANDLERS_MAX)
		&& !cerver_get_request_handler (cerver, packet_type, request_type)
	) {
		if (
			(request_type < cerver->request_handlers_size[type])
			|| !cerver_request_handlers_grow (cerver, type, request_type)
		) {
			handler->type = HANDLER_TYPE_CERVER;
			handler->cerver = cerver;

			cerver->request_handlers[type][request_type] = handler;

			retval = 0;
		}
	}

	return retval;

}

// returns the handler of the request type of a packet type, NULL if it does not have one
Handler *cerver_get_request_handler (
	const Cerver *cerver,
	const PacketType packet_type, const u32 request_type
) {

	Handler *handler = NULL;

	const unsigned int type = cerver_request_packet_type (packet_type);
	if (
		cerver && (type < CERVER_REQUEST_PACKET_TYPES)
		&& (request_type < cerver->request_handlers_size[type])
	) {
		handler = cerver->request_handlers[type][request_type];
	}

	return handler;

}

// the handlers that are not direct get their own threads
// returns 0 on success, 1 on error
static u8 cerver_request_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
		for (u32 request = 0; request < cerver->request_handlers_size[type]; request++) {
			Handler *handler = cerver->request_handlers[type][request];
			if (handler && !handler->direct_handle && handler_start (handler)) {
				cerver_log_error (
					"Failed to start cerver %s handler of the request %u!",
					cerver->info->name, request
				);

				errors = 1;
			}
		}
	}

	return errors;

}

static void cerver_request_handlers_stop (Cerver *cerver) {

	for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
		for (u32 request = 0; request < cerver->request_handlers_size[type]; request++) {
			handler_stop (cerver->request_handlers[type][request]);
		}
	}

}

#pragma endregion

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

	if (cerver) cerver->receive_buffer_size = size;

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

static void cerver_set_running (Cerver *cerver, bool value) {

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
static void cerver_start_ignore_sigpipe (const Cerver *cerver) {

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

	if (cerver) {
		cerver->on_hold_receive_buffer_size = on_hold_receive_buffer_size;
	}

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

// only handle packets with size <= max_received_packet_size
// if the packet is bigger it will be considered a bad packet 
void cerver_set_max_received_packet_size (
	Cerver *cerver, size_t max_received_packet_size
) {

	if (cerver) cerver->max_received_packet_size = max_received_packet_size;

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

#pragma region handlers

// prints info about current handlers
void cerver_handlers_print_info (Cerver *cerver) {

	if (cerver) {
		cerver_log_debug (
			"%u - current ACTIVE handlers in cerver %s",
			cerver_get_n_handlers_alive (cerver), cerver->info->name
		);

		cerver_log_debug (
			"%u - current WORKING handlers in cerver %s",
			cerver_get_n_handlers_working (cerver), cerver->info->name
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
		(void) pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->n_handlers;
		(void) pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently alive (ready to handle packets)
unsigned int cerver_get_n_handlers_alive (Cerver *cerver) {

	unsigned int retval = 0;

	// a cerver that never started its handlers does not have the lock
	if (cerver && cerver->handlers_lock) {
		(void) pthread_mutex_lock (cerver->handlers_lock);
		retval = cerver->num_handlers_alive;
		(void) pthread_mutex_unlock (cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently working (handling a packet)
unsigned int cerver_get_n_handlers_working (Cerver *cerver) {

	unsigned int retval = 0;

	// the handlers count themselves with __atomic while they handle a packet
	if (cerver) {
		retval = __atomic_load_n (&cerver->num_handlers_working, __ATOMIC_SEQ_CST);
	}

	return retval;

}

// joins the multiple app handlers threads, if any
static void cerver_multiple_app_handlers_destroy (Cerver *cerver) {

	if (cerver->handlers) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Stopping multiple app handlers in cerver %s...",
			cerver->info->name
		);

		cerver_handlers_print_info (cerver);
		#endif

		for (unsigned int i = 0; i < cerver->n_handlers; i++) {
			handler_stop (cerver->handlers[i]);
		}
	}

}

// 27/05/2020
// correctly destroy cerver's custom handlers,
// each thread is woken and joined, a handler that never started is skipped
static u8 cerver_handlers_destroy (Cerver *cerver) {

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Stopping handlers in cerver %s...",
			cerver->info->name
		);
		#endif

		if (cerver->multiple_handlers) {
			cerver_multiple_app_handlers_destroy (cerver);
		}

		handler_stop (cerver->app_packet_handler);

		handler_stop (cerver->app_error_packet_handler);

		handler_stop (cerver->custom_packet_handler);

		cerver_request_handlers_stop (cerver);
	}

	return 0;

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

				case CERVER_TYPE_WEB: break;

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

#pragma endregion

#pragma region init

// every address of the cerver's family
static void cerver_network_init_address_any (Cerver *cerver) {

	(void) memset (&cerver->address, 0, sizeof (struct sockaddr_storage));

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

}

// the address of cerver_set_bind_address (), or every address of the cerver's family
// returns 0 on success, 1 on error
static u8 cerver_network_init_address_bind (Cerver *cerver) {

	u8 retval = 0;

	if (cerver->bind_address[0]) {
		retval = (u8) network_parse_address (cerver->bind_address, cerver->port, &cerver->address);
	}

	else {
		cerver_network_init_address_any (cerver);
	}

	return retval;

}

// an IPv6 socket always sets IPV6_V6ONLY,
// so the cerver does not depend on the net.ipv6.bindv6only sysctl
static void cerver_network_init_ipv6_only (const Cerver *cerver) {

	if (cerver->use_ipv6 && sock_set_ipv6_only (cerver->sock, cerver->ipv6_only)) {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"Failed to set cerver %s IPV6_V6ONLY to %d",
			cerver->info->name, cerver->ipv6_only
		);
	}

}

// inits the cerver's address & binds the socket to it
static u8 cerver_network_init_address (Cerver *cerver) {

	u8 retval = 1;

	if (cerver_network_init_address_bind (cerver)) {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Cerver %s bind address %s is not valid!",
			cerver->info->name, cerver->bind_address
		);

		close (cerver->sock);

		return retval;
	}

	cerver_network_init_ipv6_only (cerver);

	if (cerver->reusable) {
		if (sock_set_reusable (cerver->sock)) {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Failed to set cerver's sock fd reusable falgs"
			);
		}
	}

	if (cerver->reusable_port) {
		if (sock_set_reusable_port (cerver->sock)) {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Failed to set cerver's sock fd reusable port flag"
			);
		}
	}

	if (!bind (
		cerver->sock,
		(const struct sockaddr *) &cerver->address,
		sock_address_size (&cerver->address))
	) {
		retval = 0;       // success!!
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to bind cerver %s socket!",
			cerver->info->name
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
			if (sock_set_blocking (cerver->sock, false)) {
				cerver->blocking = false;
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
					"Cerver %s socket set to non blocking mode.",
					cerver->info->name
				);
				#endif
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to set cerver %s socket to non blocking mode!",
					cerver->info->name
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

// creates the socket with the selected protocol
static i32 cerver_network_create_socket (const Cerver *cerver) {

	i32 sock = -1;

	switch (cerver->protocol) {
		case IPPROTO_TCP: {
			sock = socket (
				(cerver->use_ipv6 ? AF_INET6 : AF_INET),
				SOCK_STREAM, 0
			);
		} break;

		case IPPROTO_UDP: {
			sock = socket (
				(cerver->use_ipv6 ? AF_INET6 : AF_INET),
				SOCK_DGRAM, 0
			);
		} break;

		default:
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Unknown protocol type!"
			);
			break;
	}

	return sock;

}

// a bind address decides the family of the socket,
// even if cerver_set_network_values () changed use_ipv6 after cerver_set_bind_address ()
static void cerver_network_init_family (Cerver *cerver) {

	struct sockaddr_storage parsed;
	if (
		cerver->bind_address[0]
		&& !network_parse_address (cerver->bind_address, cerver->port, &parsed)
	) {
		cerver->use_ipv6 = (parsed.ss_family == AF_INET6);
	}

}

// inits the cerver networking capabilities
// the socket is stored with __atomic, cerver_stop_request () can read it from another thread
static u8 cerver_network_init (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_network_init_family (cerver);

		__atomic_store_n (&cerver->sock, cerver_network_create_socket (cerver), __ATOMIC_SEQ_CST);

		if (cerver->sock >= 0) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Created cerver %s socket - <%d>!",
				cerver->info->name,
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
				"Failed to create cerver %s socket!",
				cerver->info->name
			);
		}
	}

	return retval;

}

static u8 cerver_init_poll_fds (Cerver *cerver) {

	u8 retval = 1;

	// max_n_fds is the initial size, from cerver_set_poll_fds ()
	cerver->fds = (struct pollfd *) calloc (cerver->max_n_fds, sizeof (struct pollfd));
	if (cerver->fds) {
		// set all fds as available spaces
		for (u32 i = 0; i < cerver->max_n_fds; i++) cerver->fds[i].fd = -1;

		cerver->current_n_fds = 0;

		retval = 0;     // success!!
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to allocate cerver %s main fds!", cerver->info->name
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
			cerver->client_sock_fd_map = htab_create (cerver->max_n_fds, NULL, NULL);
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
					cerver->info->name
				);
				#endif
			}
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init clients avl in cerver %s",
				cerver->info->name
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
			"Initializing cerver %s...", cerver->info->name
		);

		cerver_log_msg ("Cerver type: %s", cerver_type_to_string (cerver->type));
		cerver_log_msg ("Cerver handler type: %s", cerver_handler_type_to_string (cerver->handler_type));

		if (!cerver_network_init (cerver)) {
			if (!cerver_init_data_structures (cerver)) {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
					"Done initializing cerver %s network values & data structures!",
					cerver->info->name
				);
				#endif

				retval = 0;     // success!!
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to init cerver %s data structures!",
					cerver->info->name
				);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s network!", cerver->info->name
			);
		}
	}

	return retval;

}

// only the threads handler type takes connections with the thpool,
// the poll () handles every connection in its own thread
static inline bool cerver_uses_thpool (const Cerver *cerver) {

	return (cerver->handler_type == CERVER_HANDLER_TYPE_THREADS) && (cerver->n_thpool_threads > 0);

}

static void cerver_one_time_init_thpool_unused (const Cerver *cerver) {

	if (cerver->n_thpool_threads > 0) {
		cerver_log_msg (
			"Cerver %s does not create its thpool of %u threads with the %s handler type",
			cerver->info->name, cerver->n_thpool_threads,
			cerver_handler_type_to_string (cerver->handler_type)
		);
	}

}

static u8 cerver_one_time_init_thpool (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver_uses_thpool (cerver)) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Cerver %s is configured to use a thpool with %d threads",
				cerver->info->name, cerver->n_thpool_threads
			);
			#endif

			cerver->thpool = thpool_create (cerver->n_thpool_threads);
			if (cerver->thpool) {
				thpool_set_name (cerver->thpool, cerver->info->name);

				if (cerver->thpool_queue_size) {
					errors |= thpool_set_queue_size (cerver->thpool, cerver->thpool_queue_size);
				}

				errors |= thpool_init (cerver->thpool);
			}

			else {
				errors = 1;
			}

			if (errors) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to init cerver %s thpool!", cerver->info->name
				);
			}

			else {
				cerver_log_msg (
					"Cerver %s thpool: %u threads - %u queue size - %s overload policy",
					cerver->info->name,
					cerver->n_thpool_threads,
					thpool_get_queue_size (cerver->thpool),
					cerver_overload_policy_to_string (cerver->overload_policy)
				);
			}
		}

		else {
			cerver_one_time_init_thpool_unused (cerver);
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
				"Initialized cerver %s!", cerver->info->name
			);

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
							cerver->info->name
						);
					}
				} break;

				case CERVER_TYPE_WEB: break;

				case CERVER_TYPE_FILES: break;

				default: break;
			}

			cerver->info->cerver_info_packet = cerver_packet_generate (cerver);
			if (!cerver->info->cerver_info_packet) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to generate cerver %s info packet",
					cerver->info->name
				);

				errors |= 1;
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s!", cerver->info->name
			);

			errors |= 1;
		}
	}

	return errors;

}

#pragma endregion

#pragma region start

static void *cerver_update (void *args);

static void *cerver_update_interval (void *args);

// the thread that receives the packets of the on hold connections
// joined in cerver_stop_threads () before its fds are released
static void cerver_auth_start_poll (Cerver *cerver) {

	if (thread_create_joinable (&cerver->on_hold_poll_thread_id, on_hold_poll, cerver)) {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_NONE,
			"Failed to create cerver's %s on_hold_poll () thread!",
			cerver->info->name
		);
	}

}

// inits cerver's auth capabilities
static u8 cerver_auth_start (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->auth_packet = packet_generate_request (PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_REQUEST_AUTH, NULL, 0);

		cerver->on_hold_connections = avl_init (connection_comparator, connection_delete);
		cerver->on_hold_connection_sock_fd_map = htab_create (cerver->max_on_hold_connections, NULL, NULL);
		if (cerver->on_hold_connections && cerver->on_hold_connection_sock_fd_map) {
			cerver->hold_fds = (struct pollfd *) calloc (cerver->max_on_hold_connections, sizeof (struct pollfd));
			if (cerver->hold_fds) {
				memset (cerver->hold_fds, 0, sizeof (struct pollfd) * cerver->max_on_hold_connections);

				for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
					cerver->hold_fds[i].fd = -1;

				cerver->current_on_hold_nfds = 0;

				cerver->on_hold_poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
				pthread_mutex_init (cerver->on_hold_poll_lock, NULL);

				// the peers of an UDP cerver authenticate in its receive loop (udp.c)
				if (cerver->protocol == PROTOCOL_TCP) {
					cerver_auth_start_poll (cerver);
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
			cerver->info->name
		);
		#endif

		// only the handlers that are not direct have a thread
		unsigned int started = 0;
		for (unsigned int i = 0; i < cerver->n_handlers; i++) {
			if (cerver->handlers[i] && !cerver->handlers[i]->direct_handle) {
				errors |= handler_start (cerver->handlers[i]);
				started += 1;
			}
		}

		if (!errors) {
			// wait for all handlers to be initialized,
			// they mark themselves alive with the handlers lock and signal its condition
			(void) pthread_mutex_lock (cerver->handlers_lock);
			while (cerver->num_handlers_alive < started) {
				(void) pthread_cond_wait (cerver->handlers_alive_cond, cerver->handlers_lock);
			}
			(void) pthread_mutex_unlock (cerver->handlers_lock);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Cerver %s multiple app handlers are ready!",
				cerver->info->name
			);
			#endif
		}

		else {
			cerver_log_error (
				"Failed to init ALL multiple app handlers in cerver %s",
				cerver->info->name
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
							cerver->info->name
						);
						#endif
					}

					else {
						cerver_log_error (
							"Failed to start cerver %s app_packet_handler!",
							cerver->info->name
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
							cerver->info->name
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
						cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s app_error_packet_handler!",
						cerver->info->name
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
						cerver->info->name
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
						cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s custom_packet_handler!",
						cerver->info->name
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
						cerver->info->name
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
			cerver->info->name
		);
		#endif

		cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (cerver->handlers_lock, NULL);

		cerver->handlers_alive_cond = thread_cond_new ();

		errors |= cerver_app_handlers_start (cerver);

		errors |= cerver_app_error_handler_start (cerver);

		errors |= cerver_custom_handler_start (cerver);

		errors |= cerver_request_handlers_start (cerver);

		if (!errors) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Done initializing %s handlers!",
				cerver->info->name
			);
			#endif
		}
	}

	return errors;

}

static u8 cerver_update_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_joinable (
		&cerver->update_thread_id,
		cerver_update,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE thread!",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE thread!",
			cerver->info->name
		);
	}

	return retval;

}

static u8 cerver_update_interval_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_joinable (
		&cerver->update_interval_thread_id,
		cerver_update_interval,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE INTERVAL thread!",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE INTERVAL thread!",
			cerver->info->name
		);
	}

	return retval;

}

// sleeps the seconds or until the cerver is stopped
void cerver_sleep (Cerver *cerver, u32 seconds) {

	struct timespec deadline = CERVER_ZERO_INITIALIZER;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += seconds;

	(void) pthread_mutex_lock (cerver->stop_mutex);

	int result = 0;
	while (cerver_is_running (cerver) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (cerver->stop_cond, cerver->stop_mutex, &deadline);
	}

	(void) pthread_mutex_unlock (cerver->stop_mutex);

}

// the ids of the clients that a check dropped, they are logged after the clients lock is released,
// and a reference to the socket of each of their connections, that are shut down after it
typedef struct CerverInactiveDropped {

	u64 ids[CERVER_INACTIVE_LOG_BATCH];
	unsigned int count;
	unsigned int total;

	Socket **sockets;
	size_t sockets_count;
	size_t sockets_size;

} CerverInactiveDropped;

// returns true if there is room for one more socket
static bool cerver_inactive_sockets_room (CerverInactiveDropped *dropped) {

	bool room = (dropped->sockets_count < dropped->sockets_size);
	if (!room) {
		const size_t size = dropped->sockets_size ?
			(dropped->sockets_size << 1) : CERVER_INACTIVE_SHUTDOWN_BATCH;

		Socket **sockets = (Socket **) realloc (dropped->sockets, size * sizeof (Socket *));
		if (sockets) {
			dropped->sockets = sockets;
			dropped->sockets_size = size;
			room = true;
		}
	}

	return room;

}

// takes a reference to the socket of a connection of an inactive client,
// without memory for it the socket is shut down right away, with the clients lock
static void cerver_inactive_take_socket (void *connection_ptr, void *dropped_ptr) {

	Connection *connection = (Connection *) connection_ptr;
	CerverInactiveDropped *dropped = (CerverInactiveDropped *) dropped_ptr;

	if (connection && connection->socket) {
		if (cerver_inactive_sockets_room (dropped)) {
			dropped->sockets[dropped->sockets_count] = socket_reference (connection->socket);
			dropped->sockets_count += 1;
		}

		else {
			(void) socket_shutdown (connection->socket);
		}
	}

}

// the sockets of the inactive clients are shut down without the clients lock,
// so their receive handlers end and remove them like any other closed connection,
// the close mutex of each socket keeps a shutdown from reaching an fd that was closed and reused
static void cerver_inactive_shutdown_sockets (CerverInactiveDropped *dropped) {

	for (size_t index = 0; index < dropped->sockets_count; index++) {
		(void) socket_shutdown (dropped->sockets[index]);
		socket_delete (dropped->sockets[index]);
	}

	free (dropped->sockets);
	dropped->sockets = NULL;
	dropped->sockets_count = 0;
	dropped->sockets_size = 0;

}

// with the clients lock, only the sockets of the connections of an inactive client are taken
static void cerver_inactive_check_client (
	const Client *client, const Cerver *cerver, time_t current_time,
	CerverInactiveDropped *dropped
) {

	const time_t last_activity = __atomic_load_n (&client->last_activity, __ATOMIC_RELAXED);

	if ((current_time - last_activity) >= (time_t) cerver->max_inactive_time) {
		if (client->connections) {
			(void) dlist_traverse (client->connections, cerver_inactive_take_socket, dropped);
		}

		if (dropped->count < CERVER_INACTIVE_LOG_BATCH) {
			dropped->ids[dropped->count] = client->id;
			dropped->count += 1;
		}

		dropped->total += 1;
	}

}

// the recursion depth is the tree height, that is log (n) in an avl
// must be called with the clients tree locked
static void cerver_inactive_check (
	const AVLNode *node, const Cerver *cerver, time_t current_time,
	CerverInactiveDropped *dropped
) {

	if (node) {
		cerver_inactive_check (node->right, cerver, current_time, dropped);

		if (node->id) {
			cerver_inactive_check_client ((const Client *) node->id, cerver, current_time, dropped);
		}

		cerver_inactive_check (node->left, cerver, current_time, dropped);
	}

}

// the logs of a check are written without the clients lock
static void cerver_inactive_check_log (
	const Cerver *cerver, const CerverInactiveDropped *dropped
) {

	for (unsigned int index = 0; index < dropped->count; index++) {
		cerver_log_warning (
			"Client %lu has been inactive more than %u secs and will be dropped",
			dropped->ids[index], cerver->max_inactive_time
		);
	}

	if (dropped->total > dropped->count) {
		cerver_log_warning (
			"%u more inactive clients will be dropped from cerver %s",
			dropped->total - dropped->count, cerver->info->name
		);
	}

}

static void cerver_inactive_check_all (Cerver *cerver) {

	cerver_log_debug (
		"Checking for inactive clients in cerver %s...",
		cerver->info->name
	);

	time_t current_time = time (NULL);

	CerverInactiveDropped dropped;
	dropped.count = 0;
	dropped.total = 0;
	dropped.sockets = NULL;
	dropped.sockets_count = 0;
	dropped.sockets_size = 0;

	(void) pthread_mutex_lock (cerver->clients->mutex);

	cerver_inactive_check (cerver->clients->root, cerver, current_time, &dropped);

	(void) pthread_mutex_unlock (cerver->clients->mutex);

	cerver_inactive_shutdown_sockets (&dropped);

	cerver_inactive_check_log (cerver, &dropped);

	cerver_log_debug (
		"Done checking for inactive clients in cerver %s",
		cerver->info->name
	);

}

// 17/06/2020 - thread to check for inactive clients
static void *cerver_inactive_thread (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		while (cerver_is_running (cerver)) {
			cerver_sleep (cerver, cerver->check_inactive_interval);

			if (cerver_is_running (cerver) && cerver->clients) {
				cerver_inactive_check_all (cerver);
			}
		}
	}

	return NULL;

}

static u8 cerver_start_inactive (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_log_debug (
			"Cerver %s is set to check for inactive clients with max time of <%u> secs every <%u> secs",
			cerver->info->name,
			cerver->max_inactive_time,
			cerver->check_inactive_interval
		);

		if (!thread_create_joinable (
			&cerver->inactive_thread_id,
			cerver_inactive_thread,
			cerver
		)) {
			cerver_log_success (
				"Created cerver %s INACTIVE thread!",
				cerver->info->name
			);

			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to create cerver %s INACTIVE thread!",
				cerver->info->name
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
				cerver->info->name
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
						cerver->info->name
					);

					close (cerver->sock);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_POLL - socket is NOT set to non blocking!",
					cerver->info->name
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
						cerver->info->name
					);

					close (cerver->sock);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_THREADS - socket is NOT blocking!",
					cerver->info->name
				);
			}
		} break;

		default: {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver's %s handler type is invalid!",
				cerver->info->name
			);
		} break;
	}

	return retval;

}

// every datagram is a packet, the peers are logical connections (udp.c)
// the admins have connections of their own, they can not connect to an UDP cerver
static u8 cerver_start_udp (Cerver *cerver) {

	if (cerver->admin) {
		cerver_log_error (
			"Cerver %s can not handle admins with UDP!",
			cerver->info->name
		);

		return 1;
	}

	(void) time (&cerver->info->time_started);

	cerver_event_trigger (
		CERVER_EVENT_STARTED,
		cerver,
		NULL, NULL
	);

	return cerver_udp_start (cerver);

}

// tell the cerver to start listening for connections and packets
// a stop requested before cerver_start (), or while it was setting the running flag, cancels it
// cerver_stop_request () sets stop_requested before it clears the running flag,
// so with sequential consistency every order of the two ends with the cerver stopped
static bool cerver_start_cancelled (Cerver *cerver) {

	const bool cancelled = __atomic_load_n (&cerver->stop_requested, __ATOMIC_SEQ_CST);

	if (cancelled) {
		cerver_set_running (cerver, false);

		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"The cerver %s was stopped before it started!",
			cerver->info->name
		);
	}

	return cancelled;

}

// initializes cerver's structures like thpool (if any)
// and any other processes that have been configured before
// a cerver is started only once, a stop requested before cancels it
// returns 0 on success, 1 on error
u8 cerver_start (Cerver *cerver) {

	u8 retval = 1;

	if (!cerver_is_running (cerver)) {
		cerver_set_running (cerver, true);

		if (cerver_start_cancelled (cerver)) return 0;

		cerver_start_ignore_sigpipe (cerver);

		if (!cerver_one_time_init (cerver)) {
			u8 errors = 0;

			errors |= cerver_handlers_start (cerver);

			if (cerver->auth_required) {
				cerver_log_debug (
					"Cerver %s requires authentication",
					cerver->info->name
				);

				errors |= cerver_auth_start (cerver);
			}

			if (cerver->use_sessions) {
				cerver_log_debug (
					"Cerver %s supports sessions",
					cerver->info->name
				);
			}

			// start the admin cerver
			if (cerver->admin) {
				cerver_log_debug (
					"Cerver %s can handle admins",
					cerver->info->name
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
						retval = cerver_start_udp (cerver);
					} break;

					default: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cant't start cerver %s! Unknown protocol!",
							cerver->info->name
						);
					}
					break;
				}
			}
		}

		else {
			cerver_log_error (
				"Failed cerver_one_time_init () for cerver %s!",
				cerver->info->name
			);
		}

		// the accept loop has ended or the cerver failed to start
		cerver_stop_request (cerver);
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"The cerver %s is already running!",
			cerver->info->name
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

static const i64 CERVER_MICROSECONDS_PER_SECOND = 1000000;
static const i64 CERVER_NANOSECONDS_PER_MICROSECOND = 1000;

static i64 cerver_timespec_diff_microseconds (
	const struct timespec *start, const struct timespec *end
) {

	return ((i64) (end->tv_sec - start->tv_sec) * CERVER_MICROSECONDS_PER_SECOND)
		+ ((i64) (end->tv_nsec - start->tv_nsec) / CERVER_NANOSECONDS_PER_MICROSECOND);

}

// sleeps the microseconds or until the cerver is stopped, used to limit the fps of an update
void cerver_sleep_microseconds (Cerver *cerver, i64 microseconds) {

	const i64 nanoseconds_per_second = CERVER_MICROSECONDS_PER_SECOND * CERVER_NANOSECONDS_PER_MICROSECOND;

	struct timespec deadline = CERVER_ZERO_INITIALIZER;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);

	i64 nanoseconds = (i64) deadline.tv_nsec + (microseconds * CERVER_NANOSECONDS_PER_MICROSECOND);
	deadline.tv_sec += (time_t) (nanoseconds / nanoseconds_per_second);
	deadline.tv_nsec = (long) (nanoseconds % nanoseconds_per_second);

	(void) pthread_mutex_lock (cerver->stop_mutex);

	int result = 0;
	while (cerver_is_running (cerver) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (cerver->stop_cond, cerver->stop_mutex, &deadline);
	}

	(void) pthread_mutex_unlock (cerver->stop_mutex);

}

// called in a dedicated thread only if a user method was set
// executes methods every tick
static void *cerver_update (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update () has started!",
			cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (cerver, cerver->update_args);

		const i64 time_per_frame = CERVER_MICROSECONDS_PER_SECOND / (cerver->update_ticks ? cerver->update_ticks : 1);
		i64 sleep_time = 0;

		struct timespec start = CERVER_ZERO_INITIALIZER, middle = CERVER_ZERO_INITIALIZER;

		while (cerver_is_running (cerver)) {
			(void) clock_gettime (CLOCK_MONOTONIC, &start);

			// do stuff
			if (cerver->update) cerver->update (cu);

			// limit the fps, the full timespec is used to cross every second
			(void) clock_gettime (CLOCK_MONOTONIC, &middle);
			sleep_time = time_per_frame - cerver_timespec_diff_microseconds (&start, &middle);
			if (sleep_time > 0) {
				// the stop wakes it up, it does not wait for the end of the frame
				cerver_sleep_microseconds (cerver, sleep_time);
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
			cerver->info->name
		);
		#endif
	}

	return NULL;

}

// called in a dedicated thread only if a user method was set
// executes methods every x seconds
static void *cerver_update_interval (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has started!",
			cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (
			cerver, cerver->update_interval_args
		);

		while (cerver_is_running (cerver)) {
			if (cerver->update_interval) cerver->update_interval (cu);

			cerver_sleep (cerver, cerver->update_interval_secs);
		}

		cerver_update_delete (cu);

		if (cerver->update_interval_args) {
			if (cerver->delete_update_interval_args) {
				cerver->delete_update_interval_args (
					cerver->update_interval_args
				);
			}
		}

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has ended!",
			cerver->info->name
		);
		#endif
	}

	return NULL;

}

#pragma endregion

#pragma region end

static void cerver_stop_wake_threads (Cerver *cerver) {

	if (cerver->stop_mutex && cerver->stop_cond) {
		(void) pthread_mutex_lock (cerver->stop_mutex);
		(void) pthread_cond_broadcast (cerver->stop_cond);
		(void) pthread_mutex_unlock (cerver->stop_mutex);
	}

}

// stops accepting new connections and wakes up cerver_start ()
// the listening socket is only closed by cerver_teardown (),
// so its fd can not be reused while another thread is inside accept ()
// a request before cerver_start () cancels the start
// it is called from signal handlers, so it keeps the errno of the thread that it interrupted,
// the shutdown () of an UDP socket that is not connected fails with ENOTCONN
void cerver_stop_request (Cerver *cerver) {

	const int saved_errno = errno;

	if (cerver) {
		__atomic_store_n (&cerver->stop_requested, true, __ATOMIC_SEQ_CST);

		cerver_set_running (cerver, false);

		// wakes up the threads blocked in accept () or poll (),
		// a socket created after this load is never listened in
		const i32 sock = __atomic_load_n (&cerver->sock, __ATOMIC_SEQ_CST);
		if (sock >= 0) {
			(void) shutdown (sock, SHUT_RDWR);
		}

		cerver_stop_wake_threads (cerver);
	}

	errno = saved_errno;

}

// stops accepting new connections and wakes up cerver_start ()
// it can be called after the cerver was stopped by a signal
// returns 0 on success, 1 on error
u8 cerver_shutdown (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_stop_request (cerver);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"The cerver %s has stopped accepting connections.",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	return retval;

}

static void cerver_close_socket (Cerver *cerver) {

	if (cerver->sock >= 0) {
		if (close (cerver->sock)) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to close cerver %s socket!",
				cerver->info->name
			);
		}

		cerver->sock = -1;
	}

}

static void cerver_thread_join (pthread_t *thread_id) {

	if (*thread_id) {
		(void) pthread_join (*thread_id, NULL);
		*thread_id = 0;
	}

}

// the update threads end as soon as the cerver is stopped
static void cerver_stop_threads (Cerver *cerver) {

	cerver_thread_join (&cerver->update_thread_id);
	cerver_thread_join (&cerver->update_interval_thread_id);
	cerver_thread_join (&cerver->inactive_thread_id);
	cerver_thread_join (&cerver->on_hold_poll_thread_id);

}

static void cerver_shutdown_connection (void *connection_ptr, void *args) {

	(void) args;

	Connection *connection = (Connection *) connection_ptr;
	if (connection && connection->socket) {
		(void) socket_shutdown (connection->socket);
	}

}

// must be called with the clients tree locked
static void cerver_shutdown_clients_connections (const AVLNode *node) {

	if (node) {
		cerver_shutdown_clients_connections (node->left);

		const Client *client = (const Client *) node->id;
		if (client && client->connections) {
			(void) dlist_traverse (client->connections, cerver_shutdown_connection, NULL);
		}

		cerver_shutdown_clients_connections (node->right);
	}

}

// the clients keep their connections open, so their receive threads
// only finish when the sockets are shutdown
static void cerver_shutdown_clients (Cerver *cerver) {

	if (cerver->clients) {
		(void) pthread_mutex_lock (cerver->clients->mutex);

		cerver_shutdown_clients_connections (cerver->clients->root);

		(void) pthread_mutex_unlock (cerver->clients->mutex);
	}

}

// waits for the thpool and for the detachable connection threads,
// returns true if both finished before the timeout
static bool cerver_drain_wait (Cerver *cerver, const time_t timeout) {

	bool drained = true;

	if (cerver->thpool) {
		drained = !thpool_drain (
			cerver->thpool,
			(cerver->drain_policy == CERVER_DRAIN_POLICY_RUN),
			timeout
		);
	}

	if (thread_group_wait (&cerver->connection_threads, timeout)) {
		drained = false;
	}

	return drained;

}

// logs what is still alive after the drain grace, its data is not released
static void cerver_drain_report (Cerver *cerver) {

	cerver_log_warning (
		"Cerver %s was NOT drained in %u + %u secs - "
		"%u thpool connections working - %u queued - %u connection threads - "
		"%u event threads - %u handlers alive, the data they use will not be released",
		cerver->info->name, cerver->drain_timeout, CERVER_DRAIN_GRACE_TIMEOUT,
		cerver->thpool ? thpool_get_num_threads_working (cerver->thpool) : 0,
		cerver->thpool ? thpool_get_queued (cerver->thpool) : 0,
		thread_group_alive (&cerver->connection_threads),
		thread_group_alive (&cerver->event_threads),
		cerver_get_n_handlers_alive (cerver)
	);

}

// waits for the connections in progress using the drain policy and timeout,
// if they do not finish, a short grace is given before reporting them
// a cerver configured with threads shuts its clients down and waits for them,
// also with the poll handler type, that does not create the thpool
static inline bool cerver_drain_needed (const Cerver *cerver, const bool connection_threads) {

	return cerver->thpool || (cerver->n_thpool_threads > 0) || connection_threads;

}

static void cerver_drain (Cerver *cerver) {

	cerver->drained = true;

	const bool connection_threads = thread_group_alive (&cerver->connection_threads) > 0;

	if (cerver_drain_needed (cerver, connection_threads)) {
		cerver_log_msg (
			"Draining cerver %s - %u working - %u queued - %u connection threads - %s policy - %u secs timeout",
			cerver->info->name,
			cerver->thpool ? thpool_get_num_threads_working (cerver->thpool) : 0,
			cerver->thpool ? thpool_get_queued (cerver->thpool) : 0,
			thread_group_alive (&cerver->connection_threads),
			cerver_drain_policy_to_string (cerver->drain_policy),
			cerver->drain_timeout
		);

		if (cerver->type != CERVER_TYPE_WEB) {
			cerver_shutdown_clients (cerver);
		}

		cerver->drained = cerver_drain_wait (cerver, (time_t) cerver->drain_timeout);

		if (!cerver->drained) {
			cerver_log_warning (
				"Cerver %s was NOT drained in %u secs - waiting %u more secs",
				cerver->info->name, cerver->drain_timeout, CERVER_DRAIN_GRACE_TIMEOUT
			);

			cerver->drained = cerver_drain_wait (cerver, (time_t) CERVER_DRAIN_GRACE_TIMEOUT);
		}

		if (cerver->drained) {
			cerver_log_success ("Cerver %s was drained!", cerver->info->name);
		}

		else {
			cerver_drain_report (cerver);
		}
	}

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

static void cerver_clean_thpool (Cerver *cerver) {

	if (cerver->thpool) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Cerver %s active thpool threads: %u",
			cerver->info->name,
			thpool_get_num_threads_working (cerver->thpool)
		);
		#endif

		thpool_destroy (cerver->thpool);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Destroyed cerver %s thpool!", cerver->info->name
		);
		#endif

		cerver->thpool = NULL;
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Cerver %s does NOT have a thpool to destroy",
			cerver->info->name
		);
		#endif
	}

}

static void cerver_clean_data (Cerver *cerver);

// clean cerver data structures
// the connections are drained before anything they use is released
static void cerver_clean (Cerver *cerver) {

	if (cerver) {
		// stop accepting new connections
		cerver_stop_request (cerver);

		// the lobbies stop their threads and give their connections back
		game_cerver_stop (cerver);

		cerver_stop_threads (cerver);

		cerver_drain (cerver);

		if (cerver->drained) {
			// stop any app / custom handler
			if (!cerver_handlers_destroy (cerver)) {
				cerver_log_success (
					"Done destroying handlers in cerver %s",
					cerver->info->name
				);
			}

			cerver_clean_thpool (cerver);

			cerver_clean_data (cerver);
		}

		cerver_close_socket (cerver);
	}

}

// releases the data that the connections use
// only after the thpool was drained and destroyed
static void cerver_clean_data (Cerver *cerver) {

	if (cerver) {
		switch (cerver->type) {
			case CERVER_TYPE_CUSTOM: break;

			case CERVER_TYPE_GAME: {
				if (cerver->cerver_data) {
					GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

					if (game_cerver->final_game_action)
						game_cerver->final_game_action (game_cerver->final_action_args);

					// the lobbies were torn down when the cerver stopped
					game_cerver_stop (cerver);

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

		// the peers of an UDP cerver
		cerver_udp_end (cerver);

		// clean up cerver connected clients
		cerver_destroy_clients (cerver);
		#ifdef CERVER_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_CERVER, "Done cleaning up clients.");
		#endif
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
			"Starting cerver %s teardown...", cerver->info->name
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

		if (cerver->drained) {
			// no connection or handler can trigger an event after this
			(void) thread_group_wait (&cerver->event_threads, THREAD_GROUP_WAIT_FOREVER);

			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_NONE,
				"Cerver %s teardown was successful!",
				cerver->info->name
			);

			cerver_delete (cerver);
		}

		// the connections that are still working use the cerver
		else {
			cerver_log_warning (
				"Cerver %s teardown ended without releasing its memory",
				cerver->info->name
			);
		}

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

	CerverReport *cerver_report = (CerverReport *) malloc (sizeof (CerverReport));
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

	// an UDP connection sends its auth when it connects, the cerver can not ask for it (udp.c)
	if (cerver_report && connection && (connection->protocol == PROTOCOL_UDP)) {
		return;
	}

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
							connection->name
						);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to generate connection %s auth packet!",
							connection->name
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
							connection->name
						);

						client_event_trigger (CLIENT_EVENT_AUTH_SENT, client, connection);
					}

					else {
						cerver_log_error (
							"cerver_check_info () - Failed to send connection %s auth packet!",
							connection->name
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
					connection->name
				);
			}
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_NONE,
				"Cerver does NOT require authentication"
			);
			#endif
		}
	}

}

// the names come from CERVER_TYPE_MAP, so a new type is never logged as unknown
static void cerver_report_log_type (const CerverType type) {

	const char *name = cerver_type_to_string (type);

	if ((type != CERVER_TYPE_NONE) && (name != cerver_type_to_string (CERVER_TYPE_NONE))) {
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_NONE, "Cerver type: %s", name);
	}

	else {
		cerver_log (LOG_TYPE_ERROR, LOG_TYPE_NONE, "Cerver type: UNKNOWN");
	}

}

u8 cerver_report_check_info (
	CerverReport *cerver_report,
	Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver_report && connection) {
		// the update thread sets it while the app can be ending the connection
		CerverReport *previous = __atomic_exchange_n (
			&connection->cerver_report, cerver_report, __ATOMIC_ACQ_REL
		);

		cerver_report_delete (previous);

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

		cerver_report_log_type (cerver_report->type);

		cerver_report_check_info_handle_auth (cerver_report, client, connection);

		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region serialization

SERIALIZER_DEFINE (CerverSummary, cerver_summary, CERVER_SUMMARY_VERSION, CERVER_SUMMARY_FIELDS)

// the summary of the cerver that its clients get, the texts are cut to their fields
static void cerver_summary_set (CerverSummary *summary, const Cerver *cerver) {

	serializer_object_init (&cerver_summary_schema, summary);

	summary->type = (i32) cerver->type;

	(void) snprintf (
		summary->name, CERVER_SUMMARY_NAME_SIZE,
		"%s", cerver->info->name
	);

	if (cerver->info->welcome_len) {
		(void) snprintf (
			summary->welcome, CERVER_SUMMARY_WELCOME_SIZE,
			"%s", cerver->info->welcome
		);
	}

	summary->use_ipv6 = cerver->use_ipv6;
	summary->protocol = (i32) cerver->protocol;
	summary->port = cerver->port;

	summary->auth_required = cerver->auth_required;
	summary->uses_sessions = cerver->use_sessions;

}

// the summary comes from the network already validated by its schema
CerverReport *cerver_deserialize (const CerverSummary *summary) {

	CerverReport *cerver_report = NULL;

	if (summary) {
		cerver_report = cerver_report_new ();
		if (cerver_report) {
			cerver_report->type = (CerverType) summary->type;

			cerver_report->name = str_new (summary->name);

			if (summary->welcome[0]) {
				cerver_report->welcome = str_new (summary->welcome);
			}

			cerver_report->use_ipv6 = summary->use_ipv6;
			cerver_report->protocol = (Protocol) summary->protocol;
			cerver_report->port = summary->port;

			cerver_report->auth_required = summary->auth_required;
			cerver_report->uses_sessions = summary->uses_sessions;
		}
	}

	return cerver_report;

}

// creates a cerver info packet ready to be sent
Packet *cerver_packet_generate (Cerver *cerver) {

	Packet *packet = NULL;

	if (cerver) {
		CerverSummary summary;
		cerver_summary_set (&summary, cerver);

		packet = packet_create_serialized (
			PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO,
			&cerver_summary_schema, &summary
		);
	}

	return packet;

}

#pragma endregion
