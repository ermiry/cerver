#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/cerver.h"
#include "cerver/system.h"
#include "cerver/version.h"

#include "cerver/threads/worker.h"

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

static void http_cerver_admin_generate_info_json_internal (
	const Cerver *cerver, json_t *json
) {

	(void) json_object_set_new (
		json, "name", json_string (cerver->info->name)
	);

	(void) json_object_set_new (
		json, "alias", json_string (cerver->info->alias)
	);

	(void) json_object_set_new (
		json, "time_started", json_integer ((json_int_t) cerver->info->time_started)
	);

	cerver_update_uptime ((Cerver *) cerver);

	(void) json_object_set_new (
		json, "uptime", json_integer ((json_int_t) cerver->info->uptime)
	);

}

static json_t *http_cerver_admin_generate_version_json (void) {

	json_t *version_object = json_object ();

	(void) json_object_set_new (
		version_object, "name", json_string (CERVER_VERSION_NAME)
	);

	(void) json_object_set_new (
		version_object, "date", json_string (CERVER_VERSION_DATE)
	);

	(void) json_object_set_new (
		version_object, "time", json_string (CERVER_VERSION_TIME)
	);

	(void) json_object_set_new (
		version_object, "author", json_string (CERVER_VERSION_AUTHOR)
	);

	return version_object;

}

char *http_cerver_admin_generate_info_json (
	const HttpCerver *http_cerver
) {

	char *json_string = NULL;

	json_t *json = json_object ();
	if (json) {
		http_cerver_admin_generate_info_json_internal (
			http_cerver->cerver, json
		);

		(void) json_object_set_new (
			json,
			"version",
			http_cerver_admin_generate_version_json ()
		);

		json_string = json_dumps (json, 0);

		json_decref (json);
	}

	return json_string;

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

	if (file_system_object) {
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
	}

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

			if (file_system_object) {
				(void) json_array_append_new (file_systems_array, file_system_object);
			}
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

static json_t *worker_state_to_json (Worker *worker) {

	json_t *worker_object = json_object ();

	if (worker_object) {
		(void) json_object_set_new (
			worker_object, "id", json_integer ((json_int_t) worker->id)
		);

		(void) json_object_set_new (
			worker_object, "name", json_string (worker->name)
		);

		(void) json_object_set_new (
			worker_object, "state",
			json_string (
				worker_state_to_string (worker_get_state (worker))
			)
		);
	}

	return worker_object;

}

char *http_cerver_admin_generate_workers_json (
	const HttpCerver *http_cerver
) {

	char *json_string = NULL;

	json_t *json = json_object ();
	if (json) {
		json_t *workers_array = json_array ();

		json_t *worker_object = NULL;
		for (ListElement *le = dlist_start (http_cerver->admin_workers); le; le = le->next) {
			worker_object = worker_state_to_json ((Worker *) le->data);

			if (worker_object) {
				(void) json_array_append_new (workers_array, worker_object);
			}
		}

		(void) json_object_set_new (json, "workers", workers_array);

		json_string = json_dumps (json, 0);

		json_decref (json);
	}

	return json_string;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// sets "Access-Control-Allow-Origin" header
static inline void http_cerver_admin_response_set_allow_origin_header (
	const HttpReceive *http_receive, HttpResponse *response
) {

	if (http_receive->http_cerver->admin_origin.len) {
		(void) http_response_add_cors_header (
			response,
			http_receive->http_cerver->admin_origin.value
		);
	}

	else {
		(void) http_response_add_whitelist_cors_header_from_request (
			http_receive, response
		);
	}

}

// sets "Access-Control-Allow-Methods" header based on config
static inline void http_cerver_admin_response_set_allow_methods_header (
	const HttpReceive *http_receive, HttpResponse *response
) {

	if (http_receive->http_cerver->enable_admin_head_handlers) {
		(void) http_response_add_header (
			response,
			HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS,
			"GET, HEAD, OPTIONS"
		);
	}

	else {
		(void) http_response_add_header (
			response,
			HTTP_HEADER_ACCESS_CONTROL_ALLOW_METHODS,
			"GET, OPTIONS"
		);
	}

}

// sets "Access-Control-Allow-Credentials" header
static inline void http_cerver_admin_response_set_allow_credentials (
	const HttpReceive *http_receive, HttpResponse *response
) {

	if (http_receive->http_cerver->enable_admin_routes_auth) {
		(void) http_response_add_header (
			response,
			HTTP_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS,
			"true"
		);
	}

}

// set CORS related headers based on HTTP config
static inline void http_cerver_admin_response_set_cors_headers (
	const HttpReceive *http_receive, HttpResponse *response
) {

	if (http_receive->http_cerver->enable_admin_cors_headers) {
		// set "Access-Control-Allow-Origin" header
		http_cerver_admin_response_set_allow_origin_header (
			http_receive, response
		);

		// set "Access-Control-Allow-Methods" header based on config
		http_cerver_admin_response_set_allow_methods_header (
			http_receive, response
		);

		// set "Access-Control-Allow-Credentials" header
		http_cerver_admin_response_set_allow_credentials (
			http_receive, response
		);
	}

}

// sends back a matching response to an OPTIONS preflight request
// that is generally sent to fetch CORS information
static inline void http_cerver_admin_send_options (
	const HttpReceive *http_receive
) {

	HttpResponse *response = http_response_get ();
	if (response) {
		// sets response's status to be 204 No Content
		http_response_set_status (response, HTTP_STATUS_NO_CONTENT);

		// set CORS related headers based on HTTP config
		http_cerver_admin_response_set_cors_headers (
			http_receive, response
		);

		(void) http_response_compile (response);

		(void) http_response_send (response, http_receive);

		http_response_return (response);
	}

}

// sends back a response to a HEAD request
// that only includes the headers that would be returned
// if the HEAD request's URL was instead requested with GET
static inline void http_cerver_admin_send_head (
	const HttpReceive *http_receive,
	const size_t json_len
) {

	HttpResponse *response = http_response_get ();
	if (response) {
		http_response_set_status (response, HTTP_STATUS_OK);
		http_response_add_json_headers (response, json_len);

		// set CORS related headers based on HTTP config
		http_cerver_admin_response_set_cors_headers (
			http_receive, response
		);

		(void) http_response_compile (response);

		(void) http_response_send (response, http_receive);

		http_response_return (response);
	}

}

static inline void http_cerver_admin_send_response (
	const HttpReceive *http_receive,
	const HttpRequest *request,
	const char *json, const size_t json_len
) {

	HttpResponse *response = http_response_get ();
	if (response) {
		http_response_set_status (response, HTTP_STATUS_OK);
		http_response_add_json_headers (response, json_len);

		// set CORS related headers based on HTTP config
		http_cerver_admin_response_set_cors_headers (
			http_receive, response
		);

		(void) http_response_set_data_ref (
			response, (void *) json, json_len
		);

		(void) http_response_compile (response);

		(void) http_response_send (response, http_receive);

		http_response_return (response);
	}

}

// OPTIONS [top level]/cerver/info
// OPTIONS [top level]/cerver/stats
// OPTIONS [top level]/cerver/stats/filesystems
// OPTIONS [top level]/cerver/stats/workers
static void http_cerver_admin_options_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_options (http_receive);

}

// HEAD [top level]/cerver/info
static void http_cerver_admin_info_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *info_json = http_cerver_admin_generate_info_json (
		http_cerver
	);

	if (info_json) {
		http_cerver_admin_send_head (
			http_receive, strlen (info_json)
		);

		free (info_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// GET [top level]/cerver/info
static void http_cerver_admin_info_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *info_json = http_cerver_admin_generate_info_json (
		http_cerver
	);

	if (info_json) {
		http_cerver_admin_send_response (
			http_receive, request,
			info_json, strlen (info_json)
		);

		free (info_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// HEAD [top level]/cerver/stats
static void http_cerver_admin_stats_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *routes_json = http_cerver_admin_generate_routes_stats_json (
		http_cerver
	);

	if (routes_json) {
		http_cerver_admin_send_head (
			http_receive, strlen (routes_json)
		);

		free (routes_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// GET [top level]/cerver/stats
static void http_cerver_admin_stats_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *routes_json = http_cerver_admin_generate_routes_stats_json (
		http_cerver
	);

	if (routes_json) {
		http_cerver_admin_send_response (
			http_receive, request,
			routes_json, strlen (routes_json)
		);

		free (routes_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// HEAD [top level]/cerver/stats/filesystems
static void http_cerver_admin_file_systems_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *file_systems_json = http_cerver_admin_generate_file_systems_stats_json (
		http_cerver
	);

	if (file_systems_json) {
		http_cerver_admin_send_head (
			http_receive, strlen (file_systems_json)
		);

		free (file_systems_json);
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
		http_cerver_admin_send_response (
			http_receive, request,
			file_systems_json, strlen (file_systems_json)
		);

		free (file_systems_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// HEAD [top level]/cerver/stats/workers
static void http_cerver_admin_workers_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *workers_json = http_cerver_admin_generate_workers_json (
		http_cerver
	);

	if (workers_json) {
		http_cerver_admin_send_head (
			http_receive, strlen (workers_json)
		);

		free (workers_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// GET [top level]/cerver/stats/workers
static void http_cerver_admin_workers_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	char *workers_json = http_cerver_admin_generate_workers_json (
		http_cerver
	);

	if (workers_json) {
		http_cerver_admin_send_response (
			http_receive, request,
			workers_json, strlen (workers_json)
		);

		free (workers_json);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

#pragma GCC diagnostic pop

static void http_cerver_admin_route_set_auth (
	const HttpCerver *http_cerver,
	HttpRoute *admin_route
) {

	if (http_cerver->enable_admin_routes_auth) {
		switch (http_cerver->admin_auth_type) {
			case HTTP_ROUTE_AUTH_TYPE_NONE: break;

			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				http_route_set_auth (admin_route, HTTP_ROUTE_AUTH_TYPE_BEARER);
				http_route_set_decode_data (
					admin_route,
					http_cerver->admin_decode_data,
					http_cerver->admin_delete_decoded_data
				);
			} break;

			case HTTP_ROUTE_AUTH_TYPE_CUSTOM: {
				http_route_set_auth (admin_route, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
				http_route_set_authentication_handler (
					admin_route, http_cerver->admin_auth_handler
				);
			} break;

			default: break;
		}
	}

}

static void http_cerver_admin_set_info_route (
	const HttpCerver *http_cerver, HttpRoute *top_level_route
) {

	// GET [top level]/cerver/info
	HttpRoute *info_route = http_route_create (
		REQUEST_METHOD_GET,  "cerver/info", http_cerver_admin_info_handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_route_set_handler (
			info_route,
			REQUEST_METHOD_HEAD,
			http_cerver_admin_info_head_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_route_set_handler (
			info_route,
			REQUEST_METHOD_OPTIONS,
			http_cerver_admin_options_handler
		);
	}

	http_cerver_admin_route_set_auth (http_cerver, info_route);

	http_route_set_custom_data (info_route, http_cerver->admin_routes_custom_data);

	http_route_child_add (top_level_route, info_route);

}

static void http_cerver_admin_set_stats_route (
	const HttpCerver *http_cerver, HttpRoute *top_level_route
) {

	// GET [top level]/cerver/stats
	HttpRoute *stats_route = http_route_create (
		REQUEST_METHOD_GET, "cerver/stats", http_cerver_admin_stats_handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_route_set_handler (
			stats_route,
			REQUEST_METHOD_HEAD,
			http_cerver_admin_stats_head_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_route_set_handler (
			stats_route,
			REQUEST_METHOD_OPTIONS,
			http_cerver_admin_options_handler
		);
	}

	http_cerver_admin_route_set_auth (http_cerver, stats_route);

	http_route_set_custom_data (stats_route, http_cerver->admin_routes_custom_data);

	http_route_child_add (top_level_route, stats_route);

}

static void http_cerver_admin_set_file_systems_route (
	const HttpCerver *http_cerver, HttpRoute *top_level_route
) {

	// GET [top level]/cerver/stats/filesystems
	HttpRoute *file_systems_route = http_route_create (
		REQUEST_METHOD_GET,
		"cerver/stats/filesystems",
		http_cerver_admin_file_systems_handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_route_set_handler (
			file_systems_route,
			REQUEST_METHOD_HEAD,
			http_cerver_admin_file_systems_head_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_route_set_handler (
			file_systems_route,
			REQUEST_METHOD_OPTIONS,
			http_cerver_admin_options_handler
		);
	}

	http_cerver_admin_route_set_auth (http_cerver, file_systems_route);

	http_route_set_custom_data (file_systems_route, http_cerver->admin_routes_custom_data);

	http_route_child_add (top_level_route, file_systems_route);

}

static void http_cerver_admin_set_workers_route (
	const HttpCerver *http_cerver, HttpRoute *top_level_route
) {

	// GET [top level]/cerver/stats/workers
	HttpRoute *workers_route = http_route_create (
		REQUEST_METHOD_GET,
		"cerver/stats/workers",
		http_cerver_admin_workers_handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_route_set_handler (
			workers_route,
			REQUEST_METHOD_HEAD,
			http_cerver_admin_workers_head_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_route_set_handler (
			workers_route,
			REQUEST_METHOD_OPTIONS,
			http_cerver_admin_options_handler
		);
	}

	http_cerver_admin_route_set_auth (http_cerver, workers_route);

	http_route_set_custom_data (workers_route, http_cerver->admin_routes_custom_data);

	http_route_child_add (top_level_route, workers_route);

}

u8 http_cerver_admin_init (
	const HttpCerver *http_cerver,
	HttpRoute *top_level_route
) {

	u8 retval = 1;

	if (top_level_route) {
		// GET [top level]/cerver/info
		if (http_cerver->enable_admin_info_route) {
			http_cerver_admin_set_info_route (
				http_cerver, top_level_route
			);
		}

		// GET [top level]/cerver/stats
		http_cerver_admin_set_stats_route (
			http_cerver, top_level_route
		);

		// GET [top level]/cerver/stats/filesystems
		http_cerver_admin_set_file_systems_route (
			http_cerver, top_level_route
		);

		// GET [top level]/cerver/stats/workers
		http_cerver_admin_set_workers_route (
			http_cerver, top_level_route
		);

		retval = 0;
	}

	return retval;

}
