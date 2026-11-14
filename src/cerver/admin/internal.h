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

// the methods that the parts of the admin cerver share, never exported

CERVER_HIDDEN AdminCerverStats *admin_cerver_stats_new (void);

CERVER_HIDDEN void admin_cerver_stats_delete (AdminCerverStats *admin_cerver_stats);

CERVER_HIDDEN void admin_cerver_packet_send_update_stats (
	AdminCerverStats *stats, PacketType packet_type, size_t sent
);

CERVER_HIDDEN u8 admin_cerver_poll_unregister_connection (
	AdminCerver *admin_cerver, Connection *connection
);

CERVER_HIDDEN void *admin_poll (void *cerver_ptr);

#ifdef __cplusplus
}
#endif

#endif
