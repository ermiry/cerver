#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"

const char *balancer_type_to_string (BalcancerType type) {

	switch (type) {
		#define XX(num, name, string) case BALANCER_TYPE_##name: return #string;
		BALANCER_TYPE_MAP(XX)
		#undef XX
	}

	return balancer_type_to_string (BALANCER_TYPE_NONE);

}

#pragma region main

Balancer *balancer_new (void) {

	Balancer *balancer = (Balancer *) malloc (sizeof (Balancer));
	if (balancer) {
		balancer->type = BALANCER_TYPE_NONE;

		balancer->cerver = NULL;
		balancer->client = NULL;

		balancer->next_service = 0;
		balancer->n_services = 0;
		balancer->services = NULL;
	}

	return balancer;

}

void balancer_delete (void *balancer_ptr) {

	if (balancer_ptr) {
		Balancer *balancer = (Balancer *) balancer_ptr;

		cerver_delete (balancer->cerver);
		client_delete (balancer->client);

		if (balancer->services) {
			free (balancer->services);
		}

		free (balancer_ptr);
	}

}

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
Balancer *balancer_create (
	BalcancerType type,
	u16 port, u16 connection_queue,
	unsigned int n_services
) {

	Balancer *balancer = balancer_new ();
	if (balancer) {
		balancer->type = type;

		balancer->cerver = cerver_create (
			CERVER_TYPE_BALANCER,
			"load-balancer-cerver",
			port, PROTOCOL_TCP, false, connection_queue, DEFAULT_POLL_TIMEOUT
		);

		balancer->client = client_create ();
		client_set_name (balancer->client, "balancer-client");

		balancer->n_services = n_services;
		balancer->services = (Connection **) calloc (balancer->n_services, sizeof (Connection *));
		if (balancer->services) {
			for (unsigned int i = 0; i < balancer->n_services; i++)
				balancer->services[i] = NULL;
		}
	}

	return balancer;

}

#pragma endregion

#pragma region services

// registers a new service to the load balancer
// a dedicated connection will be created when the balancer starts to handle traffic to & from the service
// returns 0 on success, 1 on error
u8 balancer_service_register (
	Balancer *balancer,
	const char *ip_address, u16 port
) {

	u8 retval = 1;

	if (balancer && ip_address) {
		Connection *connection = client_connection_create (
			balancer->client, 
			ip_address, port,
			PROTOCOL_TCP, false 
		);

		if (connection) {
			char name[64] = { 0 };
			snprintf (name, 64, "service-%d", balancer->n_services);

			connection_set_name (connection, name);
			connection_set_max_sleep (connection, 30);

			balancer->services[balancer->next_service] = connection;
			balancer->next_service += 1;

			retval = 0;
		}
	}

	return retval;

}

#pragma endregion