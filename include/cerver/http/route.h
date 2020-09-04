#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/request.h"
#include "cerver/http/socket.h"

#define DEFAULT_ROUTES_TOKENS_SIZE				16

struct _HttpRoute;

typedef enum HttpRouteModifier {

	HTTP_ROUTE_MODIFIER_NONE		= 0,

	HTTP_ROUTE_MODIFIER_WEB_SOCKET	= 1,	// enables web socket connections on this route

} HttpRouteModifier;

typedef enum HttpRouteAuthType {

	HTTP_ROUTE_AUTH_TYPE_NONE		= 0,

	HTTP_ROUTE_AUTH_TYPE_BEARER		= 1,	// we expect a bearer token in the authorization header

} HttpRouteAuthType;

typedef struct HttpRoutesTokens {

	unsigned int id;
	unsigned int n_routes;
	struct _HttpRoute **routes;
	char ***tokens;

} HttpRoutesTokens;

#define HTTP_HANDLERS_COUNT					5

typedef void (*HttpHandler)(CerverReceive *cr, HttpRequest *request);

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
	void (*ws_on_open)(struct _Cerver *, struct _Connection *);
	void (*ws_on_close)(struct _Cerver *, const char *reason);
	void (*ws_on_ping)(struct _Cerver *, struct _Connection *);
	void (*ws_on_pong)(struct _Cerver *, struct _Connection *);
	void (*ws_on_message)(struct _Cerver *, struct _Connection *, const char *msg, const size_t msg_len);
	void (*ws_on_error)(struct _Cerver *, enum _HttpWebSocketError);

	// stats
	size_t n_requests[HTTP_HANDLERS_COUNT];

};

typedef struct _HttpRoute HttpRoute;

CERVER_PUBLIC HttpRoute *http_route_new (void);

CERVER_PUBLIC void http_route_delete (void *route_ptr);

CERVER_PUBLIC int http_route_comparator_by_n_tokens (const void *a, const void *b);

// creates a new route that can be registered to be sued by an http cerver
CERVER_EXPORT HttpRoute *http_route_create ( 
	RequestMethod method, 
	const char *actual_route, 
	HttpHandler handler
);

CERVER_PRIVATE void http_route_init (HttpRoute *route);

// registers a route as a child of a parent route
CERVER_EXPORT void http_route_child_add (HttpRoute *parent, HttpRoute *child);

// sets a modifier for the selected route
CERVER_EXPORT void http_route_set_modifier (HttpRoute *route, HttpRouteModifier modifier);

// enables authentication for the selected route
CERVER_EXPORT void http_route_set_auth (HttpRoute *route, HttpRouteAuthType auth_type);

// sets the method to be used to decode incoming data from jwt & a method to delete it after use
// if no delete method is set, data won't be freed
CERVER_EXPORT void http_route_set_decode_data (HttpRoute *route, 
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *));

// sets a callback to be executed whenever a websocket connection is correctly
// opened in the selected route
CERVER_EXPORT void http_route_set_ws_on_open (HttpRoute *route, 
	void (*ws_on_open)(struct _Cerver *, struct _Connection *));

// sets a callback to be executed whenever a websocket connection
// gets closed from the selected route
CERVER_EXPORT void http_route_set_ws_on_close (HttpRoute *route, 
	void (*ws_on_close)(struct _Cerver *, const char *reason));

// sets a callback to be executed whenever a websocket ping message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_ping (HttpRoute *route, 
	void (*ws_on_ping)(struct _Cerver *, struct _Connection *));

// sets a callback to be executed whenever a websocket pong message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_pong (HttpRoute *route, 
	void (*ws_on_pong)(struct _Cerver *, struct _Connection *));

// sets a callback to be executed whenever a complete websocket message
// is received in the selected route
CERVER_EXPORT void http_route_set_ws_on_message (HttpRoute *route, 
	void (*ws_on_message)(struct _Cerver *, struct _Connection *, const char *msg, size_t msg_len));

// sets a callback to be executed whenever an error ocurred in the selected route
CERVER_EXPORT void http_route_set_ws_on_error (HttpRoute *route, 
	void (*ws_on_error)(struct _Cerver *, enum _HttpWebSocketError));

#endif