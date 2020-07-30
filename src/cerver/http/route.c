#include <stdlib.h>

#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"

#include "cerver/utils/utils.h"

#pragma region tokens

static HttpRoutesTokens *http_routes_tokens_new (void) {

	HttpRoutesTokens *http_routes_tokens = (HttpRoutesTokens *) malloc (sizeof (HttpRoutesTokens));
	if (http_routes_tokens) {
		http_routes_tokens->id = 0;
		http_routes_tokens->n_routes = 0;
		http_routes_tokens->routes = NULL;
	}

	return http_routes_tokens;

}

static void http_routes_tokens_delete (void *http_routes_tokens_ptr) {

	if (http_routes_tokens_ptr) {
		HttpRoutesTokens *http_routes_tokens = (HttpRoutesTokens *) http_routes_tokens_ptr;

		if (http_routes_tokens->routes) {
			for (unsigned int i = 0; i < http_routes_tokens->n_routes; i++) {
				if (http_routes_tokens->routes[i]) {
					for (unsigned int j = 0; j < http_routes_tokens->id; j++) {
						if (http_routes_tokens->routes[i][j]) free (http_routes_tokens->routes[i][j]);
					}

					free (http_routes_tokens->routes[i]);
				}
			}

			free (http_routes_tokens->routes);
		}

		free (http_routes_tokens_ptr);
	}

}

#pragma endregion

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