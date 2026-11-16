/**
 * @file cerver/http/routes/route.h
 * @brief The types of the handlers and the stats of the routes, and the routes of the first routes API (HttpRoute).
 */

#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include <pthread.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/handler/handler.h"

#include "cerver/http/requests/request.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpRoute;
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
typedef enum HttpRouteModifier {

	#define XX(num, name, string, description) HTTP_ROUTE_MODIFIER_##name = num,
	HTTP_ROUTE_MODIFIER_MAP (XX)
	#undef XX

} HttpRouteModifier;

/**
 * @brief Returns the string of a modifier.
 *
 * @param[in] modifier the modifier
 * @return the string of its row in #HTTP_ROUTE_MODIFIER_MAP, or the one of HTTP_ROUTE_MODIFIER_NONE for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_route_modifier_to_string (
	const HttpRouteModifier modifier
);

/**
 * @brief Returns the description of a modifier.
 *
 * @param[in] modifier the modifier
 * @return the description of its row in #HTTP_ROUTE_MODIFIER_MAP, or the one of HTTP_ROUTE_MODIFIER_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_route_modifier_description (
	const HttpRouteModifier modifier
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
typedef enum HttpRouteAuthType {

	#define XX(num, name, string, description) HTTP_ROUTE_AUTH_TYPE_##name = num,
	HTTP_ROUTE_AUTH_TYPE_MAP (XX)
	#undef XX

} HttpRouteAuthType;

/**
 * @brief Returns the string of an authentication type.
 *
 * @param[in] type the authentication type
 * @return the string of its row in #HTTP_ROUTE_AUTH_TYPE_MAP, or the one of HTTP_ROUTE_AUTH_TYPE_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_route_auth_type_to_string (
	const HttpRouteAuthType type
);

/**
 * @brief Returns the description of an authentication type.
 *
 * @param[in] type the authentication type
 * @return the description of its row in #HTTP_ROUTE_AUTH_TYPE_MAP, or the one of HTTP_ROUTE_AUTH_TYPE_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_route_auth_type_description (
	const HttpRouteAuthType type
);

/** @brief The size of a table with a handler for each request method. */
#define HTTP_HANDLERS_COUNT					REQUEST_METHOD_SIZE

/**
 * @brief The handler of a request: it answers it with the receive.
 */
typedef void (*HttpHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

/**
 * @brief The authentication handler of a route with HTTP_ROUTE_AUTH_TYPE_CUSTOM: it returns 0 when the request is
 * authenticated and 1 on error.
 */
typedef unsigned int (*HttpAuthenticationHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

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
CERVER_PUBLIC const double *http_route_stats_duration_buckets (void);

/**
 * @brief The stats of the requests of a method of a route.
 *
 * @threadsafety The values are updated with atomic operations by the threads that handle the requests, so they are
 * read with http_route_stats_snapshot ().
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
	/** @brief The mean process time, only computed by http_route_stats_snapshot (). */
	double mean_process_time;

	/** @brief The smallest request, in bytes. */
	size_t min_request_size;
	/** @brief The biggest request, in bytes. */
	size_t max_request_size;
	/** @brief The sum of the sizes of the requests. */
	size_t sum_request_sizes;
	/** @brief The mean size of the requests, only computed by http_route_stats_snapshot (). */
	size_t mean_request_size;

	/** @brief The smallest response, in bytes. */
	size_t min_response_size;
	/** @brief The biggest response, in bytes. */
	size_t max_response_size;
	/** @brief The sum of the sizes of the responses. */
	size_t sum_response_sizes;
	/** @brief The mean size of the responses, only computed by http_route_stats_snapshot (). */
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
CERVER_PRIVATE HttpRouteStats *http_route_stats_new (void);

/**
 * @brief Releases the stats of a route.
 *
 * @param[in] route_stats_ptr the stats, they can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_route_stats_delete (void *route_stats_ptr);

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
CERVER_PRIVATE void http_route_stats_update (
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
CERVER_PUBLIC void http_route_stats_snapshot (
	const HttpRouteStats *route_stats, HttpRouteStats *snapshot
);

/**
 * @brief The stats of the files uploaded to a route.
 *
 * @threadsafety The values are protected by their mutex, read them with http_route_file_stats_snapshot ().
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
CERVER_PRIVATE void http_route_file_stats_init (
	HttpRouteFileStats *route_file_stats
);

/**
 * @brief Allocates file stats without requests and without a mutex.
 *
 * @return the new stats, NULL if they could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRouteFileStats *http_route_file_stats_new (void);

/**
 * @brief Releases file stats and their mutex.
 *
 * @param[in] route_file_stats_ptr the stats, they can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_route_file_stats_delete (
	void *route_file_stats_ptr
);

/**
 * @brief Allocates the file stats of a route, with their mutex.
 *
 * @return the new stats, NULL if they could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRouteFileStats *http_route_file_stats_create (void);

/**
 * @brief Adds the file stats of a request to the ones of its route.
 *
 * @param[in,out] file_stats the stats of the route, with their mutex
 * @param[in] new_file_stats the stats of the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_route_file_stats_update (
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
CERVER_PUBLIC void http_route_file_stats_snapshot (
	const HttpRouteFileStats *file_stats, HttpRouteFileStats *snapshot
);

/**
 * @brief A route of the first routes API, that is still supported.
 *
 * When the cerver starts, each method of each route (and of its children at any depth) is translated to an
 * endpoint of the routes tree (see cerver/http/routes/tree.h), so new code should use groups and endpoints instead.
 */
struct _HttpRoute {

	/** @brief The route of its parent, "/" for a top level route; "/api/users" in /api/users/login. */
	String *base;
	/** @brief Its own part, "login" in /api/users/login. */
	String *actual;
	/** @brief The complete route, "/api/users/login". */
	String *route;

	/** @brief The children routes, the route deletes them. */
	DoubleList *children;

	/** @brief The modifier of the route. */
	HttpRouteModifier modifier;

	/** @brief The authentication type of the route. */
	HttpRouteAuthType auth_type;

	/** @brief Decodes the data of the JWT of a bearer authentication, from its claims. */
	HttpDecodeData decode_data;
	/** @brief Releases the decoded data, NULL to keep it. */
	void (*delete_decoded_data)(void *);

	/** @brief The authentication handler of HTTP_ROUTE_AUTH_TYPE_CUSTOM, it returns 0 on success. */
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	/** @brief The custom data of the route. */
	void *custom_data;
	/** @brief Releases the custom data when the route is deleted, NULL to keep it. */
	void (*delete_custom_data)(void *);

	/** @brief The handler of each request method, NULL for the methods that the route does not handle. */
	HttpHandler handlers[HTTP_HANDLERS_COUNT];

	/** @brief The stats of each method that has a handler. */
	HttpRouteStats *stats[HTTP_HANDLERS_COUNT];
	/** @brief The stats of the files uploaded to the route. */
	HttpRouteFileStats *file_stats;

	/** @brief The authentication handler needs the body of the request. */
	bool authentication_with_body;

	/**
	 * @brief The name of the pool of threads that handles its requests (http_route_set_pool ()), NULL for the
	 * default one.
	 *
	 * @since 3.0b-5
	 */
	String *pool_name;

};

/** @brief A route of the first routes API, see struct _HttpRoute. */
typedef struct _HttpRoute HttpRoute;

/**
 * @brief Allocates an empty route, without paths, children or file stats.
 *
 * @return the new route, NULL if it could not be allocated
 * @warning The route can not be printed nor have children; create a route with http_route_create ().
 */
CERVER_PUBLIC HttpRoute *http_route_new (void);

/**
 * @brief Releases a route, its children, its stats and its custom data (with its delete method).
 *
 * @param[in] route_ptr the route, it can be NULL
 */
CERVER_PUBLIC void http_route_delete (void *route_ptr);

/**
 * @brief Creates a top level route with a handler for a method, to be registered in an HTTP cerver.
 *
 * @param[in] method the request method
 * @param[in] actual_route the route without its leading "/", like "users"; "/" for the root
 * @param[in] handler the handler of the method; an invalid method is logged and gets no handler
 * @return the new route, released with http_route_delete () (the HTTP cerver releases the ones that it has); NULL
 * for a NULL actual_route or if it could not be allocated
 */
CERVER_EXPORT HttpRoute *http_route_create (
	const RequestMethod method,
	const char *actual_route,
	const HttpHandler handler
);

/**
 * @brief Sets the handler of a method of a route.
 *
 * @param[in,out] route the route
 * @param[in] method the request method, an invalid one is logged and ignored
 * @param[in] handler the handler, it replaces the one that the method had
 */
CERVER_EXPORT void http_route_set_handler (
	HttpRoute *route,
	const RequestMethod method,
	const HttpHandler handler
);

/**
 * @brief Registers a route as a child of a parent route; the parent owns it from then on.
 *
 * The complete path of a child is resolved when the cerver starts, so children can be added in any order and at
 * any depth.
 *
 * @param[in,out] parent the parent route, created with http_route_create ()
 * @param[in,out] child the child route
 */
CERVER_EXPORT void http_route_child_add (
	HttpRoute *parent, HttpRoute *child
);

/**
 * @brief Sets the modifier of a route.
 *
 * @param[in,out] route the route
 * @param[in] modifier the modifier
 */
CERVER_EXPORT void http_route_set_modifier (
	HttpRoute *route, const HttpRouteModifier modifier
);

/**
 * @brief Enables the authentication of a route.
 *
 * @param[in,out] route the route
 * @param[in] auth_type the authentication type
 */
CERVER_EXPORT void http_route_set_auth (
	HttpRoute *route, const HttpRouteAuthType auth_type
);

/**
 * @brief Sets the method that decodes the data of the JWT of a request, and the one that releases it.
 *
 * @param[in,out] route the route
 * @param[in] decode_data the method that decodes the data, from the claims of the token
 * @param[in] delete_decoded_data the method that releases it, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 * @since 3.0b-7 decode_data is a HttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT void http_route_set_decode_data (
	HttpRoute *route,
	HttpDecodeData decode_data,
	void (*delete_decoded_data)(void *)
);

/**
 * @brief Sets the method that decodes the data of the JWT of a request into a json string, released with
 * free ().
 *
 * @param[in,out] route the route
 */
CERVER_EXPORT void http_route_set_decode_data_into_json (
	HttpRoute *route
);

/**
 * @brief Sets the method that authenticates the requests of a route with HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] route the route
 * @param[in] authentication_handler the method, it returns 0 on success and 1 on error
 */
CERVER_EXPORT void http_route_set_authentication_handler (
	HttpRoute *route,
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

/**
 * @brief Sets whether the authentication handler of a route needs the body of the request.
 *
 * By default a request is authenticated when its headers are complete, before its body is received, so the files
 * of a rejected request are never saved; a route whose authentication handler needs the body (its values or files)
 * authenticates after the body was received, and its files are saved before the authentication.
 *
 * @param[in,out] route the route
 * @param[in] authentication_with_body true to authenticate with the body, the default is false
 */
CERVER_EXPORT void http_route_set_authentication_with_body (
	HttpRoute *route, const bool authentication_with_body
);

/**
 * @brief Sets the pool of threads that handles the requests of a route (cerver/http/core/pool.h), with
 * CERVER_HANDLER_TYPE_EVENTS; its children do not inherit it.
 *
 * The pool is found when the cerver starts: a name that no pool of the cerver has is an error of the routes and
 * the cerver does not start.
 *
 * @param[in,out] route the route
 * @param[in] name the name of the pool, copied; NULL or "default" for the thpool of the cerver
 * @since 3.0b-5
 */
CERVER_EXPORT void http_route_set_pool (
	HttpRoute *route, const char *name
);

/**
 * @brief Returns the custom data of a route.
 *
 * @param[in] route the route
 * @return the custom data, NULL for a NULL route
 */
CERVER_EXPORT const void *http_route_get_custom_data (
	const HttpRoute *route
);

/**
 * @brief Sets the custom data of a route; the data that it had is not released.
 *
 * @param[in,out] route the route
 * @param[in] custom_data the custom data
 */
CERVER_EXPORT void http_route_set_custom_data (
	HttpRoute *route, void *custom_data
);

/**
 * @brief Sets the method that releases the custom data of a route when it is deleted.
 *
 * @param[in,out] route the route
 * @param[in] delete_custom_data the method, NULL so the data is not released
 */
CERVER_EXPORT void http_route_set_delete_custom_data (
	HttpRoute *route, void (*delete_custom_data)(void *)
);

/**
 * @brief Prints a route: its path, its methods, its modifier, its authentication and its children.
 *
 * @param[in] route the route, created with http_route_create ()
 */
CERVER_EXPORT void http_route_print (
	const HttpRoute *route
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
