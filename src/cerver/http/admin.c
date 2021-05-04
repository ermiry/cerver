#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/system.h"

#include "cerver/http/admin.h"
#include "cerver/http/http.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/route.h"

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "cerver/utils/log.h"

HttpAdminFileSystemStats *http_admin_file_system_stats_new (void) {

	HttpAdminFileSystemStats *stats = (HttpAdminFileSystemStats *) malloc (sizeof (HttpAdminFileSystemStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (HttpAdminFileSystemStats));
	}

	return stats;

}

void http_admin_file_system_stats_delete (void *stats_ptr) {

	if (stats_ptr) {
		free (stats_ptr);
	}

}

HttpAdminFileSystemStats *http_admin_file_system_stats_create (
	const char *path
) {

	HttpAdminFileSystemStats *stats = http_admin_file_system_stats_new ();
	if (stats) {
		(void) strncpy (stats->path, path, HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE - 1);
		stats->path_len = strlen (stats->path);
	}

	return stats;

}

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

static json_t *http_cerver_admin_handler_single_route_handler_files_stats (
	const HttpRouteFileStats *file_stats
) {

	json_t *files_object = json_object ();

	(void) json_object_set_new (
		files_object, "n_uploaded_files", json_integer ((json_int_t) file_stats->n_uploaded_files)
	);

	(void) json_object_set_new (
		files_object, "min_n_files", json_integer ((json_int_t) file_stats->first ? 0 : file_stats->min_n_files)
	);

	(void) json_object_set_new (
		files_object, "max_n_files", json_integer ((json_int_t) file_stats->max_n_files)
	);

	(void) json_object_set_new (
		files_object, "mean_n_files", json_real (file_stats->mean_n_files)
	);

	(void) json_object_set_new (
		files_object, "min_file_size", json_integer ((json_int_t) file_stats->first ? 0 : file_stats->min_file_size)
	);

	(void) json_object_set_new (
		files_object, "max_file_size", json_integer ((json_int_t) file_stats->max_file_size)
	);

	(void) json_object_set_new (
		files_object, "mean_file_size", json_real (file_stats->mean_file_size)
	);

	return files_object;

}

static json_t *http_cerver_admin_handler_single_route_handlers_stats (
	const HttpRoute *route
) {

	json_t *handlers_array = json_array ();

	json_t *handler_object = NULL;
	RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;
	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		if (route->handlers[i]) {
			method = (RequestMethod) i;
			handler_object = http_cerver_admin_handler_single_route_handler_stats (
				method, route->stats[i]
			);

			if (
				(route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
				&& (method == REQUEST_METHOD_POST)
			) {
				json_t *files_object = http_cerver_admin_handler_single_route_handler_files_stats (
					route->file_stats
				);

				(void) json_object_set_new (handler_object, "files", files_object);
			}

			(void) json_array_append_new (handlers_array, handler_object);
		}
	}

	return handlers_array;

}

static json_t *http_cerver_admin_handler_single_child_stats (
	const HttpRoute *child
) {

	json_t *child_object = json_object ();

	(void) json_object_set_new (
		child_object, "route", json_string (child->route->str)
	);

	if (child->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART) {
		(void) json_object_set_new (
			child_object, "modifier",
			json_string (http_route_modifier_to_string (child->modifier))
		);
	}

	json_t *handlers_array = http_cerver_admin_handler_single_route_handlers_stats (child);
	(void) json_object_set_new (
		child_object, "handlers", handlers_array
	);

	return child_object;

}

static json_t *http_cerver_admin_handler_single_route_children_stats (
	const HttpRoute *route
) {

	json_t *children_array = json_array ();

	json_t *child_object = NULL;
	for (ListElement *le = dlist_start (route->children); le; le = le->next) {
		child_object = http_cerver_admin_handler_single_child_stats ((HttpRoute *) le->data);

		(void) json_array_append_new (children_array, child_object);
	}

	return children_array;

}

static json_t *http_cerver_admin_handler_single_route_stats (
	const HttpRoute *route
) {

	json_t *route_object = json_object ();

	(void) json_object_set_new (
		route_object, "route", json_string (route->route->str)
	);

	if (route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART) {
		(void) json_object_set_new (
			route_object, "modifier",
			json_string (http_route_modifier_to_string (route->modifier))
		);
	}

	json_t *handlers_array = http_cerver_admin_handler_single_route_handlers_stats (route);
	(void) json_object_set_new (
		route_object, "handlers", handlers_array
	);

	json_t *children_array = http_cerver_admin_handler_single_route_children_stats (route);
	(void) json_object_set_new (
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
		
		(void) json_array_append_new (routes_array, route_object);
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

char *http_cerver_admin_generate_routes_stats_json (
	const HttpCerver *http_cerver
) {

	char *json_string = NULL;

	json_t *json = json_object ();
	if (json) {
		http_cerver_admin_handler_general_stats (http_cerver, json);

		http_cerver_admin_handler_routes_stats (http_cerver, json);

		http_cerver_admin_handler_requests_stats (http_cerver, json);

		json_string = json_dumps (json, 0);

		json_decref (json);
	}

	return json_string;

}

static json_t *http_cerver_admin_handler_single_file_systems_stats (
	const HttpAdminFileSystemStats *admin_file_system
) {

	(void) system_get_stats (
		admin_file_system->path, (FileSystemStats *) &admin_file_system->stats
	);

	#ifdef HTTP_ADMIN_DEBUG
	system_print_stats (&admin_file_system->stats);
	#endif

	json_t *file_system_object = json_object ();

	(void) json_object_set_new (
		file_system_object, "path", json_string (admin_file_system->path)
	);

	(void) json_object_set_new (
		file_system_object, "total", json_real (admin_file_system->stats.total)
	);

	(void) json_object_set_new (
		file_system_object, "available", json_real (admin_file_system->stats.available)
	);

	(void) json_object_set_new (
		file_system_object, "used", json_real (admin_file_system->stats.used)
	);

	(void) json_object_set_new (
		file_system_object, "used_percentage", json_real (admin_file_system->stats.used_percentage)
	);

	return file_system_object;

}

char *http_cerver_admin_generate_file_systems_stats_json (
	const HttpCerver *http_cerver
) {

	char *json_string = NULL;

	(void) pthread_mutex_lock ((pthread_mutex_t *) http_cerver->admin_mutex);

	json_t *json = json_object ();
	if (json) {
		json_t *file_systems_array = json_array ();

		json_t *file_system_object = NULL;
		for (ListElement *le = dlist_start (http_cerver->admin_file_systems_stats); le; le = le->next) {
			file_system_object = http_cerver_admin_handler_single_file_systems_stats (
				(const HttpAdminFileSystemStats *) le->data
			);
			
			(void) json_array_append_new (file_systems_array, file_system_object);
		}

		(void) json_object_set_new (
			json, "filesystems", file_systems_array
		);

		json_string = json_dumps (json, 0);

		json_decref (json);
	}

	(void) pthread_mutex_unlock ((pthread_mutex_t *) http_cerver->admin_mutex);

	return json_string;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// GET [top level]/cerver/stats
static void http_cerver_admin_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *routes_json = http_cerver_admin_generate_routes_stats_json (
		http_cerver
	);

	if (routes_json) {
		(void) http_response_render_json (
			http_receive, HTTP_STATUS_OK,
			routes_json, strlen (routes_json)
		);

		free (routes_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// GET [top level]/cerver/stats/filesystems
static void http_cerver_admin_file_systems_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *file_systems_json = http_cerver_admin_generate_file_systems_stats_json (
		http_cerver
	);

	if (file_systems_json) {
		(void) http_response_render_json (
			http_receive, HTTP_STATUS_OK,
			file_systems_json, strlen (file_systems_json)
		);

		free (file_systems_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
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

		// GET [top level]/cerver/stats/filesystems
		HttpRoute *file_systems_route = http_route_create (
			REQUEST_METHOD_GET, "cerver/stats/filesystems", http_cerver_admin_file_systems_handler
		);

		http_route_child_add (top_level_route, file_systems_route);

		retval = 0;
	}

	return retval;

}