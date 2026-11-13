#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/connection.h"
#include "cerver/handler.h"
#include "cerver/timer.h"

#include "cerver/http/access.h"
#include "cerver/http/http.h"
#include "cerver/http/method.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"
#include "cerver/http/tree.h"

#include "cerver/http/parser/api.h"

#include "cerver/utils/log.h"

static const char *http_access_unknown_path = { "-" };

static const char *http_access_id_separator = { " - " };
static const char *http_access_empty = { "" };

#pragma region log

// writes the client's address, with " via <peer>" when a proxy forwarded the request
static void http_access_client (
	const HttpReceive *http_receive, char *client
) {

	const char *peer = http_receive->cr->connection->ip;
	const HttpRequest *request = http_receive->request;
	const char *ip = (request && request->client_ip[0]) ? request->client_ip : peer;

	if (strcmp (ip, peer)) {
		(void) snprintf (client, HTTP_ACCESS_CLIENT_SIZE, "%s via %s", ip, peer);
	}

	else {
		(void) snprintf (client, HTTP_ACCESS_CLIENT_SIZE, "%s", ip);
	}

}

static inline bool http_access_has_id (const HttpReceive *http_receive) {

	return http_receive->request && http_receive->request->id[0];

}

static inline const char *http_access_separator (const HttpReceive *http_receive) {

	return http_access_has_id (http_receive) ? http_access_id_separator : http_access_empty;

}

static inline const char *http_access_id (const HttpReceive *http_receive) {

	return http_access_has_id (http_receive) ? http_receive->request->id : http_access_empty;

}

// logs a file that was served from a static path
static void http_access_file (
	const HttpReceive *http_receive, const double process_time
) {

	cerver_log_with_date (
		LOG_TYPE_NONE, LOG_TYPE_NONE,
		"%s -> [%s] %s - %zu -> %fs -> %u - %zu",
		http_receive->cr->connection->ip,
		http_request_method_str (http_receive->request->method),
		http_receive->served_file,
		http_receive->received,
		process_time,
		(unsigned int) http_receive->status, http_receive->sent
	);

}

// logs a request that was answered by an endpoint and updates its stats,
// an OPTIONS, a 405 or a redirect answered by the cerver only logs the url
static void http_access_route (
	const HttpReceive *http_receive, const double process_time
) {

	const HttpEndpoint *endpoint = http_receive->endpoint;

	char client[HTTP_ACCESS_CLIENT_SIZE] = { 0 };
	http_access_client (http_receive, client);

	cerver_log_with_date (
		LOG_TYPE_NONE, LOG_TYPE_NONE,
		"%s -> [%s] %s - %zu -> %fs -> %u - %zu%s%s",
		client,
		http_request_method_str (http_receive->request->method),
		endpoint ? endpoint->path->str : http_receive->request->url->str,
		http_receive->received,
		process_time,
		(unsigned int) http_receive->status, http_receive->sent,
		http_access_separator (http_receive),
		http_access_id (http_receive)
	);

	if (endpoint) {
		http_route_stats_update (
			endpoint->stats, process_time, http_receive->received, http_receive->sent
		);

		if (
			(endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
			&& http_receive->is_multi_part
		) {
			http_route_file_stats_update (endpoint->file_stats, http_receive->file_stats);
		}
	}

}

// a response that failed is logged and counted in its route
// with the status that it tried to send
static void http_access_completed (
	const HttpReceive *http_receive, const double process_time
) {

	if (http_receive->type == HTTP_RECEIVE_TYPE_FILE) {
		http_access_file (http_receive, process_time);
	}

	else {
		http_access_route (http_receive, process_time);
	}

}

// no matching route to handle the request
static void http_access_unhandled (
	const HttpReceive *http_receive, const double process_time
) {

	cerver_log_with_date (
		LOG_TYPE_NONE, LOG_TYPE_NONE,
		"%s -> [%s] %s - %zu -> %fs -> No matching route -> %u - %zu",
		http_receive->cr->connection->ip,
		http_request_method_str (http_receive->request->method),
		http_receive->request->url->str,
		http_receive->received,
		process_time,
		(unsigned int) http_receive->status, http_receive->sent
	);

	CERVER_STATS_ADD (http_receive->http_cerver->n_unhandled_requests, 1);

}

// logs a request that was answered before its body was handled:
// a failed authentication, a limit that was exceeded or a file that could not be saved
static void http_access_rejected (
	const HttpReceive *http_receive, const double process_time
) {

	const char *path = http_access_unknown_path;
	if (http_receive->endpoint) path = http_receive->endpoint->path->str;
	else if (http_receive->request->url) path = http_receive->request->url->str;

	char client[HTTP_ACCESS_CLIENT_SIZE] = { 0 };
	http_access_client (http_receive, client);

	// the method that the parser found, the request was rejected before its headers were complete
	const uint8_t method = llhttp_get_method (&http_receive->parser);

	cerver_log_with_date (
		LOG_TYPE_NONE, LOG_TYPE_NONE,
		"%s -> [%s] %s - %zu -> %fs -> %s -> %u - %zu%s%s",
		client,
		http_request_method_str ((RequestMethod) method),
		path,
		http_receive->received,
		process_time,
		http_receive_status_str (http_receive->receive_status),
		(unsigned int) http_receive->status, http_receive->sent,
		http_access_separator (http_receive),
		http_access_id (http_receive)
	);

}

#pragma endregion

#pragma region stats

// a request that did not complete: the client left (aborted), it stopped sending
// until a timeout ended it (vanished), or the parser could not parse it (incompleted)
// a request whose response failed was already counted where its client left
static void http_access_count_incomplete (const HttpReceive *http_receive) {

	HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpStage stage = http_receive->stage;
	const bool counted = http_receive->response_failed;

	switch (http_receive->receive_status) {
		case HTTP_RECEIVE_STATUS_CLOSED:
			if (!counted) CERVER_STATS_ADD (http_cerver->n_aborted_requests[stage][http_receive->abort_reason], 1);
			break;

		case HTTP_RECEIVE_STATUS_TIMEOUT:
			if (!counted) CERVER_STATS_ADD (http_cerver->n_vanished_requests[stage], 1);
			break;

		default:
			CERVER_STATS_ADD (http_cerver->n_incompleted_requests, 1);
			break;
	}

}

// the request was not completed
static void http_access_incomplete (
	const HttpReceive *http_receive, const double process_time
) {

	cerver_log_with_date (
		LOG_TYPE_NONE, LOG_TYPE_NONE,
		"%s -> Bad request - fd %d - status %s - stage %s - received %zu - recvs %u"
		" - last rc %zd - errno %d - parser %s - running %d - %fs",
		http_receive->cr->connection->ip, http_receive->sock_fd,
		http_receive_status_str (http_receive->receive_status),
		http_stage_string (http_receive->stage),
		http_receive->received, http_receive->diagnostics.n_recvs,
		http_receive->diagnostics.last_rc, http_receive->diagnostics.last_errno,
		llhttp_errno_name (llhttp_get_errno (&http_receive->parser)),
		cerver_is_running (http_receive->cr->cerver), process_time
	);

	http_access_count_incomplete (http_receive);

}

#pragma endregion

void http_access_request (const HttpReceive *http_receive) {

	const double process_time = timer_get_current_time () - http_receive->start_time;

	switch (http_receive->receive_status) {
		case HTTP_RECEIVE_STATUS_COMPLETED:
		case HTTP_RECEIVE_STATUS_RESPONSE_FAILED:
			http_access_completed (http_receive, process_time);
			break;

		case HTTP_RECEIVE_STATUS_UNHANDLED:
			http_access_unhandled (http_receive, process_time);
			break;

		// answered before the body was handled: a failed authentication,
		// a limit that was exceeded or a file that could not be saved
		case HTTP_RECEIVE_STATUS_REJECTED:
		case HTTP_RECEIVE_STATUS_LIMIT:
		case HTTP_RECEIVE_STATUS_UPLOAD_ERROR:
			http_access_rejected (http_receive, process_time);
			break;

		default:
			http_access_incomplete (http_receive, process_time);
			break;
	}

}
