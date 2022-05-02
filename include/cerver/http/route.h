#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include <pthread.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/request.h"

#define DEFAULT_ROUTES_TOKENS_SIZE				16

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

struct _HttpRoutesTokens {

	unsigned int id;
	unsigned int n_routes;
	struct _HttpRoute **routes;
	char ***tokens;

};

typedef struct _HttpRoutesTokens HttpRoutesTokens;

#define HTTP_HANDLERS_COUNT					8

typedef void (*HttpHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

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

	pthread_mutex_t *mutex;

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

struct _HttpRoute {

	// eg. /api/users/login
	String *base;				// base route - / for top level - "/api/users"
	String *actual;				// the actual route "login"
	String *route;				// the complete route "/api/users/login"

	size_t n_tokens;
	char **tokens;

	DoubleList *children;		// children routes of a top level one

	HttpRoutesTokens **routes_tokens;

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

};

typedef struct _HttpRoute HttpRoute;

CERVER_PUBLIC HttpRoute *http_route_new (void);

CERVER_PUBLIC void http_route_delete (void *route_ptr);

CERVER_PUBLIC int http_route_comparator_by_n_tokens (
	const void *a, const void *b
);

// creates a new route that can be registered to be sued by an http cerver
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

CERVER_PRIVATE void http_route_init (HttpRoute *route);

// registers a route as a child of a parent route
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

#ifdef __cplusplus
}
#endif

#endif
