/**
 * @file cerver/admin/lifecycle.h
 * @brief How the admin cerver starts and ends with its cerver.
 */

#ifndef _CERVER_ADMIN_LIFECYCLE_H_
#define _CERVER_ADMIN_LIFECYCLE_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup admin_lifecycle Lifecycle
 * @ingroup admin
 * @brief Starting and ending the admin cerver.
 * @{
 */

/**
 * @brief Starts the admin cerver: its poll array and its locks, its update threads, its handlers and the thread
 * of its poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @retval 0 the admin cerver started
 * @retval 1 a NULL admin cerver, or its structures, its handlers or its poll failed to start
 * @note For the internal methods of cerver: cerver_start () calls it.
 */
CERVER_PRIVATE u8 admin_cerver_start (
	AdminCerver *admin_cerver
);

/**
 * @brief Ends the admin cerver: joins its poll and its update threads, which end when the cerver stops, ends its
 * handlers and disconnects its admins.
 *
 * @param[in,out] admin_cerver the admin cerver, it can be NULL
 * @retval 0 the admin cerver ended
 * @retval 1 its handlers or its admins failed to end
 * @note For the internal methods of cerver: cerver_teardown () calls it.
 */
CERVER_PRIVATE u8 admin_cerver_end (
	AdminCerver *admin_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
