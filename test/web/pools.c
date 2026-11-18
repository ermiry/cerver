#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include <cerver/types/string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../handler.h"

// a web cerver with a pool of threads for the routes that take long, next to its thpool:
// each route answers with the name of the thread that handled it, and the cerver has its health and ready routes

#define POOLS_PORT						8080
#define POOLS_THREADS					4
#define POOLS_SLOW_THREADS				2
#define POOLS_SLOW_QUEUE_SIZE			(1 << 3)
#define POOLS_SLOW_WAIT					4
#define POOLS_RECEIVE_BUFFER_SIZE		4096
#define POOLS_JSON_SIZE					256
#define POOLS_THREAD_NAME_SIZE			(1 << 4)

// the threads of the thpool of a cerver are named after it
static const char *pools_cerver_name = { "main" };
static const char *pools_slow_name = { "slow" };

static const char *pools_health_route = { "/health" };
static const char *pools_ready_route = { "/ready" };

static Cerver *pools_cerver = NULL;

static void end (int dummy) {

	(void) dummy;

	if (pools_cerver) (void) cerver_shutdown (pools_cerver);

}

static void pools_finish (void) {

	http_cerver_all_stats_print ((HttpCerver *) pools_cerver->cerver_data);
	(void) cerver_teardown (pools_cerver);

}

// GET /api/fast, /api/slow/job, /api/slow/fast and /old: {"thread": "<the thread that handles the request>"}
static void pools_thread_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	char name[POOLS_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, POOLS_THREAD_NAME_SIZE);

	char json[POOLS_JSON_SIZE] = { 0 };
	const int length = snprintf (json, POOLS_JSON_SIZE, "{\"thread\": \"%s\"}", name);

	(void) http_response_render_json (http_receive, HTTP_STATUS_OK, json, (size_t) length);

}

// GET /api/metrics, the metrics of the cerver
static void pools_metrics_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	String *metrics = http_cerver_metrics_generate (http_receive_get_cerver (http_receive));
	if (metrics) {
		(void) http_response_render_text (http_receive, HTTP_STATUS_OK, metrics->str, metrics->len);

		str_delete (metrics);
	}

}

static void pools_routes (HttpCerver *http_cerver) {

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");

	(void) http_group_get (api, "fast", pools_thread_handler);
	(void) http_group_get (api, "metrics", pools_metrics_handler);

	// the routes of the group run in the slow pool, one of them goes back to the thpool of the cerver
	HttpGroup *slow = http_group_create (api, "slow");
	http_group_set_pool (slow, pools_slow_name);

	(void) http_group_get (slow, "job", pools_thread_handler);
	http_endpoint_set_pool (http_group_get (slow, "fast", pools_thread_handler), HTTP_POOL_DEFAULT_NAME);

	// a route of the first routes API in the slow pool
	HttpRoute *old_route = http_route_create (REQUEST_METHOD_GET, "old", pools_thread_handler);
	http_route_set_pool (old_route, pools_slow_name);
	http_cerver_route_register (http_cerver, old_route);

}

static void pools_configure (HttpCerver *http_cerver) {

	HttpPool *slow = http_cerver_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS);
	http_pool_set_queue_size (slow, POOLS_SLOW_QUEUE_SIZE);
	http_pool_set_overload_policy (slow, CERVER_OVERLOAD_POLICY_WAIT);
	http_pool_set_wait_timeout (slow, POOLS_SLOW_WAIT);

	http_cerver_set_health_route (http_cerver, pools_health_route);
	http_cerver_set_ready_route (http_cerver, pools_ready_route);

	http_cerver_set_not_found_handler (http_cerver);

	if (test_keep_alive_is_set ()) http_cerver_set_keep_alive (http_cerver, test_keep_alive ());

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	pools_cerver = cerver_create_web (pools_cerver_name, POOLS_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (pools_cerver) {
		(void) cerver_set_bind_address (pools_cerver, test_address ());
		cerver_set_receive_buffer_size (pools_cerver, POOLS_RECEIVE_BUFFER_SIZE);
		cerver_set_thpool_n_threads (pools_cerver, POOLS_THREADS);
		cerver_set_handler_type (pools_cerver, test_handler_type ());
		cerver_set_events_backend (pools_cerver, test_events_backend ());
		cerver_set_io_threads (pools_cerver, test_io_threads ());
		cerver_set_reusable_address_flags (pools_cerver, true);

		HttpCerver *http_cerver = (HttpCerver *) pools_cerver->cerver_data;

		pools_configure (http_cerver);

		pools_routes (http_cerver);

		if (!cerver_start (pools_cerver)) {
			pools_finish ();
		}

		else {
			cerver_log_error ("Failed to start %s!", pools_cerver->info->name);
			(void) cerver_teardown (pools_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
