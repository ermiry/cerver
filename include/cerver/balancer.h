#ifndef _CERVER_BALANCER_H_
#define _CERVER_BALANCER_H_

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/config.h"
#include "cerver/packets.h"

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

typedef struct BalancerStats {

	// good types packets that the balancer can handle
	u64 n_packets_received;            // packets received from clients
	u64 receives_done;                 // receives done to clients
	u64 bytes_received;                // bytes received from clients

	// bad types packets - consumed data from sock fd until next header
	u64 bad_n_packets_received;        // packets received from clients
	u64 bad_receives_done;             // receives done to clients
	u64 bad_bytes_received;            // bytes received from clients

	// routed packets to services
	u64 n_packets_routed;              // total number of packets that were routed to services
	u64 total_bytes_routed;            // total amount of bytes routed to services

	// responses sent to the original clients
	u64 n_packets_sent;                // total number of packets that were sent
	u64 total_bytes_sent;              // total amount of bytes sent by the cerver

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

#define BALANCER_RECEIVE_BUFFER_SIZE		512
#define BALANCER_CONSUME_BUFFER_SIZE		1024

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

	pthread_mutex_t *mutex;

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
CERVER_EXPORT Balancer *balancer_create (
	const char *name, const BalancerType type,
	const u16 port, const u16 connection_queue,
	const unsigned int n_services
);

#pragma endregion

#pragma region services

#define SERVICE_NAME_SIZE					64
#define SERVICE_CONNECTION_NAME_SIZE		64

#define SERVICE_CONSUME_BUFFER_SIZE			512

#define SERVICE_DEFAULT_MAX_SLEEP			30
#define SERVICE_DEFAULT_WAIT_TIME			20

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

typedef struct ServiceStats {

	// routed packets to the service
	u64 n_packets_routed;              // total number of packets that were routed to the service
	u64 total_bytes_routed;            // total amount of bytes routed to the service

	// total amount of data received from the service
	u64 n_packets_received;            // packets received from the service
	u64 receives_done;                 // calls to recv ()
	u64 bytes_received;                // bytes received from the service

	// bad types packets - consumed data from sock fd until next header
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

	ServiceStatus status;
	Action on_status_change;

	Connection *connection;
	unsigned int reconnect_wait_time;
	pthread_t reconnect_thread_id;

	int forward_pipe_fds[2];
	int receive_pipe_fds[2];

	ServiceStats *stats;

};

typedef struct _Service Service;

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
// then the cerver gets started to enable client connections
// returns 0 on success, 1 on error
CERVER_EXPORT u8 balancer_start (Balancer *balancer);

#pragma endregion

#pragma region route

// routes the received packet to a service to be handled
// first sends the packet header with the correct sock fd
// if any data, it is forwarded from one sock fd to another using splice ()
CERVER_PRIVATE void balancer_route_to_service (
	CerverReceive *cr,
	Balancer *balancer, Connection *connection,
	PacketHeader *header
);

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