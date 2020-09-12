#ifndef _CERVER_BALANCER_H_
#define _CERVER_BALANCER_H_

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/config.h"

#define BALANCER_TYPE_MAP(XX)									\
	XX(0, 	NONE, 				None)							\
	XX(1, 	ROUND_ROBIN, 		Round-Robin)					\

typedef enum BalcancerType {

	#define XX(num, name, string) BALANCER_TYPE_##name = num,
	BALANCER_TYPE_MAP (XX)
	#undef XX

} BalcancerType;

const char *balancer_type_to_string (BalcancerType type);

typedef struct Balancer {

	BalcancerType type;

	Cerver *cerver;
	Client *client;

	unsigned int next_service;
	unsigned int n_services;		// how many services the load balancer is connected to
	Connection **services;			// references to the client's connections for direct access

} Balancer;

CERVER_PRIVATE Balancer *balancer_new (void);

CERVER_PRIVATE void balancer_delete (void *balancer_ptr);

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
CERVER_EXPORT Balancer *balancer_create (
	BalcancerType type,
	u16 port, u16 connection_queue,
	unsigned int n_services
);

// registers a new service to the load balancer
// a dedicated connection will be created when the balancer starts to handle traffic to & from the service
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_service_register (
	Balancer *balancer,
	const char *ip_address, u16 port
);

#endif