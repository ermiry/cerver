/**
 * @file cerver/admin/poll.h
 * @brief The poll of the connections of the admins.
 */

#ifndef _CERVER_ADMIN_POLL_H_
#define _CERVER_ADMIN_POLL_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup admin_poll Poll
 * @ingroup admin
 * @brief The array of the admin poll.
 * @{
 */

/**
 * @brief Adds the socket of a connection to a free slot of the admin poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] connection the connection
 * @retval 0 the socket was added
 * @retval 1 a NULL argument, or the admin poll is full
 * @threadsafety It takes the lock of the admin poll.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_cerver_poll_register_connection (
	AdminCerver *admin_cerver, struct _Connection *connection
);

/**
 * @brief Removes a socket from the admin poll.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in] sock_fd the socket
 * @retval 0 the socket was removed
 * @retval 1 a NULL admin cerver, or the socket is not in the poll
 * @threadsafety It takes the lock of the admin poll.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 admin_cerver_poll_unregister_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
