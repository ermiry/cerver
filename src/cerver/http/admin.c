#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/http/http.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/route.h"

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "cerver/utils/log.h"

static void http_cerver_admin_handler_general_stats (
	const HttpCerver *http_cerver, json_t *json
) {

	(void) json_object_set_new (
		json, "main_routes", json_integer ((json_int_t) http_cerver->routes->size)
	);

	size_t total_handlers = 0;
	size_t children_routes = http_cerver_stats_get_children_routes (
		http_cerver, &total_handlers
	);

	(void) json_object_set_new (
		json, "children_routes", json_integer ((json_int_t) children_routes)
	);

	(void) json_object_set_new (
		json, "total_handlers", json_integer ((json_int_t) total_handlers)
	);

}

static json_t *http_cerver_admin_handler_single_route_handler_stats (
	const RequestMethod method, const HttpRouteStats *stats
) {

	json_t *handler_object = json_object ();

	(void) json_object_set_new (
		handler_object, "method", json_string (http_request_method_str (method))
	);

	(void) json_object_set_new (
		handler_object, "requests", json_integer ((json_int_t) stats->n_requests)
	);

	(void) json_object_set_new (
		handler_object, "min_process_time", json_real (stats->first ? 0 : stats->min_process_time)
	);

	(void) json_object_set_new (
		handler_object, "max_process_time", json_real (stats->max_process_time)
	);

	(void) json_object_set_new (
		handler_object, "mean_process_time", json_real (stats->mean_process_time)
	);

	(void) json_object_set_new (
		handler_object, "min_request_size", json_integer ((json_int_t) (stats->first ? 0 : stats->min_request_size))
	);

	(void) json_object_set_new (
		handler_object, "max_request_size", json_integer ((json_int_t) stats->max_request_size)
	);

	(void) json_object_set_new (
		handler_object, "mean_request_size", json_integer ((json_int_t) stats->mean_request_size)
	);

	(void) json_object_set_new (
		handler_object, "min_response_size", json_integer ((json_int_t) (stats->first ? 0 : stats->min_response_size))
	);

	(void) json_object_set_new (
		handler_object, "max_response_size", json_integer ((json_int_t) stats->max_response_size)
	);

	(void) json_object_set_new (
		handler_object, "mean_response_size", json_integer ((json_int_t) stats->mean_response_size)
	);

	return handler_object;

}

static json_t *http_cerver_admin_handler_single_route_stats (
	const HttpRoute *route
) {

	json_t *route_object = json_object ();

	(void) json_object_set_new (
		route_object, "route", json_string (route->route->str)
	);

	json_t *handlers_array = json_array ();

	json_t *handler_object = NULL;
	RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;
	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		if (route->handlers[i]) {
			method = (RequestMethod) i;
			handler_object = http_cerver_admin_handler_single_route_handler_stats (
				method, route->stats[i]
			);

			// TODO: multi part stats

			(void) json_array_append (handlers_array, handler_object);
		}
	}

	(void) json_object_set (
		route_object, "handlers", handlers_array
	);

	json_t *children_array = json_array ();

	// TODO:

	(void) json_object_set (
		route_object, "children", children_array
	);

	return route_object;

}

static void http_cerver_admin_handler_routes_stats (
	const HttpCerver *http_cerver, json_t *json
) {

	json_t *routes_array = json_array ();

	json_t *route_object = NULL;
	for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
		route_object = http_cerver_admin_handler_single_route_stats (
			(const HttpRoute *) le->data
		);
		
		(void) json_array_append (routes_array, route_object);
	}

	(void) json_object_set_new (
		json, "routes", routes_array
	);

}

static void http_cerver_admin_handler_requests_stats (
	const HttpCerver *http_cerver, json_t *json
) {

	(void) json_object_set_new (
		json, "n_incompleted_requests", json_integer ((json_int_t) http_cerver->n_incompleted_requests)
	);

	(void) json_object_set_new (
		json, "n_unhandled_requests", json_integer ((json_int_t) http_cerver->n_unhandled_requests)
	);

	(void) json_object_set_new (
		json, "n_catch_all_requests", json_integer ((json_int_t) http_cerver->n_catch_all_requests)
	);

	(void) json_object_set_new (
		json, "n_failed_auth_requests", json_integer ((json_int_t) http_cerver->n_failed_auth_requests)
	);

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// GET [top level]/cerver/stats
static void http_cerver_admin_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	json_t *json = json_object ();
	if (json) {
		http_cerver_admin_handler_general_stats (http_cerver, json);

		http_cerver_admin_handler_routes_stats (http_cerver, json);

		http_cerver_admin_handler_requests_stats (http_cerver, json);

		char *json_string = json_dumps (json, 0);
		if (json_string) {
			(void) http_response_render_json (
				http_receive, json_string, strlen (json_string)
			);
		}

		json_decref (json);
	}

}

#pragma GCC diagnostic pop

u8 http_cerver_admin_init (HttpRoute *top_level_route) {

	u8 retval = 1;

	if (top_level_route) {
		// GET [top level]/cerver/stats
		HttpRoute *admin_route = http_route_create (
			REQUEST_METHOD_GET, "cerver/stats", http_cerver_admin_handler
		);

		http_route_child_add (top_level_route, admin_route);

		retval = 0;
	}

	return retval;

}