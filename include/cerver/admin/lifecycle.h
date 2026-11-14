#ifndef _CERVER_ADMIN_LIFECYCLE_H_
#define _CERVER_ADMIN_LIFECYCLE_H_

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region start

CERVER_PRIVATE u8 admin_cerver_start (
	AdminCerver *admin_cerver
);

#pragma endregion

#pragma region end

CERVER_PRIVATE u8 admin_cerver_end (
	AdminCerver *admin_cerver
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
