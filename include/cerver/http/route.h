#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/request.h"

typedef struct HttpRoutesTokens {

	unsigned int id;
	unsigned int n_routes;
	char ***routes;

} HttpRoutesTokens;

typedef struct HttpRoute {

	// eg. /api/users/login
	estring *base;				// base route - / for top level - "/api/users"
	estring *actual;			// the actual route "login"
	estring *route;				// the complete route "/api/users/login"

	unsigned int n_tokens;

	DoubleList *children;		// children routes of a top level one

	DoubleList *routes_tokens;

	void (*handler)(CerverReceive *cr, HttpRequest *request);

} HttpRoute;

extern HttpRoute *http_route_new (void);

extern void http_route_delete (void *route_ptr);

extern HttpRoute *http_route_create (const char *actual_route);

#endif