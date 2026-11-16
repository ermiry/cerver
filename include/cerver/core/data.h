/**
 * @file cerver/core/data.h
 * @brief The stats of a cerver and of the request types of its packets.
 */

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

/**
 * @defgroup core_stats Stats
 * @ingroup core
 * @brief What a cerver received, sent and handled.
 * @{
 */

/**
 * @brief Adds a value to a stat.
 *
 * @param field the field of the stats
 * @param value the value to add
 * @threadsafety The stats are updated from many threads at the same time, with relaxed __atomic operations.
 */
#define CERVER_STATS_ADD(field, value) ((void) __atomic_add_fetch (&(field), (value), __ATOMIC_RELAXED))

/**
 * @brief Subtracts a value from a stat.
 *
 * @param field the field of the stats
 * @param value the value to subtract
 * @threadsafety A relaxed __atomic operation, like #CERVER_STATS_ADD.
 */
#define CERVER_STATS_SUBTRACT(field, value) ((void) __atomic_sub_fetch (&(field), (value), __ATOMIC_RELAXED))

/**
 * @brief Reads a stat.
 *
 * @param field the field of the stats
 * @threadsafety A relaxed __atomic load, like #CERVER_STATS_ADD.
 */
#define CERVER_STATS_GET(field) (__atomic_load_n (&(field), __ATOMIC_RELAXED))

/**
 * @brief The stats of a cerver; read each counter with #CERVER_STATS_GET.
 */
typedef struct CerverStats {

	/**
	 * @brief The seconds after which the counters that grow (not the current ones nor the packets by type) are
	 * reset, 0 never resets them (the default); see cerver_stats_set_threshold_time ().
	 */
	time_t threshold_time;
	/**
	 * @brief When the current threshold time started, in seconds of CLOCK_MONOTONIC; read and written with
	 * __atomic.
	 * @since 1.9b-2
	 */
	time_t threshold_start;

	/** @brief The packets received from the clients. */
	u64 client_n_packets_received;
	/** @brief The receives done to the clients. */
	u64 client_receives_done;
	/** @brief The bytes received from the clients. */
	u64 client_bytes_received;

	/** @brief The packets received from the on hold connections. */
	u64 on_hold_n_packets_received;
	/** @brief The receives done to the on hold connections. */
	u64 on_hold_receives_done;
	/** @brief The bytes received from the on hold connections. */
	u64 on_hold_bytes_received;

	/** @brief The cerver packets received (packet header and data). */
	u64 total_n_packets_received;
	/** @brief The calls to recv (). */
	u64 total_n_receives_done;
	/** @brief The bytes received by the cerver. */
	u64 total_bytes_received;

	/** @brief The packets sent. */
	u64 n_packets_sent;
	/** @brief The bytes sent by the cerver. */
	u64 total_bytes_sent;

	/** @brief The connections of the current clients that are active in the main poll. */
	u64 current_active_client_connections;
	/** @brief The clients that are connected. */
	u64 current_n_connected_clients;
	/** @brief The on hold connections, only when the cerver requires authentication. */
	u64 current_n_hold_connections;
	/** @brief The on hold connections that the cerver had. */
	u64 total_on_hold_connections;
	/** @brief The clients that were registered to the cerver. */
	u64 total_n_clients;
	/** @brief The clients that were registered in the current threshold time (all of them without one). */
	u64 unique_clients;
	/** @brief The client connections that were made to the cerver. */
	u64 total_client_connections;
	/** @brief The connections rejected because the thpool was full, the cerver was draining or its poll had its max. */
	u64 total_rejected_connections;

	/** @brief The packets received, by packet type. */
	struct _PacketsPerType *received_packets;
	/** @brief The packets sent, by packet type. */
	struct _PacketsPerType *sent_packets;

} CerverStats;

/**
 * @brief Sets how often the stats of the cerver should be reset.
 *
 * @param[in,out] cerver the cerver
 * @param[in] threshold_time the seconds, 0 never resets them; the threshold time starts now
 * @note The counters are reset at the first registration of a client, or the first cerver_stats_print (), after
 * the threshold time passed: a cerver without clients keeps them until then.
 */
CERVER_EXPORT void cerver_stats_set_threshold_time (
	struct _Cerver *cerver, time_t threshold_time
);

/**
 * @brief Prints the stats of the cerver, of its thpool and of its request types.
 *
 * @param[in] cerver the cerver, a NULL one logs a warning
 * @param[in] received true to also print the received packets by type
 * @param[in] sent true to also print the sent packets by type
 */
CERVER_EXPORT void cerver_stats_print (
	struct _Cerver *cerver, bool received, bool sent
);

/** @brief The request types below it have their own stats in each packet type, the bigger ones share the last entry. */
#define CERVER_REQUEST_STATS_SIZE					32

/** @brief The entries of the request stats of each packet type: one per request type and the shared one. */
#define CERVER_REQUEST_STATS_ENTRIES				(CERVER_REQUEST_STATS_SIZE + 1)

/** @brief The packet types that have request stats and handlers: PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR and PACKET_TYPE_CUSTOM. */
#define CERVER_REQUEST_PACKET_TYPES					3

/** @brief The request types below it can have their own handler. */
#define CERVER_REQUEST_HANDLERS_MAX					1024

/**
 * @brief The packets of a request type that the handlers of the cerver handled.
 */
typedef struct RequestStats {

	/** @brief The microseconds that the handler took with each packet. */
	Histogram durations;
	/** @brief The bytes of each packet, with its header. */
	Histogram sizes;

} RequestStats;

/**
 * @brief Copies the stats of a request type of PACKET_TYPE_APP, PACKET_TYPE_APP_ERROR or PACKET_TYPE_CUSTOM.
 *
 * @param[in] cerver the cerver
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type; one of #CERVER_REQUEST_STATS_SIZE or bigger gets the stats that they
 * share
 * @param[out] stats where the stats are copied
 * @retval 0 the stats were copied
 * @retval 1 a NULL argument, a cerver without request stats or another packet type
 */
CERVER_EXPORT u8 cerver_get_request_stats (
	const struct _Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	RequestStats *stats
);

/**
 * @brief Prints the stats of the request types that have packets.
 *
 * @param[in] cerver the cerver
 */
CERVER_EXPORT void cerver_request_stats_print (const struct _Cerver *cerver);

/**
 * @brief Adds a packet that a handler of the cerver handled to the stats of its request type.
 *
 * @param[in,out] cerver the cerver
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type
 * @param[in] size the bytes of the packet, with its header
 * @param[in] microseconds what the handler took
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_request_stats_add (
	struct _Cerver *cerver,
	const PacketType packet_type, const u32 request_type,
	const u64 size, const u64 microseconds
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
