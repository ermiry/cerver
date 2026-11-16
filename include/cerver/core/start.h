/**
 * @file cerver/core/start.h
 * @brief How a cerver starts.
 */

#ifndef _CERVER_CORE_START_H_
#define _CERVER_CORE_START_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core_start Start
 * @ingroup core
 * @brief Starting a cerver.
 * @{
 */

/**
 * @brief Starts the cerver: it initializes its structures (the thpool, the polls), starts its handlers, its
 * authentication, its admin cerver and its update threads, and accepts connections until it is stopped.
 *
 * It blocks until cerver_shutdown () or a signal stops the cerver; a cerver is started only once, and a stop
 * requested before the call cancels it.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the cerver ran and was stopped, or its start was cancelled
 * @retval 1 a NULL cerver, one that is already running, or it failed to start
 */
CERVER_EXPORT u8 cerver_start (Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
