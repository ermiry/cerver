#ifndef _CERVER_LOAD_ROUTE_H_
#define _CERVER_LOAD_ROUTE_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region route

// how many times a recv () can time out in the middle of a packet
#define BALANCER_RECEIVE_MAX_TIMEOUTS		4

// gives a route to a client connection that the balancer receives from,
// in a free slot, with a reference to its socket
// returns the route id, or 0 when every slot is taken (BALANCER_ROUTES_MAX)
CERVER_PRIVATE u32 balancer_connection_start (
	Balancer *balancer, Socket *socket
);

// releases the route of a client connection that has ended,
// the next generation of its slot discards its pending responses
CERVER_PRIVATE void balancer_connection_end (
	Balancer *balancer, const u32 route_id
);

// returns a reference to the socket of the client connection of the route id,
// NULL if the connection has ended, even when its slot belongs to another connection now
CERVER_PRIVATE Socket *balancer_route_get (
	Balancer *balancer, const u32 route_id
);

// receives a complete packet from a client and routes it to a service
// returns 0 to keep receiving, 1 when the connection has ended
CERVER_PRIVATE u8 balancer_receive (CerverReceive *cr);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
