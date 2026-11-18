/**
 * @file cerver/load/balancer.h
 * @brief The load balancer: its types, the routes of its client connections and the Balancer.
 */

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

#include "cerver/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;
struct _Service;

/**
 * @defgroup load Load
 * @brief The load balancer: a cerver that receives the packets of its clients and routes them to its services,
 * and sends the responses of the services back to the clients.
 *
 * A balancer is created with balancer_create (), its services are registered with balancer_service_register (),
 * it is started with balancer_start (), that blocks, and released with balancer_teardown ().
 */

/**
 * @defgroup load_type Type
 * @ingroup load
 * @brief How a balancer selects the service of each packet.
 * @{
 */

/**
 * @brief The X-macro table of the types of a balancer: the number, the name and the string of each one.
 */
#define BALANCER_TYPE_MAP(XX)									\
	XX(0, 	NONE, 				None)							\
	XX(1, 	ROUND_ROBIN, 		Round-Robin)					\
	XX(2, 	HANDLER_ID, 		Handler-ID)						\
	XX(3, 	CUSTOM, 			Custom)

/**
 * @brief The type of a balancer, one `BALANCER_TYPE_<name>` for each row of #BALANCER_TYPE_MAP.
 */
typedef enum BalancerType {

	#define XX(num, name, string) BALANCER_TYPE_##name = num,
	BALANCER_TYPE_MAP (XX)
	#undef XX

} BalancerType;

/**
 * @brief Returns the string of a type of balancer.
 *
 * @param[in] type the type
 * @return the string of its row in #BALANCER_TYPE_MAP, or the one of BALANCER_TYPE_NONE for a value out of the
 * table; a static string that must not be freed
 */
const char *balancer_type_to_string (
	const BalancerType type
);

/** @} */

/**
 * @defgroup load_main Balancer
 * @ingroup load
 * @brief The routes of the client connections, the Balancer and its setters.
 * @{
 */

/**
 * @brief The route of a client connection, in a slot of its own that does not depend on its socket.
 *
 * A reference to the socket is kept while the connection is alive, so a response is never sent to a socket that
 * was deleted. The generation of the slot changes every time that a connection ends, so a response for a
 * connection that ended is discarded even when its slot was given to another one. A free slot is in the list of
 * the free slots, and the one that was released last is taken first.
 */
typedef struct BalancerRoute {

	/** @brief A reference to the socket of the connection, NULL when the slot is free. */
	Socket *socket;
	/** @brief The next free slot, #BALANCER_ROUTE_NONE at the end of the list. */
	u32 next_free;
	/** @brief The generation of the slot, never 0. */
	u16 generation;

} BalancerRoute;

/** @brief The end of the list of the free slots. */
#define BALANCER_ROUTE_NONE					UINT32_MAX

/** @brief The initial slots of the routes. */
#define BALANCER_DEFAULT_ROUTES_SIZE		64

/** @brief The factor that the slots of the routes grow by. */
#define BALANCER_ROUTES_GROWTH_FACTOR		2

/** @brief The bits of the slot in a route id, the low bits. */
#define BALANCER_ROUTE_SLOT_BITS			20

/** @brief The bits of the generation in a route id, the high bits. */
#define BALANCER_ROUTE_GENERATION_BITS		12

/** @brief The mask of the slot of a route id. */
#define BALANCER_ROUTE_SLOT_MASK			((1u << BALANCER_ROUTE_SLOT_BITS) - 1)

/** @brief The mask of the generation of a route id, after its shift. */
#define BALANCER_ROUTE_GENERATION_MASK		((1u << BALANCER_ROUTE_GENERATION_BITS) - 1)

/** @brief The connections that are routed at the same time, one more is not routed. */
#define BALANCER_ROUTES_MAX					(BALANCER_ROUTE_SLOT_MASK + 1)

/** @brief The first generation: a generation is never 0, so a route id is never 0, the id of a packet without a route. */
#define BALANCER_ROUTE_FIRST_GENERATION		1

/**
 * @brief Returns the route id of a slot and a generation, the one that travels in the header of a packet.
 *
 * @param[in] slot the slot, only its #BALANCER_ROUTE_SLOT_BITS low bits are used
 * @param[in] generation the generation, only its #BALANCER_ROUTE_GENERATION_BITS low bits are used
 * @return the generation in the high bits and the slot in the low bits
 */
CERVER_PUBLIC u32 balancer_route_id (const u32 slot, const u16 generation);

/**
 * @brief Returns the slot of a route id.
 *
 * @param[in] route_id the route id
 * @return the slot
 */
CERVER_PUBLIC u32 balancer_route_id_slot (const u32 route_id);

/**
 * @brief Returns the generation of a route id.
 *
 * @param[in] route_id the route id
 * @return the generation
 */
CERVER_PUBLIC u16 balancer_route_id_generation (const u32 route_id);

/**
 * @brief Returns the generation that comes after another one, that wraps around without 0.
 *
 * @param[in] generation the generation
 * @return the next generation, #BALANCER_ROUTE_FIRST_GENERATION after the last one
 */
CERVER_PUBLIC u16 balancer_route_next_generation (const u16 generation);

/**
 * @brief Selects the service of a packet in a BALANCER_TYPE_CUSTOM balancer, from its header and its body.
 *
 * It runs in the thread of the client connection, without the lock of the balancer. It returns the index of the
 * service; a negative index, or one without a working service, sends the packet to the on unavailable services
 * method.
 */
typedef int (*BalancerSelectService) (
	const PacketHeader *header, const char *body, const size_t body_size, void *args
);

/**
 * @brief A load balancer.
 */
struct _Balancer {

	/** @brief The name of the balancer. */
	String *name;
	/** @brief How it selects the service of each packet. */
	BalancerType type;

	/** @brief The method that selects the service in a BALANCER_TYPE_CUSTOM balancer. */
	BalancerSelectService select_service;
	/** @brief The arguments of #select_service. */
	void *select_service_args;

	/** @brief The cerver that the clients connect to, it handles each client in its own thread. */
	Cerver *cerver;
	/** @brief The client with a connection to each service. */
	Client *client;

	/** @brief The next service of the round robin, and while the services are registered, how many there are. */
	int next_service;
	/** @brief How many services the balancer is connected to. */
	int n_services;
	/** @brief The services, with references to the connections of the client. */
	struct _Service **services;

	/** @brief Runs with a reference to the packet when no service is available. */
	Action on_unavailable_services;

	/** @brief The stats of the balancer. */
	BalancerStats *stats;

	/** @brief Protects the next service and the routes. */
	pthread_mutex_t *mutex;

	/** @brief The slots of the routes that were allocated. */
	BalancerRoute *routes;
	/** @brief The number of #routes. */
	u32 routes_size;
	/** @brief The slots that were ever used. */
	u32 routes_used;
	/** @brief The first free slot, #BALANCER_ROUTE_NONE when there is none. */
	u32 free_route;

	/** @brief The balancer is running, read and written with __atomic. */
	bool running;
	/** @brief The services that were down when the balancer started wait for their next attempt in it. */
	pthread_cond_t *cond;

};

/** @brief A load balancer, see struct _Balancer. */
typedef struct _Balancer Balancer;

/**
 * @brief Allocates an empty balancer.
 *
 * @return the new balancer, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Balancer *balancer_new (void);

/**
 * @brief Releases a balancer: its cerver, its client, its services, its stats and its routes.
 *
 * @param[in] balancer_ptr the balancer, it can be NULL
 * @note For the internal methods of cerver: an application calls balancer_teardown ().
 */
CERVER_PRIVATE void balancer_delete (void *balancer_ptr);

/**
 * @brief Returns the type of a balancer.
 *
 * @param[in] balancer the balancer
 * @return the type, BALANCER_TYPE_NONE for a NULL balancer
 */
CERVER_EXPORT BalancerType balancer_get_type (const Balancer *balancer);

/**
 * @brief Sets a method that runs whenever no service is available for a packet.
 *
 * @param[in,out] balancer the balancer
 * @param[in] on_unavailable_services the method, it gets a reference to the received packet
 */
CERVER_EXPORT void balancer_set_on_unavailable_services (
	Balancer *balancer, const Action on_unavailable_services
);

/**
 * @brief Sets the method that selects the service of each packet in a BALANCER_TYPE_CUSTOM balancer.
 *
 * @param[in,out] balancer the balancer
 * @param[in] select_service the method
 * @param[in] args the arguments that are passed to it with each packet
 * @note Call it before balancer_start ().
 */
CERVER_EXPORT void balancer_set_select_service (
	Balancer *balancer, const BalancerSelectService select_service, void *args
);

/**
 * @brief Sets the TLS of the connections of the balancer to its services; the balancer takes the settings.
 *
 * It is client_set_tls () of the client of the balancer: each service is reached with TLS, and its certificate is
 * verified with the host of the service in its SAN. The clients of the balancer get TLS with cerver_set_tls () of its
 * cerver.
 *
 * @param[in,out] balancer the balancer, before balancer_start ()
 * @param[in] settings the settings, owned by the balancer even when it fails
 * @retval 0 the services are reached with TLS
 * @retval 1 a NULL argument or no memory
 * @since 2.1b-22
 */
CERVER_EXPORT u8 balancer_set_services_tls (Balancer *balancer, TlsSettings *settings);

/**
 * @brief Creates a load balancer of a type with its network values and the number of services that it will have.
 *
 * Its cerver handles each client in its own thread (CERVER_HANDLER_TYPE_THREADS).
 *
 * @param[in] name the name, required
 * @param[in] type the type
 * @param[in] port the port of its cerver
 * @param[in] connection_queue the backlog of listen ()
 * @param[in] n_services the number of services, required; balancer_start () needs all of them registered
 * @return the new balancer, released with balancer_teardown (); NULL without a name or services, or if it could
 * not be created
 */
CERVER_EXPORT Balancer *balancer_create (
	const char *name, const BalancerType type,
	const u16 port, const u16 connection_queue,
	const unsigned int n_services
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
