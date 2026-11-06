#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/http/compatibility.h"
#include "cerver/http/middleware.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"
#include "cerver/http/tree.h"

static const char *http_compatibility_root_path = { "/" };

// the settings of a route are shared by all its methods
static void http_compatibility_settings (
	HttpEndpoint *endpoint, const HttpRoute *route
) {

	HttpRouteSettings *resolved = &endpoint->resolved;

	resolved->modifier = route->modifier;

	resolved->auth_type = route->auth_type;

	resolved->decode_data = route->decode_data;
	resolved->delete_decoded_data = route->delete_decoded_data;

	resolved->authentication_handler = route->authentication_handler;
	resolved->authentication_with_body = route->authentication_with_body;

	// the route owns its custom data
	resolved->custom_data = route->custom_data;
	resolved->delete_custom_data = NULL;

}

// creates the endpoint of a method of the route and adds it to the tree
// returns 1 if it was added
static size_t http_compatibility_translate_method (
	HttpTree *tree, HttpRoute *route, const String *path, const RequestMethod method
) {

	size_t added = 0;

	HttpEndpoint *endpoint = http_endpoint_create (path->str, method, route->handlers[method]);
	if (endpoint) {
		endpoint->route = route;

		// the stats keep living in the route, so the admin routes still read them
		endpoint->stats = route->stats[method];
		endpoint->file_stats = route->file_stats;
		endpoint->owns_stats = false;

		http_compatibility_settings (endpoint, route);
		(void) http_middlewares_append (&endpoint->resolved.middlewares, &tree->middlewares);

		if (!http_tree_insert (tree, endpoint, true)) {
			(void) dlist_insert_at_end_unsafe (tree->compatibility_endpoints, endpoint);
			added = 1;
		}

		else {
			http_endpoint_delete (endpoint);
		}
	}

	return added;

}

// translates a route and its children, the complete path is resolved from its parents,
// so a child that was added before its parent was attached gets its real path
static size_t http_compatibility_translate_route (
	HttpTree *tree, HttpRoute *route, const char *parent_path
) {

	size_t added = 0;

	char error[HTTP_TREE_ERROR_SIZE] = { 0 };
	if (http_tree_path_validate (route->actual->str, error, HTTP_TREE_ERROR_SIZE)) {
		http_tree_warning (
			"route \"%s\" under \"%s\" - %s - it is not registered with its children",
			route->actual->str, parent_path, error
		);

		return added;
	}

	String *path = http_tree_path_join (parent_path, route->actual->str);
	if (path) {
		str_delete (route->route);
		route->route = path;

		for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
			if (route->handlers[method]) {
				added += http_compatibility_translate_method (tree, route, path, (RequestMethod) method);
			}
		}

		for (ListElement *le = dlist_start (route->children); le; le = le->next) {
			added += http_compatibility_translate_route (tree, (HttpRoute *) le->data, path->str);
		}
	}

	return added;

}

// translates the routes of the first routes API (HttpRoute) to endpoints of the routes tree
// returns the number of endpoints that were added
size_t http_compatibility_translate (
	HttpTree *tree, const DoubleList *routes
) {

	size_t added = 0;

	if (tree && routes) {
		dlist_reset (tree->compatibility_endpoints);

		for (ListElement *le = dlist_start (routes); le; le = le->next) {
			added += http_compatibility_translate_route (
				tree, (HttpRoute *) le->data, http_compatibility_root_path
			);
		}
	}

	return added;

}
