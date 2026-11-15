#ifndef _CERVER_LOAD_STATS_H_
#define _CERVER_LOAD_STATS_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/header.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;

#pragma region stats

// every value is updated with atomic operations,
// the receive threads of the clients and the services update them
typedef struct BalancerStats {

	// good types packets that the balancer can handle
	u64 n_packets_received;            // packets received from clients
	u64 receives_done;                 // receives done to clients
	u64 bytes_received;                // bytes received from clients

	// bad types packets - their data was read and discarded
	u64 bad_n_packets_received;        // packets received from clients
	u64 bad_receives_done;             // receives done to clients
	u64 bad_bytes_received;            // bytes received from clients

	// routed packets to services
	u64 n_packets_routed;              // total number of packets that were routed to services
	u64 total_bytes_routed;            // total amount of bytes routed to services

	// responses sent to the original clients
	u64 n_packets_sent;                // total number of packets that were sent
	u64 total_bytes_sent;              // total amount of bytes sent by the cerver

	// responses of clients that were already disconnected
	u64 discarded_responses;

	// packets handled by balancer on unavailable services
	u64 backup_handled_packets;
	u64 backup_handled_bytes;

	// packets that the balancer was unable to handle
	u64 unhandled_packets;
	u64 unhandled_bytes;

	u64 received_packets[PACKETS_MAX_TYPES];
	u64 routed_packets[PACKETS_MAX_TYPES];
	u64 sent_packets[PACKETS_MAX_TYPES];

} BalancerStats;

CERVER_EXPORT void balancer_stats_print (
	const struct _Balancer *balancer
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
