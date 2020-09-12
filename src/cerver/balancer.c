#include <stdlib.h>

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"

Balancer *balancer_new (void) {

	Balancer *balancer = (Balancer *) malloc (sizeof (Balancer));
	if (balancer) {
		balancer->type = BALANCER_TYPE_NONE;

		balancer->cerver = NULL;
		balancer->client = NULL;

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