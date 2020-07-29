#include <stdlib.h>

#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"

HttpRoute *http_route_new (void) {

	HttpRoute *route = (HttpRoute *) malloc (sizeof (HttpRoute));
	if (route) {
		route->base = NULL;
		route->actual = NULL;
		route->route = NULL;

		route->children = NULL;

		route->handler = NULL;
	}

	return route;

}

void http_route_delete (void *route_ptr) {

	if (route_ptr) {
		HttpRoute *route = (HttpRoute *) route_ptr;

		estring_delete (route->base);
		estring_delete (route->actual);
		estring_delete (route->route);

		dlist_delete (route->children);

		free (route_ptr);
	}

}

HttpRoute *http_route_create (const char *actual_route) {

	HttpRoute *route = http_route_new ();
	if (route) {
		route->actual = actual_route ? estring_new (actual_route) : NULL;
	}

	return route;

}