#ifndef _CERVER_LOAD_SERVICE_H_
#define _CERVER_LOAD_SERVICE_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
