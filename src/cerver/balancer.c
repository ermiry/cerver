#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/uio.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/socket.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// a routed packet is sent as its header and its body
#define BALANCER_SEND_PARTS					2

static const char *balancer_services_unavailable = { "Services unavailable" };

void balancer_service_delete (void *service_ptr);

void balancer_service_stats_print (const Service *service);

static u8 balancer_client_receive (
	void *custom_data_ptr,
	char *buffer, const size_t buffer_size
);

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

#pragma region stats

static BalancerStats *balancer_stats_new (void) {

	BalancerStats *stats = (BalancerStats *) malloc (sizeof (BalancerStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (BalancerStats));
	}

	return stats;

}

static void balancer_stats_delete (BalancerStats *stats) {

	if (stats) free (stats);

}

// the index of a packet type in the stats arrays,
// the type comes from the network, so it is checked against the arrays size
static inline unsigned int balancer_packet_type_index (const PacketType packet_type) {

	return (((unsigned int) packet_type == PACKET_TYPE_NONE) || ((unsigned int) packet_type >= PACKET_TYPE_BAD)) ?
		(unsigned int) PACKET_TYPE_BAD : (unsigned int) packet_type;

}

void balancer_stats_print (const Balancer *balancer) {

	if (balancer) {
		if (cerver_log_get_output_type () != LOG_OUTPUT_TYPE_FILE)
			cerver_log_msg (LOG_COLOR_BLUE "Balancer: %s\n" LOG_COLOR_RESET, balancer->name->str);

		else cerver_log_msg ("Balancer: %s\n", balancer->name->str);

		// good types packets that the balancer can handle
		cerver_log_msg ("Receives done:               %lu", CERVER_STATS_GET (balancer->stats->receives_done));
		cerver_log_msg ("Received packets:            %lu", CERVER_STATS_GET (balancer->stats->n_packets_received));
		cerver_log_msg ("Received bytes:              %lu\n", CERVER_STATS_GET (balancer->stats->bytes_received));

		// bad types packets - their data was read and discarded
		cerver_log_msg ("Bad receives done:           %lu", CERVER_STATS_GET (balancer->stats->bad_receives_done));
		cerver_log_msg ("Bad received packets:        %lu", CERVER_STATS_GET (balancer->stats->bad_n_packets_received));
		cerver_log_msg ("Bad received bytes:          %lu\n", CERVER_STATS_GET (balancer->stats->bad_bytes_received));

		// routed packets to services
		cerver_log_msg ("Routed packets:              %lu", CERVER_STATS_GET (balancer->stats->n_packets_routed));
		cerver_log_msg ("Routed bytes:                %lu\n", CERVER_STATS_GET (balancer->stats->total_bytes_routed));

		// responses sent to the original clients
		cerver_log_msg ("Responses sent packets:      %lu", CERVER_STATS_GET (balancer->stats->n_packets_sent));
		cerver_log_msg ("Responses sent bytes:        %lu", CERVER_STATS_GET (balancer->stats->total_bytes_sent));
		cerver_log_msg ("Discarded responses:         %lu\n", CERVER_STATS_GET (balancer->stats->discarded_responses));

		// packets handled by balancer on unavailable services
		cerver_log_msg ("Backup packets:              %lu", CERVER_STATS_GET (balancer->stats->backup_handled_packets));
		cerver_log_msg ("Backup bytes:                %lu\n", CERVER_STATS_GET (balancer->stats->backup_handled_bytes));

		// packets that the balancer was unable to handle
		cerver_log_msg ("Unhandled packets:           %lu", CERVER_STATS_GET (balancer->stats->unhandled_packets));
		cerver_log_msg ("Unhandled bytes:             %lu\n", CERVER_STATS_GET (balancer->stats->unhandled_bytes));

		cerver_log_msg ("Received packets:");
		packets_per_type_array_print (balancer->stats->received_packets);

		cerver_log_line_break ();
		cerver_log_msg ("Routed packets:");
		packets_per_type_array_print (balancer->stats->routed_packets);

		cerver_log_line_break ();
		cerver_log_msg ("Responses packets:");
		packets_per_type_array_print (balancer->stats->sent_packets);

		for (int i = 0; i < balancer->n_services; i++) {
			cerver_log_line_break ();
			balancer_service_stats_print (balancer->services[i]);
			cerver_log_line_break ();
		}
	}

}

#pragma endregion

#pragma region main

Balancer *balancer_new (void) {

	Balancer *balancer = (Balancer *) malloc (sizeof (Balancer));
	if (balancer) {
		balancer->name = NULL;
		balancer->type = BALANCER_TYPE_NONE;

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

		balancer->running = false;
		balancer->cond = NULL;
	}

	return balancer;

}

// releases the references of the routes that are left
static void balancer_routes_delete (Balancer *balancer) {

	if (balancer->routes) {
		for (size_t i = 0; i < balancer->routes_size; i++) {
			socket_delete (balancer->routes[i].socket);
		}

		free (balancer->routes);
		balancer->routes = NULL;
		balancer->routes_size = 0;
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

	balancer->routes = (BalancerRoute *) calloc (BALANCER_DEFAULT_ROUTES_SIZE, sizeof (BalancerRoute));
	if (balancer->routes) balancer->routes_size = BALANCER_DEFAULT_ROUTES_SIZE;

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

#pragma region services

// auxiliary structure to be passed in ConnectionCustomReceiveData
typedef struct {

	Balancer *balancer;
	Service *service;

} BalancerService;

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

static void balancer_service_set_status (
	Service *service, const ServiceStatus status
) {

	__atomic_store_n (&service->status, status, __ATOMIC_SEQ_CST);

	if (service->on_status_change) {
		service->on_status_change (service);
	}

}

// the status only changes if it still is the expected one,
// so a status of the teardown is never replaced
static void balancer_service_change_status (
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

			connection_set_receive_buffer_size (connection, sizeof (PacketHeader));
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
static Service *balancer_service_get_by_connection (
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
static u8 balancer_service_connect (
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

static void balancer_service_reconnect_start (
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

#pragma region events

// the event of a connection of the balancer's client
static Service *balancer_event_service (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	Balancer *balancer = (Balancer *) event_data->action_args;
	Service *service = balancer_service_get_by_connection (balancer, event_data->connection);

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

static void balancer_events_register (Balancer *balancer) {

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

#pragma region route

typedef enum BalancerReceiveResult {

	BALANCER_RECEIVE_RESULT_DONE		= 0,
	BALANCER_RECEIVE_RESULT_IDLE		= 1,	// nothing was received before the timeout
	BALANCER_RECEIVE_RESULT_FAILED		= 2

} BalancerReceiveResult;

// receives exactly size bytes from the socket
// an idle socket returns BALANCER_RECEIVE_RESULT_IDLE only if allow_idle is set
// and nothing was received, a packet that stops in the middle fails
// after BALANCER_RECEIVE_MAX_TIMEOUTS timeouts
static BalancerReceiveResult balancer_receive_exact (
	const int sock_fd, char *buffer, const size_t size,
	const bool allow_idle
) {

	BalancerReceiveResult result = BALANCER_RECEIVE_RESULT_DONE;

	size_t received = 0;
	unsigned int timeouts = 0;
	while ((result == BALANCER_RECEIVE_RESULT_DONE) && (received < size)) {
		ssize_t rc = recv (sock_fd, buffer + received, size - received, 0);
		if (rc > 0) {
			received += (size_t) rc;
			timeouts = 0;
		}

		else if (!rc) result = BALANCER_RECEIVE_RESULT_FAILED;

		else if (errno == EINTR) continue;

		else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			if (allow_idle && !received) result = BALANCER_RECEIVE_RESULT_IDLE;
			else if (++timeouts >= BALANCER_RECEIVE_MAX_TIMEOUTS) result = BALANCER_RECEIVE_RESULT_FAILED;
		}

		else result = BALANCER_RECEIVE_RESULT_FAILED;
	}

	return result;

}

// sends the header and the body of a packet to a socket,
// with a single call when the socket takes them
// returns 0 on success, 1 on error
static u8 balancer_send_packet (
	Socket *socket,
	const PacketHeader *header, const char *body, const size_t body_size
) {

	u8 retval = 1;

	struct iovec parts[BALANCER_SEND_PARTS] = {
		{ .iov_base = (void *) header, .iov_len = sizeof (PacketHeader) },
		{ .iov_base = (void *) body, .iov_len = body_size }
	};

	(void) pthread_mutex_lock (socket->write_mutex);

	if (socket->sock_fd > -1) {
		retval = socket_send_vector (
			socket,
			parts, (body && body_size) ? BALANCER_SEND_PARTS : 1,
			0, (time_t) CERVER_DEFAULT_SEND_TIMEOUT,
			NULL, NULL
		);
	}

	(void) pthread_mutex_unlock (socket->write_mutex);

	return retval;

}

// a header from the network must describe a packet that can be received
static inline bool balancer_header_is_valid (
	const PacketHeader *header, const size_t max_packet_size
) {

	return (header->packet_size >= sizeof (PacketHeader))
		&& (!max_packet_size || (header->packet_size <= max_packet_size));

}

// receives the body of a packet, the caller frees it
// returns 0 on success, 1 on error
static u8 balancer_receive_body (
	const int sock_fd, const PacketHeader *header,
	char **body, size_t *body_size
) {

	u8 retval = 0;

	*body_size = header->packet_size - sizeof (PacketHeader);
	*body = NULL;

	if (*body_size) {
		*body = (char *) malloc (*body_size);
		if (!*body || balancer_receive_exact (sock_fd, *body, *body_size, false)) {
			free (*body);
			*body = NULL;
			retval = 1;
		}
	}

	return retval;

}

static inline int balancer_get_next_service_round_robin (
	Balancer *balancer
) {

	balancer->next_service += 1;
	if (balancer->next_service >= balancer->n_services)
		balancer->next_service = 0;

	return balancer->next_service;

}

static inline bool balancer_service_is_working (const Service *service) {

	return service && (balancer_service_get_status (service) == SERVICE_STATUS_WORKING);

}

// the handler id comes from the network, so it is checked against the services
static int balancer_get_next_service (
	Balancer *balancer, const PacketHeader *header
) {

	int retval = -1;

	(void) pthread_mutex_lock (balancer->mutex);

	switch (balancer->type) {
		case BALANCER_TYPE_ROUND_ROBIN: {
			for (int count = 0; count < balancer->n_services; count++) {
				const int next = balancer_get_next_service_round_robin (balancer);
				if (balancer_service_is_working (balancer->services[next])) {
					retval = next;
					break;
				}
			}
		} break;

		case BALANCER_TYPE_HANDLER_ID: {
			if (
				((int) header->handler_id < balancer->n_services)
				&& balancer_service_is_working (balancer->services[header->handler_id])
			) {
				retval = (int) header->handler_id;
			}
		} break;

		default: break;
	}

	(void) pthread_mutex_unlock (balancer->mutex);

	return retval;

}

// a packet that references the received header and body,
// it is only used while it is handled
static void balancer_packet_init (
	Packet *packet, CerverReceive *cr,
	const PacketHeader *header, char *body, const size_t body_size
) {

	(void) memset (packet, 0, sizeof (Packet));

	packet->cerver = cr->cerver;
	packet->client = cr->client;
	packet->connection = cr->connection;
	packet->lobby = cr->lobby;

	packet->packet_type = header->packet_type;
	packet->req_type = header->request_type;

	packet->data_size = body_size;
	packet->data = body;
	packet->data_ptr = body;
	packet->data_end = body ? body + body_size : NULL;
	packet->data_ref = true;

	packet->header = *header;
	packet->version = header->version;

	packet->packet_size = header->packet_size;
	packet->packet_ref = true;

}

static void balancer_handle_unavailable_services (
	CerverReceive *cr, Balancer *balancer,
	const PacketHeader *header, char *body, const size_t body_size
) {

	if (balancer->on_unavailable_services) {
		Packet packet;
		balancer_packet_init (&packet, cr, header, body, body_size);

		// handle actual packet
		balancer->on_unavailable_services (&packet);

		CERVER_STATS_ADD (balancer->stats->backup_handled_packets, 1);
		CERVER_STATS_ADD (balancer->stats->backup_handled_bytes, header->packet_size);
	}

	else {
		// send back generic error message
		(void) error_packet_generate_and_send (
			CERVER_ERROR_PACKET_ERROR, balancer_services_unavailable,
			balancer->cerver, cr->client, cr->connection
		);

		CERVER_STATS_ADD (balancer->stats->unhandled_packets, 1);
		CERVER_STATS_ADD (balancer->stats->unhandled_bytes, header->packet_size);
	}

}

// routes the received packet to a service to be handled
// the header carries the client's sock fd to route the response back
static void balancer_route_to_service (
	CerverReceive *cr, Balancer *balancer,
	PacketHeader *header, char *body, const size_t body_size
) {

	int selected_service = balancer_get_next_service (balancer, header);
	if (selected_service >= 0) {
		Service *service = balancer->services[selected_service];

		header->sock_fd = (u16) cr->socket->sock_fd;

		if (!balancer_send_packet (service->connection->socket, header, body, body_size)) {
			const unsigned int type = balancer_packet_type_index (header->packet_type);

			CERVER_STATS_ADD (service->stats->n_packets_routed, 1);
			CERVER_STATS_ADD (service->stats->total_bytes_routed, header->packet_size);
			CERVER_STATS_ADD (service->stats->routed_packets[type], 1);

			CERVER_STATS_ADD (balancer->stats->n_packets_routed, 1);
			CERVER_STATS_ADD (balancer->stats->total_bytes_routed, header->packet_size);
			CERVER_STATS_ADD (balancer->stats->routed_packets[type], 1);
		}

		else {
			// a packet that was sent in part breaks the stream of the service,
			// so its connection is closed and it connects again
			cerver_log_warning (
				"Packet routing to %s has failed!", service->name
			);

			(void) connection_reconnect (service->connection);

			balancer_handle_unavailable_services (cr, balancer, header, body, body_size);
		}
	}

	else {
		cerver_log_warning ("No available services to handle packets!");

		balancer_handle_unavailable_services (cr, balancer, header, body, body_size);
	}

}

// registers the route of a client connection that the balancer receives from
void balancer_connection_start (
	Balancer *balancer, const i32 sock_fd, Socket *socket
) {

	if (balancer && (sock_fd > -1) && socket) {
		(void) pthread_mutex_lock (balancer->mutex);

		size_t new_size = balancer->routes_size;
		while ((size_t) sock_fd >= new_size) new_size *= BALANCER_ROUTES_GROWTH_FACTOR;

		if (new_size != balancer->routes_size) {
			BalancerRoute *routes = (BalancerRoute *) realloc (balancer->routes, new_size * sizeof (BalancerRoute));
			if (routes) {
				(void) memset (routes + balancer->routes_size, 0, (new_size - balancer->routes_size) * sizeof (BalancerRoute));
				balancer->routes = routes;
				balancer->routes_size = new_size;
			}
		}

		if ((size_t) sock_fd < balancer->routes_size) {
			socket_delete (balancer->routes[sock_fd].socket);
			balancer->routes[sock_fd].socket = socket_reference (socket);
		}

		(void) pthread_mutex_unlock (balancer->mutex);
	}

}

// removes the route of a client connection that has ended,
// its pending responses are discarded
void balancer_connection_end (
	Balancer *balancer, const i32 sock_fd
) {

	if (balancer && (sock_fd > -1)) {
		Socket *socket = NULL;

		(void) pthread_mutex_lock (balancer->mutex);

		if ((size_t) sock_fd < balancer->routes_size) {
			socket = balancer->routes[sock_fd].socket;
			balancer->routes[sock_fd].socket = NULL;
		}

		(void) pthread_mutex_unlock (balancer->mutex);

		socket_delete (socket);
	}

}

// returns a reference to the socket of the client connection, NULL if it has ended
static Socket *balancer_route_get (
	Balancer *balancer, const i32 sock_fd
) {

	Socket *socket = NULL;

	(void) pthread_mutex_lock (balancer->mutex);

	if (((size_t) sock_fd < balancer->routes_size) && balancer->routes[sock_fd].socket) {
		socket = socket_reference (balancer->routes[sock_fd].socket);
	}

	(void) pthread_mutex_unlock (balancer->mutex);

	return socket;

}

// a packet of type PACKET_TYPE_CLIENT is handled by the balancer's cerver
// returns 1 if the connection was ended by the packet
static u8 balancer_receive_client_packet (
	CerverReceive *cr, const PacketHeader *header
) {

	Packet packet;
	balancer_packet_init (&packet, cr, header, NULL, 0);

	(void) cerver_client_packet_handle (&packet);

	// the request closes the connection or disconnects the client
	return 1;

}

static void balancer_receive_bad_packet (
	Balancer *balancer, const PacketHeader *header
) {

	CERVER_STATS_ADD (balancer->stats->bad_receives_done, 1);
	CERVER_STATS_ADD (balancer->stats->bad_n_packets_received, 1);
	CERVER_STATS_ADD (balancer->stats->bad_bytes_received, header->packet_size);

	CERVER_STATS_ADD (balancer->stats->received_packets[PACKET_TYPE_BAD], 1);

}

static u8 balancer_receive_handle (
	CerverReceive *cr, Balancer *balancer,
	PacketHeader *header, char *body, const size_t body_size
) {

	u8 retval = 0;

	CERVER_STATS_ADD (cr->cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (cr->cerver->stats->total_bytes_received, header->packet_size);

	switch (header->packet_type) {
		case PACKET_TYPE_CLIENT:
			CERVER_STATS_ADD (cr->cerver->stats->received_packets->n_client_packets, 1);
			retval = balancer_receive_client_packet (cr, header);
			break;

		// only route packets of these types to services
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM:
		case PACKET_TYPE_TEST: {
			CERVER_STATS_ADD (balancer->stats->receives_done, 1);
			CERVER_STATS_ADD (balancer->stats->n_packets_received, 1);
			CERVER_STATS_ADD (balancer->stats->bytes_received, header->packet_size);
			CERVER_STATS_ADD (balancer->stats->received_packets[header->packet_type], 1);

			balancer_route_to_service (cr, balancer, header, body, body_size);
		} break;

		// the balancer is unable to handle these types
		case PACKET_TYPE_ERROR:
		case PACKET_TYPE_AUTH:
		case PACKET_TYPE_NONE:
		default: {
			CERVER_STATS_ADD (cr->cerver->stats->received_packets->n_bad_packets, 1);

			balancer_receive_bad_packet (balancer, header);
		} break;
	}

	return retval;

}

// receives a complete packet from a client and routes it to a service
// returns 0 to keep receiving, 1 when the connection has ended
u8 balancer_receive (CerverReceive *cr) {

	u8 retval = 1;

	Balancer *balancer = cr->cerver->balancer;

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	switch (balancer_receive_exact (cr->socket->sock_fd, (char *) &header, sizeof (PacketHeader), true)) {
		case BALANCER_RECEIVE_RESULT_DONE: {
			char *body = NULL;
			size_t body_size = 0;

			// a bad size can not be skipped, the next header is unknown
			if (!balancer_header_is_valid (&header, cr->cerver->max_received_packet_size)) {
				balancer_receive_bad_packet (balancer, &header);
			}

			else if (!balancer_receive_body (cr->socket->sock_fd, &header, &body, &body_size)) {
				retval = balancer_receive_handle (cr, balancer, &header, body, body_size);

				free (body);
			}
		} break;

		case BALANCER_RECEIVE_RESULT_IDLE: retval = 0; break;

		default: break;
	}

	return retval;

}

// routes a response of a service back to the original client,
// a response of a connection that has ended is discarded
static void balancer_client_route_response (
	BalancerService *bs, PacketHeader *header,
	const char *body, const size_t body_size
) {

	Balancer *balancer = bs->balancer;

	Socket *socket = balancer_route_get (balancer, (i32) header->sock_fd);
	if (socket) {
		if (!balancer_send_packet (socket, header, body, body_size)) {
			CERVER_STATS_ADD (balancer->stats->n_packets_sent, 1);
			CERVER_STATS_ADD (balancer->stats->total_bytes_sent, header->packet_size);
			CERVER_STATS_ADD (balancer->stats->sent_packets[balancer_packet_type_index (header->packet_type)], 1);
		}

		else {
			CERVER_STATS_ADD (balancer->stats->discarded_responses, 1);
		}

		socket_delete (socket);
	}

	else {
		#ifdef SERVICE_DEBUG
		cerver_log_warning (
			"balancer_client_route_response () - "
			"the connection with sock fd %u has ended",
			(unsigned int) header->sock_fd
		);
		#endif

		CERVER_STATS_ADD (balancer->stats->discarded_responses, 1);
	}

}

static void balancer_client_bad_packet (
	BalancerService *bs, const PacketHeader *header
) {

	CERVER_STATS_ADD (bs->service->stats->bad_receives_done, 1);
	CERVER_STATS_ADD (bs->service->stats->bad_n_packets_received, 1);
	CERVER_STATS_ADD (bs->service->stats->bad_bytes_received, header->packet_size);

}

// handles a complete packet from a service
static void balancer_client_receive_success (
	BalancerService *bs, Connection *connection,
	PacketHeader *header, const char *body, const size_t body_size
) {

	CERVER_STATS_ADD (bs->service->stats->receives_done, 1);
	CERVER_STATS_ADD (bs->service->stats->n_packets_received, 1);
	CERVER_STATS_ADD (bs->service->stats->bytes_received, header->packet_size);
	CERVER_STATS_ADD (bs->service->stats->received_packets[balancer_packet_type_index (header->packet_type)], 1);

	switch (header->packet_type) {
		// a packet of the service itself, like the cerver info
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_TEST: {
			if (header->sock_fd) {
				balancer_client_route_response (bs, header, body, body_size);
			}

			else {
				#ifdef SERVICE_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
					"Got a packet of type %u from service %s",
					(unsigned int) header->packet_type, connection->name
				);
				#endif
			}
		} break;

		// only route packets of these types back to original client
		case PACKET_TYPE_ERROR:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM:
			balancer_client_route_response (bs, header, body, body_size);
			break;

		case PACKET_TYPE_CLIENT:
		case PACKET_TYPE_AUTH:
		case PACKET_TYPE_NONE:
		default: {
			#ifdef SERVICE_DEBUG
			cerver_log_warning (
				"balancer_client_receive () - "
				"got a packet of unknown type %u from service %s",
				(unsigned int) header->packet_type, connection->name
			);
			#endif

			balancer_client_bad_packet (bs, header);
		} break;
	}

	(void) connection;

}

// custom receive method for packets comming from the services
// a failed receive ends the receive loop, then the connection reconnects
// returns 0 on success handle, 1 if any error ocurred
static u8 balancer_client_receive (
	void *custom_data_ptr,
	char *buffer, const size_t buffer_size
) {

	u8 retval = 1;

	(void) buffer;
	(void) buffer_size;

	ConnectionCustomReceiveData *custom_data
		= (ConnectionCustomReceiveData *) custom_data_ptr;

	BalancerService *bs = (BalancerService *) custom_data->args;
	Connection *connection = custom_data->connection;

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	switch (balancer_receive_exact (connection->socket->sock_fd, (char *) &header, sizeof (PacketHeader), true)) {
		case BALANCER_RECEIVE_RESULT_DONE: {
			char *body = NULL;
			size_t body_size = 0;

			// a bad size breaks the stream, the connection connects again
			if (!balancer_header_is_valid (&header, custom_data->client->max_received_packet_size)) {
				balancer_client_bad_packet (bs, &header);
			}

			else if (!balancer_receive_body (connection->socket->sock_fd, &header, &body, &body_size)) {
				balancer_client_receive_success (bs, connection, &header, body, body_size);

				free (body);

				retval = 0;
			}
		} break;

		case BALANCER_RECEIVE_RESULT_IDLE: retval = 0; break;

		default: break;
	}

	return retval;

}

#pragma endregion

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
