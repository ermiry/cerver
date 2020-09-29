#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>

#include "cerver/types/types.h"

#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"

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

		balancer->cerver->balancer = balancer;

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
		retval = balancer_service_test (balancer, service);
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

#pragma region handler

// custom receive method for packets comming from the services
// returns 0 on success handle, 1 if any error ocurred and must likely the connection was ended
static u8 balancer_client_receive (void *custom_data_ptr) {

	unsigned int retval = 1;

	ConnectionCustomReceiveData *custom_data = (ConnectionCustomReceiveData *) custom_data_ptr;

	Client *client = custom_data->client;
	Connection *connection = custom_data->connection;

	if (client && connection) {
		char *packet_buffer = (char *) calloc (connection->receive_packet_buffer_size, sizeof (char));
		if (packet_buffer) {
			ssize_t rc = recv (connection->socket->sock_fd, packet_buffer, connection->receive_packet_buffer_size, 0);

			switch (rc) {
				case -1: {
					if (errno != EWOULDBLOCK) {
						#ifdef CLIENT_DEBUG
						char *s = c_string_create ("client_receive () - rc < 0 - sock fd: %d", connection->socket->sock_fd);
						if (s) {
							cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_NONE, s);
							free (s);
						}
						perror ("Error");
						#endif

						client_receive_handle_failed (client, connection);
					}
				} break;

				case 0: {
					// man recv -> steam socket perfomed an orderly shutdown
					// but in dgram it might mean something?
					#ifdef CLIENT_DEBUG
					char *s = c_string_create ("client_receive () - rc == 0 - sock fd: %d",
						connection->socket->sock_fd);
					if (s) {
						cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_NONE, s);
						free (s);
					}
					// perror ("Error");
					#endif

					client_receive_handle_failed (client, connection);
				} break;

				default: {
					// char *s = c_string_create ("Connection %s rc: %ld",
					//     connection->name->str, rc);
					// if (s) {
					//     cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CLIENT, s);
					//     free (s);
					// }

					client->stats->n_receives_done += 1;
					client->stats->total_bytes_received += rc;

					connection->stats->n_receives_done += 1;
					connection->stats->total_bytes_received += rc;

					// FIXME:
					// handle the recived packet buffer -> split them in packets of the correct size
					// client_receive_handle_buffer (
					// 	client,
					// 	connection,
					// 	packet_buffer,
					// 	rc
					// );

					retval = 0;
				} break;
			}

			free (packet_buffer);
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
				"Failed to allocate a new packet buffer!");
			#endif
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

// TODO:

#pragma endregion