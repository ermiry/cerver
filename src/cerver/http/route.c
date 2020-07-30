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

		route->n_tokens = 0;

		route->children = NULL;

		route->routes_tokens = NULL;

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

		dlist_delete (route->routes_tokens);

		free (route_ptr);
	}

}

int http_route_comparator_by_n_tokens (const void *a, const void *b) {

	return ((HttpRoute *) a)->n_tokens - ((HttpRoute *) b)->n_tokens;

}

HttpRoute *http_route_create (const char *actual_route, 
	void (*handler)(CerverReceive *cr, HttpRequest *request)) {

	HttpRoute *route = http_route_new ();
	if (route) {
		route->actual = estring_new (actual_route);

		// by default, all routes are top level when they are created
		route->base = estring_new ("/");
		route->route = estring_create ("/%s", actual_route);

		route->children = dlist_init (http_route_delete, NULL);

		route->handler = handler;
	}

	return route;

}

void http_route_init (HttpRoute *route) {

	if (route) {
		if (route->children) {
			// route->n_routes = route->children->size;
			// route->routes = (char ***) calloc (route->n_routes, sizeof (char **));
			// route->routes_lens = (unsigned int *) calloc (route->n_routes, sizeof (unsigned int));
			// if (route->routes && route->routes_lens) {
			// 	unsigned int idx = 0;
			// 	HttpRoute *child = NULL;
			// 	for (ListElement *le = dlist_start (route->children); le; le = le->next) {
			// 		child = (HttpRoute *) le->data;

			// 		route->routes[idx] = c_string_split (child->actual->str, '/', &route->routes_lens[idx]);

			// 		idx += 1;
			// 	}
			// }
		}
	}

}

void http_route_child_add (HttpRoute *parent, HttpRoute *child) {

	if (parent && child) {
		dlist_insert_after (parent->children, dlist_end (parent->children), child);

		// refactor child paths
		estring_delete (child->base);
		child->base = estring_new (parent->route->str);

		estring_delete (child->route);
		child->route = estring_create ("%s/%s", child->base->str, child->actual->str);
	}

}