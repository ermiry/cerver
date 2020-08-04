#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/request.h"

#define DEFAULT_ROUTES_TOKENS_SIZE				16

struct _HttpRoute;

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

struct _HttpRoute {

	// eg. /api/users/login
	estring *base;				// base route - / for top level - "/api/users"
	estring *actual;			// the actual route "login"
	estring *route;				// the complete route "/api/users/login"

	size_t n_tokens;
	char **tokens;

	DoubleList *children;		// children routes of a top level one

	HttpRoutesTokens **routes_tokens;

	HttpRouteAuthType auth_type;

	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	void (*handler)(CerverReceive *cr, HttpRequest *request);

};

typedef struct _HttpRoute HttpRoute;

extern HttpRoute *http_route_new (void);

extern void http_route_delete (void *route_ptr);

extern int http_route_comparator_by_n_tokens (const void *a, const void *b);

extern HttpRoute *http_route_create (const char *actual_route, 
	void (*handler)(CerverReceive *cr, HttpRequest *request));

extern void http_route_init (HttpRoute *route);

extern void http_route_child_add (HttpRoute *parent, HttpRoute *child);

// enables authentication for the selected route
extern void http_route_set_auth (HttpRoute *route, HttpRouteAuthType auth_type);

// sets the method to be used to decode incoming data from jwt & a method to delete it after use
// if no delete method is set, data won't be freed
extern void http_route_set_decode_data (HttpRoute *route, 
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *));

#endif