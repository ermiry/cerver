#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static void balancer_service_delete (void *service_ptr);

void balancer_service_stats_print (Service *service);

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
		memset (stats, 0, sizeof (BalancerStats));
	}

	return stats;

}

static void balancer_stats_delete (BalancerStats *stats) {

	if (stats) free (stats);

}

void balancer_stats_print (const Balancer *balancer) {

	if (balancer) {
		if (cerver_log_get_output_type () != LOG_OUTPUT_TYPE_FILE)
			cerver_log_msg (LOG_COLOR_BLUE "Balancer: %s\n" LOG_COLOR_RESET, balancer->name->str);

		else cerver_log_msg ("Balancer: %s\n", balancer->name->str);
	
		// good types packets that the balancer can handle
		cerver_log_msg ("Receives done:               %ld", balancer->stats->receives_done);
		cerver_log_msg ("Received packets:            %ld", balancer->stats->n_packets_received);
		cerver_log_msg ("Received bytes:              %ld\n", balancer->stats->bytes_received);

		// bad types packets - consumed data from sock fd until next header
		cerver_log_msg ("Bad receives done:           %ld", balancer->stats->bad_receives_done);
		cerver_log_msg ("Bad received packets:        %ld", balancer->stats->bad_n_packets_received);
		cerver_log_msg ("Bad received bytes:          %ld\n", balancer->stats->bad_bytes_received);

		// routed packets to services
		cerver_log_msg ("Routed packets:              %ld", balancer->stats->n_packets_routed);
		cerver_log_msg ("Routed bytes:                %ld\n", balancer->stats->total_bytes_routed);

		// responses sent to the original clients
		cerver_log_msg ("Responses sent packets:      %ld", balancer->stats->n_packets_sent);
		cerver_log_msg ("Responses sent bytes:        %ld\n", balancer->stats->total_bytes_sent);

		// packets that the balancer was unable to handle
		cerver_log_msg ("Unhandled packets:           %ld", balancer->stats->unhandled_packets);
		cerver_log_msg ("Unhandled bytes:             %ld\n", balancer->stats->unhandled_bytes);

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

		balancer->stats = NULL;

		balancer->mutex = NULL;
	}

	return balancer;

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

		pthread_mutex_delete (balancer->mutex);

		free (balancer_ptr);
	}

}

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
Balancer *balancer_create (
	const char *name, BalancerType type,
	u16 port, u16 connection_queue,
	unsigned int n_services
) {

	Balancer *balancer = balancer_new ();
	if (balancer) {
		balancer->name = str_new (name);
		balancer->type = type;

		balancer->cerver = cerver_create (
			CERVER_TYPE_BALANCER,
			"load-balancer-cerver",
			port, PROTOCOL_TCP, false, connection_queue
		);

		balancer->cerver->balancer = balancer;

		balancer->client = client_create ();
		client_set_name (balancer->client, "balancer-client");

		balancer->n_services = n_services;
		balancer->services = (Service **) calloc (balancer->n_services, sizeof (Service *));
		if (balancer->services) {
			for (int i = 0; i < balancer->n_services; i++)
				balancer->services[i] = NULL;
		}

		balancer->stats = balancer_stats_new ();

		balancer->mutex = pthread_mutex_new ();
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

	if (bs_ptr) {
		BalancerService *bs = (BalancerService *) bs_ptr;

		bs->balancer = NULL;
		bs->service = NULL;

		free (bs_ptr);
	}

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

void balancer_service_stats_print (Service *service) {

	if (service) {
		if (cerver_log_get_output_type () != LOG_OUTPUT_TYPE_FILE)
			cerver_log_msg (LOG_COLOR_BLUE "Service: %s\n" LOG_COLOR_RESET, service->connection->name->str);

		else cerver_log_msg ("Service: %s\n", service->connection->name->str);

		// routed packets to the service
		cerver_log_msg ("Routed packets:              %ld", service->stats->n_packets_routed);
		cerver_log_msg ("Routed bytes:                %ld\n", service->stats->total_bytes_routed);

		// good types packets received from the service
		cerver_log_msg ("Receives done:               %ld", service->stats->receives_done);
		cerver_log_msg ("Received packets:            %ld", service->stats->n_packets_received);
		cerver_log_msg ("Received bytes:              %ld\n", service->stats->bytes_received);

		// bad types packets - consumed data from sock fd until next header
		cerver_log_msg ("Bad receives done:           %ld", service->stats->bad_receives_done);
		cerver_log_msg ("Bad received packets:        %ld", service->stats->bad_n_packets_received);
		cerver_log_msg ("Bad received bytes:          %ld\n", service->stats->bad_bytes_received);

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
		service->status = SERVICE_STATUS_NONE;

		service->connection = NULL;
		service->reconnect_wait_time = DEFAULT_SERVICE_WAIT_TIME;

		service->stats = NULL;
	}

	return service;

}

static void balancer_service_delete (void *service_ptr) {

	if (service_ptr) {
		Service *service = (Service *) service_ptr;

		service->connection = NULL;

		balancer_service_stats_delete (service->stats);

		free (service_ptr);
	}

}

static Service *balancer_service_create (void) {

	Service *service = balancer_service_new ();
	if (service) {
		service->stats = balancer_service_stats_new ();
	}

	return service;

}

static void balancer_service_set_status (
	Service *service, ServiceStatus status
) {

	if (service) {
		service->status = status;
	}

}

static u8 balancer_service_forward_pipe_create (
	Service *service
) {

	u8 retval = 1;

	if (service) {
		if (!pipe (service->forward_pipe_fds)) {
			#ifdef SERVICE_DEBUG
			cerver_log_debug (
				"balancer_service_forward_pipe_create () - "
				"created %s FORWARD pipe",
				service->connection->name->str
			);
			#endif

			retval = 0;
		}

		else {
			cerver_log_error (
				"balancer_service_forward_pipe_create () - "
				"%s FORWARD pipe () failed!",
				service->connection->name->str
			);
			perror ("Error");
			printf ("\n");
		}
	}

	return retval;

}

static u8 balancer_service_receive_pipe_create (
	Service *service
) {

	u8 retval = 1;

	if (service) {
		if (!pipe (service->receive_pipe_fds)) {
			#ifdef SERVICE_DEBUG
			cerver_log_debug (
				"balancer_service_receive_pipe_create () - "
				"created %s RECEIVE pipe",
				service->connection->name->str
			);
			#endif

			retval = 0;
		}

		else {
			cerver_log_error (
				"balancer_service_receive_pipe_create () - "
				"%s RECEIVE pipe () failed!",
				service->connection->name->str
			);
			perror ("Error");
			printf ("\n");
		}
	}

	return retval;

}

static void balancer_service_pipe_destroy (Service *service) {

	if (service) {
		(void) close (service->forward_pipe_fds[0]);
		(void) close (service->forward_pipe_fds[1]);

		(void) close (service->receive_pipe_fds[0]);
		(void) close (service->receive_pipe_fds[1]);
	}

}

// static ServiceStatus balancer_service_get_status (Service *service) {

// 	ServiceStatus retval = SERVICE_STATUS_NONE;

// 	if (service) {
// 		retval =  service->status;
// 	}

// 	return retval;

// }

static inline int balancer_get_next_service_round_robin (
	Balancer *balancer
) {

	balancer->next_service += 1;
	if (balancer->next_service >= balancer->n_services)
		balancer->next_service = 0;

	return balancer->next_service;

}

static int balancer_get_next_service (
	Balancer *balancer
) {

	int retval = -1;

	pthread_mutex_lock (balancer->mutex);

	switch (balancer->type) {
		case BALANCER_TYPE_ROUND_ROBIN: {
			int count = 0;
			do {
				retval = balancer_get_next_service_round_robin (balancer);
				if (balancer->services[retval]->status == SERVICE_STATUS_WORKING) break;
				else count += 1;
			} while (count < balancer->n_services);

			if (count >= balancer->n_services) retval = -1;
		} break;

		default: break;
	}

	pthread_mutex_unlock (balancer->mutex);

	return retval;

}

// registers a new service to the load balancer
// a dedicated connection will be created when the balancer starts to handle traffic to & from the service
// returns 0 on success, 1 on error
u8 balancer_service_register (
	Balancer *balancer,
	const char *ip_address, u16 port
) {

	u8 retval = 1;

	if (balancer && ip_address) {
		if ((balancer->next_service + 1) <= balancer->n_services) {
			Service *service = balancer_service_create ();
			if (service) {
				Connection *connection = client_connection_create (
					balancer->client,
					ip_address, port,
					PROTOCOL_TCP, false
				);

				if (connection) {
					service->connection = connection;

					char name[64] = { 0 };
					(void) snprintf (name, 64, "service-%d", balancer->next_service);

					connection_set_name (connection, name);
					connection_set_max_sleep (connection, 30);

					connection_set_receive_buffer_size (connection, sizeof (PacketHeader));
					connection_set_custom_receive (
						connection, 
						balancer_client_receive,
						balancer_service_aux_new (balancer, service),
						balancer_service_aux_delete
					);

					balancer->services[balancer->next_service] = service;
					balancer->next_service += 1;

					retval = 0;
				}
			}
		}
	}

	return retval;

}

// sets the service's name
void balancer_service_set_name (
	Service *service, const char *name
) {

	if (service && name) {
		connection_set_name (service->connection, name);
	}

}

// sets the time (in secs) to wait to attempt a reconnection whenever the service disconnects
// the default value is 20 secs
void balancer_service_set_reconnect_wait_time (
	Service *service, unsigned int wait_time
) {

	if (service) service->reconnect_wait_time = wait_time;

}

// sends a test message to the service & waits for the request
static u8 balancer_service_test (
	Balancer *balancer, Service *service
) {

	u8 retval = 1;

	Packet *packet = packet_generate_request (PACKET_TYPE_TEST, 0, NULL, 0);
	if (packet) {
		// packet_set_network_values (packet, balancer->cerver, balancer->client, service, NULL);
		if (!client_request_to_cerver (balancer->client, service->connection, packet)) {
			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to send test request to %s",
				service->connection->name->str
			);
		}

		packet_delete (packet);
	}

	return retval;

}

// connects to the service & sends a test packet to check its ability to handle requests
// returns 0 on success, 1 on error
static u8 balancer_service_connect (
	Balancer *balancer, Service *service
) {

	u8 retval = 1;

	balancer_service_set_status (service, SERVICE_STATUS_CONNECTING);

	if (!client_connect_to_cerver (balancer->client, service->connection)) {
		cerver_log_success (
			"Connected to %s",
			service->connection->name->str
		);

		// we are ready to test service handler
		balancer_service_set_status (service, SERVICE_STATUS_READY);

		// send test message to service
		if (!balancer_service_test (balancer, service)) {
			if (!client_connection_start (balancer->client, service->connection)) {
				// the service is able to handle packets
				balancer_service_set_status (service, SERVICE_STATUS_WORKING);

				retval = 0;
			}
		}
	}

	else {
		cerver_log_error (
			"Failed to connect to %s",
			service->connection->name->str
		);

		balancer_service_set_status (service, SERVICE_STATUS_UNAVAILABLE);
	}

	return retval;

}

static void *balancer_service_reconnect_thread (void *bs_ptr) {

	BalancerService *bs = (BalancerService *) bs_ptr;

	Balancer *balancer = bs->balancer;
	Service *service = bs->service;

	(void) connection_init (service->connection);

	do {
		sleep (service->reconnect_wait_time);

		cerver_log_debug (
			"Attempting connection to balancer %s service %s",
			balancer->name->str, service->connection->name->str
		);

	} while (balancer_service_connect (balancer, service));

	return NULL;

}

#pragma endregion

#pragma region start

static u8 balancer_start_check (Balancer *balancer) {

	u8 retval = 1;

	if (balancer->next_service) {
		if (balancer->next_service == balancer->n_services) {
			// dirty fix to use first service on the first loop
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

static u8 balancer_start_services (Balancer *balancer) {

	u8 errors = 0;

	for (int i = 0; i < balancer->n_services; i++) {
		errors |= balancer_service_forward_pipe_create (balancer->services[i]);
		errors |= balancer_service_receive_pipe_create (balancer->services[i]);
	}

	return errors;

}

static u8 balancer_start_client (Balancer *balancer) {

	u8 errors = 0;

	for (int i = 0; i < balancer->n_services; i++) {
		errors |= balancer_service_connect (balancer, balancer->services[i]);
	}

	return errors;

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
// then the cerver gets started to enable client connections
// returns 0 on success, 1 on error
u8 balancer_start (Balancer *balancer) {

	u8 retval = 1;

	if (balancer) {
		if (!balancer_start_check (balancer)) {
			if (!balancer_start_services (balancer)) {
				if (!balancer_start_client (balancer)) {
					retval = balancer_start_cerver (balancer);
				}
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region route

// move from socket to pipe buffer
static inline u8 balancer_route_to_service_receive (
	int from_fd, int pipefd, int buff_size,
	ssize_t *received
) {

	u8 retval = 1;

	*received = splice (
		from_fd, NULL,
		pipefd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*received) {
		case -1: {
			#ifdef BALANCER_DEBUG
			perror ("balancer_route_to_service_receive () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef BALANCER_DEBUG
			perror ("balancer_route_to_service_receive () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef BALANCER_DEBUG
			cerver_log_debug ("balancer_route_to_service_receive () - spliced %ld bytes", *received);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// move from pipe buffer to service
static inline u8 balancer_route_to_service_move (
	int pipefd, int to_fd, int buff_size,
	ssize_t *moved
) {

	u8 retval = 1;

	*moved = splice (
		pipefd, NULL,
		to_fd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*moved) {
		case -1: {
			#ifdef BALANCER_DEBUG
			perror ("balancer_route_to_service_move () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef BALANCER_DEBUG
			perror ("balancer_route_to_service_move () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef BALANCER_DEBUG
			cerver_log_debug ("balancer_route_to_service_move () - spliced %ld bytes", *moved);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

static u8 balancer_route_to_service_actual (
	Service *service,
	Connection *from, Connection *to,
	PacketHeader *header, size_t *sent
) {

	u8 retval = 1;

	pthread_mutex_lock (to->socket->write_mutex);

	// first send the header
	ssize_t s = send (to->socket->sock_fd, header, sizeof (PacketHeader), 0);
	if (s > 0) {
		size_t left = header->packet_size - sizeof (PacketHeader);
		if (left) {
			ssize_t received = 0;
			ssize_t moved = 0;
			size_t buff_size = 4096;
			while (left > 0) {
				if (buff_size > left) buff_size = left;

				if (balancer_route_to_service_receive (from->socket->sock_fd, service->forward_pipe_fds[1], buff_size, &received)) break;

				if (balancer_route_to_service_move (service->forward_pipe_fds[0], to->socket->sock_fd, buff_size, &moved)) break;

				*sent += moved;

				left -= buff_size;
			}

			// we are done!
			if (left <= 0) retval = 0;
		}

		else {
			// we are done
			if (sent) *sent = s;
			retval = 0;
		}
	}

	pthread_mutex_unlock (to->socket->write_mutex);


	return retval;

}

// routes the received packet to a service to be handled
// first sends the packet header with the correct sock fd
// if any data, it is forwarded from one sock fd to another using splice ()
void balancer_route_to_service (
	CerverReceive *cr,
	Balancer *balancer, Connection *connection,
	PacketHeader *header
) {

	int selected_service = balancer_get_next_service (balancer);
	if (selected_service >= 0) {
		Service *service = balancer->services[selected_service];

		header->sock_fd = connection->socket->sock_fd;

		size_t sent = 0;
		if (!balancer_route_to_service_actual (
			service,
			connection, service->connection,
			header, &sent
		)) {
			service->stats->n_packets_routed += 1;
			service->stats->total_bytes_routed += sent;

			service->stats->routed_packets[header->packet_type] += 1;

			#ifdef BALANCER_DEBUG
			cerver_log_debug (
				"Routed %ld between %d (original) -> %d (%s)",
				sent,
				connection->socket->sock_fd, 
				service->connection->socket->sock_fd, service->connection->name->str
			);
			#endif
		}

		else {
			#ifdef BALANCER_DEBUG
			cerver_log_error (
				"Packet routing between %d (original) -> %d (%s) has failed!",
				connection->socket->sock_fd, 
				service->connection->socket->sock_fd, service->connection->name->str
			);
			#endif
		}
	}

	else {
		cerver_log_warning ("No available services to handle packets!");

		error_packet_generate_and_send (
			CERVER_ERROR_PACKET_ERROR, "Services unavailable",
			balancer->cerver, balancer->client, connection
		);

		if (header->packet_size > sizeof (PacketHeader)) {
			// consume data from socket to get next packet
			balancer_receive_consume_from_connection (cr, header->packet_size - sizeof (PacketHeader));
		}

		balancer->stats->unhandled_packets += 1;
		balancer->stats->unhandled_bytes += header->packet_size;
	}

}

#pragma endregion

#pragma region handler

static void balancer_client_receive_handle_failed (
	BalancerService *bs, 
	Client *client, Connection *connection
);

// consume from the service's connection socket until the next packet header
// returns 0 on success, 1 on error
static u8 balancer_client_consume_from_service (BalancerService *bs, PacketHeader *header) {

	u8 retval = 1;

	if (header->packet_size > sizeof (PacketHeader)) {
		char buffer[SERVICE_CONSUME_BUFFER_SIZE] = { 0 };

		size_t data_size = header->packet_size - sizeof (PacketHeader);

		size_t to_read = 0;
		size_t rc = 0;

		do {
			to_read = data_size >= SERVICE_CONSUME_BUFFER_SIZE ? SERVICE_CONSUME_BUFFER_SIZE : data_size;
			rc = recv (bs->service->connection->socket->sock_fd, buffer, to_read, 0);
			if (rc <= 0) {
				// #ifdef SERVICE_DEBUG
				snprintf (
					buffer, SERVICE_CONSUME_BUFFER_SIZE, 
					"balancer_client_consume_from_service () - rc <= 0 - service %s", 
					bs->service->connection->name->str
				);
				cerver_log_warning (buffer);
				// #endif

				// end the connection & flag the service as unavailable
				balancer_client_receive_handle_failed (
					bs, 
					bs->balancer->client, bs->service->connection
				);
				break;
			}

			else {
				bs->service->stats->bad_receives_done += 1;
				bs->service->stats->bad_bytes_received += rc;
			}

			data_size -= to_read;
		} while (data_size <= 0);

		if (!data_size) retval = 0;
	}

	bs->service->stats->bad_n_packets_received += 1;

	return retval;

}

// move from socket to pipe buffer
static inline u8 balancer_client_route_receive (
	int from_fd, int pipefd, int buff_size,
	ssize_t *received
) {

	u8 retval = 1;

	*received = splice (
		from_fd, NULL,
		pipefd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*received) {
		case -1: {
			#ifdef SERVICE_DEBUG
			perror ("balancer_client_route_receive () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef SERVICE_DEBUG
			perror ("balancer_client_route_receive () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef SERVICE_DEBUG
			cerver_log_debug ("balancer_client_route_receive () - spliced %ld bytes", *received);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// move from pipe buffer to service
static inline u8 balancer_client_route_move (
	int pipefd, int to_fd, int buff_size,
	ssize_t *moved
) {

	u8 retval = 1;

	*moved = splice (
		pipefd, NULL,
		to_fd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*moved) {
		case -1: {
			#ifdef SERVICE_DEBUG
			perror ("balancer_client_route_move () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef SERVICE_DEBUG
			perror ("balancer_client_route_move () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef SERVICE_DEBUG
			cerver_log_debug ("balancer_client_route_move () - spliced %ld bytes", *moved);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

static u8 balancer_client_route_actual (
	Service *service,
	Connection *from, Connection *to,
	PacketHeader *header, size_t *sent
) {

	u8 retval = 1;

	pthread_mutex_lock (to->socket->write_mutex);

	// first send the header
	ssize_t s = send (to->socket->sock_fd, header, sizeof (PacketHeader), 0);
	if (s > 0) {
		size_t left = header->packet_size - sizeof (PacketHeader);
		if (left) {
			ssize_t received = 0;
			ssize_t moved = 0;
			size_t buff_size = 4096;
			while (left > 0) {
				if (buff_size > left) buff_size = left;

				if (balancer_client_route_receive (from->socket->sock_fd, service->receive_pipe_fds[1], buff_size, &received)) break;

				if (balancer_client_route_move (service->receive_pipe_fds[0], to->socket->sock_fd, buff_size, &moved)) break;

				*sent += moved;

				left -= buff_size;
			}

			// we are done!
			if (left <= 0) retval = 0;
		}

		else {
			// we are done
			if (sent) *sent = s;
			retval = 0;
		}
	}

	pthread_mutex_unlock (to->socket->write_mutex);


	return retval;

}

// route the service's response back to the original client
static void balancer_client_route_response (
	BalancerService *bs,
	Client *client, Connection *connection,
	PacketHeader *header
) {

	// get the original connection by the sockfd from the packet header
	Client *original_client = client_get_by_sock_fd (bs->balancer->cerver, header->sock_fd);
	if (original_client) {
		Connection *original_connection = connection_get_by_sock_fd_from_client (original_client, header->sock_fd);
		if (original_connection) {
			size_t sent = 0;
			if (!balancer_client_route_actual (
				bs->service,
				connection, original_connection,
				header, &sent
			)) {
				#ifdef SERVICE_DEBUG
				cerver_log_debug (
					"Routed %ld between %d (%s) -> %d (original)",
					sent,
					connection->socket->sock_fd, connection->name->str,
					original_connection->socket->sock_fd
				);
				#endif
			}

			else {
				#ifdef SERVICE_DEBUG
				cerver_log_error (
					"Packet routing between %d (%s) -> %d (original) has failed!",
					connection->socket->sock_fd, connection->name->str,
					original_connection->socket->sock_fd
				);
				#endif
			}
		}

		else {
			cerver_log_error (
				"balancer_client_route_response () - unable to find CONNECTION with sock fd %d", 
				header->sock_fd
			);

			// consume data from socket to get next packet
			balancer_client_consume_from_service (bs, header);
		}
	}

	else {
		cerver_log_error (
			"balancer_client_route_response () - unable to find CLIENT with sock fd %d", 
			header->sock_fd
		);

		// consume data from socket to get next packet
		balancer_client_consume_from_service (bs, header);
	}

}

// handles a PACKET_TYPE_TEST packet from the service
static void balancer_client_handle_test (
	BalancerService *bs,
	Client *client, Connection *connection,
	PacketHeader *header
) {

	if (!header->sock_fd) {
		cerver_log (
			LOG_TYPE_TEST, LOG_TYPE_HANDLER,
			"Got a TEST packet from service %s",
			connection->name->str
		);
	}

	else {
		balancer_client_route_response (bs, client, connection, header);
	}

}

static void balancer_client_receive_success (
	BalancerService *bs,
	Client *client, Connection *connection,
	PacketHeader *header
) {

	bs->service->stats->receives_done += 1;
	bs->service->stats->n_packets_received += 1;

	bs->service->stats->bytes_received += header->packet_size;

	bs->service->stats->received_packets[
		((!header->packet_type) || (header->packet_type >= PACKET_TYPE_BAD)) ? 
			PACKET_TYPE_BAD : header->packet_type
	] += 1;

	switch (header->packet_type) {
		case PACKET_TYPE_CLIENT:
			balancer_client_consume_from_service (bs, header);
			break;

		case PACKET_TYPE_AUTH:
			balancer_client_consume_from_service (bs, header);
			break;

		// handle whether the response was sent by the handler or by a client
		case PACKET_TYPE_TEST: {
			balancer_client_handle_test (
				bs,
				client, connection,
				header
			);
		} break;

		// only route packets of these types back to original client
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_ERROR:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM: {
			balancer_client_route_response (
				bs,
				client, connection,
				header
			);
		} break;

		case PACKET_TYPE_NONE:
		default: {
			#ifdef SERVICE_DEBUG
			cerver_log_warning (
				"balancer_client_receive () - got a packet of unknown type from service %s",
				connection->name->str
			);
			#endif

			balancer_client_consume_from_service (bs, header);
		} break;
	}

}

// handles a failed receive from the client's connection
// ends the connection & flags the service as unavailable to attempt a reconnection
static void balancer_client_receive_handle_failed (
	BalancerService *bs, 
	Client *client, Connection *connection
) {

	(void) client_connection_stop (client, connection);

	printf ("\n");
	cerver_log_warning (
		"Balancer %s - service %s has disconnected!\n",
		bs->balancer->name->str, bs->service->connection->name->str
	);

	balancer_service_set_status (bs->service, SERVICE_STATUS_DISCONNECTED);

	pthread_t thread_id = 0;
	if (thread_create_detachable (&thread_id, balancer_service_reconnect_thread, bs)) {
		cerver_log_error (
			"Failed to create reconnect thread for balancer %s service %s",
			bs->balancer->name->str, bs->service->connection->name->str
		);
	}

}

// custom receive method for packets comming from the services
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
static u8 balancer_client_receive (
	void *custom_data_ptr,
	char *buffer, const size_t buffer_size
) {

	unsigned int retval = 1;

	ConnectionCustomReceiveData *custom_data = (ConnectionCustomReceiveData *) custom_data_ptr;

	if (custom_data->client && custom_data->connection) {
		ssize_t rc = recv (custom_data->connection->socket->sock_fd, buffer, buffer_size, 0);

		switch (rc) {
			case -1: {
				if (errno != EWOULDBLOCK) {
					#ifdef SERVICE_DEBUG
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_HANDLER,
						"balancer_client_receive () - rc < 0 - sock fd: %d",
						custom_data->connection->socket->sock_fd
					);
					perror ("Error");
					#endif
					
					balancer_client_receive_handle_failed (
						(BalancerService *) custom_data->args, 
						custom_data->client, custom_data->connection
					);
				}
			} break;

			case 0: {
				#ifdef SERVICE_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
					"balancer_client_receive () - rc == 0 - sock fd: %d",
					custom_data->connection->socket->sock_fd
				);
				#endif

				balancer_client_receive_handle_failed (
					(BalancerService *) custom_data->args, 
					custom_data->client, custom_data->connection
				);
			} break;

			default: {
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
					"Connection %s rc: %ld",
					custom_data->connection->name->str, rc
				);

				balancer_client_receive_success (
					(BalancerService *) custom_data->args,
					custom_data->client, custom_data->connection,
					(PacketHeader *) buffer
				);

				retval = 0;
			} break;
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

// first ends and destroys the balancer's internal cerver
// then disconnects from each of the registered services
// last frees any balancer memory left
// returns 0 on success, 1 on error
u8 balancer_teardown (Balancer *balancer) {

	u8 retval = 1;

	if (balancer) {
		(void) cerver_teardown (balancer->cerver);
		balancer->cerver = NULL;

		client_teardown (balancer->client);
		balancer->client = NULL;

		// end services
		for (int i = 0; i < balancer->n_services; i++)
			balancer_service_pipe_destroy (balancer->services[i]);

		balancer_delete (balancer);
	}

	return retval;

}

#pragma endregion