#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/internal.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region stats

void connection_stats_init (ConnectionStats *stats) {

	(void) memset (stats, 0, sizeof (ConnectionStats));

	stats->received_packets = &stats->received_packets_storage;
	stats->sent_packets = &stats->sent_packets_storage;

}

ConnectionStats *connection_stats_new (void) {

	ConnectionStats *stats = (ConnectionStats *) malloc (sizeof (ConnectionStats));
	if (stats) {
		connection_stats_init (stats);
	}

	return stats;

}

// the stats of a connection live inside it,
// only stats that were created with connection_stats_new () are released
void connection_stats_delete (
	Connection *connection, ConnectionStats *stats
) {

	if (stats && (stats != &connection->stats_storage)) {
		free (stats);
	}

}

void connection_stats_print (Connection *connection) {

	if (connection) {
		if (connection->stats) {
			cerver_log_msg ("Threshold time:            %ld", connection->stats->threshold_time);

			cerver_log_msg ("N receives done:           %lu", connection->stats->n_receives_done);

			cerver_log_msg ("Total bytes received:      %lu", connection->stats->total_bytes_received);
			cerver_log_msg ("Total bytes sent:          %lu", connection->stats->total_bytes_sent);

			cerver_log_msg ("N packets received:        %lu", connection->stats->n_packets_received);
			cerver_log_msg ("N packets sent:            %lu", connection->stats->n_packets_sent);

			cerver_log_msg ("\nReceived packets:");
			packets_per_type_print (connection->stats->received_packets);

			cerver_log_msg ("\nSent packets:");
			packets_per_type_print (connection->stats->sent_packets);
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_NONE,
				"Connection does not have a reference to a connection stats!"
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_NONE,
			"Can't get stats of a NULL connection!"
		);
	}

}

#pragma endregion
