#ifndef _CERVER_LOAD_START_H_
#define _CERVER_LOAD_START_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region start

// starts the load balancer by first connecting to the registered services
// and checking their ability to handle requests
// the services that are down are connected later by a dedicated thread,
// at least one service must be working
// then the cerver gets started to enable client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_start (Balancer *balancer);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
