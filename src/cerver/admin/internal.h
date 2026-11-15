/**
 * @file admin/internal.h
 * @internal
 * @brief The methods that the parts of the admin cerver share, never exported by libcerver.so.
 */

#ifndef _CERVER_ADMIN_INTERNAL_H_
#define _CERVER_ADMIN_INTERNAL_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"
#include "cerver/admin/stats.h"
#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Allocates the stats of the admin cerver, all in zero.
 *
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_HIDDEN AdminCerverStats *admin_cerver_stats_new (void);

/**
 * @internal
 * @brief Releases the stats of the admin cerver.
 *
 * @param[in] admin_cerver_stats the stats, they can be NULL
 */
CERVER_HIDDEN void admin_cerver_stats_delete (AdminCerverStats *admin_cerver_stats);

/**
 * @internal
 * @brief Counts a packet that was sent to an admin in the stats.
 *
 * @param[in,out] stats the stats
 * @param[in] packet_type the type of the packet
 * @param[in] sent the bytes that were sent
 */
CERVER_HIDDEN void admin_cerver_packet_send_update_stats (
	AdminCerverStats *stats, PacketType packet_type, size_t sent
);

/**
 * @internal
 * @brief Removes the socket of a connection from the admin poll, see admin_cerver_poll_unregister_sock_fd ().
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] connection the connection
 * @retval 0 the socket was removed
 * @retval 1 a NULL argument, or the socket is not in the poll
 */
CERVER_HIDDEN u8 admin_cerver_poll_unregister_connection (
	AdminCerver *admin_cerver, Connection *connection
);

/**
 * @internal
 * @brief The thread of the admin poll: receives the packets of the admins until the cerver stops.
 *
 * @param[in] cerver_ptr the cerver
 * @return NULL
 */
CERVER_HIDDEN void *admin_poll (void *cerver_ptr);

#ifdef __cplusplus
}
#endif

#endif
