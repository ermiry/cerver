#ifndef _CERVER_BALANCER_H_
#define _CERVER_BALANCER_H_

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/config.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/socket.h"

#include "cerver/threads/thread.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Balancer;
struct _Service;

#pragma region type

#define BALANCER_TYPE_MAP(XX)									\
	XX(0, 	NONE, 				None)							\
	XX(1, 	ROUND_ROBIN, 		Round-Robin)					\
	XX(2, 	HANDLER_ID, 		Handler-ID)

typedef enum BalancerType {

	#define XX(num, name, string) BALANCER_TYPE_##name = num,
	BALANCER_TYPE_MAP (XX)
	#undef XX

} BalancerType;

const char *balancer_type_to_string (
	const BalancerType type
);

#pragma endregion

#pragma region stats

// every value is updated with atomic operations,
// the receive threads of the clients and the services update them
typedef struct BalancerStats {

	// good types packets that the balancer can handle
	u64 n_packets_received;            // packets received from clients
	u64 receives_done;                 // receives done to clients
	u64 bytes_received;                // bytes received from clients

	// bad types packets - their data was read and discarded
	u64 bad_n_packets_received;        // packets received from clients
	u64 bad_receives_done;             // receives done to clients
	u64 bad_bytes_received;            // bytes received from clients

	// routed packets to services
	u64 n_packets_routed;              // total number of packets that were routed to services
	u64 total_bytes_routed;            // total amount of bytes routed to services

	// responses sent to the original clients
	u64 n_packets_sent;                // total number of packets that were sent
	u64 total_bytes_sent;              // total amount of bytes sent by the cerver

	// responses of clients that were already disconnected
	u64 discarded_responses;

	// packets handled by balancer on unavailable services
	u64 backup_handled_packets;
	u64 backup_handled_bytes;

	// packets that the balancer was unable to handle
	u64 unhandled_packets;
	u64 unhandled_bytes;

	u64 received_packets[PACKETS_MAX_TYPES];
	u64 routed_packets[PACKETS_MAX_TYPES];
	u64 sent_packets[PACKETS_MAX_TYPES];

} BalancerStats;

CERVER_EXPORT void balancer_stats_print (
	const struct _Balancer *balancer
);

#pragma endregion

#pragma region main

// the route of each client connection, by its sock fd,
// a reference to the socket is kept while the connection is alive,
// so a response is never sent to a socket that was deleted
typedef struct BalancerRoute {

	Socket *socket;

} BalancerRoute;

#define BALANCER_DEFAULT_ROUTES_SIZE		64
#define BALANCER_ROUTES_GROWTH_FACTOR		2

struct _Balancer {

	String *name;
	BalancerType type;

	Cerver *cerver;
	Client *client;

	int next_service;
	int n_services;					// how many services the load balancer is connected to
	struct _Service **services;		// references to the client's connections for direct access

	Action on_unavailable_services;

	BalancerStats *stats;

	// protects the next service and the routes
	pthread_mutex_t *mutex;

	BalancerRoute *routes;
	size_t routes_size;

	// the services that were down when the balancer started
	// wait for their next attempt in this condition
	bool running;
	pthread_cond_t *cond;

};

typedef struct _Balancer Balancer;

CERVER_PRIVATE Balancer *balancer_new (void);

CERVER_PRIVATE void balancer_delete (void *balancer_ptr);

CERVER_EXPORT BalancerType balancer_get_type (const Balancer *balancer);

// sets a cb method to be used whenever there are no services available
// a reference to the received packet will be passed to the method
CERVER_EXPORT void balancer_set_on_unavailable_services (
	Balancer *balancer, const Action on_unavailable_services
);

// create a new load balancer of the selected type
// set its network values & set the number of services it will handle
// the cerver of the balancer handles each client in a dedicated thread
CERVER_EXPORT Balancer *balancer_create (
	const char *name, const BalancerType type,
	const u16 port, const u16 connection_queue,
	const unsigned int n_services
);

#pragma endregion

#pragma region services

#define SERVICE_NAME_SIZE					64
#define SERVICE_CONNECTION_NAME_SIZE		64

#define SERVICE_DEFAULT_MAX_SLEEP			30
#define SERVICE_DEFAULT_WAIT_TIME			20

// the connection to a service makes a single attempt
#define SERVICE_CONNECT_MAX_SLEEP			1

#define SERVICE_STATUS_MAP(XX)																			\
	XX(0, 	NONE, 			None, 			None)														\
	XX(1, 	CONNECTING, 	Connecting, 	Creating connection with service)							\
	XX(2, 	READY, 			Ready, 			Ready to start accepting packets)							\
	XX(3, 	WORKING, 		Working, 		Handling packets)											\
	XX(4, 	DISCONNECTING, 	Disconnecting, 	In the process of closing the connection)					\
	XX(5, 	DISCONNECTED, 	Disconnected, 	The balancer has been disconnected from the service)		\
	XX(6, 	UNAVAILABLE, 	Unavailable, 	The service is down or is not handling packets)				\
	XX(7, 	ENDED, 			Ended, 			The connection with the service has ended)

typedef enum ServiceStatus {

	#define XX(num, name, string, description) SERVICE_STATUS_##name = num,
	SERVICE_STATUS_MAP (XX)
	#undef XX

} ServiceStatus;

CERVER_EXPORT const char *balancer_service_status_to_string (
	const ServiceStatus status
);

CERVER_EXPORT const char *balancer_service_status_description (
	const ServiceStatus status
);

// every value is updated with atomic operations
typedef struct ServiceStats {

	// routed packets to the service
	u64 n_packets_routed;              // total number of packets that were routed to the service
	u64 total_bytes_routed;            // total amount of bytes routed to the service

	// total amount of data received from the service
	u64 n_packets_received;            // packets received from the service
	u64 receives_done;                 // calls to recv ()
	u64 bytes_received;                // bytes received from the service

	// bad types packets - their data was read and discarded
	u64 bad_n_packets_received;        // bad packets received from the service
	u64 bad_receives_done;             // calls to recv ()
	u64 bad_bytes_received;            // bad bytes received from the service

	u64 routed_packets[PACKETS_MAX_TYPES];
	u64 received_packets[PACKETS_MAX_TYPES];

} ServiceStats;

CERVER_EXPORT void balancer_service_stats_print (
	const struct _Service *service
);

struct _Service {

	unsigned int id;
	char name[SERVICE_NAME_SIZE];

	// read and written with atomic operations
	ServiceStatus status;
	Action on_status_change;

	// the connection reconnects by itself after it was connected once
	Connection *connection;
	unsigned int reconnect_wait_time;

	// a service that was down when the balancer started is connected by this thread
	bool reconnect_thread_running;
	pthread_t reconnect_thread_id;

	ServiceStats *stats;

};

typedef struct _Service Service;

CERVER_PRIVATE void balancer_service_delete (void *service_ptr);

CERVER_PRIVATE Service *balancer_service_create (void);

CERVER_PUBLIC ServiceStatus balancer_service_get_status (
	const Service *service
);

CERVER_PUBLIC const char *balancer_service_get_status_string (
	const Service *service
);

CERVER_EXPORT void balancer_service_set_on_status_change (
	Service *service, const Action on_status_change
);

// registers a new service to the load balancer
// a dedicated connection will be created when the balancer starts
// to handle traffic to & from the service
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int balancer_service_register (
	Balancer *balancer,
	const char *ip_address, const u16 port
);

CERVER_EXPORT const char *balancer_service_get_name (
	const Service *service
);

// sets the service's name
CERVER_EXPORT void balancer_service_set_name (
	Service *service, const char *name
);

// sets the time (in secs) to wait to attempt a reconnection
// whenever the service disconnects
// the default value is 20 secs
CERVER_EXPORT void balancer_service_set_reconnect_wait_time (
	Service *service, const unsigned int wait_time
);

#pragma endregion

#pragma region start

// starts the load balancer by first connecting to the registered services
// and checking their ability to handle requests
// the services that are down are connected later by a dedicated thread,
// at least one service must be working
// then the cerver gets started to enable client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_start (Balancer *balancer);

#pragma endregion

#pragma region route

// how many times a recv () can time out in the middle of a packet
#define BALANCER_RECEIVE_MAX_TIMEOUTS		4

// registers the route of a client connection that the balancer receives from
CERVER_PRIVATE void balancer_connection_start (
	Balancer *balancer, const i32 sock_fd, Socket *socket
);

// removes the route of a client connection that has ended,
// its pending responses are discarded
CERVER_PRIVATE void balancer_connection_end (
	Balancer *balancer, const i32 sock_fd
);

// receives a complete packet from a client and routes it to a service
// returns 0 to keep receiving, 1 when the connection has ended
CERVER_PRIVATE u8 balancer_receive (CerverReceive *cr);

#pragma endregion

#pragma region end

// first ends and destroys the balancer's internal cerver
// then disconnects from each of the registered services
// last frees any balancer memory left
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_teardown (Balancer *balancer);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
