#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/stats.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region stats

size_t http_cerver_stats_get_children_routes (
	const HttpCerver *http_cerver, size_t *handlers
) {

	size_t count = 0;

	unsigned int i = 0;
	HttpRoute *route = NULL;
	for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
		route = (HttpRoute *) le->data;

		// main route handlers
		for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (route->handlers[i]) *handlers += 1;
		}

		count += route->children->size;

		HttpRoute *child = NULL;
		for (ListElement *child_le = dlist_start (route->children); child_le; child_le = child_le->next) {
			child = (HttpRoute *) child_le->data;

			for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
				if (child->handlers[i]) *handlers += 1;
			}
		}
	}

	return count;

}

// print number of routes & handlers
void http_cerver_routes_stats_print (const HttpCerver *http_cerver) {

	if (http_cerver) {
		cerver_log_msg ("Main routes: %zu", http_cerver->routes->size);

		size_t total_handlers = 0;
		size_t children_routes = http_cerver_stats_get_children_routes (
			http_cerver, &total_handlers
		);

		cerver_log_msg ("Children routes: %zu", children_routes);
		cerver_log_msg ("Total handlers: %zu", total_handlers);
	}

}

static void http_cerver_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *route_stats
) {

	HttpRouteStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteStats));
	http_route_stats_snapshot (route_stats, &snapshot);
	const HttpRouteStats *stats = &snapshot;

	cerver_log_msg (
		"\t\t%s\t%zu",
		http_request_method_str (method),
		stats->n_requests
	);

	// route stats
	cerver_log_msg ("\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\tMin request size: %zu", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\tMax request size: %zu", stats->max_request_size);
	cerver_log_msg ("\t\tMean request size: %zu", stats->mean_request_size);

	cerver_log_msg ("\t\tMin response size: %zu", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\tMax response size: %zu", stats->max_response_size);
	cerver_log_msg ("\t\tMean response size: %zu", stats->mean_response_size);

}

static void http_cerver_route_file_stats_print (
	const HttpRouteFileStats *route_file_stats
) {

	HttpRouteFileStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteFileStats));
	http_route_file_stats_snapshot (route_file_stats, &snapshot);
	const HttpRouteFileStats *file_stats = &snapshot;

	cerver_log_line_break ();
	cerver_log_msg ("\t\tUploaded files: %zu", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\tMin n files: %zu", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\tMax n files: %zu", file_stats->max_n_files);
	cerver_log_msg ("\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\tMin file size: %zu", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\tMax file size: %zu", file_stats->max_file_size);
	cerver_log_msg ("\t\tMean file size: %.2f", file_stats->mean_file_size);

}

static void http_cerver_child_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *route_stats
) {

	HttpRouteStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteStats));
	http_route_stats_snapshot (route_stats, &snapshot);
	const HttpRouteStats *stats = &snapshot;

	cerver_log_msg (
		"\t\t\t%s\t%zu",
		http_request_method_str (method),
		stats->n_requests
	);

	cerver_log_msg ("\t\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\t\tMin request size: %zu", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\t\tMax request size: %zu", stats->max_request_size);
	cerver_log_msg ("\t\t\tMean request size: %zu", stats->mean_request_size);

	cerver_log_msg ("\t\t\tMin response size: %zu", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\t\tMax response size: %zu", stats->max_response_size);
	cerver_log_msg ("\t\t\tMean response size: %zu", stats->mean_response_size);

}

static void http_cerver_child_route_file_stats_print (
	const HttpRouteFileStats *route_file_stats
) {

	HttpRouteFileStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteFileStats));
	http_route_file_stats_snapshot (route_file_stats, &snapshot);
	const HttpRouteFileStats *file_stats = &snapshot;

	cerver_log_msg ("\t\t\tUploaded files: %zu", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\t\tMin n files: %zu", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\t\tMax n files: %zu", file_stats->max_n_files);
	cerver_log_msg ("\t\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\t\tMin file size: %zu", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\t\tMax file size: %zu", file_stats->max_file_size);
	cerver_log_msg ("\t\t\tMean file size: %.2f", file_stats->mean_file_size);

}

static void http_cerver_route_children_stats_print (
	const HttpRoute *route
) {

	cerver_log_msg ("\t\t%zu children:", route->children->size);

	RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;

	HttpRoute *child = NULL;
	for (ListElement *le = dlist_start (route->children); le; le = le->next) {
		child = (HttpRoute *) le->data;

		cerver_log_msg ("\t\t%s:", child->actual->str);

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (child->handlers[i]) {
				method = (RequestMethod) i;
				http_cerver_child_route_handler_stats_print (
					method, child->stats[i]
				);

				if (
					(child->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
					&& (method == REQUEST_METHOD_POST)
				) {
					http_cerver_child_route_file_stats_print (
						child->file_stats
					);
				}
			}
		}
	}

}

// print route's stats
void http_cerver_route_stats_print (const HttpRoute *route) {

	if (route) {
		cerver_log_msg ("\t%s:", route->route->str);

		RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;
		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (route->handlers[i]) {
				method = (RequestMethod) i;
				http_cerver_route_handler_stats_print (
					method, route->stats[i]
				);

				if (
					(route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
					&& (method == REQUEST_METHOD_POST)
				) {
					http_cerver_route_file_stats_print (
						route->file_stats
					);
				}
			}
		}

		if (route->children->size) {
			http_cerver_route_children_stats_print (route);
		}

		else {
			cerver_log_msg ("\t\tNo children");
		}
	}

}

// print all http cerver stats, general & by route
// the responses sent by their status code, only the codes that were sent
static void http_cerver_responses_stats_print (const HttpCerver *http_cerver) {

	for (size_t status = 0; status < HTTP_CERVER_RESPONSES_STATUS_SIZE; status++) {
		const size_t count = CERVER_STATS_GET (http_cerver->n_responses_by_status[status]);
		if (count) cerver_log_msg ("Responses %zu: %zu", status, count);
	}

}

// the requests whose client left and the ones that vanished, only the stages that have any
static void http_cerver_gone_stats_print (const HttpCerver *http_cerver) {

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			const size_t aborted = CERVER_STATS_GET (http_cerver->n_aborted_requests[stage][reason]);
			if (aborted) {
				cerver_log_msg (
					"Aborted requests (%s, %s): %zu",
					http_stage_string ((HttpStage) stage), http_abort_reason_string ((HttpAbortReason) reason),
					aborted
				);
			}
		}

		const size_t vanished = CERVER_STATS_GET (http_cerver->n_vanished_requests[stage]);
		if (vanished) {
			cerver_log_msg ("Vanished requests (%s): %zu", http_stage_string ((HttpStage) stage), vanished);
		}
	}

}

void http_cerver_all_stats_print (const HttpCerver *http_cerver) {

	if (http_cerver) {
		http_cerver_routes_stats_print (http_cerver);

		// print stats by route
		for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
			http_cerver_route_stats_print ((HttpRoute *) le->data);
		}

		// print general stats
		cerver_log_msg ("Incompleted requests: %zu", CERVER_STATS_GET (http_cerver->n_incompleted_requests));
		cerver_log_msg ("Unhandled requests: %zu", CERVER_STATS_GET (http_cerver->n_unhandled_requests));

		cerver_log_msg ("Catch requests: %zu", CERVER_STATS_GET (http_cerver->n_catch_all_requests));
		cerver_log_msg ("Failed auth requests: %zu", CERVER_STATS_GET (http_cerver->n_failed_auth_requests));

		cerver_log_msg ("Limited requests: %zu", CERVER_STATS_GET (http_cerver->n_limited_requests));
		cerver_log_msg ("Timeout requests: %zu", CERVER_STATS_GET (http_cerver->n_timeout_requests));
		cerver_log_msg ("Unanswered requests: %zu", CERVER_STATS_GET (http_cerver->n_unanswered_requests));
		cerver_log_msg ("Failed responses: %zu", CERVER_STATS_GET (http_cerver->n_failed_responses));

		http_cerver_responses_stats_print (http_cerver);

		http_cerver_gone_stats_print (http_cerver);
	}

}

#pragma endregion
