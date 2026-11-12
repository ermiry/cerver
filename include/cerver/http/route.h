#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include <pthread.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/request.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpRoute;
struct _HttpReceive;

#define HTTP_ROUTE_MODIFIER_MAP(XX)															\
	XX(0,	NONE, 			None,			Undefined)										\
	XX(1,	MULTI_PART, 	Multi-Part,		Enables multi-part requests on the route)		\
	XX(2,	WEB_SOCKET, 	WebSocket,		Enables websocket connections on the route)

typedef enum HttpRouteModifier {

	#define XX(num, name, string, description) HTTP_ROUTE_MODIFIER_##name = num,
	HTTP_ROUTE_MODIFIER_MAP (XX)
	#undef XX

} HttpRouteModifier;

CERVER_PUBLIC const char *http_route_modifier_to_string (
	const HttpRouteModifier modifier
);

CERVER_PUBLIC const char *http_route_modifier_description (
	const HttpRouteModifier modifier
);

#define HTTP_ROUTE_AUTH_TYPE_MAP(XX)																\
	XX(0,	NONE, 			None,		Undefined)													\
	XX(1,	BEARER, 		Bearer,		A bearer token is expected in the authorization header)		\
	XX(2,	CUSTOM, 		Custom,		A custom method is used to handle authentication)

typedef enum HttpRouteAuthType {

	#define XX(num, name, string, description) HTTP_ROUTE_AUTH_TYPE_##name = num,
	HTTP_ROUTE_AUTH_TYPE_MAP (XX)
	#undef XX

} HttpRouteAuthType;

CERVER_PUBLIC const char *http_route_auth_type_to_string (
	const HttpRouteAuthType type
);

CERVER_PUBLIC const char *http_route_auth_type_description (
	const HttpRouteAuthType type
);

// a table with a handler for each request method
#define HTTP_HANDLERS_COUNT					REQUEST_METHOD_SIZE

typedef void (*HttpHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// the authentication handler of a route with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// returns 0 on success and 1 on error
typedef unsigned int (*HttpAuthenticationHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// the buckets of the histogram of the process times, the ones of the Prometheus clients,
// a time bigger than the last bucket is only counted in n_requests (the "+Inf" bucket)
#define HTTP_ROUTE_STATS_DURATION_BUCKETS		11

// the upper bound in secs of each bucket of the process times
CERVER_PUBLIC const double *http_route_stats_duration_buckets (void);

// the values are updated with atomic operations by the threads that handle the requests,
// so they are read with http_route_stats_snapshot ()
struct _HttpRouteStats {

	bool first;

	size_t n_requests;

	double min_process_time;
	double max_process_time;
	double sum_process_times;
	double mean_process_time;

	size_t min_request_size;
	size_t max_request_size;
	size_t sum_request_sizes;
	size_t mean_request_size;

	size_t min_response_size;
	size_t max_response_size;
	size_t sum_response_sizes;
	size_t mean_response_size;

	// the requests whose process time was inside each bucket (they are not cumulative)
	size_t duration_buckets[HTTP_ROUTE_STATS_DURATION_BUCKETS];

};

typedef struct _HttpRouteStats HttpRouteStats;

CERVER_PRIVATE HttpRouteStats *http_route_stats_new (void);

CERVER_PRIVATE void http_route_stats_delete (void *route_stats_ptr);

// adds one request to counter
// updates process times & request & response sizes
CERVER_PRIVATE void http_route_stats_update (
	HttpRouteStats *route_stats,
	const double process_time,
	const size_t request_size, const size_t response_size
);

// copies the current values of the stats, the means are computed from the sums
CERVER_PUBLIC void http_route_stats_snapshot (
	const HttpRouteStats *route_stats, HttpRouteStats *snapshot
);

struct _HttpRouteFileStats {

	bool first;

	size_t n_requests;

	size_t n_uploaded_files;

	size_t min_n_files;
	size_t max_n_files;
	size_t sum_n_files;
	double mean_n_files;

	size_t min_file_size;
	size_t max_file_size;
	size_t sum_file_size;
	double mean_file_size;

	pthread_mutex_t *mutex;

};

typedef struct _HttpRouteFileStats HttpRouteFileStats;

// sets the values of stats without requests, without a mutex
CERVER_PRIVATE void http_route_file_stats_init (
	HttpRouteFileStats *route_file_stats
);

CERVER_PRIVATE HttpRouteFileStats *http_route_file_stats_new (void);

CERVER_PRIVATE void http_route_file_stats_delete (
	void *route_file_stats_ptr
);

CERVER_PRIVATE HttpRouteFileStats *http_route_file_stats_create (void);

// updates route's file stats with http receive file stats
CERVER_PRIVATE void http_route_file_stats_update (
	HttpRouteFileStats *file_stats,
	const HttpRouteFileStats *new_file_stats
);

// copies the current values of the file stats
CERVER_PUBLIC void http_route_file_stats_snapshot (
	const HttpRouteFileStats *file_stats, HttpRouteFileStats *snapshot
);

// the route of the first routes API, it is still supported:
// when the cerver starts, each method of each route (and of its children at any depth)
// is translated to an endpoint of the routes tree (see router.h and compatibility.h),
// so new code should use groups and endpoints instead
struct _HttpRoute {

	// eg. /api/users/login
	String *base;				// base route - / for top level - "/api/users"
	String *actual;				// the actual route "login"
	String *route;				// the complete route "/api/users/login"

	DoubleList *children;		// children routes

	HttpRouteModifier modifier;

	// auth
	HttpRouteAuthType auth_type;

	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	void *custom_data;
	void (*delete_custom_data)(void *);

	// handler
	HttpHandler handlers[HTTP_HANDLERS_COUNT];

	// stats
	HttpRouteStats *stats[HTTP_HANDLERS_COUNT];
	HttpRouteFileStats *file_stats;

	// the authentication handler needs the request's body
	bool authentication_with_body;

};

typedef struct _HttpRoute HttpRoute;

CERVER_PUBLIC HttpRoute *http_route_new (void);

CERVER_PUBLIC void http_route_delete (void *route_ptr);

// creates a new route that can be registered to be used by an http cerver
CERVER_EXPORT HttpRoute *http_route_create ( 
	const RequestMethod method, 
	const char *actual_route, 
	const HttpHandler handler
);

// sets the route's handler for the selected http method
CERVER_EXPORT void http_route_set_handler (
	HttpRoute *route,
	const RequestMethod method,
	const HttpHandler handler
);

// registers a route as a child of a parent route
// the complete path of a child is resolved when the cerver starts,
// so children can be added in any order and at any depth
CERVER_EXPORT void http_route_child_add (
	HttpRoute *parent, HttpRoute *child
);

// sets a modifier for the selected route
CERVER_EXPORT void http_route_set_modifier (
	HttpRoute *route, const HttpRouteModifier modifier
);

// enables authentication for the selected route
CERVER_EXPORT void http_route_set_auth (
	HttpRoute *route, const HttpRouteAuthType auth_type
);

// sets the method to be used to decode incoming data from JWT
// also sets a method to delete it after use
// if no delete method is set, data won't be freed
CERVER_EXPORT void http_route_set_decode_data (
	HttpRoute *route, 
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
);

// sets a method to decode data from a jwt into a json string
CERVER_EXPORT void http_route_set_decode_data_into_json (
	HttpRoute *route
);

// sets a method to be used to handle auth in a private route
// that has been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
CERVER_EXPORT void http_route_set_authentication_handler (
	HttpRoute *route,
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// by default a request is authenticated when its headers are complete,
// before its body is received, so the files of a rejected request are never saved
// a route whose authentication handler needs the body (its values or files)
// authenticates after the body was received, and its files are saved before the authentication
// the default value is false
CERVER_EXPORT void http_route_set_authentication_with_body (
	HttpRoute *route, const bool authentication_with_body
);

CERVER_EXPORT const void *http_route_get_custom_data (
	const HttpRoute *route
);

CERVER_EXPORT void http_route_set_custom_data (
	HttpRoute *route, void *custom_data
);

CERVER_EXPORT void http_route_set_delete_custom_data (
	HttpRoute *route, void (*delete_custom_data)(void *)
);

CERVER_EXPORT void http_route_print (
	const HttpRoute *route
);

// fits every method of REQUEST_METHOD_MAP with its separator
#define HTTP_ROUTE_METHODS_STRING_SIZE		128

// writes in buffer the methods with a handler, separated with ", "
// returns the length of the string
CERVER_PUBLIC size_t http_route_methods_string (
	HttpHandler const *handlers, char *buffer, const size_t buffer_size
);

#ifdef __cplusplus
}
#endif

#endif
