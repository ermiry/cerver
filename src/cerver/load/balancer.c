#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/load/balancer.h"
#include "cerver/load/internal.h"
#include "cerver/load/service.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region types

const char *balancer_type_to_string (const BalancerType type) {

	switch (type) {
		#define XX(num, name, string) case BALANCER_TYPE_##name: return #string;
		BALANCER_TYPE_MAP(XX)
		#undef XX
	}

	return balancer_type_to_string (BALANCER_TYPE_NONE);

}

#pragma endregion

#pragma region main

Balancer *balancer_new (void) {

	Balancer *balancer = (Balancer *) malloc (sizeof (Balancer));
	if (balancer) {
		balancer->name = NULL;
		balancer->type = BALANCER_TYPE_NONE;

		balancer->select_service = NULL;
		balancer->select_service_args = NULL;

		balancer->cerver = NULL;
		balancer->client = NULL;

		balancer->next_service = 0;
		balancer->n_services = 0;
		balancer->services = NULL;

		balancer->on_unavailable_services = NULL;

		balancer->stats = NULL;

		balancer->mutex = NULL;

		balancer->routes = NULL;
		balancer->routes_size = 0;
		balancer->routes_used = 0;
		balancer->free_route = BALANCER_ROUTE_NONE;

		balancer->running = false;
		balancer->cond = NULL;
	}

	return balancer;

}

// a slot that was never used starts in the first generation
void balancer_routes_init (BalancerRoute *routes, const u32 count) {

	for (u32 i = 0; i < count; i++) {
		routes[i].socket = NULL;
		routes[i].next_free = BALANCER_ROUTE_NONE;
		routes[i].generation = BALANCER_ROUTE_FIRST_GENERATION;
	}

}

// releases the references of the routes that are left
static void balancer_routes_delete (Balancer *balancer) {

	if (balancer->routes) {
		for (u32 i = 0; i < balancer->routes_used; i++) {
			socket_delete (balancer->routes[i].socket);
		}

		free (balancer->routes);
		balancer->routes = NULL;
		balancer->routes_size = 0;
		balancer->routes_used = 0;
		balancer->free_route = BALANCER_ROUTE_NONE;
	}

}

void balancer_delete (void *balancer_ptr) {

	if (balancer_ptr) {
		Balancer *balancer = (Balancer *) balancer_ptr;

		str_delete (balancer->name);

		cerver_delete (balancer->cerver);
		client_delete (balancer->client);

		if (balancer->services) {
			for (int i = 0; i < balancer->n_services; i++)
				balancer_service_delete (balancer->services[i]);

			free (balancer->services);
		}

		balancer_stats_delete (balancer->stats);

		balancer_routes_delete (balancer);

		thread_mutex_delete (balancer->mutex);
		thread_cond_delete (balancer->cond);

		free (balancer_ptr);
	}

}

BalancerType balancer_get_type (const Balancer *balancer) {

	return balancer->type;

}

// sets a cb method to be used whenever there are no services available
// a reference to the received packet will be passed to the method
void balancer_set_on_unavailable_services (
	Balancer *balancer, const Action on_unavailable_services
) {

	if (balancer) {
		balancer->on_unavailable_services = on_unavailable_services;
	}

}

// sets, before the balancer starts, the method that selects the service of each packet
// in a BALANCER_TYPE_CUSTOM balancer
void balancer_set_select_service (
	Balancer *balancer, const BalancerSelectService select_service, void *args
) {

	if (balancer) {
		balancer->select_service = select_service;
		balancer->select_service_args = args;
	}

}

static bool balancer_create_values (
	Balancer *balancer,
	const u16 port, const u16 connection_queue
) {

	balancer->cerver = cerver_create (
		CERVER_TYPE_BALANCER,
		"balancer-cerver",
		port, PROTOCOL_TCP, false, connection_queue
	);

	if (balancer->cerver) {
		balancer->cerver->balancer = balancer;

		// the balancer receives and routes complete packets in each client's thread
		cerver_set_handler_type (balancer->cerver, CERVER_HANDLER_TYPE_THREADS);
	}

	balancer->client = client_create ();
	if (balancer->client) client_set_name (balancer->client, "balancer-client");

	balancer->services = (Service **) calloc ((size_t) balancer->n_services, sizeof (Service *));

	balancer->stats = balancer_stats_new ();

	balancer->mutex = thread_mutex_new ();
	balancer->cond = thread_cond_new ();

	balancer->routes = (BalancerRoute *) malloc (BALANCER_DEFAULT_ROUTES_SIZE * sizeof (BalancerRoute));
	if (balancer->routes) {
		balancer_routes_init (balancer->routes, BALANCER_DEFAULT_ROUTES_SIZE);
		balancer->routes_size = BALANCER_DEFAULT_ROUTES_SIZE;
	}

	return balancer->cerver && balancer->client
		&& balancer->services && balancer->stats
		&& balancer->mutex && balancer->cond && balancer->routes;

}

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
// the cerver of the balancer handles each client in a dedicated thread
Balancer *balancer_create (
	const char *name, const BalancerType type,
	const u16 port, const u16 connection_queue,
	const unsigned int n_services
) {

	Balancer *balancer = NULL;

	if (name && n_services) {
		balancer = balancer_new ();
		if (balancer) {
			balancer->name = str_new (name);
			balancer->type = type;

			balancer->n_services = (int) n_services;

			if (!balancer->name || !balancer_create_values (balancer, port, connection_queue)) {
				cerver_log_error ("balancer_create () - failed to create balancer %s!", name);

				balancer_delete (balancer);
				balancer = NULL;
			}
		}
	}

	return balancer;

}

#pragma endregion
