#ifndef _CERVER_ADMIN_STATS_H_
#define _CERVER_ADMIN_STATS_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region stats

struct _AdminCerverStats {

	time_t threshold_time;				// every time we want to reset cerver stats (like packets), defaults 24hrs

	u64 total_n_packets_received;		// total number of packets received (packet header + data)
	u64 total_n_receives_done;			// total amount of actual calls to recv ()
	u64 total_bytes_received;			// total amount of bytes received in the cerver

	u64 total_n_packets_sent;			// total number of packets that were sent
	u64 total_bytes_sent;				// total amount of bytes sent by the cerver

	u64 current_connections;			// all of the current active connections from all the admins (registered in the poll array)
	u64 current_connected_admins;		// the current number of auth admins connected (unique clients)

	u64 total_admin_connections;		// the total amount of admin connections that have been done to the cerver
	u64 total_n_admins;					// the total amount of admins that were registered to the cerver

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

};

typedef struct _AdminCerverStats AdminCerverStats;

CERVER_PUBLIC void admin_cerver_stats_print (
	AdminCerverStats *stats
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
