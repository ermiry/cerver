/**
 * @file load/internal.h
 * @internal
 * @brief The methods that the parts of the balancer share, never exported by libcerver.so.
 */

#ifndef _CERVER_LOAD_INTERNAL_H_
#define _CERVER_LOAD_INTERNAL_H_

#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/load/balancer.h"
#include "cerver/load/service.h"
#include "cerver/load/stats.h"
#include "cerver/packets/packets.h"

/** @internal @brief A routed packet is sent as its header and its body. */
#define BALANCER_SEND_PARTS					2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief The custom data of the receive of the connection to a service.
 */
typedef struct {

	/** @internal @brief The balancer. */
	Balancer *balancer;
	/** @internal @brief The service of the connection. */
	Service *service;

} BalancerService;

/**
 * @internal
 * @brief Returns the index of a packet type in the stats arrays.
 *
 * The type comes from the network, so it is checked against the size of the arrays.
 *
 * @param[in] packet_type the packet type
 * @return the index, PACKET_TYPE_BAD for PACKET_TYPE_NONE or a type out of range
 */
static inline unsigned int balancer_packet_type_index (const PacketType packet_type) {

	return (((unsigned int) packet_type == PACKET_TYPE_NONE) || ((unsigned int) packet_type >= PACKET_TYPE_BAD)) ?
		(unsigned int) PACKET_TYPE_BAD : (unsigned int) packet_type;

}

/**
 * @internal
 * @brief Allocates the stats of a balancer, all in zero.
 *
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_HIDDEN BalancerStats *balancer_stats_new (void);

/**
 * @internal
 * @brief Releases the stats of a balancer.
 *
 * @param[in] stats the stats, they can be NULL
 */
CERVER_HIDDEN void balancer_stats_delete (BalancerStats *stats);

/**
 * @internal
 * @brief Initializes routes as free slots of the first generation, without a socket.
 *
 * @param[out] routes the routes
 * @param[in] count the number of routes
 */
CERVER_HIDDEN void balancer_routes_init (BalancerRoute *routes, const u32 count);

/**
 * @internal
 * @brief Sets the status of a service with __atomic, and runs its on status change method.
 *
 * @param[in,out] service the service
 * @param[in] status the new status
 */
CERVER_HIDDEN void balancer_service_set_status (
	Service *service, const ServiceStatus status
);

/**
 * @internal
 * @brief Changes the status of a service only if it has the expected one (a compare and exchange), and then runs
 * its on status change method.
 *
 * @param[in,out] service the service
 * @param[in] expected the status that it must have
 * @param[in] status the new status
 */
CERVER_HIDDEN void balancer_service_change_status (
	Service *service, ServiceStatus expected, const ServiceStatus status
);

/**
 * @internal
 * @brief Returns the service of a connection of the client of the balancer.
 *
 * @param[in] balancer the balancer
 * @param[in] connection the connection
 * @return the service, NULL if no service has it
 */
CERVER_HIDDEN Service *balancer_service_get_with_connection (
	const Balancer *balancer, const Connection *connection
);

/**
 * @internal
 * @brief Connects to a service, tests that it handles packets and starts its connection.
 *
 * @param[in,out] balancer the balancer
 * @param[in,out] service the service, SERVICE_STATUS_WORKING on success and SERVICE_STATUS_UNAVAILABLE on error
 * @retval 0 the service is working
 * @retval 1 it could not be connected, tested or started; its connection is reset for the next attempt
 */
CERVER_HIDDEN u8 balancer_service_connect (
	Balancer *balancer, Service *service
);

/**
 * @internal
 * @brief Starts the thread that connects to a service that was down when the balancer started.
 *
 * @param[in] balancer the balancer
 * @param[in,out] service the service
 */
CERVER_HIDDEN void balancer_service_reconnect_start (
	Balancer *balancer, Service *service
);

/**
 * @internal
 * @brief Registers the events of the client of the balancer: connected, connection closed and connection failed.
 *
 * @param[in,out] balancer the balancer
 */
CERVER_HIDDEN void balancer_events_register (Balancer *balancer);

/**
 * @internal
 * @brief The custom receive of the connections to the services: receives a response and sends it to its client.
 *
 * A failed receive ends the receive loop, and then the connection reconnects.
 *
 * @param[in] custom_data_ptr the ConnectionCustomReceiveData, with a BalancerService
 * @param[in] buffer the buffer of the connection
 * @param[in] buffer_size the size of the buffer
 * @retval 0 the response was handled
 * @retval 1 an error happened
 */
CERVER_HIDDEN u8 balancer_client_receive (
	void *custom_data_ptr,
	char *buffer, const size_t buffer_size
);

#ifdef __cplusplus
}
#endif

#endif
