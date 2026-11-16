/**
 * @file cerver/load/service.h
 * @brief The services of a load balancer: their status, their stats and their registry.
 */

#ifndef _CERVER_LOAD_SERVICE_H_
#define _CERVER_LOAD_SERVICE_H_

#include "cerver/config.h"

#include "cerver/load/balancer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup load_services Services
 * @ingroup load
 * @brief The services that a balancer routes the packets to.
 * @{
 */

/** @brief The size of the name of a service, with its terminator. */
#define SERVICE_NAME_SIZE					64

/**
 * @brief The size of the name of the connection of a service.
 *
 * @note cerver does not use it: the connection takes the name of the service.
 * @deprecated It goes away in the 3.0.
 */
#define SERVICE_CONNECTION_NAME_SIZE		64

/**
 * @brief The max sleep of the connection of a service.
 *
 * @note cerver does not use it: the connection makes a single attempt, see #SERVICE_CONNECT_MAX_SLEEP.
 * @deprecated It goes away in the 3.0.
 */
#define SERVICE_DEFAULT_MAX_SLEEP			30

/** @brief The default seconds that a service waits to reconnect after it disconnected. */
#define SERVICE_DEFAULT_WAIT_TIME			20

/** @brief The max sleep of the connection to a service: it makes a single attempt. */
#define SERVICE_CONNECT_MAX_SLEEP			1

/**
 * @brief The X-macro table of the statuses of a service: the number, the name, the string and the description of
 * each one.
 */
#define SERVICE_STATUS_MAP(XX)																			\
	XX(0, 	NONE, 			None, 			None)														\
	XX(1, 	CONNECTING, 	Connecting, 	Creating connection with service)							\
	XX(2, 	READY, 			Ready, 			Ready to start accepting packets)							\
	XX(3, 	WORKING, 		Working, 		Handling packets)											\
	XX(4, 	DISCONNECTING, 	Disconnecting, 	In the process of closing the connection)					\
	XX(5, 	DISCONNECTED, 	Disconnected, 	The balancer has been disconnected from the service)		\
	XX(6, 	UNAVAILABLE, 	Unavailable, 	The service is down or is not handling packets)				\
	XX(7, 	ENDED, 			Ended, 			The connection with the service has ended)

/**
 * @brief The status of a service, one `SERVICE_STATUS_<name>` for each row of #SERVICE_STATUS_MAP.
 */
typedef enum ServiceStatus {

	#define XX(num, name, string, description) SERVICE_STATUS_##name = num,
	SERVICE_STATUS_MAP (XX)
	#undef XX

} ServiceStatus;

/**
 * @brief Returns the string of a status.
 *
 * @param[in] status the status
 * @return the string of its row in #SERVICE_STATUS_MAP, or the one of SERVICE_STATUS_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_EXPORT const char *balancer_service_status_to_string (
	const ServiceStatus status
);

/**
 * @brief Returns the description of a status.
 *
 * @param[in] status the status
 * @return the description of its row in #SERVICE_STATUS_MAP, or the one of SERVICE_STATUS_NONE for a value out of
 * the table; a static string that must not be freed
 */
CERVER_EXPORT const char *balancer_service_status_description (
	const ServiceStatus status
);

/**
 * @brief The stats of a service.
 *
 * @threadsafety Every value is updated with __atomic operations; read them with CERVER_STATS_GET ().
 */
typedef struct ServiceStats {

	/** @brief The packets routed to the service. */
	u64 n_packets_routed;
	/** @brief The bytes routed to the service. */
	u64 total_bytes_routed;

	/** @brief The packets received from the service. */
	u64 n_packets_received;
	/** @brief The calls to recv (). */
	u64 receives_done;
	/** @brief The bytes received from the service. */
	u64 bytes_received;

	/** @brief The packets of bad types received from the service, their data was read and discarded. */
	u64 bad_n_packets_received;
	/** @brief The calls to recv () for the bad packets. */
	u64 bad_receives_done;
	/** @brief The bytes of the bad packets. */
	u64 bad_bytes_received;

	/** @brief The packets routed, by packet type. */
	u64 routed_packets[PACKETS_MAX_TYPES];
	/** @brief The packets received, by packet type. */
	u64 received_packets[PACKETS_MAX_TYPES];

} ServiceStats;

/**
 * @brief Prints the stats of a service.
 *
 * @param[in] service the service, NULL prints nothing
 */
CERVER_EXPORT void balancer_service_stats_print (
	const struct _Service *service
);

/**
 * @brief A service of a balancer.
 */
struct _Service {

	/** @brief The id of the service, its index in the balancer. */
	unsigned int id;
	/** @brief The name of the service, "service-<id>" by default. */
	char name[SERVICE_NAME_SIZE];

	/** @brief The status, read and written with __atomic. */
	ServiceStatus status;
	/** @brief Runs with the service every time that its status changes. */
	Action on_status_change;

	/** @brief The connection of the client of the balancer to the service, it reconnects by itself after it was connected once. */
	Connection *connection;
	/** @brief The seconds to wait to reconnect after the service disconnected. */
	unsigned int reconnect_wait_time;

	/** @brief A service that was down when the balancer started is connected by its reconnect thread. */
	bool reconnect_thread_running;
	/** @brief The reconnect thread. */
	pthread_t reconnect_thread_id;

	/** @brief The stats of the service. */
	ServiceStats *stats;

};

/** @brief A service of a balancer, see struct _Service. */
typedef struct _Service Service;

/**
 * @brief Releases a service and its stats; its connection belongs to the client of the balancer.
 *
 * @param[in] service_ptr the service, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void balancer_service_delete (void *service_ptr);

/**
 * @brief Creates a service with its stats.
 *
 * @return the new service, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Service *balancer_service_create (void);

/**
 * @brief Returns the status of a service.
 *
 * @param[in] service the service
 * @return the status, SERVICE_STATUS_NONE for a NULL service
 * @threadsafety Safe from any thread, it reads the status with __atomic.
 */
CERVER_PUBLIC ServiceStatus balancer_service_get_status (
	const Service *service
);

/**
 * @brief Returns the string of the status of a service.
 *
 * @param[in] service the service
 * @return the string of its status, a static string that must not be freed
 */
CERVER_PUBLIC const char *balancer_service_get_status_string (
	const Service *service
);

/**
 * @brief Sets a method that runs every time that the status of a service changes.
 *
 * @param[in,out] service the service
 * @param[in] on_status_change the method, it gets the service
 */
CERVER_EXPORT void balancer_service_set_on_status_change (
	Service *service, const Action on_status_change
);

/**
 * @brief Registers a new service to the balancer; its connection is made when the balancer starts.
 *
 * @param[in,out] balancer the balancer
 * @param[in] ip_address the address of the service
 * @param[in] port the port of the service
 * @retval 0 the service was registered
 * @retval 1 a NULL argument, the balancer already has its n_services, or the service could not be created
 */
CERVER_EXPORT unsigned int balancer_service_register (
	Balancer *balancer,
	const char *ip_address, const u16 port
);

/**
 * @brief Returns the name of a service.
 *
 * @param[in] service the service
 * @return the name, NULL for a NULL service
 */
CERVER_EXPORT const char *balancer_service_get_name (
	const Service *service
);

/**
 * @brief Sets the name of a service.
 *
 * @param[in,out] service the service
 * @param[in] name the name, truncated to SERVICE_NAME_SIZE - 1 characters; NULL keeps the current one
 */
CERVER_EXPORT void balancer_service_set_name (
	Service *service, const char *name
);

/**
 * @brief Sets the seconds to wait to reconnect whenever the service disconnects.
 *
 * @param[in,out] service the service
 * @param[in] wait_time the seconds, the default is #SERVICE_DEFAULT_WAIT_TIME
 */
CERVER_EXPORT void balancer_service_set_reconnect_wait_time (
	Service *service, const unsigned int wait_time
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
