#ifndef _CERVER_CORE_INTERNAL_H_
#define _CERVER_CORE_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/core/report.h"

// the inactive clients of one check that are logged by their id, the rest only by their count
#define CERVER_INACTIVE_LOG_BATCH		256

// the first room for the sockets of a check that are shut down after the clients lock, it doubles
#define CERVER_INACTIVE_SHUTDOWN_BATCH	256

// the packet types in the request stats and handlers
#define CERVER_REQUEST_APP				0
#define CERVER_REQUEST_APP_ERROR		1
#define CERVER_REQUEST_CUSTOM			2

#ifdef __cplusplus
extern "C" {
#endif

// the methods that the parts of the cerver share, never exported

CERVER_HIDDEN void cerver_set_running (Cerver *cerver, bool value);

CERVER_HIDDEN void cerver_start_ignore_sigpipe (const Cerver *cerver);

CERVER_HIDDEN CerverInfo *cerver_info_new (void);

CERVER_HIDDEN void cerver_info_delete (CerverInfo *cerver_info);

CERVER_HIDDEN void cerver_set_name (Cerver *cerver, const char *name);

CERVER_HIDDEN CerverStats *cerver_stats_new (void);

CERVER_HIDDEN void cerver_stats_delete (CerverStats *cerver_stats);

CERVER_HIDDEN RequestStats (*cerver_request_stats_new (void))[CERVER_REQUEST_STATS_ENTRIES];

CERVER_HIDDEN u8 cerver_request_handlers_start (Cerver *cerver);

CERVER_HIDDEN void cerver_request_handlers_stop (Cerver *cerver);

CERVER_HIDDEN void cerver_request_handlers_delete (Cerver *cerver);

CERVER_HIDDEN u8 cerver_handlers_destroy (Cerver *cerver);

CERVER_HIDDEN void cerver_close_socket (Cerver *cerver);

CERVER_HIDDEN void *cerver_update (void *args);

CERVER_HIDDEN void *cerver_update_interval (void *args);

CERVER_HIDDEN CerverReport *cerver_report_new (void);

#ifdef __cplusplus
}
#endif

#endif
