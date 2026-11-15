/**
 * @file cerver/load/start.h
 * @brief How a load balancer starts.
 */

#ifndef _CERVER_LOAD_START_H_
#define _CERVER_LOAD_START_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup load_start Start
 * @ingroup load
 * @brief Starting a balancer.
 * @{
 */

/**
 * @brief Starts a balancer: it connects to its services and tests that they handle packets, and then starts its
 * cerver for the clients.
 *
 * The services that are down are connected later by their reconnect threads, but at least one service must be
 * working. It blocks in cerver_start () until the cerver of the balancer is stopped.
 *
 * @param[in,out] balancer the balancer, with all its services registered
 * @retval 0 the balancer ran and its cerver was stopped
 * @retval 1 a NULL balancer, services missing, none of them working, or its cerver failed to start
 */
CERVER_EXPORT u8 balancer_start (Balancer *balancer);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
