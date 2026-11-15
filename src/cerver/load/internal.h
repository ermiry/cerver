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

// a routed packet is sent as its header and its body
#define BALANCER_SEND_PARTS					2

#ifdef __cplusplus
extern "C" {
#endif

// auxiliary structure to be passed in ConnectionCustomReceiveData
typedef struct {

	Balancer *balancer;
	Service *service;

} BalancerService;

// the index of a packet type in the stats arrays,
// the type comes from the network, so it is checked against the arrays size
static inline unsigned int balancer_packet_type_index (const PacketType packet_type) {

	return (((unsigned int) packet_type == PACKET_TYPE_NONE) || ((unsigned int) packet_type >= PACKET_TYPE_BAD)) ?
		(unsigned int) PACKET_TYPE_BAD : (unsigned int) packet_type;

}

// the methods that the parts of the balancer share, never exported

CERVER_HIDDEN BalancerStats *balancer_stats_new (void);

CERVER_HIDDEN void balancer_stats_delete (BalancerStats *stats);

CERVER_HIDDEN void balancer_routes_init (BalancerRoute *routes, const u32 count);

CERVER_HIDDEN void balancer_service_set_status (
	Service *service, const ServiceStatus status
);

CERVER_HIDDEN void balancer_service_change_status (
	Service *service, ServiceStatus expected, const ServiceStatus status
);

CERVER_HIDDEN Service *balancer_service_get_with_connection (
	const Balancer *balancer, const Connection *connection
);

CERVER_HIDDEN u8 balancer_service_connect (
	Balancer *balancer, Service *service
);

CERVER_HIDDEN void balancer_service_reconnect_start (
	Balancer *balancer, Service *service
);

CERVER_HIDDEN void balancer_events_register (Balancer *balancer);

CERVER_HIDDEN u8 balancer_client_receive (
	void *custom_data_ptr,
	char *buffer, const size_t buffer_size
);

#ifdef __cplusplus
}
#endif

#endif
