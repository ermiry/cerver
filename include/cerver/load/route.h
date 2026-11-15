/**
 * @file cerver/load/route.h
 * @brief How a load balancer routes the packets of its clients and the responses of its services.
 */

#ifndef _CERVER_LOAD_ROUTE_H_
#define _CERVER_LOAD_ROUTE_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup load_route Route
 * @ingroup load
 * @brief The routes of the client connections.
 * @{
 */

/** @brief How many times a recv () can time out in the middle of a packet. */
#define BALANCER_RECEIVE_MAX_TIMEOUTS		4

/**
 * @brief Gives a route to a client connection that the balancer receives from, in a free slot, with a reference
 * to its socket.
 *
 * @param[in,out] balancer the balancer
 * @param[in] socket the socket of the connection
 * @return the route id, 0 for a NULL argument or when every slot is taken (#BALANCER_ROUTES_MAX)
 * @threadsafety It takes the lock of the balancer.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u32 balancer_connection_start (
	Balancer *balancer, Socket *socket
);

/**
 * @brief Releases the route of a client connection that has ended; the next generation of its slot discards its
 * pending responses.
 *
 * @param[in,out] balancer the balancer
 * @param[in] route_id the route id
 * @threadsafety It takes the lock of the balancer.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void balancer_connection_end (
	Balancer *balancer, const u32 route_id
);

/**
 * @brief Returns a reference to the socket of the client connection of a route id.
 *
 * @param[in] balancer the balancer
 * @param[in] route_id the route id
 * @return a new reference to the socket, released with socket_delete (); NULL if the connection has ended, even
 * when its slot belongs to another connection now
 * @threadsafety It takes the lock of the balancer.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Socket *balancer_route_get (
	Balancer *balancer, const u32 route_id
);

/**
 * @brief Receives a complete packet from a client and routes it to a service.
 *
 * @param[in] cr the receive of the client connection
 * @retval 0 the connection keeps receiving
 * @retval 1 the connection has ended
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 balancer_receive (CerverReceive *cr);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
