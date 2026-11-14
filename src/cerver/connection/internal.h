#ifndef _CERVER_CONNECTION_INTERNAL_H_
#define _CERVER_CONNECTION_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// the methods that the parts of the connections share, never exported

CERVER_HIDDEN void connection_stats_init (ConnectionStats *stats);

CERVER_HIDDEN void connection_stats_delete (
	Connection *connection, ConnectionStats *stats
);

CERVER_HIDDEN Connection *connection_allocate (void);

CERVER_HIDDEN void connection_recycle (Connection *connection);

CERVER_HIDDEN void connection_synchronization_delete (Connection *connection);

CERVER_HIDDEN u8 connection_connect_round (Connection *connection, const bool fresh);

CERVER_HIDDEN void *connection_send_thread (void *connection_ptr);

#ifdef __cplusplus
}
#endif

#endif
