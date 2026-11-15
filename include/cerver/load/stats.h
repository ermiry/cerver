/**
 * @file cerver/load/stats.h
 * @brief The stats of a load balancer.
 */

#ifndef _CERVER_LOAD_STATS_H_
#define _CERVER_LOAD_STATS_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/packets/header.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;

/**
 * @defgroup load_stats Stats
 * @ingroup load
 * @brief What a balancer received, routed and sent.
 * @{
 */

/**
 * @brief The stats of a balancer.
 *
 * @threadsafety Every value is updated with __atomic operations by the receive threads of the clients and of the
 * services; read them with CERVER_STATS_GET ().
 */
typedef struct BalancerStats {

	/** @brief The packets of good types received from the clients. */
	u64 n_packets_received;
	/** @brief The receives done to the clients. */
	u64 receives_done;
	/** @brief The bytes received from the clients. */
	u64 bytes_received;

	/** @brief The packets of bad types received from the clients, their data was read and discarded. */
	u64 bad_n_packets_received;
	/** @brief The receives done to the clients for the bad packets. */
	u64 bad_receives_done;
	/** @brief The bytes of the bad packets. */
	u64 bad_bytes_received;

	/** @brief The packets routed to the services. */
	u64 n_packets_routed;
	/** @brief The bytes routed to the services. */
	u64 total_bytes_routed;

	/** @brief The responses sent to the clients. */
	u64 n_packets_sent;
	/** @brief The bytes of the responses sent to the clients. */
	u64 total_bytes_sent;

	/** @brief The responses of clients that were already disconnected. */
	u64 discarded_responses;

	/** @brief The packets handled by the on unavailable services method. */
	u64 backup_handled_packets;
	/** @brief The bytes of the packets handled by the on unavailable services method. */
	u64 backup_handled_bytes;

	/** @brief The packets that the balancer was unable to handle. */
	u64 unhandled_packets;
	/** @brief The bytes of the packets that the balancer was unable to handle. */
	u64 unhandled_bytes;

	/** @brief The packets received, by packet type. */
	u64 received_packets[PACKETS_MAX_TYPES];
	/** @brief The packets routed, by packet type. */
	u64 routed_packets[PACKETS_MAX_TYPES];
	/** @brief The responses sent, by packet type. */
	u64 sent_packets[PACKETS_MAX_TYPES];

} BalancerStats;

/**
 * @brief Prints the stats of a balancer and of each of its services.
 *
 * @param[in] balancer the balancer, NULL prints nothing
 */
CERVER_EXPORT void balancer_stats_print (
	const struct _Balancer *balancer
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
