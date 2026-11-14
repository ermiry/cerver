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
#include "cerver/client/events.h"
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

#pragma region events

// the event of a connection of the balancer's client
static Service *balancer_event_service (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	Balancer *balancer = (Balancer *) event_data->action_args;
	Service *service = balancer_service_get_with_connection (balancer, event_data->connection);

	client_event_data_delete (event_data);

	return service;

}

// a service that its connection connected again
static void *balancer_event_connected (void *event_data_ptr) {

	Service *service = balancer_event_service (event_data_ptr);
	if (service) {
		balancer_service_change_status (service, SERVICE_STATUS_DISCONNECTED, SERVICE_STATUS_WORKING);
		balancer_service_change_status (service, SERVICE_STATUS_UNAVAILABLE, SERVICE_STATUS_WORKING);
	}

	return NULL;

}

// the service closed its connection, it reconnects by itself
static void *balancer_event_connection_close (void *event_data_ptr) {

	Service *service = balancer_event_service (event_data_ptr);
	if (service) {
		balancer_service_change_status (service, SERVICE_STATUS_WORKING, SERVICE_STATUS_DISCONNECTED);
	}

	return NULL;

}

static void *balancer_event_connection_failed (void *event_data_ptr) {

	Service *service = balancer_event_service (event_data_ptr);
	if (service) {
		balancer_service_change_status (service, SERVICE_STATUS_DISCONNECTED, SERVICE_STATUS_UNAVAILABLE);
	}

	return NULL;

}

void balancer_events_register (Balancer *balancer) {

	(void) client_event_register (
		balancer->client, CLIENT_EVENT_CONNECTED,
		balancer_event_connected, balancer, NULL,
		false, false
	);

	(void) client_event_register (
		balancer->client, CLIENT_EVENT_CONNECTION_CLOSE,
		balancer_event_connection_close, balancer, NULL,
		false, false
	);

	(void) client_event_register (
		balancer->client, CLIENT_EVENT_CONNECTION_FAILED,
		balancer_event_connection_failed, balancer, NULL,
		false, false
	);

}

#pragma endregion
