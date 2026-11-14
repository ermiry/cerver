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
#include "cerver/core/start.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/load/balancer.h"
#include "cerver/load/internal.h"
#include "cerver/load/service.h"
#include "cerver/load/start.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region start

static u8 balancer_start_check (Balancer *balancer) {

	u8 retval = 1;

	if (balancer->next_service) {
		if (balancer->next_service == balancer->n_services) {
			// the first round robin selection starts with the first service
			balancer->next_service = balancer->n_services;

			retval = 0;
		}

		else {
			cerver_log_error (
				"Balancer registered services doesn't match the configured number: %d != %d",
				balancer->next_service, balancer->n_services
			);
		}
	}

	else {
		cerver_log_error ("No service has been registered to the load balancer!");
	}

	return retval;

}

// the services that are down are connected later by a dedicated thread
// returns 0 if at least one service is working, 1 on error
static u8 balancer_start_client (Balancer *balancer) {

	int working = 0;

	for (int i = 0; i < balancer->n_services; i++) {
		if (!balancer_service_connect (balancer, balancer->services[i])) {
			working += 1;
		}
	}

	if (working) {
		for (int i = 0; i < balancer->n_services; i++) {
			if (balancer_service_get_status (balancer->services[i]) == SERVICE_STATUS_UNAVAILABLE) {
				balancer_service_reconnect_start (balancer, balancer->services[i]);
			}
		}
	}

	else {
		cerver_log_error ("Balancer %s - none of its services is available!", balancer->name->str);
	}

	return working ? 0 : 1;

}

static u8 balancer_start_cerver (Balancer *balancer) {

	u8 errors = 0;

	if (cerver_start (balancer->cerver)) {
		cerver_log_error ("Failed to start load balancer's cerver!");
		errors = 1;
	}

	return errors;

}

// starts the load balancer by first connecting to the registered services
// and checking their ability to handle requests
// the services that are down are connected later by a dedicated thread,
// at least one service must be working
// then the cerver gets started to enable client connections
// returns 0 on success, 1 on error
u8 balancer_start (Balancer *balancer) {

	u8 retval = 1;

	if (balancer) {
		if (!balancer_start_check (balancer)) {
			__atomic_store_n (&balancer->running, true, __ATOMIC_SEQ_CST);

			balancer_events_register (balancer);

			if (!balancer_start_client (balancer)) {
				retval = balancer_start_cerver (balancer);
			}
		}
	}

	return retval;

}

#pragma endregion
