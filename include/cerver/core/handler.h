/**
 * @file cerver/core/handler.h
 * @brief The app handlers of a cerver with multiple handlers, and the counts of its handlers.
 */

#ifndef _CERVER_CORE_HANDLER_H_
#define _CERVER_CORE_HANDLER_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core_handlers Handlers
 * @ingroup core
 * @brief The handlers of the packets of a cerver.
 * @{
 */

/**
 * @brief Logs, as debug messages, how many handlers of the cerver are alive and how many are working.
 *
 * @param[in] cerver the cerver
 */
CERVER_EXPORT void cerver_handlers_print_info (Cerver *cerver);

/**
 * @brief Adds a handler, created with handler_create_with_id (), to the app handlers of the cerver.
 *
 * Call it after cerver_set_multiple_handlers (); the cerver owns the handler from then on.
 *
 * @param[in,out] cerver the cerver
 * @param[in] handler the handler, its id is its index
 * @retval 0 the handler was added
 * @retval 1 a NULL argument, a cerver without multiple handlers, an id that is not below n_handlers or that already
 * has a handler; then the handler still belongs to the caller
 */
CERVER_EXPORT u8 cerver_handlers_add (
	Cerver *cerver, struct _Handler *handler
);

/**
 * @brief Returns the number of app handlers of the multiple handlers option.
 *
 * @param[in] cerver the cerver
 * @return the number of handlers, 0 for a NULL cerver
 */
CERVER_EXPORT unsigned int cerver_get_n_handlers (Cerver *cerver);

/**
 * @brief Returns the number of handlers that are alive (ready to handle packets).
 *
 * @param[in] cerver the cerver
 * @return the handlers, 0 for a NULL cerver or one that never started its handlers
 * @threadsafety Safe from any thread, it reads the count with the lock of the handlers.
 */
CERVER_EXPORT unsigned int cerver_get_n_handlers_alive (Cerver *cerver);

/**
 * @brief Returns the number of handlers that are handling a packet.
 *
 * @param[in] cerver the cerver
 * @return the handlers, 0 for a NULL cerver
 * @threadsafety Safe from any thread, the handlers count themselves with __atomic.
 */
CERVER_EXPORT unsigned int cerver_get_n_handlers_working (Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
