#ifndef _CERVER_ADMIN_POLL_H_
#define _CERVER_ADMIN_POLL_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region poll

// regsiters a client connection to the cerver's admin poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_poll_register_connection (
	AdminCerver *admin_cerver, struct _Connection *connection
);

// unregsiters a sock fd connection from the cerver's admin poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_poll_unregister_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
