#ifndef _CERVER_CORE_START_H_
#define _CERVER_CORE_START_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region start

// tell the cerver to start listening for connections and packets
// initializes cerver's structures like thpool (if any)
// and any other processes that have been configured before
// returns 0 on success, 1 on error
CERVER_EXPORT u8 cerver_start (Cerver *cerver);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
