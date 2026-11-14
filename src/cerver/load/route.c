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
#include "cerver/connection/lifecycle.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/load/balancer.h"
#include "cerver/load/internal.h"
#include "cerver/load/route.h"
#include "cerver/load/service.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static const char *balancer_services_unavailable = { "Services unavailable" };

#pragma region route

typedef enum BalancerReceiveResult {

	BALANCER_RECEIVE_RESULT_DONE		= 0,
	BALANCER_RECEIVE_RESULT_IDLE		= 1,	// nothing was received before the timeout
	BALANCER_RECEIVE_RESULT_FAILED		= 2,
	BALANCER_RECEIVE_RESULT_BAD			= 3		// a header with another format, flags or reserved bytes

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

	u8 encoded[PACKET_HEADER_SIZE];
	packet_header_encode (header, encoded);

	struct iovec parts[BALANCER_SEND_PARTS] = {
		{ .iov_base = (void *) encoded, .iov_len = PACKET_HEADER_SIZE },
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

	return (header->packet_size >= PACKET_HEADER_SIZE)
		&& (!max_packet_size || (header->packet_size <= (u64) max_packet_size));

}

// receives the bytes of a header and decodes them,
// a header that can not be decoded is a bad packet, the result is BALANCER_RECEIVE_RESULT_BAD
static BalancerReceiveResult balancer_receive_header (
	const int sock_fd, PacketHeader *header
) {

	u8 encoded[PACKET_HEADER_SIZE] = { 0 };
	(void) memset (header, 0, sizeof (PacketHeader));

	BalancerReceiveResult result = balancer_receive_exact (
		sock_fd, (char *) encoded, PACKET_HEADER_SIZE, true
	);

	if (
		(result == BALANCER_RECEIVE_RESULT_DONE)
		&& (packet_header_decode (encoded, header) != PACKET_HEADER_RESULT_NONE)
	) {
		result = BALANCER_RECEIVE_RESULT_BAD;
	}

	return result;

}

// receives the body of a packet, the caller frees it
// returns 0 on success, 1 on error
static u8 balancer_receive_body (
	const int sock_fd, const PacketHeader *header,
	char **body, size_t *body_size
) {

	u8 retval = 0;

	*body_size = (size_t) header->packet_size - PACKET_HEADER_SIZE;
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

// the index of the method of the app is checked against the services,
// the method runs without the lock, so it can use the balancer
static int balancer_get_custom_service (
	Balancer *balancer, const PacketHeader *header,
	const char *body, const size_t body_size
) {

	int retval = -1;

	if (balancer->select_service) {
		const int selected = balancer->select_service (
			header, body, body_size, balancer->select_service_args
		);

		if (
			(selected >= 0) && (selected < balancer->n_services)
			&& balancer_service_is_working (balancer->services[selected])
		) {
			retval = selected;
		}
	}

	return retval;

}

// the handler id comes from the network, so it is checked against the services
static int balancer_get_service_with_type (
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

// returns the index of the service that handles the packet, -1 if none can handle it
static int balancer_get_next_service (
	Balancer *balancer, const PacketHeader *header,
	const char *body, const size_t body_size
) {

	return (balancer->type == BALANCER_TYPE_CUSTOM)
		? balancer_get_custom_service (balancer, header, body, body_size)
		: balancer_get_service_with_type (balancer, header);

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
// the header carries the route id of the client connection to route the response back
static void balancer_route_to_service (
	CerverReceive *cr, Balancer *balancer,
	PacketHeader *header, char *body, const size_t body_size
) {

	int selected_service = balancer_get_next_service (balancer, header, body, body_size);
	if (selected_service >= 0) {
		Service *service = balancer->services[selected_service];

		header->route_id = cr->connection ? cr->connection->route_id : 0;

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

u32 balancer_route_id (const u32 slot, const u16 generation) {

	return (((u32) generation & BALANCER_ROUTE_GENERATION_MASK) << BALANCER_ROUTE_SLOT_BITS)
		| (slot & BALANCER_ROUTE_SLOT_MASK);

}

u32 balancer_route_id_slot (const u32 route_id) {

	return route_id & BALANCER_ROUTE_SLOT_MASK;

}

u16 balancer_route_id_generation (const u32 route_id) {

	return (u16) ((route_id >> BALANCER_ROUTE_SLOT_BITS) & BALANCER_ROUTE_GENERATION_MASK);

}

u16 balancer_route_next_generation (const u16 generation) {

	const u16 next = (u16) ((generation + 1) & BALANCER_ROUTE_GENERATION_MASK);

	return next ? next : BALANCER_ROUTE_FIRST_GENERATION;

}

// doubles the slots up to BALANCER_ROUTES_MAX, with the balancer's mutex
// returns true when there are more slots
static bool balancer_routes_grow (Balancer *balancer) {

	bool grown = false;

	u32 new_size = balancer->routes_size * BALANCER_ROUTES_GROWTH_FACTOR;
	if (new_size > BALANCER_ROUTES_MAX) new_size = BALANCER_ROUTES_MAX;

	if (new_size > balancer->routes_size) {
		BalancerRoute *routes = (BalancerRoute *) realloc (balancer->routes, (size_t) new_size * sizeof (BalancerRoute));
		if (routes) {
			balancer_routes_init (routes + balancer->routes_size, new_size - balancer->routes_size);
			balancer->routes = routes;
			balancer->routes_size = new_size;
			grown = true;
		}
	}

	return grown;

}

// takes the free slot that was released last, or one that was never used,
// with the balancer's mutex
// returns the slot, or BALANCER_ROUTE_NONE when every slot is taken
static u32 balancer_route_take_slot (Balancer *balancer) {

	u32 slot = balancer->free_route;

	if (slot != BALANCER_ROUTE_NONE) {
		balancer->free_route = balancer->routes[slot].next_free;
		balancer->routes[slot].next_free = BALANCER_ROUTE_NONE;
	}

	else if ((balancer->routes_used < balancer->routes_size) || balancer_routes_grow (balancer)) {
		slot = balancer->routes_used++;
	}

	return slot;

}

// gives a route to a client connection that the balancer receives from,
// in a free slot, with a reference to its socket
// returns the route id, or 0 when every slot is taken (BALANCER_ROUTES_MAX)
u32 balancer_connection_start (
	Balancer *balancer, Socket *socket
) {

	u32 route_id = 0;

	if (balancer && socket) {
		(void) pthread_mutex_lock (balancer->mutex);

		const u32 slot = balancer_route_take_slot (balancer);
		if (slot != BALANCER_ROUTE_NONE) {
			balancer->routes[slot].socket = socket_reference (socket);
			route_id = balancer_route_id (slot, balancer->routes[slot].generation);
		}

		(void) pthread_mutex_unlock (balancer->mutex);

		if (!route_id) {
			cerver_log_warning ("The balancer can not route more than %u connections!", BALANCER_ROUTES_MAX);
		}
	}

	return route_id;

}

// the route of the id with the balancer's mutex, NULL if its slot has another generation or is free
static BalancerRoute *balancer_route_find (Balancer *balancer, const u32 route_id) {

	BalancerRoute *route = NULL;

	const u32 slot = balancer_route_id_slot (route_id);

	if (
		route_id && (slot < balancer->routes_used)
		&& balancer->routes[slot].socket
		&& (balancer->routes[slot].generation == balancer_route_id_generation (route_id))
	) {
		route = &balancer->routes[slot];
	}

	return route;

}

// releases the route of a client connection that has ended,
// the next generation of its slot discards its pending responses
void balancer_connection_end (
	Balancer *balancer, const u32 route_id
) {

	if (balancer) {
		Socket *socket = NULL;

		(void) pthread_mutex_lock (balancer->mutex);

		BalancerRoute *route = balancer_route_find (balancer, route_id);
		if (route) {
			socket = route->socket;
			route->socket = NULL;
			route->generation = balancer_route_next_generation (route->generation);

			route->next_free = balancer->free_route;
			balancer->free_route = balancer_route_id_slot (route_id);
		}

		(void) pthread_mutex_unlock (balancer->mutex);

		socket_delete (socket);
	}

}

// returns a reference to the socket of the client connection of the route id,
// NULL if the connection has ended, even when its slot belongs to another connection now
Socket *balancer_route_get (
	Balancer *balancer, const u32 route_id
) {

	Socket *socket = NULL;

	if (balancer) {
		(void) pthread_mutex_lock (balancer->mutex);

		const BalancerRoute *route = balancer_route_find (balancer, route_id);
		if (route) {
			socket = socket_reference (route->socket);
		}

		(void) pthread_mutex_unlock (balancer->mutex);
	}

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
	switch (balancer_receive_header (cr->socket->sock_fd, &header)) {
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

		// the stream of the client can not be parsed, its connection is closed
		case BALANCER_RECEIVE_RESULT_BAD: balancer_receive_bad_packet (balancer, &header); break;

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

	Socket *socket = balancer_route_get (balancer, header->route_id);
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
			"the connection with route id %u has ended",
			header->route_id
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
			if (header->route_id) {
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
u8 balancer_client_receive (
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
	switch (balancer_receive_header (connection->socket->sock_fd, &header)) {
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

		// the stream of the service can not be parsed, the connection connects again
		case BALANCER_RECEIVE_RESULT_BAD: balancer_client_bad_packet (bs, &header); break;

		default: break;
	}

	return retval;

}

#pragma endregion
