#ifndef _CERVER_CONNECTION_STATS_H_
#define _CERVER_CONNECTION_STATS_H_

#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/header.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Connection;

struct _ConnectionStats {

	time_t threshold_time;                  // every time we want to reset the connection's stats

	u64 n_receives_done;                    // n calls to recv ()

	u64 total_bytes_received;               // total amount of bytes received from this connection
	u64 total_bytes_sent;                   // total amount of bytes that have been sent to the connection

	u64 n_packets_received;                 // total number of packets received from this connection (packet header + data)
	u64 n_packets_sent;                     // total number of packets sent to this connection

	// point to the storage below, so the stats of a connection are not extra allocations
	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

	struct _PacketsPerType received_packets_storage;
	struct _PacketsPerType sent_packets_storage;

};

typedef struct _ConnectionStats ConnectionStats;

CERVER_PUBLIC ConnectionStats *connection_stats_new (void);

CERVER_PUBLIC void connection_stats_print (
	struct _Connection *connection
);

#ifdef __cplusplus
}
#endif

#endif
