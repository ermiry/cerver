/**
 * @file cerver/connection/stats.h
 * @brief The stats of a connection.
 */

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

/**
 * @defgroup connection_stats Stats
 * @ingroup connection
 * @brief What a connection received and sent.
 * @{
 */

/**
 * @brief The stats of a connection.
 */
struct _ConnectionStats {

	/**
	 * @brief How often the stats should be reset.
	 *
	 * @warning It is only stored and printed: cerver does not reset the stats with it.
	 * @deprecated Only the stats of the cerver are reset (CerverStats::threshold_time); it goes away in the 3.0.
	 */
	time_t threshold_time;

	/** @brief The calls to recv (). */
	u64 n_receives_done;

	/** @brief The bytes received from the connection. */
	u64 total_bytes_received;
	/** @brief The bytes sent to the connection. */
	u64 total_bytes_sent;

	/** @brief The packets received from the connection (packet header and data). */
	u64 n_packets_received;
	/** @brief The packets sent to the connection. */
	u64 n_packets_sent;

	/** @brief The packets received by type, it points to #received_packets_storage. */
	struct _PacketsPerType *received_packets;
	/** @brief The packets sent by type, it points to #sent_packets_storage. */
	struct _PacketsPerType *sent_packets;

	/** @brief The storage of #received_packets, so the stats of a connection are not extra allocations. */
	struct _PacketsPerType received_packets_storage;
	/** @brief The storage of #sent_packets. */
	struct _PacketsPerType sent_packets_storage;

};

/** @brief The stats of a connection, see struct _ConnectionStats. */
typedef struct _ConnectionStats ConnectionStats;

/**
 * @brief Allocates the stats of a connection, all in zero.
 *
 * @return the new stats, released with free (); NULL if they could not be allocated
 */
CERVER_PUBLIC ConnectionStats *connection_stats_new (void);

/**
 * @brief Prints the stats of the connection and its received and sent packets by type.
 *
 * @param[in] connection the connection, a NULL one logs a warning and one without stats an error
 */
CERVER_PUBLIC void connection_stats_print (
	struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
