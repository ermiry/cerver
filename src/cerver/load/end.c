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
#include "cerver/client/connect.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/load/balancer.h"
#include "cerver/load/end.h"
#include "cerver/load/internal.h"
#include "cerver/load/service.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region end

// stops the threads that connect the services that were down when the balancer started
static void balancer_reconnect_threads_stop (Balancer *balancer) {

	(void) pthread_mutex_lock (balancer->mutex);
	__atomic_store_n (&balancer->running, false, __ATOMIC_SEQ_CST);
	(void) pthread_cond_broadcast (balancer->cond);
	(void) pthread_mutex_unlock (balancer->mutex);

	for (int i = 0; i < balancer->n_services; i++) {
		Service *service = balancer->services[i];
		if (service && service->reconnect_thread_running) {
			(void) pthread_join (service->reconnect_thread_id, NULL);
			service->reconnect_thread_running = false;
			service->reconnect_thread_id = 0;
		}
	}

}

static void balancer_services_set_status (
	Balancer *balancer, const ServiceStatus status
) {

	for (int i = 0; i < balancer->n_services; i++) {
		if (balancer->services[i]) {
			balancer_service_set_status (balancer->services[i], status);
		}
	}

}

// first ends and destroys the balancer's internal cerver
// then disconnects from each of the registered services
// last frees any balancer memory left
// returns 0 on success, 1 on error
u8 balancer_teardown (Balancer *balancer) {

	u8 retval = 1;

	if (balancer) {
		balancer_services_set_status (balancer, SERVICE_STATUS_DISCONNECTING);

		balancer_reconnect_threads_stop (balancer);

		// the receive threads of the clients end and remove their routes
		(void) cerver_teardown (balancer->cerver);
		balancer->cerver = NULL;

		// the update threads of the services are joined
		(void) client_teardown (balancer->client);
		balancer->client = NULL;

		// the connections with the services have ended
		balancer_services_set_status (balancer, SERVICE_STATUS_ENDED);

		balancer_delete (balancer);

		retval = 0;
	}

	return retval;

}

#pragma endregion
