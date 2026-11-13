#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/version.h"

#include "cerver/threads/thpool.h"

#include "cerver/http/http.h"
#include "cerver/http/metrics.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"
#include "cerver/http/tree.h"

#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"

#define HTTP_METRICS_HEADER_FORMAT				"# HELP %s %s\n# TYPE %s %s\n"

#define HTTP_METRICS_INFO_FORMAT				"cerver_info{version=\"%s\"} 1\n"
#define HTTP_METRICS_VALUE_FORMAT				"%s %zu\n"
#define HTTP_METRICS_STATUS_FORMAT				"cerver_http_responses_total{code=\"%zu\"} %zu\n"

#define HTTP_METRICS_ENDPOINT_FORMAT			"%s{method=\"%s\",path=\"%s\"} %zu\n"
#define HTTP_METRICS_BUCKET_FORMAT				"cerver_http_request_duration_seconds_bucket{method=\"%s\",path=\"%s\",le=\"%g\"} %zu\n"
#define HTTP_METRICS_INFINITE_BUCKET_FORMAT		"cerver_http_request_duration_seconds_bucket{method=\"%s\",path=\"%s\",le=\"+Inf\"} %zu\n"
#define HTTP_METRICS_SUM_FORMAT					"cerver_http_request_duration_seconds_sum{method=\"%s\",path=\"%s\"} %.6f\n"
#define HTTP_METRICS_COUNT_FORMAT				"cerver_http_request_duration_seconds_count{method=\"%s\",path=\"%s\"} %zu\n"

#define HTTP_METRICS_THPOOL_BUCKET_FORMAT			"%s_bucket{le=\"%g\"} %zu\n"
#define HTTP_METRICS_THPOOL_INFINITE_BUCKET_FORMAT	"%s_bucket{le=\"+Inf\"} %zu\n"
#define HTTP_METRICS_THPOOL_SUM_FORMAT				"%s_sum %.6f\n"
#define HTTP_METRICS_THPOOL_COUNT_FORMAT			"%s_count %zu\n"

// the buckets of the thpool histograms are in ms, the metrics in secs
#define HTTP_METRICS_MILLISECONDS_PER_SECOND		1000.0
#define HTTP_METRICS_MICROSECONDS_PER_SECOND		1000000.0

static const char *http_metrics_type_counter = { "counter" };
static const char *http_metrics_type_gauge = { "gauge" };
static const char *http_metrics_type_histogram = { "histogram" };

static const char http_metrics_escape = '\\';
static const char http_metrics_quote = '"';
static const char http_metrics_new_line = '\n';
static const char http_metrics_escaped_new_line = 'n';

// the first status code that is counted
#define HTTP_METRICS_FIRST_STATUS				100

// a metric of the requests of the cerver
typedef struct HttpMetricsRequests {

	const char *name;
	const char *help;
	size_t offset;

} HttpMetricsRequests;

static const HttpMetricsRequests http_metrics_requests[] = {
	{
		"cerver_http_requests_incompleted_total", "The requests that the parser could not parse.",
		offsetof (HttpCerver, n_incompleted_requests)
	},
	{
		"cerver_http_requests_unhandled_total", "The requests that did not match a route.",
		offsetof (HttpCerver, n_unhandled_requests)
	},
	{
		"cerver_http_requests_catch_all_total", "The requests that were answered by the catch all handler.",
		offsetof (HttpCerver, n_catch_all_requests)
	},
	{
		"cerver_http_requests_failed_auth_total", "The requests that failed to authenticate.",
		offsetof (HttpCerver, n_failed_auth_requests)
	},
	{
		"cerver_http_requests_limited_total", "The requests that exceeded a limit.",
		offsetof (HttpCerver, n_limited_requests)
	},
	{
		"cerver_http_requests_timeout_total", "The requests that were answered with 408.",
		offsetof (HttpCerver, n_timeout_requests)
	},
	{
		"cerver_http_requests_unanswered_total", "The requests that were answered with 500 because their handler did not respond.",
		offsetof (HttpCerver, n_unanswered_requests)
	},
	{
		"cerver_http_failed_responses_total", "The responses that could not be sent completely.",
		offsetof (HttpCerver, n_failed_responses)
	}
};

#define HTTP_METRICS_ABORTED_NAME				"cerver_http_aborted_requests_total"
#define HTTP_METRICS_ABORTED_FORMAT				HTTP_METRICS_ABORTED_NAME "{stage=\"%s\",reason=\"%s\"} %zu\n"

#define HTTP_METRICS_VANISHED_NAME				"cerver_http_vanished_requests_total"
#define HTTP_METRICS_VANISHED_FORMAT			HTTP_METRICS_VANISHED_NAME "{stage=\"%s\"} %zu\n"

static const size_t http_metrics_n_requests = (
	sizeof (http_metrics_requests) / sizeof (HttpMetricsRequests)
);

static void http_metrics_append (String *metrics, const char *line, const int written) {

	if (written > 0) {
		const size_t length = ((size_t) written < HTTP_METRICS_LINE_SIZE) ?
			(size_t) written : HTTP_METRICS_LINE_SIZE - 1;

		(void) str_append_bytes (metrics, line, length);
	}

}

static void http_metrics_header (
	String *metrics, const char *name, const char *help, const char *type
) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };
	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_HEADER_FORMAT, name, help, name, type)
	);

}

static void http_metrics_value (
	String *metrics, const char *name, const size_t value
) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };
	http_metrics_append (
		metrics, line, snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_VALUE_FORMAT, name, value)
	);

}

// escapes the backslashes, the quotes and the new lines of a label value
static void http_metrics_label_escape (const char *value, char *escaped) {

	size_t length = 0;
	for (const char *current = value; *current && (length < (HTTP_METRICS_LABEL_SIZE - 2)); current++) {
		if ((*current == http_metrics_escape) || (*current == http_metrics_quote)) {
			escaped[length++] = http_metrics_escape;
			escaped[length++] = *current;
		}

		else if (*current == http_metrics_new_line) {
			escaped[length++] = http_metrics_escape;
			escaped[length++] = http_metrics_escaped_new_line;
		}

		else {
			escaped[length++] = *current;
		}
	}

	escaped[length] = '\0';

}

static void http_metrics_cerver (String *metrics, const HttpCerver *http_cerver) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (metrics, "cerver_info", "The version of cerver.", http_metrics_type_gauge);
	http_metrics_append (
		metrics, line, snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_INFO_FORMAT, CERVER_VERSION)
	);

	Cerver *cerver = http_cerver->cerver;
	if (cerver && cerver->info) {
		cerver_update_uptime (cerver);

		http_metrics_header (
			metrics, "cerver_uptime_seconds", "The secs since the cerver started.", http_metrics_type_gauge
		);

		http_metrics_value (metrics, "cerver_uptime_seconds", (size_t) cerver->info->uptime);
	}

}

static void http_metrics_responses (String *metrics, const HttpCerver *http_cerver) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (
		metrics, "cerver_http_responses_total", "The responses sent by their status code.",
		http_metrics_type_counter
	);

	for (size_t status = HTTP_METRICS_FIRST_STATUS; status < HTTP_CERVER_RESPONSES_STATUS_SIZE; status++) {
		const size_t count = CERVER_STATS_GET (http_cerver->n_responses_by_status[status]);
		if (count) {
			http_metrics_append (
				metrics, line,
				snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_STATUS_FORMAT, status, count)
			);
		}
	}

}

static void http_metrics_requests_counters (String *metrics, const HttpCerver *http_cerver) {

	for (size_t i = 0; i < http_metrics_n_requests; i++) {
		const HttpMetricsRequests *counter = &http_metrics_requests[i];
		const size_t *field = (const size_t *) ((const char *) http_cerver + counter->offset);

		http_metrics_header (metrics, counter->name, counter->help, http_metrics_type_counter);
		http_metrics_value (metrics, counter->name, CERVER_STATS_GET (*field));
	}

}

// the requests whose client left, by the stage where cerver noticed it and how it left
static void http_metrics_aborted (String *metrics, const HttpCerver *http_cerver) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (
		metrics, HTTP_METRICS_ABORTED_NAME,
		"The requests whose client left, by the stage where it left and how it ended its connection.",
		http_metrics_type_counter
	);

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			http_metrics_append (
				metrics, line,
				snprintf (
					line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_ABORTED_FORMAT,
					http_stage_label ((HttpStage) stage), http_abort_reason_label ((HttpAbortReason) reason),
					CERVER_STATS_GET (http_cerver->n_aborted_requests[stage][reason])
				)
			);
		}
	}

}

// the requests whose client stopped sending or reading, by the stage where a timeout ended them
static void http_metrics_vanished (String *metrics, const HttpCerver *http_cerver) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (
		metrics, HTTP_METRICS_VANISHED_NAME,
		"The requests whose client stopped sending or reading, by the stage where a timeout ended them.",
		http_metrics_type_counter
	);

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		http_metrics_append (
			metrics, line,
			snprintf (
				line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_VANISHED_FORMAT,
				http_stage_label ((HttpStage) stage),
				CERVER_STATS_GET (http_cerver->n_vanished_requests[stage])
			)
		);
	}

}

// the counters of the jobs of the thpool, that are the connections that it handled
static void http_metrics_thpool_counters (String *metrics, const ThpoolStats *stats) {

	http_metrics_header (
		metrics, "cerver_thpool_jobs_total", "The connections that entered the queue of the pool.",
		http_metrics_type_counter
	);

	http_metrics_value (metrics, "cerver_thpool_jobs_total", (size_t) stats->total_jobs);

	http_metrics_header (
		metrics, "cerver_thpool_jobs_completed_total", "The connections that the threads finished.",
		http_metrics_type_counter
	);

	http_metrics_value (metrics, "cerver_thpool_jobs_completed_total", (size_t) stats->completed_jobs);

	http_metrics_header (
		metrics, "cerver_thpool_rejected_total", "The connections that could not enter the queue.",
		http_metrics_type_counter
	);

	http_metrics_value (metrics, "cerver_thpool_rejected_total", (size_t) stats->rejected_jobs);

	http_metrics_header (
		metrics, "cerver_thpool_dropped_total", "The queued connections that a drain rejected.",
		http_metrics_type_counter
	);

	http_metrics_value (metrics, "cerver_thpool_dropped_total", (size_t) stats->dropped_jobs);

	http_metrics_header (
		metrics, "cerver_thpool_waited_total", "The times that a connection waited for space in the queue.",
		http_metrics_type_counter
	);

	http_metrics_value (metrics, "cerver_thpool_waited_total", (size_t) stats->waited_jobs);

	http_metrics_header (
		metrics, "cerver_thpool_queued_max", "The most connections that waited in the queue at once.",
		http_metrics_type_gauge
	);

	http_metrics_value (metrics, "cerver_thpool_queued_max", (size_t) stats->max_queued);

}

// a histogram of the thpool, its buckets are of 2^i ms and the last one has the rest
static void http_metrics_thpool_histogram (
	String *metrics, const char *name, const char *help,
	const u64 *histogram, const u64 total_microseconds
) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (metrics, name, help, http_metrics_type_histogram);

	size_t cumulative = 0;
	for (unsigned int i = 0; i < (THPOOL_HISTOGRAM_BUCKETS - 1); i++) {
		cumulative += (size_t) histogram[i];

		const double bucket = (double) (1UL << i) / HTTP_METRICS_MILLISECONDS_PER_SECOND;
		http_metrics_append (
			metrics, line,
			snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_THPOOL_BUCKET_FORMAT, name, bucket, cumulative)
		);
	}

	cumulative += (size_t) histogram[THPOOL_HISTOGRAM_BUCKETS - 1];

	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_THPOOL_INFINITE_BUCKET_FORMAT, name, cumulative)
	);

	http_metrics_append (
		metrics, line,
		snprintf (
			line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_THPOOL_SUM_FORMAT,
			name, (double) total_microseconds / HTTP_METRICS_MICROSECONDS_PER_SECOND
		)
	);

	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_THPOOL_COUNT_FORMAT, name, cumulative)
	);

}

static void http_metrics_thpool (String *metrics, const HttpCerver *http_cerver) {

	Thpool *thpool = http_cerver->cerver ? http_cerver->cerver->thpool : NULL;
	if (thpool) {
		http_metrics_header (
			metrics, "cerver_thpool_threads", "The threads of the pool.", http_metrics_type_gauge
		);

		http_metrics_value (metrics, "cerver_thpool_threads", thpool_get_num_threads_alive (thpool));

		http_metrics_header (
			metrics, "cerver_thpool_threads_working", "The threads of the pool that are handling a connection.",
			http_metrics_type_gauge
		);

		http_metrics_value (metrics, "cerver_thpool_threads_working", thpool_get_num_threads_working (thpool));

		http_metrics_header (
			metrics, "cerver_thpool_queued", "The connections that wait for a thread.", http_metrics_type_gauge
		);

		http_metrics_value (metrics, "cerver_thpool_queued", thpool_get_queued (thpool));

		ThpoolStats stats;
		thpool_get_stats (thpool, &stats);

		http_metrics_thpool_counters (metrics, &stats);

		http_metrics_thpool_histogram (
			metrics, "cerver_thpool_wait_seconds", "The time that the connections waited for a thread.",
			stats.wait_histogram, stats.wait_total_microseconds
		);

		http_metrics_thpool_histogram (
			metrics, "cerver_thpool_run_seconds", "The time that the threads handled each connection.",
			stats.run_histogram, stats.run_total_microseconds
		);
	}

}

// calls the method for every endpoint of the cerver,
// the ones of the groups and the ones of the first routes API
typedef void (*HttpMetricsEndpointMethod)(String *metrics, const HttpEndpoint *endpoint);

static void http_metrics_group_endpoints (
	String *metrics, const HttpGroup *group, const HttpMetricsEndpointMethod method
) {

	for (ListElement *le = dlist_start (group->endpoints); le; le = le->next) {
		method (metrics, (const HttpEndpoint *) le->data);
	}

	for (ListElement *le = dlist_start (group->groups); le; le = le->next) {
		http_metrics_group_endpoints (metrics, (const HttpGroup *) le->data, method);
	}

}

static void http_metrics_endpoints (
	String *metrics, const HttpCerver *http_cerver, const HttpMetricsEndpointMethod method
) {

	const HttpTree *tree = http_cerver->tree;

	for (ListElement *le = dlist_start (tree->groups); le; le = le->next) {
		http_metrics_group_endpoints (metrics, (const HttpGroup *) le->data, method);
	}

	for (ListElement *le = dlist_start (tree->compatibility_endpoints); le; le = le->next) {
		method (metrics, (const HttpEndpoint *) le->data);
	}

}

// the stats are updated with atomic operations while the requests are handled
static void http_metrics_endpoint_snapshot (
	const HttpEndpoint *endpoint, HttpRouteStats *snapshot
) {

	(void) memset (snapshot, 0, sizeof (HttpRouteStats));

	http_route_stats_snapshot (endpoint->stats, snapshot);

}

static void http_metrics_endpoint_value (
	String *metrics, const HttpEndpoint *endpoint, const char *name, const size_t value
) {

	char path[HTTP_METRICS_LABEL_SIZE] = { 0 };
	http_metrics_label_escape (endpoint->path->str, path);

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };
	http_metrics_append (
		metrics, line,
		snprintf (
			line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_ENDPOINT_FORMAT,
			name, http_request_method_str (endpoint->method), path, value
		)
	);

}

static void http_metrics_endpoint_requests (String *metrics, const HttpEndpoint *endpoint) {

	HttpRouteStats snapshot;
	http_metrics_endpoint_snapshot (endpoint, &snapshot);

	http_metrics_endpoint_value (metrics, endpoint, "cerver_http_requests_total", snapshot.n_requests);

}

static void http_metrics_endpoint_received (String *metrics, const HttpEndpoint *endpoint) {

	HttpRouteStats snapshot;
	http_metrics_endpoint_snapshot (endpoint, &snapshot);

	http_metrics_endpoint_value (metrics, endpoint, "cerver_http_request_bytes_total", snapshot.sum_request_sizes);

}

static void http_metrics_endpoint_sent (String *metrics, const HttpEndpoint *endpoint) {

	HttpRouteStats snapshot;
	http_metrics_endpoint_snapshot (endpoint, &snapshot);

	http_metrics_endpoint_value (metrics, endpoint, "cerver_http_response_bytes_total", snapshot.sum_response_sizes);

}

static void http_metrics_endpoint_histogram (String *metrics, const HttpEndpoint *endpoint) {

	HttpRouteStats snapshot;
	http_metrics_endpoint_snapshot (endpoint, &snapshot);

	char path[HTTP_METRICS_LABEL_SIZE] = { 0 };
	http_metrics_label_escape (endpoint->path->str, path);

	const char *method = http_request_method_str (endpoint->method);
	const double *buckets = http_route_stats_duration_buckets ();

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	size_t cumulative = 0;
	for (unsigned int i = 0; i < HTTP_ROUTE_STATS_DURATION_BUCKETS; i++) {
		cumulative += snapshot.duration_buckets[i];
		http_metrics_append (
			metrics, line,
			snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_BUCKET_FORMAT, method, path, buckets[i], cumulative)
		);
	}

	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_INFINITE_BUCKET_FORMAT, method, path, snapshot.n_requests)
	);

	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_SUM_FORMAT, method, path, snapshot.sum_process_times)
	);

	http_metrics_append (
		metrics, line,
		snprintf (line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_COUNT_FORMAT, method, path, snapshot.n_requests)
	);

}

static void http_metrics_endpoints_all (String *metrics, const HttpCerver *http_cerver) {

	http_metrics_header (
		metrics, "cerver_http_requests_total", "The requests handled by each endpoint.",
		http_metrics_type_counter
	);

	http_metrics_endpoints (metrics, http_cerver, http_metrics_endpoint_requests);

	http_metrics_header (
		metrics, "cerver_http_request_bytes_total", "The bytes received by each endpoint.",
		http_metrics_type_counter
	);

	http_metrics_endpoints (metrics, http_cerver, http_metrics_endpoint_received);

	http_metrics_header (
		metrics, "cerver_http_response_bytes_total", "The bytes sent by each endpoint.",
		http_metrics_type_counter
	);

	http_metrics_endpoints (metrics, http_cerver, http_metrics_endpoint_sent);

	http_metrics_header (
		metrics, "cerver_http_request_duration_seconds", "The process time of the requests of each endpoint.",
		http_metrics_type_histogram
	);

	http_metrics_endpoints (metrics, http_cerver, http_metrics_endpoint_histogram);

}

// a counter of the WebSocket connections
typedef struct HttpMetricsWebSocket {

	const char *name;
	const char *help;
	size_t offset;

} HttpMetricsWebSocket;

static const HttpMetricsWebSocket http_metrics_web_socket_counters[] = {
	{ "cerver_websocket_opened_total", "The WebSocket connections that were opened.", offsetof (HttpWebSocketRegistry, n_opened) },
	{ "cerver_websocket_closed_total", "The WebSocket connections that ended.", offsetof (HttpWebSocketRegistry, n_closed) },
	{ "cerver_websocket_messages_received_total", "The messages that the WebSocket connections received.", offsetof (HttpWebSocketRegistry, n_messages_received) },
	{ "cerver_websocket_messages_sent_total", "The messages that the WebSocket connections sent.", offsetof (HttpWebSocketRegistry, n_messages_sent) },
	{ "cerver_websocket_received_bytes_total", "The bytes of the messages that were received, after they were decompressed.", offsetof (HttpWebSocketRegistry, n_bytes_received) },
	{ "cerver_websocket_sent_bytes_total", "The bytes of the messages that were sent, before they were compressed.", offsetof (HttpWebSocketRegistry, n_bytes_sent) },
	{ "cerver_websocket_failed_sends_total", "The WebSocket connections that failed because a send did not end in time.", offsetof (HttpWebSocketRegistry, n_slow) },
	{ "cerver_websocket_errors_total", "The WebSocket connections that ended with an error.", offsetof (HttpWebSocketRegistry, n_errors) }
};

#define HTTP_METRICS_WEB_SOCKET_COUNTERS		(sizeof (http_metrics_web_socket_counters) / sizeof (HttpMetricsWebSocket))

#define HTTP_METRICS_WEB_SOCKET_CONNECTIONS_NAME		"cerver_websocket_connections"

#define HTTP_METRICS_WEB_SOCKET_REJECTED_NAME			"cerver_websocket_rejected_total"
#define HTTP_METRICS_WEB_SOCKET_REJECTED_FORMAT			HTTP_METRICS_WEB_SOCKET_REJECTED_NAME "{reason=\"%s\"} %zu\n"

#define HTTP_METRICS_WEB_SOCKET_CLOSES_NAME				"cerver_websocket_closes_total"
#define HTTP_METRICS_WEB_SOCKET_CLOSES_FORMAT			HTTP_METRICS_WEB_SOCKET_CLOSES_NAME "{code=\"%u\"} %zu\n"
#define HTTP_METRICS_WEB_SOCKET_CLOSES_OTHER_FORMAT		HTTP_METRICS_WEB_SOCKET_CLOSES_NAME "{code=\"other\"} %zu\n"

static void http_metrics_web_socket_rejected (String *metrics, const HttpWebSocketRegistry *registry) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (
		metrics, HTTP_METRICS_WEB_SOCKET_REJECTED_NAME,
		"The upgrades to WebSocket that were rejected, by their reason.", http_metrics_type_counter
	);

	for (unsigned int rejection = 0; rejection < HTTP_WEB_SOCKET_REJECTIONS; rejection++) {
		http_metrics_append (
			metrics, line,
			snprintf (
				line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_WEB_SOCKET_REJECTED_FORMAT,
				http_web_socket_rejection_to_string ((HttpWebSocketRejection) rejection),
				(size_t) CERVER_STATS_GET (registry->n_rejected[rejection])
			)
		);
	}

}

static void http_metrics_web_socket_closes (String *metrics, const HttpWebSocketRegistry *registry) {

	char line[HTTP_METRICS_LINE_SIZE] = { 0 };

	http_metrics_header (
		metrics, HTTP_METRICS_WEB_SOCKET_CLOSES_NAME,
		"The WebSocket connections that ended, by the code of their close (1006 without it).", http_metrics_type_counter
	);

	for (unsigned int i = 0; i < HTTP_WEB_SOCKET_CLOSE_CODES_SIZE; i++) {
		http_metrics_append (
			metrics, line,
			snprintf (
				line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_WEB_SOCKET_CLOSES_FORMAT,
				HTTP_WEB_SOCKET_CLOSE_CODES_FIRST + i, (size_t) CERVER_STATS_GET (registry->n_closes[i])
			)
		);
	}

	http_metrics_append (
		metrics, line,
		snprintf (
			line, HTTP_METRICS_LINE_SIZE, HTTP_METRICS_WEB_SOCKET_CLOSES_OTHER_FORMAT,
			(size_t) CERVER_STATS_GET (registry->n_closes_other)
		)
	);

}

// the WebSocket connections that are open and what they did
static void http_metrics_web_sockets (String *metrics, const HttpCerver *http_cerver) {

	HttpWebSocketRegistry *registry = http_cerver->web_sockets;

	(void) pthread_mutex_lock (&registry->mutex);
	const size_t connections = registry->count;
	(void) pthread_mutex_unlock (&registry->mutex);

	http_metrics_header (
		metrics, HTTP_METRICS_WEB_SOCKET_CONNECTIONS_NAME, "The WebSocket connections that are open.", http_metrics_type_gauge
	);

	http_metrics_value (metrics, HTTP_METRICS_WEB_SOCKET_CONNECTIONS_NAME, connections);

	for (size_t i = 0; i < HTTP_METRICS_WEB_SOCKET_COUNTERS; i++) {
		const HttpMetricsWebSocket *counter = &http_metrics_web_socket_counters[i];
		const u64 *field = (const u64 *) ((const char *) registry + counter->offset);

		http_metrics_header (metrics, counter->name, counter->help, http_metrics_type_counter);
		http_metrics_value (metrics, counter->name, (size_t) CERVER_STATS_GET (*field));
	}

	http_metrics_web_socket_rejected (metrics, registry);

	http_metrics_web_socket_closes (metrics, registry);

}

String *http_cerver_metrics_generate (const HttpCerver *http_cerver) {

	String *metrics = NULL;

	if (http_cerver) {
		metrics = str_new (NULL);
		if (metrics && !str_reserve (metrics, HTTP_METRICS_INITIAL_SIZE)) {
			http_metrics_cerver (metrics, http_cerver);

			http_metrics_responses (metrics, http_cerver);

			http_metrics_requests_counters (metrics, http_cerver);

			http_metrics_aborted (metrics, http_cerver);

			http_metrics_vanished (metrics, http_cerver);

			http_metrics_thpool (metrics, http_cerver);

			if (http_cerver->web_sockets) http_metrics_web_sockets (metrics, http_cerver);

			if (http_cerver->tree) http_metrics_endpoints_all (metrics, http_cerver);
		}
	}

	return metrics;

}
