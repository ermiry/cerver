#ifndef _CERVER_CORE_DATA_H_
#define _CERVER_CORE_DATA_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/packets.h"

#include "cerver/utils/histogram.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _PacketsPerType;

#pragma region stats

// the stats are updated from many threads at the same time
#define CERVER_STATS_ADD(field, value) ((void) __atomic_add_fetch (&(field), (value), __ATOMIC_RELAXED))
#define CERVER_STATS_SUBTRACT(field, value) ((void) __atomic_sub_fetch (&(field), (value), __ATOMIC_RELAXED))
#define CERVER_STATS_GET(field) (__atomic_load_n (&(field), __ATOMIC_RELAXED))

typedef struct CerverStats {

	time_t threshold_time;                          // every time we want to reset cerver stats (like packets), defaults 24hrs

	u64 client_n_packets_received;                  // packets received from clients
	u64 client_receives_done;                       // receives done to clients
	u64 client_bytes_received;                      // bytes received from clients

	u64 on_hold_n_packets_received;                 // packets received from on hold connections
	u64 on_hold_receives_done;                      // received done to on hold connections
	u64 on_hold_bytes_received;                     // bytes received from on hold connections

	u64 total_n_packets_received;                   // total number of cerver packets received (packet header + data)
	u64 total_n_receives_done;                      // total amount of actual calls to recv ()
	u64 total_bytes_received;                       // total amount of bytes received in the cerver

	u64 n_packets_sent;                             // total number of packets that were sent
	u64 total_bytes_sent;                           // total amount of bytes sent by the cerver

	u64 current_active_client_connections;          // all of the current active connections for all current clients (active in main poll array)
	u64 current_n_connected_clients;                // the current number of clients connected
	u64 current_n_hold_connections;                 // current numbers of on hold connections (only if the cerver requires authentication)
	u64 total_on_hold_connections;                  // the total amount of on hold connections
	u64 total_n_clients;                            // the total amount of clients that were registered to the cerver (no auth required)
	u64 unique_clients;                             // n unique clients connected in a threshold time (check used authentication)
	u64 total_client_connections;                   // the total amount of client connections that have been done to the cerver
	u64 total_rejected_connections;                 // connections rejected because the thpool was full, the cerver was draining or its poll had its max

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

} CerverStats;

// sets the cerver stats threshold time (how often the stats get reset)
CERVER_EXPORT void cerver_stats_set_threshold_time (
	struct _Cerver *cerver, time_t threshold_time
);

// prints the cerver stats
CERVER_EXPORT void cerver_stats_print (
	struct _Cerver *cerver, bool received, bool sent
);

// the request types below it have their own stats in each packet type,
// the bigger ones share the last entry
#define CERVER_REQUEST_STATS_SIZE					32
#define CERVER_REQUEST_STATS_ENTRIES				(CERVER_REQUEST_STATS_SIZE + 1)

// PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR and PACKET_TYPE_CUSTOM have request stats and handlers
#define CERVER_REQUEST_PACKET_TYPES					3

// the request types below it can have their own handler
#define CERVER_REQUEST_HANDLERS_MAX					1024

// the packets of a request type that the handlers of the cerver handled
typedef struct RequestStats {

	Histogram durations;			// microseconds that the handler took with each packet
	Histogram sizes;				// bytes of each packet, with its header

} RequestStats;

// copies the stats of a request type of PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR or PACKET_TYPE_CUSTOM,
// a request type of CERVER_REQUEST_STATS_SIZE or bigger gets the stats that they share
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_get_request_stats (
	const struct _Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	RequestStats *stats
);

// prints the stats of the request types that have packets
CERVER_EXPORT void cerver_request_stats_print (const struct _Cerver *cerver);

// adds a packet that a handler of the cerver handled to the stats of its request type
CERVER_PRIVATE void cerver_request_stats_add (
	struct _Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	const u64 size, const u64 microseconds
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
