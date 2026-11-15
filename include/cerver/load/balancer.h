#ifndef _CERVER_LOAD_BALANCER_H_
#define _CERVER_LOAD_BALANCER_H_

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/load/stats.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;
struct _Service;

#pragma region type

#define BALANCER_TYPE_MAP(XX)									\
	XX(0, 	NONE, 				None)							\
	XX(1, 	ROUND_ROBIN, 		Round-Robin)					\
	XX(2, 	HANDLER_ID, 		Handler-ID)						\
	XX(3, 	CUSTOM, 			Custom)

typedef enum BalancerType {

	#define XX(num, name, string) BALANCER_TYPE_##name = num,
	BALANCER_TYPE_MAP (XX)
	#undef XX

} BalancerType;

const char *balancer_type_to_string (
	const BalancerType type
);

#pragma endregion

#pragma region main

// the route of each client connection, in a slot of its own that does not depend on its sock fd,
// a reference to the socket is kept while the connection is alive,
// so a response is never sent to a socket that was deleted
// the generation of the slot changes every time that a connection ends,
// so a response for a connection that ended is discarded even when its slot was given to another one
// a free slot is in the list of the free slots, the one that was released last is taken first
typedef struct BalancerRoute {

	Socket *socket;
	u32 next_free;
	u16 generation;

} BalancerRoute;

// the end of the list of the free slots
#define BALANCER_ROUTE_NONE					UINT32_MAX

#define BALANCER_DEFAULT_ROUTES_SIZE		64
#define BALANCER_ROUTES_GROWTH_FACTOR		2

// the route id that travels in the header of a packet:
// the generation of the slot in the high bits, and the slot in the low bits
#define BALANCER_ROUTE_SLOT_BITS			20
#define BALANCER_ROUTE_GENERATION_BITS		12
#define BALANCER_ROUTE_SLOT_MASK			((1u << BALANCER_ROUTE_SLOT_BITS) - 1)
#define BALANCER_ROUTE_GENERATION_MASK		((1u << BALANCER_ROUTE_GENERATION_BITS) - 1)

// the connections that are routed at the same time, one more is not routed
#define BALANCER_ROUTES_MAX					(BALANCER_ROUTE_SLOT_MASK + 1)

// a generation is never 0, so a route id is never 0, the id of a packet without a route
#define BALANCER_ROUTE_FIRST_GENERATION		1

// returns the route id of a slot and a generation
CERVER_PUBLIC u32 balancer_route_id (const u32 slot, const u16 generation);

// returns the slot of a route id
CERVER_PUBLIC u32 balancer_route_id_slot (const u32 route_id);

// returns the generation of a route id
CERVER_PUBLIC u16 balancer_route_id_generation (const u32 route_id);

// returns the generation that comes after the one passed, without 0
CERVER_PUBLIC u16 balancer_route_next_generation (const u16 generation);

// selects the service of a packet in a BALANCER_TYPE_CUSTOM balancer, from its header and its body,
// it runs in the thread of the client connection, without the lock of the balancer
// returns the index of the service; a negative index, or one without a working service,
// sends the packet to the on unavailable services method
typedef int (*BalancerSelectService) (
	const PacketHeader *header, const char *body, const size_t body_size, void *args
);

struct _Balancer {

	String *name;
	BalancerType type;

	BalancerSelectService select_service;
	void *select_service_args;

	Cerver *cerver;
	Client *client;

	int next_service;
	int n_services;					// how many services the load balancer is connected to
	struct _Service **services;		// references to the client's connections for direct access

	Action on_unavailable_services;

	BalancerStats *stats;

	// protects the next service and the routes
	pthread_mutex_t *mutex;

	// the slots that were allocated, the ones that were ever used and the first free one
	BalancerRoute *routes;
	u32 routes_size;
	u32 routes_used;
	u32 free_route;

	// the services that were down when the balancer started
	// wait for their next attempt in this condition
	bool running;
	pthread_cond_t *cond;

};

typedef struct _Balancer Balancer;

CERVER_PRIVATE Balancer *balancer_new (void);

CERVER_PRIVATE void balancer_delete (void *balancer_ptr);

CERVER_EXPORT BalancerType balancer_get_type (const Balancer *balancer);

// sets a cb method to be used whenever there are no services available
// a reference to the received packet will be passed to the method
CERVER_EXPORT void balancer_set_on_unavailable_services (
	Balancer *balancer, const Action on_unavailable_services
);

// sets, before the balancer starts, the method that selects the service of each packet
// in a BALANCER_TYPE_CUSTOM balancer, args are passed to it with each packet
CERVER_EXPORT void balancer_set_select_service (
	Balancer *balancer, const BalancerSelectService select_service, void *args
);

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
// the cerver of the balancer handles each client in a dedicated thread
CERVER_EXPORT Balancer *balancer_create (
	const char *name, const BalancerType type,
	const u16 port, const u16 connection_queue,
	const unsigned int n_services
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
