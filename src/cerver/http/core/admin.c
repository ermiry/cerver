#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/version.h"

#include "cerver/core/cerver.h"
#include "cerver/events/internal.h"
#include "cerver/system/system.h"

#include "cerver/http/core/admin.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/metrics.h"
#include "cerver/http/core/pool.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/core/stats.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/websockets/registry.h"

#include "cerver/json/json.h"
#include "cerver/json/writer.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/worker.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the decimal status code, the key of the responses by status
#define HTTP_ADMIN_STATUS_CODE_SIZE				8

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
		(void) c_string_copy_bounded (stats->path, path, HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE);
		stats->path_len = strlen (stats->path);
	}

	return stats;

}

#pragma region json

static void http_cerver_admin_write_version (CerverJsonWriter *writer) {

	cerver_json_writer_key (writer, "version");
	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "name", CERVER_VERSION_NAME);
	http_json_member_string (writer, "date", CERVER_VERSION_DATE);
	http_json_member_string (writer, "time", CERVER_VERSION_TIME);
	http_json_member_string (writer, "author", CERVER_VERSION_AUTHOR);

	cerver_json_writer_object_end (writer);

}

// the info of the cerver: its name, its alias, when it started, its uptime and its version
static void http_cerver_admin_write_info (
	CerverJsonWriter *writer, const void *http_cerver_ptr
) {

	const Cerver *cerver = ((const HttpCerver *) http_cerver_ptr)->cerver;

	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "name", cerver->info->name);
	http_json_member_string (writer, "alias", cerver->info->alias);
	http_json_member_integer (writer, "time_started", (i64) cerver->info->time_started);

	cerver_update_uptime ((Cerver *) cerver);

	http_json_member_integer (writer, "uptime", (i64) cerver->info->uptime);

	http_cerver_admin_write_version (writer);

	cerver_json_writer_object_end (writer);

}

char *http_cerver_admin_generate_info_json (
	const HttpCerver *http_cerver
) {

	return http_json_generate (http_cerver_admin_write_info, http_cerver);

}

static void http_cerver_admin_handler_general_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	http_json_member_integer (writer, "main_routes", (i64) http_cerver->routes->size);

	size_t total_handlers = 0;
	size_t children_routes = http_cerver_stats_get_children_routes (
		http_cerver, &total_handlers
	);

	http_json_member_integer (writer, "children_routes", (i64) children_routes);
	http_json_member_integer (writer, "total_handlers", (i64) total_handlers);

}

// the stats of a handler, the members of the object of its route or of its endpoint
static void http_cerver_admin_handler_single_route_handler_stats (
	CerverJsonWriter *writer,
	const RequestMethod method, const HttpRouteStats *route_stats
) {

	// the stats are updated with atomic operations while the requests are handled
	HttpRouteStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteStats));
	http_route_stats_snapshot (route_stats, &snapshot);
	const HttpRouteStats *stats = &snapshot;

	http_json_member_string (writer, "method", http_request_method_str (method));
	http_json_member_integer (writer, "requests", (i64) stats->n_requests);

	http_json_member_real (writer, "min_process_time", stats->first ? 0 : stats->min_process_time);
	http_json_member_real (writer, "max_process_time", stats->max_process_time);
	http_json_member_real (writer, "mean_process_time", stats->mean_process_time);

	http_json_member_integer (writer, "min_request_size", (i64) (stats->first ? 0 : stats->min_request_size));
	http_json_member_integer (writer, "max_request_size", (i64) stats->max_request_size);
	http_json_member_integer (writer, "mean_request_size", (i64) stats->mean_request_size);

	http_json_member_integer (writer, "min_response_size", (i64) (stats->first ? 0 : stats->min_response_size));
	http_json_member_integer (writer, "max_response_size", (i64) stats->max_response_size);
	http_json_member_integer (writer, "mean_response_size", (i64) stats->mean_response_size);

}

static void http_cerver_admin_handler_single_route_handler_files_stats (
	CerverJsonWriter *writer, const HttpRouteFileStats *route_file_stats
) {

	HttpRouteFileStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteFileStats));
	http_route_file_stats_snapshot (route_file_stats, &snapshot);
	const HttpRouteFileStats *file_stats = &snapshot;

	cerver_json_writer_key (writer, "files");
	cerver_json_writer_object_begin (writer);

	http_json_member_integer (writer, "n_uploaded_files", (i64) file_stats->n_uploaded_files);
	http_json_member_integer (writer, "min_n_files", (i64) file_stats->min_n_files);
	http_json_member_integer (writer, "max_n_files", (i64) file_stats->max_n_files);
	http_json_member_real (writer, "mean_n_files", file_stats->mean_n_files);

	http_json_member_integer (writer, "min_file_size", (i64) file_stats->min_file_size);
	http_json_member_integer (writer, "max_file_size", (i64) file_stats->max_file_size);
	http_json_member_real (writer, "mean_file_size", file_stats->mean_file_size);

	cerver_json_writer_object_end (writer);

}

// a handler of a route with its stats, and the ones of its files when it takes uploads
static void http_cerver_admin_handler_single_route_handler (
	CerverJsonWriter *writer,
	const HttpRoute *route, const RequestMethod method
) {

	cerver_json_writer_object_begin (writer);

	http_cerver_admin_handler_single_route_handler_stats (
		writer, method, route->stats[method]
	);

	if (
		(route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
		&& (method == REQUEST_METHOD_POST)
	) {
		http_cerver_admin_handler_single_route_handler_files_stats (writer, route->file_stats);
	}

	cerver_json_writer_object_end (writer);

}

static void http_cerver_admin_handler_single_route_handlers_stats (
	CerverJsonWriter *writer, const HttpRoute *route
) {

	cerver_json_writer_key (writer, "handlers");
	cerver_json_writer_array_begin (writer);

	for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		if (route->handlers[i] && route->stats[i]) {
			http_cerver_admin_handler_single_route_handler (writer, route, (RequestMethod) i);
		}
	}

	cerver_json_writer_array_end (writer);

}

static void http_cerver_admin_handler_single_child_stats (
	CerverJsonWriter *writer, const HttpRoute *child
) {

	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "route", child->route->str);

	if (child->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART) {
		http_json_member_string (writer, "modifier", http_route_modifier_to_string (child->modifier));
	}

	http_cerver_admin_handler_single_route_handlers_stats (writer, child);

	cerver_json_writer_object_end (writer);

}

static void http_cerver_admin_handler_single_route_children_stats (
	CerverJsonWriter *writer, const HttpRoute *route
) {

	cerver_json_writer_key (writer, "children");
	cerver_json_writer_array_begin (writer);

	for (ListElement *le = dlist_start (route->children); le; le = le->next) {
		http_cerver_admin_handler_single_child_stats (writer, (const HttpRoute *) le->data);
	}

	cerver_json_writer_array_end (writer);

}

static void http_cerver_admin_handler_single_route_stats (
	CerverJsonWriter *writer, const HttpRoute *route
) {

	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "route", route->route->str);

	if (route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART) {
		http_json_member_string (writer, "modifier", http_route_modifier_to_string (route->modifier));
	}

	http_cerver_admin_handler_single_route_handlers_stats (writer, route);
	http_cerver_admin_handler_single_route_children_stats (writer, route);

	cerver_json_writer_object_end (writer);

}

static void http_cerver_admin_handler_routes_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	cerver_json_writer_key (writer, "routes");
	cerver_json_writer_array_begin (writer);

	for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
		http_cerver_admin_handler_single_route_stats (writer, (const HttpRoute *) le->data);
	}

	cerver_json_writer_array_end (writer);

}

static void http_cerver_admin_handler_endpoint_stats (
	CerverJsonWriter *writer, const HttpEndpoint *endpoint
) {

	cerver_json_writer_object_begin (writer);

	http_cerver_admin_handler_single_route_handler_stats (
		writer, endpoint->method, endpoint->stats
	);

	http_json_member_string (writer, "route", endpoint->path->str);

	if (endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_MULTI_PART) {
		http_cerver_admin_handler_single_route_handler_files_stats (writer, endpoint->file_stats);
	}

	cerver_json_writer_object_end (writer);

}

// the endpoints of a group and of its subgroups
static void http_cerver_admin_handler_group_stats (
	CerverJsonWriter *writer, const HttpGroup *group
) {

	for (ListElement *le = dlist_start (group->endpoints); le; le = le->next) {
		http_cerver_admin_handler_endpoint_stats (writer, (const HttpEndpoint *) le->data);
	}

	for (ListElement *le = dlist_start (group->groups); le; le = le->next) {
		http_cerver_admin_handler_group_stats (writer, (const HttpGroup *) le->data);
	}

}

// the endpoints that were registered with groups
static void http_cerver_admin_handler_endpoints_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	cerver_json_writer_key (writer, "endpoints");
	cerver_json_writer_array_begin (writer);

	for (ListElement *le = dlist_start (http_cerver->tree->groups); le; le = le->next) {
		http_cerver_admin_handler_group_stats (writer, (const HttpGroup *) le->data);
	}

	cerver_json_writer_array_end (writer);

}

static void http_cerver_admin_handler_requests_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	http_json_member_integer (
		writer, "n_incompleted_requests", (i64) CERVER_STATS_GET (http_cerver->n_incompleted_requests)
	);

	http_json_member_integer (
		writer, "n_unhandled_requests", (i64) CERVER_STATS_GET (http_cerver->n_unhandled_requests)
	);

	http_json_member_integer (
		writer, "n_catch_all_requests", (i64) CERVER_STATS_GET (http_cerver->n_catch_all_requests)
	);

	http_json_member_integer (
		writer, "n_failed_auth_requests", (i64) CERVER_STATS_GET (http_cerver->n_failed_auth_requests)
	);

	http_json_member_integer (
		writer, "n_limited_requests", (i64) CERVER_STATS_GET (http_cerver->n_limited_requests)
	);

	http_json_member_integer (
		writer, "n_timeout_requests", (i64) CERVER_STATS_GET (http_cerver->n_timeout_requests)
	);

	http_json_member_integer (
		writer, "n_unanswered_requests", (i64) CERVER_STATS_GET (http_cerver->n_unanswered_requests)
	);

	http_json_member_integer (
		writer, "n_failed_responses", (i64) CERVER_STATS_GET (http_cerver->n_failed_responses)
	);

}

// the responses sent by their status code, only the codes that were sent
static void http_cerver_admin_handler_responses_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	cerver_json_writer_key (writer, "n_responses_by_status");
	cerver_json_writer_object_begin (writer);

	char code[HTTP_ADMIN_STATUS_CODE_SIZE] = { 0 };
	for (size_t status = 0; status < HTTP_CERVER_RESPONSES_STATUS_SIZE; status++) {
		const size_t count = CERVER_STATS_GET (http_cerver->n_responses_by_status[status]);
		if (count) {
			(void) snprintf (code, HTTP_ADMIN_STATUS_CODE_SIZE, "%zu", status);
			http_json_member_integer (writer, code, (i64) count);
		}
	}

	cerver_json_writer_object_end (writer);

}

// the requests whose client left in a stage, by the reason
static void http_cerver_admin_handler_aborted_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver, const unsigned int stage
) {

	cerver_json_writer_key (writer, http_stage_label ((HttpStage) stage));
	cerver_json_writer_object_begin (writer);

	for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
		http_json_member_integer (
			writer, http_abort_reason_label ((HttpAbortReason) reason),
			(i64) CERVER_STATS_GET (http_cerver->n_aborted_requests[stage][reason])
		);
	}

	cerver_json_writer_object_end (writer);

}

// the requests whose client left, by stage and reason, and the ones that vanished, by stage
static void http_cerver_admin_handler_gone_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	cerver_json_writer_key (writer, "n_aborted_requests");
	cerver_json_writer_object_begin (writer);

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		http_cerver_admin_handler_aborted_stats (writer, http_cerver, stage);
	}

	cerver_json_writer_object_end (writer);

	cerver_json_writer_key (writer, "n_vanished_requests");
	cerver_json_writer_object_begin (writer);

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		http_json_member_integer (
			writer, http_stage_label ((HttpStage) stage),
			(i64) CERVER_STATS_GET (http_cerver->n_vanished_requests[stage])
		);
	}

	cerver_json_writer_object_end (writer);

}

// the WebSocket connections that are open, what they did and how they ended
static void http_cerver_admin_handler_web_socket_values (
	CerverJsonWriter *writer, const HttpWebSocketStats *stats
) {

	cerver_json_writer_key (writer, "websockets");
	cerver_json_writer_object_begin (writer);

	http_json_member_integer (writer, "connections", (i64) stats->n_connections);
	http_json_member_integer (writer, "n_opened", (i64) stats->n_opened);
	http_json_member_integer (writer, "n_closed", (i64) stats->n_closed);
	http_json_member_integer (writer, "n_messages_received", (i64) stats->n_messages_received);
	http_json_member_integer (writer, "n_messages_sent", (i64) stats->n_messages_sent);
	http_json_member_integer (writer, "n_bytes_received", (i64) stats->n_bytes_received);
	http_json_member_integer (writer, "n_bytes_sent", (i64) stats->n_bytes_sent);
	http_json_member_integer (writer, "n_failed_sends", (i64) stats->n_slow);
	http_json_member_integer (writer, "n_errors", (i64) stats->n_errors);

	cerver_json_writer_key (writer, "n_rejected");
	cerver_json_writer_object_begin (writer);

	for (unsigned int rejection = 0; rejection < HTTP_WEB_SOCKET_REJECTIONS; rejection++) {
		http_json_member_integer (
			writer, http_web_socket_rejection_to_string ((HttpWebSocketRejection) rejection),
			(i64) stats->n_rejected[rejection]
		);
	}

	cerver_json_writer_object_end (writer);

	cerver_json_writer_object_end (writer);

}

// a cerver without a registry of WebSocket connections has no stats of them
static void http_cerver_admin_handler_web_socket_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	HttpWebSocketStats stats;
	if (!http_cerver_web_socket_get_stats (http_cerver, &stats)) {
		http_cerver_admin_handler_web_socket_values (writer, &stats);
	}

}

// a pool that has threads: what it has now and what it did
static void http_cerver_admin_handler_pool_stats (
	CerverJsonWriter *writer, const HttpPool *pool
) {

	ThpoolStats stats;
	thpool_get_stats (pool->thpool, &stats);

	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "name", pool->name);

	http_json_member_integer (writer, "threads", (i64) thpool_get_num_threads_alive (pool->thpool));
	http_json_member_integer (writer, "working", (i64) thpool_get_num_threads_working (pool->thpool));
	http_json_member_integer (writer, "queued", (i64) thpool_get_queued (pool->thpool));
	http_json_member_integer (
		writer, "parked", (i64) event_loop_group_lane_parked (pool->http_cerver->cerver, pool->index)
	);

	http_json_member_integer (writer, "n_jobs", (i64) stats.total_jobs);
	http_json_member_integer (writer, "n_parked", (i64) __atomic_load_n (&pool->n_parked, __ATOMIC_RELAXED));
	http_json_member_integer (
		writer, "n_unavailable", (i64) __atomic_load_n (&pool->n_unavailable, __ATOMIC_RELAXED)
	);
	http_json_member_integer (
		writer, "parked_microseconds",
		(i64) __atomic_load_n (&pool->parked_total_microseconds, __ATOMIC_RELAXED)
	);

	cerver_json_writer_object_end (writer);

}

// the pools of the cerver that have threads, the first one is the thpool of the cerver;
// a cerver that has not started has none
static void http_cerver_admin_handler_pools_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	cerver_json_writer_key (writer, "pools");
	cerver_json_writer_array_begin (writer);

	for (unsigned int idx = 0; idx < http_cerver->n_pools; idx++) {
		const HttpPool *pool = http_cerver->pools[idx];

		if (pool->thpool) http_cerver_admin_handler_pool_stats (writer, pool);
	}

	cerver_json_writer_array_end (writer);

}

// the connections of the event loops: the ones that are open, the ones that wait for their next request
// and every one that was accepted
static void http_cerver_admin_handler_loops_stats (
	CerverJsonWriter *writer, const Cerver *cerver
) {

	http_json_member_integer (writer, "open", (i64) event_loop_group_connections (cerver));
	http_json_member_integer (writer, "idle", (i64) event_loop_group_idle (cerver));
	http_json_member_integer (writer, "n_accepted", (i64) event_loop_group_accepted (cerver));

}

// the connections that were reused, and the ones that ended with their requests
static void http_cerver_admin_handler_connections_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	size_t ended = 0;
	for (unsigned int i = 0; i < HTTP_CERVER_CONNECTION_REQUESTS_BUCKETS; i++) {
		ended += CERVER_STATS_GET (http_cerver->connection_requests[i]);
	}

	cerver_json_writer_key (writer, "connections");
	cerver_json_writer_object_begin (writer);

	http_json_member_integer (writer, "n_reused", (i64) CERVER_STATS_GET (http_cerver->n_reused_connections));
	http_json_member_integer (writer, "n_followed", (i64) CERVER_STATS_GET (http_cerver->n_followed_requests));
	http_json_member_integer (writer, "n_pool_read", (i64) CERVER_STATS_GET (http_cerver->n_pool_read_requests));
	http_json_member_integer (writer, "n_ended", (i64) ended);
	http_json_member_integer (writer, "n_requests", (i64) CERVER_STATS_GET (http_cerver->n_connection_requests));

	if (http_cerver->cerver && http_cerver->cerver->event_loops) {
		http_cerver_admin_handler_loops_stats (writer, http_cerver->cerver);
	}

	cerver_json_writer_object_end (writer);

}

// the requests of the health and the ready routes, and what the uploads wrote
static void http_cerver_admin_handler_health_stats (
	CerverJsonWriter *writer, const HttpCerver *http_cerver
) {

	http_json_member_integer (writer, "n_health_requests", (i64) CERVER_STATS_GET (http_cerver->n_health_requests));
	http_json_member_integer (writer, "n_ready_requests", (i64) CERVER_STATS_GET (http_cerver->n_ready_requests));
	http_json_member_integer (
		writer, "n_not_ready_requests", (i64) CERVER_STATS_GET (http_cerver->n_not_ready_requests)
	);

	http_json_member_integer (
		writer, "uploads_write_microseconds", (i64) CERVER_STATS_GET (http_cerver->uploads_write_microseconds)
	);

	http_json_member_integer (
		writer, "uploads_write_bytes", (i64) CERVER_STATS_GET (http_cerver->uploads_write_bytes)
	);

}

// the stats of the routes of the first API, of the endpoints of the groups and of the cerver
static void http_cerver_admin_write_routes_stats (
	CerverJsonWriter *writer, const void *http_cerver_ptr
) {

	const HttpCerver *http_cerver = (const HttpCerver *) http_cerver_ptr;

	cerver_json_writer_object_begin (writer);

	http_cerver_admin_handler_general_stats (writer, http_cerver);

	http_cerver_admin_handler_routes_stats (writer, http_cerver);

	http_cerver_admin_handler_endpoints_stats (writer, http_cerver);

	http_cerver_admin_handler_requests_stats (writer, http_cerver);

	http_cerver_admin_handler_responses_stats (writer, http_cerver);

	http_cerver_admin_handler_gone_stats (writer, http_cerver);

	http_cerver_admin_handler_web_socket_stats (writer, http_cerver);

	http_cerver_admin_handler_pools_stats (writer, http_cerver);

	http_cerver_admin_handler_connections_stats (writer, http_cerver);

	http_cerver_admin_handler_health_stats (writer, http_cerver);

	cerver_json_writer_object_end (writer);

}

char *http_cerver_admin_generate_routes_stats_json (
	const HttpCerver *http_cerver
) {

	return http_json_generate (http_cerver_admin_write_routes_stats, http_cerver);

}

static void http_cerver_admin_handler_single_file_systems_stats (
	CerverJsonWriter *writer, const HttpAdminFileSystemStats *admin_file_system
) {

	(void) system_get_stats (
		admin_file_system->path, (FileSystemStats *) &admin_file_system->stats
	);

	#ifdef HTTP_ADMIN_DEBUG
	system_print_stats (&admin_file_system->stats);
	#endif

	cerver_json_writer_object_begin (writer);

	http_json_member_string (writer, "path", admin_file_system->path);

	// the percentage of a file system without blocks is not a number, and it is left out
	http_json_member_real (writer, "total", admin_file_system->stats.total);
	http_json_member_real (writer, "available", admin_file_system->stats.available);
	http_json_member_real (writer, "used", admin_file_system->stats.used);
	http_json_member_real (writer, "used_percentage", admin_file_system->stats.used_percentage);

	cerver_json_writer_object_end (writer);

}

// the stats of the registered file systems, read when they are written
static void http_cerver_admin_write_file_systems_stats (
	CerverJsonWriter *writer, const void *http_cerver_ptr
) {

	const HttpCerver *http_cerver = (const HttpCerver *) http_cerver_ptr;

	(void) pthread_mutex_lock ((pthread_mutex_t *) http_cerver->admin_mutex);

	cerver_json_writer_object_begin (writer);

	cerver_json_writer_key (writer, "filesystems");
	cerver_json_writer_array_begin (writer);

	for (ListElement *le = dlist_start (http_cerver->admin_file_systems_stats); le; le = le->next) {
		http_cerver_admin_handler_single_file_systems_stats (
			writer, (const HttpAdminFileSystemStats *) le->data
		);
	}

	cerver_json_writer_array_end (writer);

	cerver_json_writer_object_end (writer);

	(void) pthread_mutex_unlock ((pthread_mutex_t *) http_cerver->admin_mutex);

}

char *http_cerver_admin_generate_file_systems_stats_json (
	const HttpCerver *http_cerver
) {

	return http_json_generate (http_cerver_admin_write_file_systems_stats, http_cerver);

}

static void http_cerver_admin_handler_worker_state (
	CerverJsonWriter *writer, Worker *worker
) {

	cerver_json_writer_object_begin (writer);

	http_json_member_integer (writer, "id", (i64) worker->id);
	http_json_member_string (writer, "name", worker->name);
	http_json_member_string (writer, "state", worker_state_to_string (worker_get_state (worker)));

	cerver_json_writer_object_end (writer);

}

// the states of the registered workers, in the order that they were registered
static void http_cerver_admin_write_workers (
	CerverJsonWriter *writer, const void *http_cerver_ptr
) {

	const HttpCerver *http_cerver = (const HttpCerver *) http_cerver_ptr;

	cerver_json_writer_object_begin (writer);

	cerver_json_writer_key (writer, "workers");
	cerver_json_writer_array_begin (writer);

	for (ListElement *le = dlist_start (http_cerver->admin_workers); le; le = le->next) {
		http_cerver_admin_handler_worker_state (writer, (Worker *) le->data);
	}

	cerver_json_writer_array_end (writer);

	cerver_json_writer_object_end (writer);

}

char *http_cerver_admin_generate_workers_json (
	const HttpCerver *http_cerver
) {

	return http_json_generate (http_cerver_admin_write_workers, http_cerver);

}

#pragma endregion

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

// answers a GET with the json that a method writes in the arena of the request:
// nothing is allocated for its values, and the text lives until the request ends
static void http_cerver_admin_send_json (
	const HttpReceive *http_receive,
	const HttpRequest *request,
	const HttpJsonWrite write
) {

	CerverJsonWriter writer;

	// a writer without an arena has an error from its start
	cerver_json_writer_init_arena (
		&writer, http_request_get_arena ((HttpRequest *) request), CERVER_JSON_FLAGS_NONE
	);

	write (&writer, http_receive->http_cerver);

	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_NONE) {
		http_cerver_admin_send_response (
			http_receive, request,
			cerver_json_writer_text (&writer), cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) http_response_send (server_error, http_receive);
	}

}

// answers a HEAD with the length of the json that a method writes: its bytes are only counted
static void http_cerver_admin_send_json_head (
	const HttpReceive *http_receive,
	const HttpJsonWrite write
) {

	CerverJsonWriter writer;
	cerver_json_writer_init (&writer, NULL, 0, CERVER_JSON_FLAGS_NONE);

	write (&writer, http_receive->http_cerver);

	// a writer without a buffer ends as a text that did not fit
	if (cerver_json_writer_end (&writer) == CERVER_JSON_ERROR_TRUNCATED) {
		http_cerver_admin_send_head (
			http_receive, cerver_json_writer_length (&writer)
		);
	}

	else {
		(void) http_response_send (server_error, http_receive);
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

	http_cerver_admin_send_json_head (http_receive, http_cerver_admin_write_info);

}

// GET [top level]/cerver/info
static void http_cerver_admin_info_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json (http_receive, request, http_cerver_admin_write_info);

}

// HEAD [top level]/cerver/stats
static void http_cerver_admin_stats_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json_head (http_receive, http_cerver_admin_write_routes_stats);

}

// GET [top level]/cerver/stats
static void http_cerver_admin_stats_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json (http_receive, request, http_cerver_admin_write_routes_stats);

}

// HEAD [top level]/cerver/stats/filesystems
static void http_cerver_admin_file_systems_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json_head (http_receive, http_cerver_admin_write_file_systems_stats);

}

// GET [top level]/cerver/stats/filesystems
static void http_cerver_admin_file_systems_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json (http_receive, request, http_cerver_admin_write_file_systems_stats);

}

// HEAD [top level]/cerver/stats/workers
static void http_cerver_admin_workers_head_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json_head (http_receive, http_cerver_admin_write_workers);

}

// GET [top level]/cerver/stats/workers
static void http_cerver_admin_workers_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_cerver_admin_send_json (http_receive, request, http_cerver_admin_write_workers);

}

static void http_cerver_admin_send_metrics (
	const HttpReceive *http_receive, const String *metrics
) {

	HttpResponse *response = http_response_get ();
	if (response) {
		http_response_set_status (response, HTTP_STATUS_OK);
		(void) http_response_add_header (response, HTTP_HEADER_CONTENT_TYPE, HTTP_METRICS_CONTENT_TYPE);
		(void) http_response_add_content_length_header (response, metrics->len);

		http_cerver_admin_response_set_cors_headers (http_receive, response);

		(void) http_response_set_data_ref (response, metrics->str, metrics->len);

		(void) http_response_send (response, http_receive);

		http_response_return (response);
	}

}

// GET [top level]/cerver/metrics
// HEAD [top level]/cerver/metrics
static void http_cerver_admin_metrics_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	String *metrics = http_cerver_metrics_generate (http_receive->http_cerver);
	if (metrics) {
		http_cerver_admin_send_metrics (http_receive, metrics);

		str_delete (metrics);
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

static void http_cerver_admin_set_metrics_route (
	const HttpCerver *http_cerver, HttpRoute *top_level_route
) {

	// GET [top level]/cerver/metrics
	HttpRoute *metrics_route = http_route_create (
		REQUEST_METHOD_GET, "cerver/metrics", http_cerver_admin_metrics_handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_route_set_handler (
			metrics_route,
			REQUEST_METHOD_HEAD,
			http_cerver_admin_metrics_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_route_set_handler (
			metrics_route,
			REQUEST_METHOD_OPTIONS,
			http_cerver_admin_options_handler
		);
	}

	http_cerver_admin_route_set_auth (http_cerver, metrics_route);

	http_route_set_custom_data (metrics_route, http_cerver->admin_routes_custom_data);

	http_route_child_add (top_level_route, metrics_route);

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

		// GET [top level]/cerver/metrics
		http_cerver_admin_set_metrics_route (
			http_cerver, top_level_route
		);

		retval = 0;
	}

	return retval;

}

// the same auth of the admin routes of the first routes API,
// without its own auth an admin endpoint would inherit the one of its group
static void http_cerver_admin_endpoint_set_auth (
	const HttpCerver *http_cerver, HttpEndpoint *endpoint
) {

	if (http_cerver->enable_admin_routes_auth) {
		switch (http_cerver->admin_auth_type) {
			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				http_endpoint_set_auth (endpoint, HTTP_ROUTE_AUTH_TYPE_BEARER);
				http_endpoint_set_decode_data (
					endpoint,
					http_cerver->admin_decode_data,
					http_cerver->admin_delete_decoded_data
				);
			} break;

			case HTTP_ROUTE_AUTH_TYPE_CUSTOM: {
				http_endpoint_set_auth (endpoint, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
				http_endpoint_set_authentication_handler (
					endpoint, http_cerver->admin_auth_handler
				);
			} break;

			default: {
				http_endpoint_set_auth (endpoint, HTTP_ROUTE_AUTH_TYPE_NONE);
			} break;
		}
	}

	else {
		http_endpoint_set_auth (endpoint, HTTP_ROUTE_AUTH_TYPE_NONE);
	}

	// the cerver owns the admin custom data
	http_endpoint_set_custom_data (
		endpoint, http_cerver->admin_routes_custom_data, NULL
	);

}

static void http_cerver_admin_group_endpoint (
	const HttpCerver *http_cerver, HttpGroup *group,
	const RequestMethod method, const char *path, const HttpHandler handler
) {

	HttpEndpoint *endpoint = http_group_route_add (group, method, path, handler);
	if (endpoint) {
		http_cerver_admin_endpoint_set_auth (http_cerver, endpoint);
	}

}

// the GET endpoint of the path, with its HEAD and OPTIONS if they are enabled
static void http_cerver_admin_group_route (
	const HttpCerver *http_cerver, HttpGroup *group, const char *path,
	const HttpHandler handler, const HttpHandler head_handler
) {

	http_cerver_admin_group_endpoint (
		http_cerver, group, REQUEST_METHOD_GET, path, handler
	);

	if (http_cerver->enable_admin_head_handlers) {
		http_cerver_admin_group_endpoint (
			http_cerver, group, REQUEST_METHOD_HEAD, path, head_handler
		);
	}

	if (http_cerver->enable_admin_options_handlers) {
		http_cerver_admin_group_endpoint (
			http_cerver, group, REQUEST_METHOD_OPTIONS, path, http_cerver_admin_options_handler
		);
	}

}

u8 http_cerver_admin_group_init (
	const HttpCerver *http_cerver,
	HttpGroup *group
) {

	u8 retval = 1;

	if (group) {
		// GET [group]/cerver/info
		if (http_cerver->enable_admin_info_route) {
			http_cerver_admin_group_route (
				http_cerver, group, "cerver/info",
				http_cerver_admin_info_handler, http_cerver_admin_info_head_handler
			);
		}

		// GET [group]/cerver/stats
		http_cerver_admin_group_route (
			http_cerver, group, "cerver/stats",
			http_cerver_admin_stats_handler, http_cerver_admin_stats_head_handler
		);

		// GET [group]/cerver/stats/filesystems
		http_cerver_admin_group_route (
			http_cerver, group, "cerver/stats/filesystems",
			http_cerver_admin_file_systems_handler, http_cerver_admin_file_systems_head_handler
		);

		// GET [group]/cerver/stats/workers
		http_cerver_admin_group_route (
			http_cerver, group, "cerver/stats/workers",
			http_cerver_admin_workers_handler, http_cerver_admin_workers_head_handler
		);

		// GET [group]/cerver/metrics, a HEAD is answered without the body by the same handler
		http_cerver_admin_group_route (
			http_cerver, group, "cerver/metrics",
			http_cerver_admin_metrics_handler, http_cerver_admin_metrics_handler
		);

		retval = 0;
	}

	return retval;

}

#pragma region admin

// enables the ability to have admin routes
// to fetch cerver's HTTP stats
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN
void http_cerver_enable_admin_routes (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_routes = enable;
	}

}

// enables the ability to have an admin info route
// to fetch cerver's information
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO
void http_cerver_enable_admin_info_route (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_info_route = enable;
	}

}

// enables HTTP admin routes to handle HEAD requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS
void http_cerver_enable_admin_head_handlers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_head_handlers = enable;
	}

}

// enables HTTP admin routes to handle OPTIONS requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS
void http_cerver_enable_admin_options_handlers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_options_handlers = enable;
	}

}

// enables authentication in admin routes
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH
void http_cerver_enable_admin_routes_authentication (
	HttpCerver *http_cerver, const HttpRouteAuthType auth_type
) {

	if (http_cerver) {
		http_cerver->enable_admin_routes_auth = true;
		http_cerver->admin_auth_type = auth_type;
	}

}

// sets the method to be used to decode incoming data from JWT
// and sets a method to delete it after use
// if no delete method is set, data won't be freed
void http_cerver_admin_routes_auth_set_decode_data (
	HttpCerver *http_cerver,
	HttpDecodeData decode_data, void (*delete_decoded_data)(void *)
) {

	if (http_cerver) {
		http_cerver->admin_decode_data = decode_data;
		http_cerver->admin_delete_decoded_data = delete_decoded_data;
	}

}

// works like http_cerver_enable_admin_routes_authentication ()
// but sets a method to decode data from a JWT into a json string
void http_cerver_admin_routes_auth_decode_to_json (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->admin_decode_data = http_decode_data_into_json;
		http_cerver->admin_delete_decoded_data = free;
	}

}

// sets a method to be used to handle auth in admin routes
// HTTP cerver must had been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
void http_cerver_admin_routes_set_authentication_handler (
	HttpCerver *http_cerver,
	unsigned int (*authentication_handler)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->admin_auth_handler = authentication_handler;
	}

}

// sets a custom data reference to be set in all admin routes
void http_cerver_admin_routes_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
) {

	if (http_cerver) {
		http_cerver->admin_routes_custom_data = custom_data;
	}

}

// sets how the custom data will be disposed when the service gets destroyed
void http_cerver_admin_routes_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
) {

	if (http_cerver) {
		http_cerver->delete_admin_routes_custom_data = delete_custom_data;
	}

}

// enables CORS headers in admin routes responses
// always uses admin origin's value
// if there is no dedicated origin, it will dynamically
// set the header based on the origins whitelist
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS
void http_cerver_enable_admin_cors_headers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_cors_headers = enable;
	}

}

// sets the dedicated domain that will be walways set
// in the admin responses CORS headers
void http_cerver_admin_set_origin (
	HttpCerver *http_cerver, const char *domain
) {

	if (http_cerver) {
		http_origin_init (
			&http_cerver->admin_origin,
			domain
		);
	}

}

// registers a new file system to be handled
// when requesting for fs stats, a NULL path is not registered
void http_cerver_register_admin_file_system (
	HttpCerver *http_cerver, const char *path
) {

	if (http_cerver && path) {
		(void) dlist_insert_at_end_unsafe (
			http_cerver->admin_file_systems_stats,
			http_admin_file_system_stats_create (path)
		);
	}

}

// registers an existing worker to be handled
// when requisting for workers states
void http_cerver_register_admin_worker (
	HttpCerver *http_cerver, const Worker *worker
) {

	if (http_cerver && worker) {
		(void) dlist_insert_at_end_unsafe (
			http_cerver->admin_workers,
			worker
		);
	}

}

#pragma endregion
