#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/load/balancer.h"
#include "cerver/load/internal.h"
#include "cerver/load/service.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region stats

BalancerStats *balancer_stats_new (void) {

	BalancerStats *stats = (BalancerStats *) malloc (sizeof (BalancerStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (BalancerStats));
	}

	return stats;

}

void balancer_stats_delete (BalancerStats *stats) {

	if (stats) free (stats);

}

void balancer_stats_print (const Balancer *balancer) {

	if (balancer) {
		if (cerver_log_get_output_type () != LOG_OUTPUT_TYPE_FILE)
			cerver_log_msg (LOG_COLOR_BLUE "Balancer: %s\n" LOG_COLOR_RESET, balancer->name->str);

		else cerver_log_msg ("Balancer: %s\n", balancer->name->str);

		// good types packets that the balancer can handle
		cerver_log_msg ("Receives done:               %lu", CERVER_STATS_GET (balancer->stats->receives_done));
		cerver_log_msg ("Received packets:            %lu", CERVER_STATS_GET (balancer->stats->n_packets_received));
		cerver_log_msg ("Received bytes:              %lu\n", CERVER_STATS_GET (balancer->stats->bytes_received));

		// bad types packets - their data was read and discarded
		cerver_log_msg ("Bad receives done:           %lu", CERVER_STATS_GET (balancer->stats->bad_receives_done));
		cerver_log_msg ("Bad received packets:        %lu", CERVER_STATS_GET (balancer->stats->bad_n_packets_received));
		cerver_log_msg ("Bad received bytes:          %lu\n", CERVER_STATS_GET (balancer->stats->bad_bytes_received));

		// routed packets to services
		cerver_log_msg ("Routed packets:              %lu", CERVER_STATS_GET (balancer->stats->n_packets_routed));
		cerver_log_msg ("Routed bytes:                %lu\n", CERVER_STATS_GET (balancer->stats->total_bytes_routed));

		// responses sent to the original clients
		cerver_log_msg ("Responses sent packets:      %lu", CERVER_STATS_GET (balancer->stats->n_packets_sent));
		cerver_log_msg ("Responses sent bytes:        %lu", CERVER_STATS_GET (balancer->stats->total_bytes_sent));
		cerver_log_msg ("Discarded responses:         %lu\n", CERVER_STATS_GET (balancer->stats->discarded_responses));

		// packets handled by balancer on unavailable services
		cerver_log_msg ("Backup packets:              %lu", CERVER_STATS_GET (balancer->stats->backup_handled_packets));
		cerver_log_msg ("Backup bytes:                %lu\n", CERVER_STATS_GET (balancer->stats->backup_handled_bytes));

		// packets that the balancer was unable to handle
		cerver_log_msg ("Unhandled packets:           %lu", CERVER_STATS_GET (balancer->stats->unhandled_packets));
		cerver_log_msg ("Unhandled bytes:             %lu\n", CERVER_STATS_GET (balancer->stats->unhandled_bytes));

		cerver_log_msg ("Received packets:");
		packets_per_type_array_print (balancer->stats->received_packets);

		cerver_log_line_break ();
		cerver_log_msg ("Routed packets:");
		packets_per_type_array_print (balancer->stats->routed_packets);

		cerver_log_line_break ();
		cerver_log_msg ("Responses packets:");
		packets_per_type_array_print (balancer->stats->sent_packets);

		for (int i = 0; i < balancer->n_services; i++) {
			cerver_log_line_break ();
			balancer_service_stats_print (balancer->services[i]);
			cerver_log_line_break ();
		}
	}

}

#pragma endregion
