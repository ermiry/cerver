/**
 * @file cerver/core/end.h
 * @brief How a cerver stops and how it is torn down.
 */

#ifndef _CERVER_CORE_END_H_
#define _CERVER_CORE_END_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup core_end End
 * @ingroup core
 * @brief Stopping a cerver, draining its connections and releasing it.
 * @{
 */

/**
 * @brief Stops accepting new connections and wakes up cerver_start () and the threads of the cerver that sleep.
 *
 * It can be called more than once and from another thread; an interrupted accept () or poll () (a signal without
 * SA_RESTART) also stops the cerver. The connections in progress are drained by cerver_teardown ().
 *
 * @param[in,out] cerver the cerver, it can be NULL
 * @warning It locks the mutex of the sleeping threads, which is not async-signal-safe: called from a signal
 * handler that interrupts a thread of the cerver while it holds that mutex, it can block.
 * @note For the internal methods of cerver: an application calls cerver_shutdown ().
 */
CERVER_PRIVATE void cerver_stop_request (Cerver *cerver);

/**
 * @brief Stops accepting new connections and wakes up cerver_start ().
 *
 * It can be called after the cerver was stopped by a signal; the connections in progress are drained by
 * cerver_teardown ().
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the cerver was stopped
 * @retval 1 the cerver is NULL
 * @warning Like cerver_stop_request (), it is not async-signal-safe.
 */
CERVER_EXPORT u8 cerver_shutdown (Cerver *cerver);

/**
 * @brief Sleeps the seconds or until the cerver is stopped.
 *
 * @param[in] cerver the cerver
 * @param[in] seconds the seconds
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_sleep (Cerver *cerver, u32 seconds);

/**
 * @brief Sleeps the microseconds or until the cerver is stopped, to limit the ticks per second of an update.
 *
 * @param[in] cerver the cerver
 * @param[in] microseconds the microseconds
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_sleep_microseconds (Cerver *cerver, i64 microseconds);

/**
 * @brief Tears down a cerver: stops it, drains its connections with the drain policy and timeout, ends its admin
 * cerver and releases it.
 *
 * It triggers CERVER_EVENT_TEARDOWN first, and waits for the threads of the events before the cerver is released.
 *
 * @param[in] cerver the cerver, invalid after the call
 * @retval 0 the cerver was torn down
 * @retval 1 the cerver is NULL
 * @warning If some connections did not finish before the drain timeout, the memory of the cerver is not
 * released, so they never use it after a free.
 */
CERVER_EXPORT u8 cerver_teardown (Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
