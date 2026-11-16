/**
 * @file cerver/admin/stats.h
 * @brief The stats of the admin cerver.
 */

#ifndef _CERVER_ADMIN_STATS_H_
#define _CERVER_ADMIN_STATS_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup admin_stats Stats
 * @ingroup admin
 * @brief What the admin cerver received and sent.
 * @{
 */

/**
 * @brief The stats of the admin cerver.
 *
 * @warning The counters are plain integers, not the __atomic ones of CerverStats: they are updated by the admin
 * poll, the handlers and the on hold poll without a common lock.
 */
struct _AdminCerverStats {

	/**
	 * @brief How often the stats should be reset.
	 *
	 * @warning It is only stored and printed: cerver does not reset the stats with it.
	 * @deprecated Only the stats of the cerver are reset (CerverStats::threshold_time); it goes away in the 3.0.
	 */
	time_t threshold_time;

	/** @brief The packets received (packet header and data). */
	u64 total_n_packets_received;
	/** @brief The calls to recv (). */
	u64 total_n_receives_done;
	/** @brief The bytes received. */
	u64 total_bytes_received;

	/** @brief The packets sent. */
	u64 total_n_packets_sent;
	/** @brief The bytes sent. */
	u64 total_bytes_sent;

	/** @brief The connections of the admins that are registered in the admin poll. */
	u64 current_connections;
	/** @brief The authenticated admins that are connected. */
	u64 current_connected_admins;

	/** @brief The admin connections that were made to the cerver. */
	u64 total_admin_connections;
	/** @brief The admins that were registered to the cerver. */
	u64 total_n_admins;

	/** @brief The packets received, by packet type. */
	struct _PacketsPerType *received_packets;
	/** @brief The packets sent, by packet type. */
	struct _PacketsPerType *sent_packets;

};

/** @brief The stats of the admin cerver, see struct _AdminCerverStats. */
typedef struct _AdminCerverStats AdminCerverStats;

/**
 * @brief Prints the stats of the admin cerver, with the packets by type.
 *
 * @param[in] stats the stats, NULL prints nothing
 */
CERVER_PUBLIC void admin_cerver_stats_print (
	AdminCerverStats *stats
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
