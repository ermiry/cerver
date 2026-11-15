/**
 * @file core/internal.h
 * @internal
 * @brief The methods that the parts of the Cerver share, never exported by libcerver.so.
 */

#ifndef _CERVER_CORE_INTERNAL_H_
#define _CERVER_CORE_INTERNAL_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/core/report.h"

/** @internal @brief The inactive clients of one check that are logged by their id, the rest only by their count. */
#define CERVER_INACTIVE_LOG_BATCH		256

/** @internal @brief The first room for the sockets of a check that are shut down after the clients lock, it doubles. */
#define CERVER_INACTIVE_SHUTDOWN_BATCH	256

/** @internal @brief The index of PACKET_TYPE_APP in the request stats and handlers. */
#define CERVER_REQUEST_APP				0
/** @internal @brief The index of PACKET_TYPE_APP_ERROR in the request stats and handlers. */
#define CERVER_REQUEST_APP_ERROR		1
/** @internal @brief The index of PACKET_TYPE_CUSTOM in the request stats and handlers. */
#define CERVER_REQUEST_CUSTOM			2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Sets whether the cerver is running, with __atomic.
 *
 * @param[in,out] cerver the cerver
 * @param[in] value the new value
 */
CERVER_HIDDEN void cerver_set_running (Cerver *cerver, bool value);

/**
 * @internal
 * @brief Returns whether a stop of the cerver was requested (cerver_stop_request ()), with __atomic: a web
 * cerver that stops does not keep its connections for another request.
 *
 * A cerver that has not started is not stopping.
 *
 * @param[in] cerver the cerver
 * @return true after a stop was requested, false for a NULL cerver
 */
CERVER_HIDDEN bool cerver_is_stopping (const Cerver *cerver);

/**
 * @internal
 * @brief Ignores SIGPIPE when the cerver is configured to and the process has the default handler for it.
 *
 * @param[in] cerver the cerver
 */
CERVER_HIDDEN void cerver_start_ignore_sigpipe (const Cerver *cerver);

/**
 * @internal
 * @brief Allocates the info of a cerver, with the default alias.
 *
 * @return the new info, NULL if it could not be allocated
 */
CERVER_HIDDEN CerverInfo *cerver_info_new (void);

/**
 * @internal
 * @brief Releases the info of a cerver.
 *
 * @param[in] cerver_info the info, it can be NULL
 */
CERVER_HIDDEN void cerver_info_delete (CerverInfo *cerver_info);

/**
 * @internal
 * @brief Copies the name of the cerver into its info, truncated to CERVER_INFO_NAME_SIZE - 1 characters.
 *
 * @param[in,out] cerver the cerver, with its info
 * @param[in] name the name
 */
CERVER_HIDDEN void cerver_set_name (Cerver *cerver, const char *name);

/**
 * @internal
 * @brief Allocates the stats of a cerver, all in zero.
 *
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_HIDDEN CerverStats *cerver_stats_new (void);

/**
 * @internal
 * @brief Releases the stats of a cerver.
 *
 * @param[in] cerver_stats the stats, they can be NULL
 */
CERVER_HIDDEN void cerver_stats_delete (CerverStats *cerver_stats);

/**
 * @internal
 * @brief Allocates the request stats of a cerver: one array of #CERVER_REQUEST_STATS_ENTRIES for each of the
 * #CERVER_REQUEST_PACKET_TYPES.
 *
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_HIDDEN RequestStats (*cerver_request_stats_new (void))[CERVER_REQUEST_STATS_ENTRIES];

/**
 * @internal
 * @brief Starts the request handlers that are not direct.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 every handler started
 * @retval 1 a handler failed to start
 */
CERVER_HIDDEN u8 cerver_request_handlers_start (Cerver *cerver);

/**
 * @internal
 * @brief Stops every request handler.
 *
 * @param[in,out] cerver the cerver
 */
CERVER_HIDDEN void cerver_request_handlers_stop (Cerver *cerver);

/**
 * @internal
 * @brief Deletes every request handler and their arrays.
 *
 * @param[in,out] cerver the cerver
 */
CERVER_HIDDEN void cerver_request_handlers_delete (Cerver *cerver);

/**
 * @internal
 * @brief Stops the app, the app error, the custom, the multiple and the request handlers.
 *
 * @param[in,out] cerver the cerver
 * @return always 0
 */
CERVER_HIDDEN u8 cerver_handlers_destroy (Cerver *cerver);

/**
 * @internal
 * @brief Closes the socket of the cerver, if it has one, and sets it to -1.
 *
 * @param[in,out] cerver the cerver
 */
CERVER_HIDDEN void cerver_close_socket (Cerver *cerver);

/**
 * @internal
 * @brief Creates another socket that listens in the address of the cerver, for an event loop that is not the
 * first one: non blocking and with SO_REUSEPORT, like the socket of a cerver with more than one I/O thread.
 *
 * @param[in] cerver the cerver, with its address already set by its own socket
 * @return the socket, -1 on error
 */
CERVER_HIDDEN i32 cerver_network_listen_socket_create (const Cerver *cerver);

/**
 * @internal
 * @brief The thread of the update method: calls it every tick until the cerver stops.
 *
 * @param[in] args the cerver
 * @return NULL
 */
CERVER_HIDDEN void *cerver_update (void *args);

/**
 * @internal
 * @brief The thread of the update interval method: calls it every interval until the cerver stops.
 *
 * @param[in] args the cerver
 * @return NULL
 */
CERVER_HIDDEN void *cerver_update_interval (void *args);

/**
 * @internal
 * @brief Allocates an empty report.
 *
 * @return the new report, NULL if it could not be allocated
 */
CERVER_HIDDEN CerverReport *cerver_report_new (void);

#ifdef __cplusplus
}
#endif

#endif
