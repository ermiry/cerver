#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>

#include "cerver/types/types.h"

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static u8 balancer_client_receive (void *custom_data_ptr);

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

		balancer->mutex = NULL;
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

		pthread_mutex_delete (balancer->mutex);

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

		balancer->cerver->balancer = balancer;

		balancer->client = client_create ();
		client_set_name (balancer->client, "balancer-client");

		balancer->n_services = n_services;
		balancer->services = (Connection **) calloc (balancer->n_services, sizeof (Connection *));
		if (balancer->services) {
			for (unsigned int i = 0; i < balancer->n_services; i++)
				balancer->services[i] = NULL;
		}

		balancer->mutex = pthread_mutex_new ();
	}

	return balancer;

}

#pragma endregion

#pragma region services

const char *balancer_service_status_to_string (ServiceStatus status) {

	switch (status) {
		#define XX(num, name, string, description) case SERVICE_STATUS_##name: return #string;
		SERVICE_STATUS_MAP(XX)
		#undef XX
	}

	return balancer_service_status_to_string (SERVICE_STATUS_NONE);

}

const char *balancer_service_status_description (ServiceStatus status) {

	switch (status) {
		#define XX(num, name, string, description) case SERVICE_STATUS_##name: return #description;
		SERVICE_STATUS_MAP(XX)
		#undef XX
	}

	return balancer_service_status_description (SERVICE_STATUS_NONE);

}

static Service *balancer_service_new (void) {

	Service *service = (Service *) malloc (sizeof (Service));
	if (service) {
		service->status = SERVICE_STATUS_NONE;
		service->connection = NULL;
	}

	return service;

}

static void balancer_service_delete (void *service_ptr) {

	if (service_ptr) {
		Service *service = (Service *) service_ptr;

		service->connection = NULL;

		free (service_ptr);
	}

}

static unsigned int balancer_get_next_service (Balancer *balancer) {

	unsigned int retval = 0;

	pthread_mutex_lock (balancer->mutex);

	switch (balancer->type) {
		case BALANCER_TYPE_ROUND_ROBIN: {
			retval = balancer->next_service;

			balancer->next_service += 1;
			if (balancer->next_service >= balancer->n_services)
				balancer->next_service = 0;
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
			Connection *connection = client_connection_create (
				balancer->client,
				ip_address, port,
				PROTOCOL_TCP, false
			);

			if (connection) {
				char name[64] = { 0 };
				snprintf (name, 64, "service-%d", balancer->next_service);

				connection_set_name (connection, name);
				connection_set_max_sleep (connection, 30);

				connection_set_custom_receive (connection, balancer_client_receive, NULL);

				balancer->services[balancer->next_service] = connection;
				balancer->next_service += 1;

				retval = 0;
			}
		}
	}

	return retval;

}

// sends a test message to the service & waits for the request
static u8 balancer_service_test (Balancer *balancer, Connection *service) {

	u8 retval = 1;

	Packet *packet = packet_generate_request (PACKET_TYPE_TEST, 0, NULL, 0);
	if (packet) {
		// packet_set_network_values (packet, balancer->cerver, balancer->client, service, NULL);
		if (!client_request_to_cerver (balancer->client, service, packet)) {
			retval = 0;
		}

		else {
			char *status = c_string_create ("Failed to send test request to %s", service->name->str);
			if (status) {
				cerver_log_error (status);
				free (status);
			}
		}

		packet_delete (packet);
	}

	return retval;

}

// connects to the service & sends a test packet to check its ability to handle requests
// returns 0 on success, 1 on error
static u8 balancer_service_connect (Balancer *balancer, Connection *service) {

	u8 retval = 1;

	if (!client_connect_to_cerver (balancer->client, service)) {
		char *status = c_string_create ("Connected to %s", service->name->str);
		if (status) {
			cerver_log_success (status);
			free (status);
		}

		// send test message to service
		if (!balancer_service_test (balancer, service)) {
			if (!client_connection_start (balancer->client, service)) {
				retval = 0;
			}
		}
	}

	else {
		char *status = c_string_create ("Failed to connect to %s", service->name->str);
		if (status) {
			cerver_log_error (status);
			free (status);
		}
	}

	return retval;

}

#pragma endregion

#pragma region start

static u8 balancer_start_check (Balancer *balancer) {

	u8 retval = 1;

	if (balancer->next_service) {
		if (balancer->next_service == balancer->n_services) {
			balancer->next_service = 0;

			retval = 0;
		}

		else {
			char *status = c_string_create (
				"Balancer registered services doesn't match the configured number: %d != %d",
				balancer->next_service, balancer->n_services
			);

			if (status) {
				cerver_log_error (status);
				free (status);
			}
		}
	}

	else {
		cerver_log_error ("No service has been registered to the load balancer!");
	}

	return retval;

}

static u8 balancer_start_client (Balancer *balancer) {

	u8 errors = 0;

	for (unsigned int i = 0; i < balancer->n_services; i++) {
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
			if (!balancer_start_client (balancer)) {
				retval = balancer_start_cerver (balancer);
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region route

// routes the received packet to a service to be handled
// first sends the packet header with the correct sock fd
// if any data, it is forwarded from one sock fd to another using splice ()
void balancer_route_to_service (
	Balancer *balancer, Connection *connection,
	PacketHeader *header
) {

	Connection *service = balancer->services[balancer_get_next_service (balancer)];

	header->sock_fd = connection->socket->sock_fd;

	// send the header to the selected service
	send (service->socket->sock_fd, header, sizeof (PacketHeader), 0);

	// splice remaining packet to service
	size_t left = header->packet_size - sizeof (PacketHeader);
	if (left) {
		splice (connection->socket->sock_fd, NULL, service->socket->sock_fd, NULL, left, 0);
	}

}

#pragma endregion

#pragma region handler

// FIXME: discard buffer on bad types
static void balancer_client_receive_success (
	Client *client, Connection *connection,
	PacketHeader *header
) {

	switch (header->packet_type) {
		case PACKET_TYPE_NONE: break;

		case PACKET_TYPE_CLIENT: break;

		case PACKET_TYPE_AUTH: break;

		case PACKET_TYPE_TEST: {
			client->stats->received_packets->n_test_packets += 1;
			connection->stats->received_packets->n_test_packets += 1;
			cerver_log_msg (stdout, LOG_TYPE_TEST, LOG_TYPE_HANDLER, "Got a test packet from service");
		} break;

		// only route packets of these types back to original client
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_ERROR:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM: {
			// FIXME: better handler

			printf ("sockfd: %d\n", header->sock_fd);

			// send the header to the selected service
			send (header->sock_fd, header, sizeof (PacketHeader), 0);

			// splice remaining packet to service
			size_t left = header->packet_size - sizeof (PacketHeader);
			if (left) {
				splice (connection->socket->sock_fd, NULL, header->sock_fd, NULL, left, 0);
			}
		} break;

		default: {
			client->stats->received_packets->n_bad_packets += 1;
			connection->stats->received_packets->n_bad_packets += 1;
			#ifdef CLIENT_DEBUG
			cerver_log_msg (stdout, LOG_TYPE_WARNING, LOG_TYPE_HANDLER, "Got a packet of unknown type.");
			#endif
		} break;
	}

	// FIXME: update stats
	// client->stats->n_receives_done += 1;
	// client->stats->total_bytes_received += rc;

	// connection->stats->n_receives_done += 1;
	// connection->stats->total_bytes_received += rc;

}

// FIXME: handle disconnect from a service - route them to the other services
// custom receive method for packets comming from the services
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
static u8 balancer_client_receive (void *custom_data_ptr) {

	unsigned int retval = 1;

	ConnectionCustomReceiveData *custom_data = (ConnectionCustomReceiveData *) custom_data_ptr;

	Client *client = custom_data->client;
	Connection *connection = custom_data->connection;

	if (client && connection) {
		char header_buffer[sizeof (PacketHeader)] = { 0 };
		ssize_t rc = recv (connection->socket->sock_fd, header_buffer, sizeof (PacketHeader), MSG_DONTWAIT);

		switch (rc) {
			case -1: {
				if (errno != EWOULDBLOCK) {
					#ifdef CLIENT_DEBUG
					char *s = c_string_create ("balancer_client_receive () - rc < 0 - sock fd: %d", connection->socket->sock_fd);
					if (s) {
						cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_HANDLER, s);
						free (s);
					}
					perror ("Error");
					#endif

					// FIXME:
					// client_receive_handle_failed (client, connection);
				}
			} break;

			case 0: {
				#ifdef CLIENT_DEBUG
				char *s = c_string_create ("balancer_client_receive () - rc == 0 - sock fd: %d",
					connection->socket->sock_fd);
				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, s);
					free (s);
				}
				#endif

				// FIXME:
				// client_receive_handle_failed (client, connection);
			} break;

			default: {
				char *s = c_string_create ("Connection %s rc: %ld",
					connection->name->str, rc);
				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CLIENT, s);
					free (s);
				}

				balancer_client_receive_success (
					client, connection,
					(PacketHeader *) header_buffer
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

		balancer_delete (balancer);
	}

	return retval;

}

#pragma endregion