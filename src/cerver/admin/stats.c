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
#include "cerver/admin/internal.h"
#include "cerver/admin/stats.h"
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

#pragma region stats

AdminCerverStats *admin_cerver_stats_new (void) {

	AdminCerverStats *admin_cerver_stats = (AdminCerverStats *) malloc (sizeof (AdminCerverStats));
	if (admin_cerver_stats) {
		(void) memset (admin_cerver_stats, 0, sizeof (AdminCerverStats));
		admin_cerver_stats->received_packets = packets_per_type_new ();
		admin_cerver_stats->sent_packets = packets_per_type_new ();
	}

	return admin_cerver_stats;

}

void admin_cerver_stats_delete (AdminCerverStats *admin_cerver_stats) {

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

		cerver_log_msg ("Total n receives done:                  %lu", stats->total_n_receives_done);
		cerver_log_msg ("Total n packets received:               %lu", stats->total_n_packets_received);
		cerver_log_msg ("Total bytes received:                   %lu\n", stats->total_bytes_received);

		cerver_log_msg ("Total n packets sent:                   %lu", stats->total_n_packets_sent);
		cerver_log_msg ("Total bytes sent:                       %lu\n", stats->total_bytes_sent);

		cerver_log_msg ("Current connections:                    %lu", stats->current_connections);
		cerver_log_msg ("Current connected admins:               %lu\n", stats->current_connected_admins);

		cerver_log_msg ("Total admin connections:                %lu", stats->total_admin_connections);
		cerver_log_msg ("Total n admins:                         %lu", stats->total_n_admins);

		cerver_log_msg ("\nReceived packets:");
		packets_per_type_print (stats->received_packets);

		cerver_log_msg ("\nSent packets:");
		packets_per_type_print (stats->sent_packets);
	}

}

void admin_cerver_packet_send_update_stats (
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
