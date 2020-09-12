#ifndef _CERVER_BALANCER_H_
#define _CERVER_BALANCER_H_

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

typedef struct Balancer {

	BalcancerType type;

	Cerver *cerver;
	Client *client;

	unsigned int n_services;		// how many services the load balancer is connected to
	Connection **services;			// references to the client's connections for direct access

} Balancer;

CERVER_PRIVATE Balancer *balancer_new (void);

CERVER_PRIVATE void balancer_delete (void *balancer_ptr);

#endif