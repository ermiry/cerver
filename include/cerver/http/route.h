#ifndef _CERVER_HTTP_ROUTE_H_
#define _CERVER_HTTP_ROUTE_H_

#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/request.h"

typedef struct HttpRoute {

	estring *base;
	estring *actual;
	estring *route;

	DoubleList *children;

	void (*handler)(CerverReceive *cr, HttpRequest *request);

} HttpRoute;

extern HttpRoute *http_route_new (void);

extern void http_route_delete (void *route_ptr);

extern HttpRoute *http_route_create (const char *actual_route);

#endif