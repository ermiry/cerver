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
#include "cerver/client/requests.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
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

#pragma region services

static BalancerService *balancer_service_aux_new (
	Balancer *balancer, Service *service
) {

	BalancerService *bs = (BalancerService *) malloc (sizeof (BalancerService));
	if (bs) {
		bs->balancer = balancer;
		bs->service = service;
	}

	return bs;

}

static void balancer_service_aux_delete (void *bs_ptr) {

	if (bs_ptr) free (bs_ptr);

}

const char *balancer_service_status_to_string (
	const ServiceStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case SERVICE_STATUS_##name: return #string;
		SERVICE_STATUS_MAP(XX)
		#undef XX
	}

	return balancer_service_status_to_string (SERVICE_STATUS_NONE);

}

const char *balancer_service_status_description (
	const ServiceStatus status
) {

	switch (status) {
		#define XX(num, name, string, description) case SERVICE_STATUS_##name: return #description;
		SERVICE_STATUS_MAP(XX)
		#undef XX
	}

	return balancer_service_status_description (SERVICE_STATUS_NONE);

}

static ServiceStats *balancer_service_stats_new (void) {

	ServiceStats *stats = (ServiceStats *) malloc (sizeof (ServiceStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (ServiceStats));
	}

	return stats;

}

static void balancer_service_stats_delete (ServiceStats *stats) {

	if (stats) free (stats);

}

void balancer_service_stats_print (const Service *service) {

	if (service) {
		if (cerver_log_get_output_type () != LOG_OUTPUT_TYPE_FILE)
			cerver_log_msg (LOG_COLOR_BLUE "Service: %s\n" LOG_COLOR_RESET, service->name);

		else cerver_log_msg ("Service: %s\n", service->name);

		// routed packets to the service
		cerver_log_msg ("Routed packets:              %lu", CERVER_STATS_GET (service->stats->n_packets_routed));
		cerver_log_msg ("Routed bytes:                %lu\n", CERVER_STATS_GET (service->stats->total_bytes_routed));

		// good types packets received from the service
		cerver_log_msg ("Receives done:               %lu", CERVER_STATS_GET (service->stats->receives_done));
		cerver_log_msg ("Received packets:            %lu", CERVER_STATS_GET (service->stats->n_packets_received));
		cerver_log_msg ("Received bytes:              %lu\n", CERVER_STATS_GET (service->stats->bytes_received));

		// bad types packets - their data was read and discarded
		cerver_log_msg ("Bad receives done:           %lu", CERVER_STATS_GET (service->stats->bad_receives_done));
		cerver_log_msg ("Bad received packets:        %lu", CERVER_STATS_GET (service->stats->bad_n_packets_received));
		cerver_log_msg ("Bad received bytes:          %lu\n", CERVER_STATS_GET (service->stats->bad_bytes_received));

		cerver_log_msg ("Routed packets:");
		packets_per_type_array_print (service->stats->routed_packets);

		cerver_log_line_break ();
		cerver_log_msg ("Received packets:");
		packets_per_type_array_print (service->stats->received_packets);
	}

}

static Service *balancer_service_new (void) {

	Service *service = (Service *) malloc (sizeof (Service));
	if (service) {
		service->id = 0;
		(void) memset (service->name, 0, SERVICE_NAME_SIZE);

		service->status = SERVICE_STATUS_NONE;
		service->on_status_change = NULL;

		service->connection = NULL;
		service->reconnect_wait_time = SERVICE_DEFAULT_WAIT_TIME;

		service->reconnect_thread_running = false;
		service->reconnect_thread_id = 0;

		service->stats = NULL;
	}

	return service;

}

void balancer_service_delete (void *service_ptr) {

	if (service_ptr) {
		Service *service = (Service *) service_ptr;

		// the connection belongs to the balancer's client
		service->connection = NULL;

		balancer_service_stats_delete (service->stats);

		free (service_ptr);
	}

}

Service *balancer_service_create (void) {

	Service *service = balancer_service_new ();
	if (service) {
		service->stats = balancer_service_stats_new ();
		if (!service->stats) {
			balancer_service_delete (service);
			service = NULL;
		}
	}

	return service;

}

ServiceStatus balancer_service_get_status (const Service *service) {

	return __atomic_load_n (&service->status, __ATOMIC_SEQ_CST);

}

const char *balancer_service_get_status_string (
	const Service *service
) {

	return balancer_service_status_to_string (balancer_service_get_status (service));

}

void balancer_service_set_status (
	Service *service, const ServiceStatus status
) {

	__atomic_store_n (&service->status, status, __ATOMIC_SEQ_CST);

	if (service->on_status_change) {
		service->on_status_change (service);
	}

}

// the status only changes if it still is the expected one,
// so a status of the teardown is never replaced
void balancer_service_change_status (
	Service *service, ServiceStatus expected, const ServiceStatus status
) {

	if (__atomic_compare_exchange_n (
		&service->status, &expected, status,
		false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST
	)) {
		if (service->on_status_change) {
			service->on_status_change (service);
		}
	}

}

void balancer_service_set_on_status_change (
	Service *service, const Action on_status_change
) {

	if (service) {
		service->on_status_change = on_status_change;
	}

}

static unsigned int balancer_service_register_internal (
	Balancer *balancer,
	const char *ip_address, const u16 port
) {

	unsigned int retval = 1;

	Service *service = balancer_service_create ();
	if (service) {
		Connection *connection = client_connection_create (
			balancer->client,
			ip_address, port,
			PROTOCOL_TCP, false
		);

		BalancerService *bs = balancer_service_aux_new (balancer, service);

		if (connection && bs) {
			service->id = (unsigned int) balancer->next_service;

			(void) snprintf (
				service->name, SERVICE_NAME_SIZE,
				"service-%u", service->id
			);

			service->connection = connection;

			connection_set_name (connection, service->name);
			// a single attempt, the reconnect thread and the connection itself retry
			connection_set_max_sleep (connection, SERVICE_CONNECT_MAX_SLEEP);

			connection_set_receive_buffer_size (connection, PACKET_HEADER_SIZE);
			connection_set_custom_receive (
				connection,
				balancer_client_receive,
				bs,
				balancer_service_aux_delete
			);

			balancer->services[balancer->next_service] = service;
			balancer->next_service += 1;

			retval = 0;
		}

		else {
			balancer_service_aux_delete (bs);
			balancer_service_delete (service);
		}
	}

	return retval;

}

// registers a new service to the load balancer
// a dedicated connection will be created when the balancer starts
// to handle traffic to & from the service
// returns 0 on success, 1 on error
unsigned int balancer_service_register (
	Balancer *balancer,
	const char *ip_address, const u16 port
) {

	unsigned int retval = 1;

	if (balancer && ip_address) {
		if ((balancer->next_service + 1) <= balancer->n_services) {
			retval = balancer_service_register_internal (
				balancer, ip_address, port
			);
		}
	}

	return retval;

}

const char *balancer_service_get_name (
	const Service *service
) {

	return service ? service->name : NULL;

}

// sets the service's name
void balancer_service_set_name (
	Service *service, const char *name
) {

	if (service && name) {
		(void) strncpy (service->name, name, SERVICE_NAME_SIZE - 1);
	}

}

// sets the time (in secs) to wait to attempt a reconnection whenever the service disconnects
// the default value is 20 secs
void balancer_service_set_reconnect_wait_time (
	Service *service, const unsigned int wait_time
) {

	if (service) service->reconnect_wait_time = wait_time;

}

// returns the service that uses the connection, NULL if there is none
Service *balancer_service_get_with_connection (
	const Balancer *balancer, const Connection *connection
) {

	Service *service = NULL;

	for (int i = 0; i < balancer->n_services; i++) {
		if (balancer->services[i] && (balancer->services[i]->connection == connection)) {
			service = balancer->services[i];
			break;
		}
	}

	return service;

}

// sends a test message to the service & waits for the request
static u8 balancer_service_test (
	Balancer *balancer, Service *service
) {

	u8 retval = 1;

	// the ping header is initialized with its padding zeroed
	Packet ping;
	packet_init_ping (&ping);

	if (!client_request_to_cerver (
		balancer->client, service->connection, &ping
	)) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to send test request to %s",
			service->name
		);
	}

	return retval;

}

// connects to the service, the connection reconnects by itself after this
// sends a test packet to check its ability to handle requests
// returns 0 on success, 1 on error
u8 balancer_service_connect (
	Balancer *balancer, Service *service
) {

	u8 retval = 1;

	balancer_service_set_status (service, SERVICE_STATUS_CONNECTING);

	connection_set_attempt_reconnect (service->connection, service->reconnect_wait_time);

	if (!client_connect_to_cerver (balancer->client, service->connection)) {
		cerver_log_success ("Connected to %s", service->name);

		// we are ready to test service handler
		balancer_service_set_status (service, SERVICE_STATUS_READY);

		// send test message to service
		if (
			!balancer_service_test (balancer, service)
			&& !client_connection_start (balancer->client, service->connection)
		) {
			// the service is able to handle packets
			balancer_service_set_status (service, SERVICE_STATUS_WORKING);

			retval = 0;
		}
	}

	if (retval) {
		cerver_log_error ("Failed to connect to %s", service->name);

		// a new socket for the next attempt
		connection_reset (service->connection);

		balancer_service_set_status (service, SERVICE_STATUS_UNAVAILABLE);
	}

	return retval;

}

static bool balancer_is_running (Balancer *balancer) {

	return __atomic_load_n (&balancer->running, __ATOMIC_SEQ_CST);

}

// waits the time between two attempts, the teardown wakes it up
// returns true if the balancer is still running
static bool balancer_service_reconnect_wait (
	Balancer *balancer, const Service *service
) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) service->reconnect_wait_time;

	(void) pthread_mutex_lock (balancer->mutex);

	int result = 0;
	while (balancer_is_running (balancer) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (balancer->cond, balancer->mutex, &deadline);
	}

	(void) pthread_mutex_unlock (balancer->mutex);

	return balancer_is_running (balancer);

}

// connects a service that was down when the balancer started,
// after it is connected, its connection reconnects by itself
static void *balancer_service_reconnect_thread (void *bs_ptr) {

	BalancerService *bs = (BalancerService *) bs_ptr;

	Balancer *balancer = bs->balancer;
	Service *service = bs->service;

	balancer_service_aux_delete (bs);

	bool connected = false;
	while (!connected && balancer_service_reconnect_wait (balancer, service)) {
		cerver_log_debug (
			"Attempting connection to balancer %s service %s",
			balancer->name->str, service->name
		);

		connected = !balancer_service_connect (balancer, service);
	}

	return NULL;

}

void balancer_service_reconnect_start (
	Balancer *balancer, Service *service
) {

	BalancerService *bs = balancer_service_aux_new (balancer, service);
	if (bs) {
		service->reconnect_thread_running = true;

		if (thread_create_joinable (
			&service->reconnect_thread_id,
			balancer_service_reconnect_thread, bs
		)) {
			service->reconnect_thread_running = false;
			balancer_service_aux_delete (bs);

			cerver_log_error (
				"Failed to create reconnect thread for balancer %s service %s",
				balancer->name->str, service->name
			);
		}
	}

}

#pragma endregion
