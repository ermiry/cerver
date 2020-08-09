#include <stdlib.h>

#include "cerver/types/string.h"

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
		http_routes_tokens->tokens = NULL;
	}

	return http_routes_tokens;

}

static void http_routes_tokens_delete (void *http_routes_tokens_ptr) {

	if (http_routes_tokens_ptr) {
		HttpRoutesTokens *http_routes_tokens = (HttpRoutesTokens *) http_routes_tokens_ptr;

		if (http_routes_tokens->routes) free (http_routes_tokens->routes);

		if (http_routes_tokens->tokens) {
			for (unsigned int i = 0; i < http_routes_tokens->n_routes; i++) {
				if (http_routes_tokens->tokens[i]) {
					for (unsigned int j = 0; j < http_routes_tokens->id; j++) {
						if (http_routes_tokens->tokens[i][j]) free (http_routes_tokens->tokens[i][j]);
					}

					free (http_routes_tokens->tokens[i]);
				}
			}

			free (http_routes_tokens->tokens);
		}

		free (http_routes_tokens_ptr);
	}

}

#pragma endregion

#pragma region route

HttpRoute *http_route_new (void) {

	HttpRoute *route = (HttpRoute *) malloc (sizeof (HttpRoute));
	if (route) {
		route->base = NULL;
		route->actual = NULL;
		route->route = NULL;

		route->n_tokens = 0;
		route->tokens = NULL;

		route->children = NULL;

		route->routes_tokens = NULL;

		route->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

		route->decode_data = NULL;
		route->delete_decoded_data = NULL;

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++)
			route->handlers[i] = NULL;
	}

	return route;

}

void http_route_delete (void *route_ptr) {

	if (route_ptr) {
		HttpRoute *route = (HttpRoute *) route_ptr;

		str_delete (route->base);
		str_delete (route->actual);
		str_delete (route->route);

		// 03/08/2020 - deleted in http_routes_tokens_delete ()
		// free (route->tokens);

		dlist_delete (route->children);

		if (route->routes_tokens) {
			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				if (route->routes_tokens[i]) http_routes_tokens_delete (route->routes_tokens[i]);
			}

			free (route->routes_tokens);
		}

		free (route_ptr);
	}

}

int http_route_comparator_by_n_tokens (const void *a, const void *b) {

	return ((HttpRoute *) a)->n_tokens - ((HttpRoute *) b)->n_tokens;

}

// creates a new route that can be registered to be sued by an http cerver
HttpRoute *http_route_create ( 
	RequestMethod method, 
	const char *actual_route, 
	HttpHandler handler
) {

	HttpRoute *route = http_route_new ();
	if (route) {
		route->actual = str_new (actual_route);

		// by default, all routes are top level when they are created
		route->base = str_new ("/");
		route->route = str_create ("/%s", actual_route);

		route->children = dlist_init (http_route_delete, NULL);

		route->handlers[method] = handler;
	}

	return route;

}

void http_route_init (HttpRoute *route) {

	if (route) {
		if (route->children) {
			route->routes_tokens = (HttpRoutesTokens **) calloc (DEFAULT_ROUTES_TOKENS_SIZE, sizeof (HttpRoutesTokens *));

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				route->routes_tokens[i] = http_routes_tokens_new ();
				route->routes_tokens[i]->id = i + 1;
			}

			// prepare children routes
			HttpRoute *child = NULL;
			for (ListElement *le = dlist_start (route->children); le; le = le->next) {
				child = (HttpRoute *) le->data;

				child->tokens = c_string_split (child->actual->str, '/', &child->n_tokens);
				if (!child->tokens) {
					child->tokens = (char **) calloc (1, sizeof (char *));
					child->tokens[0] = strdup (child->actual->str);
					child->n_tokens = 1;
				}

				// printf ("%s -> n_tokens: %ld\n", child->actual->str, child->n_tokens);

				// check if route params have been setup
				for (unsigned int i = 0; i < child->n_tokens; i++) {
					if (c_string_count_tokens (child->tokens[i], ':')) {
						free (child->tokens[i]);

						child->tokens[i] = (char *) calloc (2, sizeof (char));
						child->tokens[i][0] = '*';
						child->tokens[i][1] = '\0';
					}
				}

				route->routes_tokens[child->n_tokens - 1]->n_routes += 1;
			}

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				route->routes_tokens[i]->routes = (HttpRoute **) calloc (route->routes_tokens[i]->n_routes, sizeof (HttpRoute));
				route->routes_tokens[i]->tokens = (char ***) calloc (route->routes_tokens[i]->n_routes, sizeof (char **));

				for (unsigned int route_idx = 0; route_idx < route->routes_tokens[i]->n_routes; route_idx++) {
					route->routes_tokens[i]->routes[route_idx] = NULL;
					route->routes_tokens[i]->tokens[route_idx] = NULL;
				}
			}

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				unsigned int n_tokens = i + 1;
				unsigned int idx = 0;
				for (ListElement *le = dlist_start (route->children); le; le = le->next) {
					child = (HttpRoute *) le->data;

					if (child->n_tokens == n_tokens) {
						// printf ("%s\n", child->actual->str);
						route->routes_tokens[n_tokens - 1]->routes[idx] = child;
						route->routes_tokens[n_tokens - 1]->tokens[idx] = child->tokens;
						idx++;
					}
				}
			}
		}
	}

}

void http_route_child_add (HttpRoute *parent, HttpRoute *child) {

	if (parent && child) {
		dlist_insert_after (parent->children, dlist_end (parent->children), child);

		// refactor child paths
		str_delete (child->base);
		child->base = str_new (parent->route->str);

		str_delete (child->route);
		child->route = str_create ("%s/%s", child->base->str, child->actual->str);
	}

}

// enables authentication for the selected route
void http_route_set_auth (HttpRoute *route, HttpRouteAuthType auth_type) {

	if (route) route->auth_type = auth_type;

}

// sets the method to be used to decode incoming data from jwt & a method to delete it after use
// if no delete method is set, data won't be freed
void http_route_set_decode_data (HttpRoute *route, 
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)) {

	if (route) {
		route->decode_data = decode_data;
		route->delete_decoded_data = delete_decoded_data;
	}

}

#pragma endregion