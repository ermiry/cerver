/**
 * @file cerver/load/end.h
 * @brief How a load balancer is torn down.
 */

#ifndef _CERVER_LOAD_END_H_
#define _CERVER_LOAD_END_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup load_end End
 * @ingroup load
 * @brief Releasing a balancer.
 * @{
 */

/**
 * @brief Tears down a balancer: stops the reconnect threads of its services, tears down its cerver (the receive
 * threads of the clients end and remove their routes) and its client (the connections with the services end), and
 * releases it.
 *
 * @param[in] balancer the balancer, invalid after the call
 * @retval 0 the balancer was torn down
 * @retval 1 the balancer is NULL
 */
CERVER_EXPORT u8 balancer_teardown (Balancer *balancer);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
