#ifndef _CERVER_LOAD_END_H_
#define _CERVER_LOAD_END_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region end

// first ends and destroys the balancer's internal cerver
// then disconnects from each of the registered services
// last frees any balancer memory left
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_teardown (Balancer *balancer);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
