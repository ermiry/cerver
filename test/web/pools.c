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

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/limits.h>
#include <cerver/http/core/metrics.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
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

	cerver_http_all_stats_print (cerver_http_get (pools_cerver));
	(void) cerver_teardown (pools_cerver);

}

// GET /api/fast, /api/slow/job, /api/slow/fast and /old: {"thread": "<the thread that handles the request>"}
static void pools_thread_handler (CerverHttpContext *context) {

	char name[POOLS_THREAD_NAME_SIZE] = { 0 };
	(void) pthread_getname_np (pthread_self (), name, POOLS_THREAD_NAME_SIZE);

	char json[POOLS_JSON_SIZE] = { 0 };
	const int length = snprintf (json, POOLS_JSON_SIZE, "{\"thread\": \"%s\"}", name);

	(void) cerver_http_response_send_json (context, json, (size_t) length);

}

// GET /api/metrics, the metrics of the cerver
static void pools_metrics_handler (CerverHttpContext *context) {

	String *metrics = cerver_http_metrics_generate (cerver_http_context_http (context));
	if (metrics) {
		(void) cerver_http_response_send_text (context, metrics->str, metrics->len);

		str_delete (metrics);
	}

}

static void pools_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *api = cerver_http_group_create (http_cerver, "api");

	(void) cerver_http_group_get (api, "fast", pools_thread_handler);
	(void) cerver_http_group_get (api, "metrics", pools_metrics_handler);

	// the routes of the group run in the slow pool, one of them goes back to the thpool of the cerver
	CerverHttpGroup *slow = cerver_http_group_subgroup_add (api, "slow");
	cerver_http_group_set_pool (slow, pools_slow_name);

	(void) cerver_http_group_get (slow, "job", pools_thread_handler);
	cerver_http_endpoint_set_pool (cerver_http_group_get (slow, "fast", pools_thread_handler), HTTP_POOL_DEFAULT_NAME);

	// the route that was of the first routes API, in the slow pool
	CerverHttpGroup *old = cerver_http_group_create (http_cerver, "/old");
	CerverHttpEndpoint *old_endpoint = cerver_http_group_root_add (old, REQUEST_METHOD_GET, pools_thread_handler);
	cerver_http_endpoint_set_pool (old_endpoint, pools_slow_name);

}

static void pools_configure (CerverHttp *http_cerver) {

	// the client expects the routes that do not choose a pool in the thpool of the cerver
	(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

	HttpPool *slow = cerver_http_pool_create (http_cerver, pools_slow_name, POOLS_SLOW_THREADS);
	cerver_http_pool_set_queue_size (slow, POOLS_SLOW_QUEUE_SIZE);
	cerver_http_pool_set_overload_policy (slow, CERVER_OVERLOAD_POLICY_WAIT);
	cerver_http_pool_set_wait_timeout (slow, POOLS_SLOW_WAIT);

	cerver_http_set_health_route (http_cerver, pools_health_route);
	cerver_http_set_ready_route (http_cerver, pools_ready_route);

	cerver_http_set_not_found_handler (http_cerver);

	if (test_keep_alive_is_set ()) cerver_http_set_keep_alive (http_cerver, test_keep_alive ());

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
		cerver_set_events_backend (pools_cerver, test_events_backend ());
		cerver_set_io_threads (pools_cerver, test_io_threads ());
		cerver_set_reusable_address_flags (pools_cerver, true);

		CerverHttp *http_cerver = cerver_http_get (pools_cerver);

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
