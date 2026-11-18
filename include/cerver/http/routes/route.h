/**
 * @file cerver/http/routes/route.h
 * @brief The modifier and the authentication of a route, the types of the handlers and the stats of the routes.
 */

#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include <pthread.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/handler/handler.h"

#include "cerver/http/core/context.h"
#include "cerver/http/requests/request.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

/**
 * @defgroup http_routes_route Route
 * @ingroup http_routes
 * @brief The modifiers, the authentication types, the handlers and the stats of a route.
 * @{
 */

/**
 * @brief The X-macro table of the modifiers of a route: the number, the name, the string and the description of
 * each one.
 */
#define HTTP_ROUTE_MODIFIER_MAP(XX)															\
	XX(0,	NONE, 			None,			Undefined)										\
	XX(1,	MULTI_PART, 	Multi-Part,		Enables multi-part requests on the route)		\
	XX(2,	WEB_SOCKET, 	WebSocket,		Enables websocket connections on the route)

/**
 * @brief The modifier of a route, one `HTTP_ROUTE_MODIFIER_<name>` for each row of #HTTP_ROUTE_MODIFIER_MAP.
 */
typedef enum CerverHttpRouteModifier {

	#define XX(num, name, string, description) HTTP_ROUTE_MODIFIER_##name = num,
	HTTP_ROUTE_MODIFIER_MAP (XX)
	#undef XX

} CerverHttpRouteModifier;

/**
 * @brief Returns the string of a modifier.
 *
 * @param[in] modifier the modifier
 * @return the string of its row in #HTTP_ROUTE_MODIFIER_MAP, or the one of HTTP_ROUTE_MODIFIER_NONE for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_route_modifier_to_string (
	const CerverHttpRouteModifier modifier
);

/**
 * @brief Returns the description of a modifier.
 *
 * @param[in] modifier the modifier
 * @return the description of its row in #HTTP_ROUTE_MODIFIER_MAP, or the one of HTTP_ROUTE_MODIFIER_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_route_modifier_description (
	const CerverHttpRouteModifier modifier
);

/**
 * @brief The X-macro table of the authentication types of a route: the number, the name, the string and the
 * description of each one.
 */
#define HTTP_ROUTE_AUTH_TYPE_MAP(XX)																\
	XX(0,	NONE, 			None,		Undefined)													\
	XX(1,	BEARER, 		Bearer,		A bearer token is expected in the authorization header)		\
	XX(2,	CUSTOM, 		Custom,		A custom method is used to handle authentication)

/**
 * @brief The authentication type of a route, one `HTTP_ROUTE_AUTH_TYPE_<name>` for each row of
 * #HTTP_ROUTE_AUTH_TYPE_MAP.
 */
typedef enum CerverHttpRouteAuthType {

	#define XX(num, name, string, description) HTTP_ROUTE_AUTH_TYPE_##name = num,
	HTTP_ROUTE_AUTH_TYPE_MAP (XX)
	#undef XX

} CerverHttpRouteAuthType;

/**
 * @brief Returns the string of an authentication type.
 *
 * @param[in] type the authentication type
 * @return the string of its row in #HTTP_ROUTE_AUTH_TYPE_MAP, or the one of HTTP_ROUTE_AUTH_TYPE_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_route_auth_type_to_string (
	const CerverHttpRouteAuthType type
);

/**
 * @brief Returns the description of an authentication type.
 *
 * @param[in] type the authentication type
 * @return the description of its row in #HTTP_ROUTE_AUTH_TYPE_MAP, or the one of HTTP_ROUTE_AUTH_TYPE_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_route_auth_type_description (
	const CerverHttpRouteAuthType type
);

/** @brief The size of a table with a handler for each request method. */
#define HTTP_HANDLERS_COUNT					REQUEST_METHOD_SIZE

/** @brief The handler of a request, see #CerverHttpHandler. */
typedef CerverHttpHandler HttpHandler;

/** @brief The authentication handler of an endpoint with HTTP_ROUTE_AUTH_TYPE_CUSTOM, see #CerverHttpAuthenticationHandler. */
typedef CerverHttpAuthenticationHandler HttpAuthenticationHandler;

/**
 * @brief The buckets of the histogram of the process times, the ones of the Prometheus clients.
 *
 * A time bigger than the last bucket is only counted in n_requests (the "+Inf" bucket).
 */
#define HTTP_ROUTE_STATS_DURATION_BUCKETS		11

/**
 * @brief Returns the upper bounds of the buckets of the process times.
 *
 * @return #HTTP_ROUTE_STATS_DURATION_BUCKETS bounds in seconds, a static array
 */
CERVER_PUBLIC const double *cerver_http_route_stats_duration_buckets (void);

/**
 * @brief The stats of the requests of a method of a route.
 *
 * @threadsafety The values are updated with atomic operations by the threads that handle the requests, so they are
 * read with cerver_http_route_stats_snapshot ().
 */
struct _HttpRouteStats {

	/** @brief The route has not handled a request. */
	bool first;

	/** @brief The requests that were handled. */
	size_t n_requests;

	/** @brief The shortest process time, in seconds. */
	double min_process_time;
	/** @brief The longest process time, in seconds. */
	double max_process_time;
	/** @brief The sum of the process times, in seconds. */
	double sum_process_times;
	/** @brief The mean process time, only computed by cerver_http_route_stats_snapshot (). */
	double mean_process_time;

	/** @brief The smallest request, in bytes. */
	size_t min_request_size;
	/** @brief The biggest request, in bytes. */
	size_t max_request_size;
	/** @brief The sum of the sizes of the requests. */
	size_t sum_request_sizes;
	/** @brief The mean size of the requests, only computed by cerver_http_route_stats_snapshot (). */
	size_t mean_request_size;

	/** @brief The smallest response, in bytes. */
	size_t min_response_size;
	/** @brief The biggest response, in bytes. */
	size_t max_response_size;
	/** @brief The sum of the sizes of the responses. */
	size_t sum_response_sizes;
	/** @brief The mean size of the responses, only computed by cerver_http_route_stats_snapshot (). */
	size_t mean_response_size;

	/** @brief The requests whose process time was inside each bucket (they are not cumulative). */
	size_t duration_buckets[HTTP_ROUTE_STATS_DURATION_BUCKETS];

};

/** @brief The stats of the requests of a method of a route, see struct _HttpRouteStats. */
typedef struct _HttpRouteStats HttpRouteStats;

/**
 * @brief Allocates stats without requests.
 *
 * @return the new stats, NULL if they could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRouteStats *cerver_http_route_stats_new (void);

/**
 * @brief Releases the stats of a route.
 *
 * @param[in] route_stats_ptr the stats, they can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_route_stats_delete (void *route_stats_ptr);

/**
 * @brief Adds a request to the stats: its process time, and the sizes of the request and the response.
 *
 * @param[in,out] route_stats the stats, they can be NULL
 * @param[in] process_time the process time, in seconds
 * @param[in] request_size the size of the request
 * @param[in] response_size the size of the response
 * @threadsafety Safe from many threads, with atomic operations.
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_route_stats_update (
	HttpRouteStats *route_stats,
	const double process_time,
	const size_t request_size, const size_t response_size
);

/**
 * @brief Copies the current values of the stats and computes the means from the sums.
 *
 * Stats without requests have 0 in their minimums.
 *
 * @param[in] route_stats the stats
 * @param[out] snapshot where the values are copied
 * @threadsafety Safe while the stats are updated, each value is read with an atomic load.
 */
CERVER_PUBLIC void cerver_http_route_stats_snapshot (
	const HttpRouteStats *route_stats, HttpRouteStats *snapshot
);

/**
 * @brief The stats of the files uploaded to a route.
 *
 * @threadsafety The values are protected by their mutex, read them with cerver_http_route_file_stats_snapshot ().
 */
struct _HttpRouteFileStats {

	/** @brief The route has not received a request with files. */
	bool first;

	/** @brief The requests that were counted. */
	size_t n_requests;

	/** @brief The files that were uploaded. */
	size_t n_uploaded_files;

	/** @brief The fewest files in a request. */
	size_t min_n_files;
	/** @brief The most files in a request. */
	size_t max_n_files;
	/** @brief The sum of the files of the requests. */
	size_t sum_n_files;
	/** @brief The mean files of a request. */
	double mean_n_files;

	/** @brief The smallest file, in bytes. */
	size_t min_file_size;
	/** @brief The biggest file, in bytes. */
	size_t max_file_size;
	/** @brief The sum of the sizes of the files. */
	size_t sum_file_size;
	/** @brief The mean size of a file. */
	double mean_file_size;

	/** @brief Protects the values, NULL in the stats of a single request and in a snapshot. */
	pthread_mutex_t *mutex;

};

/** @brief The stats of the files uploaded to a route, see struct _HttpRouteFileStats. */
typedef struct _HttpRouteFileStats HttpRouteFileStats;

/**
 * @brief Sets the values of file stats without requests, without a mutex.
 *
 * @param[out] route_file_stats the stats, they must not be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_route_file_stats_init (
	HttpRouteFileStats *route_file_stats
);

/**
 * @brief Allocates file stats without requests and without a mutex.
 *
 * @return the new stats, NULL if they could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRouteFileStats *cerver_http_route_file_stats_new (void);

/**
 * @brief Releases file stats and their mutex.
 *
 * @param[in] route_file_stats_ptr the stats, they can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_route_file_stats_delete (
	void *route_file_stats_ptr
);

/**
 * @brief Allocates the file stats of a route, with their mutex.
 *
 * @return the new stats, NULL if they could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRouteFileStats *cerver_http_route_file_stats_create (void);

/**
 * @brief Adds the file stats of a request to the ones of its route.
 *
 * @param[in,out] file_stats the stats of the route, with their mutex
 * @param[in] new_file_stats the stats of the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_route_file_stats_update (
	HttpRouteFileStats *file_stats,
	const HttpRouteFileStats *new_file_stats
);

/**
 * @brief Copies the current values of the file stats, with their mutex.
 *
 * The snapshot does not have a mutex, and stats without requests have 0 in their minimums.
 *
 * @param[in] file_stats the stats, with their mutex
 * @param[out] snapshot where the values are copied
 */
CERVER_PUBLIC void cerver_http_route_file_stats_snapshot (
	const HttpRouteFileStats *file_stats, HttpRouteFileStats *snapshot
);

/** @brief The size that fits every method of REQUEST_METHOD_MAP with its separator (35 methods, 319 bytes). */
#define HTTP_ROUTE_METHODS_STRING_SIZE		512

/**
 * @brief Writes the methods with a handler, separated with ", ".
 *
 * @param[in] handlers the table of #HTTP_HANDLERS_COUNT handlers
 * @param[out] buffer where the methods are written, it always ends with a terminator
 * @param[in] buffer_size the size of the buffer, a method that does not fit is not written
 * @return the length of the string
 */
CERVER_PUBLIC size_t http_route_methods_string (
	HttpHandler const *handlers, char *buffer, const size_t buffer_size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
