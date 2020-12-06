#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include <pthread.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/request.h"
#include "cerver/http/websockets.h"

#define DEFAULT_ROUTES_TOKENS_SIZE				16

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
	HttpRouteModifier modifier
);

CERVER_PUBLIC const char *http_route_modifier_description (
	HttpRouteModifier modifier
);

#define HTTP_ROUTE_AUTH_TYPE_MAP(XX)																\
	XX(0,	NONE, 			None,		Undefined)													\
	XX(1,	BEARER, 		Bearer,		A bearer token is expected in the authorization header)

typedef enum HttpRouteAuthType {

	#define XX(num, name, string, description) HTTP_ROUTE_AUTH_TYPE_##name = num,
	HTTP_ROUTE_AUTH_TYPE_MAP (XX)
	#undef XX

} HttpRouteAuthType;

CERVER_PUBLIC const char *http_route_auth_type_to_string (
	HttpRouteAuthType type
);

CERVER_PUBLIC const char *http_route_auth_type_description (
	HttpRouteAuthType type
);

struct _HttpRoutesTokens {

	unsigned int id;
	unsigned int n_routes;
	struct _HttpRoute **routes;
	char ***tokens;

};

typedef struct _HttpRoutesTokens HttpRoutesTokens;

#define HTTP_HANDLERS_COUNT					5

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
	double process_time,
	size_t request_size, size_t response_size
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

CERVER_PRIVATE void http_route_file_stats_delete (void *route_file_stats_ptr);

CERVER_PRIVATE HttpRouteFileStats *http_route_file_stats_create (void);

// updates route's file stats with http receive file stats
CERVER_PRIVATE void http_route_file_stats_update (
	HttpRouteFileStats *file_stats,
	HttpRouteFileStats *new_file_stats
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

	HttpRouteAuthType auth_type;

	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	HttpHandler handlers[HTTP_HANDLERS_COUNT];

	// web sockets
	void (*ws_on_open)(const struct _HttpReceive *http_receive);
	void (*ws_on_close)(
		const struct _HttpReceive *, const char *reason
	);
	void (*ws_on_ping)(const struct _HttpReceive *http_receive);
	void (*ws_on_pong)(const struct _HttpReceive *http_receive);
	void (*ws_on_message)(
		const struct _HttpReceive *http_receive,
		const char *msg, const size_t msg_len
	);
	void (*ws_on_error)(
		const struct _HttpReceive *, enum _HttpWebSocketError
	);

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
	RequestMethod method, 
	const char *actual_route, 
	HttpHandler handler
);

// sets the route's handler for the selected http method
CERVER_EXPORT void http_route_set_handler (
	HttpRoute *route, RequestMethod method, HttpHandler handler
);

CERVER_PRIVATE void http_route_init (HttpRoute *route);

// registers a route as a child of a parent route
CERVER_EXPORT void http_route_child_add (
	HttpRoute *parent, HttpRoute *child
);

// sets a modifier for the selected route
CERVER_EXPORT void http_route_set_modifier (
	HttpRoute *route, HttpRouteModifier modifier
);

// enables authentication for the selected route
CERVER_EXPORT void http_route_set_auth (
	HttpRoute *route, HttpRouteAuthType auth_type
);

// sets the method to be used to decode incoming data from jwt & a method to delete it after use
// if no delete method is set, data won't be freed
CERVER_EXPORT void http_route_set_decode_data (
	HttpRoute *route, 
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
);

// sets a callback to be executed whenever a websocket connection is correctly
// opened in the selected route
CERVER_EXPORT void http_route_set_ws_on_open (
	HttpRoute *route, 
	void (*ws_on_open)(const struct _HttpReceive *)
);

// sets a callback to be executed whenever a websocket connection
// gets closed from the selected route
CERVER_EXPORT void http_route_set_ws_on_close (
	HttpRoute *route, 
	void (*ws_on_close)(
		const struct _HttpReceive *,
		const char *reason
	)
);

// sets a callback to be executed whenever a websocket ping message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_ping (
	HttpRoute *route, 
	void (*ws_on_ping)(const struct _HttpReceive *)
);

// sets a callback to be executed whenever a websocket pong message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_pong (
	HttpRoute *route, 
	void (*ws_on_pong)(const struct _HttpReceive *)
);

// sets a callback to be executed whenever a complete websocket message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_message (
	HttpRoute *route, 
	void (*ws_on_message)(
		const struct _HttpReceive *,
		const char *msg, size_t msg_len
	)
);

// sets a callback to be executed whenever an error ocurred in the selected route
CERVER_EXPORT void http_route_set_ws_on_error (
	HttpRoute *route, 
	void (*ws_on_error)(
		const struct _HttpReceive *,
		enum _HttpWebSocketError
	)
);

CERVER_EXPORT void http_route_print (HttpRoute *route);

#endif