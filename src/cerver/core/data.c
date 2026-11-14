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
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/internal.h"
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

#pragma region stats

CerverStats *cerver_stats_new (void) {

	CerverStats *cerver_stats = (CerverStats *) malloc (sizeof (CerverStats));
	if (cerver_stats) {
		(void) memset (cerver_stats, 0, sizeof (CerverStats));
		cerver_stats->received_packets = packets_per_type_new ();
		cerver_stats->sent_packets = packets_per_type_new ();
	}

	return cerver_stats;

}

void cerver_stats_delete (CerverStats *cerver_stats) {

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

RequestStats (*cerver_request_stats_new (void))[CERVER_REQUEST_STATS_ENTRIES] {

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

void cerver_request_handlers_delete (Cerver *cerver) {

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
u8 cerver_request_handlers_start (Cerver *cerver) {

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

void cerver_request_handlers_stop (Cerver *cerver) {

	for (unsigned int type = 0; type < CERVER_REQUEST_PACKET_TYPES; type++) {
		for (u32 request = 0; request < cerver->request_handlers_size[type]; request++) {
			handler_stop (cerver->request_handlers[type][request]);
		}
	}

}

#pragma endregion
