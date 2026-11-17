#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/core/internal.h"
#include "cerver/core/types.h"
#include "cerver/events/internal.h"

#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/pool.h"
#include "cerver/http/headers/fields.h"

#include "cerver/threads/thpool.h"

#include "cerver/utils/log.h"

// the position of the default pool in the pools of its cerver
#define HTTP_POOL_DEFAULT_INDEX				0

// the least seconds that the drain of a pool gets when the time of the others ended:
// 0 would wait until every job has finished
#define HTTP_POOL_DRAIN_TIMEOUT_MINIMUM		1

#pragma region pool

static bool http_pool_name_is_valid (const char *name) {

	const size_t length = strlen (name);

	return (length < HTTP_POOL_NAME_SIZE) && http_header_name_is_valid (name, length);

}

static HttpPool *http_pool_new (
	HttpCerver *http_cerver, const char *name, const unsigned int n_threads
) {

	HttpPool *pool = (HttpPool *) calloc (1, sizeof (HttpPool));
	if (pool) {
		(void) snprintf (pool->name, HTTP_POOL_NAME_SIZE, "%s", name);
		pool->index = http_cerver->n_pools;

		pool->http_cerver = http_cerver;

		pool->n_threads = n_threads;

		pool->overload_policy = CERVER_DEFAULT_OVERLOAD_POLICY;
		pool->wait_timeout = CERVER_DEFAULT_OVERLOAD_WAIT_TIMEOUT;
	}

	return pool;

}

// adds a pool at the end of the ones of its cerver
static HttpPool *http_cerver_pool_add (
	HttpCerver *http_cerver, const char *name, const unsigned int n_threads
) {

	HttpPool *pool = http_pool_new (http_cerver, name, n_threads);
	if (pool) {
		http_cerver->pools[http_cerver->n_pools] = pool;
		http_cerver->n_pools += 1;
	}

	return pool;

}

static bool http_cerver_pool_can_be_created (
	const HttpCerver *http_cerver, const char *name, const unsigned int n_threads
) {

	return http_cerver && name && n_threads
		&& !http_cerver->pools_started
		&& (http_cerver->n_pools < HTTP_CERVER_POOLS_SIZE)
		&& http_pool_name_is_valid (name)
		&& !http_cerver_pool_get_with_name (http_cerver, name);

}

HttpPool *http_cerver_pool_create (
	HttpCerver *http_cerver, const char *name, const unsigned int n_threads
) {

	HttpPool *pool = NULL;

	if (http_cerver_pool_can_be_created (http_cerver, name, n_threads)) {
		pool = http_cerver_pool_add (http_cerver, name, n_threads);
	}

	else {
		cerver_log_error (
			"http_cerver_pool_create () - the pool \"%s\" of %u threads can not be created",
			name ? name : "", n_threads
		);
	}

	return pool;

}

HttpPool *http_cerver_pool_get_with_name (
	const HttpCerver *http_cerver, const char *name
) {

	HttpPool *pool = NULL;

	if (http_cerver && name) {
		for (unsigned int idx = 0; (idx < http_cerver->n_pools) && !pool; idx++) {
			if (!strcmp (http_cerver->pools[idx]->name, name)) pool = http_cerver->pools[idx];
		}
	}

	return pool;

}

const char *http_pool_get_name (const HttpPool *pool) {

	return pool ? pool->name : NULL;

}

void http_pool_set_queue_size (
	HttpPool *pool, const unsigned int queue_size
) {

	if (pool) pool->queue_size = queue_size;

}

static bool http_pool_overload_policy_is_valid (const CerverOverloadPolicy policy) {

	return (policy > CERVER_OVERLOAD_POLICY_NONE) && (policy < CERVER_OVERLOAD_POLICY_MAX);

}

void http_pool_set_overload_policy (
	HttpPool *pool, const CerverOverloadPolicy policy
) {

	if (pool && http_pool_overload_policy_is_valid (policy)) {
		pool->overload_policy = policy;
		pool->policy_configured = true;
	}

}

void http_pool_set_wait_timeout (
	HttpPool *pool, const unsigned int seconds
) {

	if (pool) {
		pool->wait_timeout = seconds;
		pool->wait_configured = true;
	}

}

CerverOverloadPolicy http_pool_overload_policy (
	const struct _Cerver *cerver, const struct HttpPool *pool
) {

	return (pool && pool->policy_configured) ? pool->overload_policy : cerver->overload_policy;

}

unsigned int http_pool_wait_timeout (
	const struct _Cerver *cerver, const struct HttpPool *pool
) {

	return (pool && pool->wait_configured) ? pool->wait_timeout : cerver->overload_wait_timeout;

}

void http_pool_parked_count (struct HttpPool *pool, const u64 milliseconds) {

	if (pool) {
		const u64 microseconds = milliseconds * THPOOL_MICROSECONDS_PER_MILLISECOND;

		(void) __atomic_add_fetch (
			&pool->parked_histogram[thpool_histogram_bucket (microseconds)], 1, __ATOMIC_RELAXED
		);

		(void) __atomic_add_fetch (&pool->parked_total_microseconds, microseconds, __ATOMIC_RELAXED);
	}

}

#pragma endregion

#pragma region lifecycle

unsigned int http_cerver_pools_create (struct _HttpCerver *http_cerver) {

	return http_cerver_pool_add (http_cerver, HTTP_POOL_DEFAULT_NAME, 0) ? 0 : 1;

}

void http_cerver_pools_delete (struct _HttpCerver *http_cerver) {

	// a cerver that was not ended, like the one of a test that only was initialized
	http_cerver_pools_end (http_cerver);

	for (unsigned int idx = 0; idx < http_cerver->n_pools; idx++) {
		free (http_cerver->pools[idx]);
		http_cerver->pools[idx] = NULL;
	}

	http_cerver->n_pools = 0;

}

// the thpool of a named pool, with its own threads and its own queue
// returns 0 on success, 1 on error
static unsigned int http_pool_start (HttpPool *pool) {

	unsigned int errors = 1;

	Thpool *thpool = thpool_create (pool->n_threads);
	if (thpool) {
		thpool_set_name (thpool, pool->name);

		errors = pool->queue_size ? thpool_set_queue_size (thpool, pool->queue_size) : 0;

		errors |= thpool_init (thpool);
	}

	// the threads that started end with the thpool
	if (errors) {
		thpool_destroy (thpool);
	}

	else {
		pool->thpool = thpool;
		pool->owns_thpool = true;
	}

	return errors;

}

static void http_pool_start_log (const HttpPool *pool) {

	const Cerver *cerver = pool->http_cerver->cerver;

	cerver_log_msg (
		"Cerver %s pool %s: %u threads - %u queue size - %s overload policy",
		cerver->info->name, pool->name,
		pool->n_threads, thpool_get_queue_size (pool->thpool),
		cerver_overload_policy_to_string (http_pool_overload_policy (cerver, pool))
	);

}

// returns 0 on success, 1 on error
static unsigned int http_cerver_pools_start_named (HttpCerver *http_cerver) {

	unsigned int errors = 0;

	for (unsigned int idx = HTTP_POOL_DEFAULT_INDEX + 1; (idx < http_cerver->n_pools) && !errors; idx++) {
		HttpPool *pool = http_cerver->pools[idx];

		// a cerver that is initialized again keeps the threads that it has
		if (!pool->owns_thpool) errors = http_pool_start (pool);

		if (!errors) http_pool_start_log (pool);
	}

	if (errors) {
		cerver_log_error (
			"Failed to init the pools of cerver %s!", http_cerver->cerver->info->name
		);
	}

	return errors;

}

// only an event loop gives each request to the pool of its endpoint:
// with the other handler types the thread that has a connection handles its requests
static void http_cerver_pools_unused (const HttpCerver *http_cerver) {

	if (http_cerver->n_pools > (HTTP_POOL_DEFAULT_INDEX + 1)) {
		cerver_log_warning (
			"Cerver %s does not start its %u named pools with the %s handler type: "
			"every request is handled by the thread of its connection",
			http_cerver->cerver->info->name,
			http_cerver->n_pools - (HTTP_POOL_DEFAULT_INDEX + 1),
			cerver_handler_type_to_string (http_cerver->cerver->handler_type)
		);
	}

}

// the default pool is the thpool that the cerver already started
static void http_cerver_pools_start_default (HttpCerver *http_cerver) {

	if (http_cerver->n_pools) {
		HttpPool *pool = http_cerver->pools[HTTP_POOL_DEFAULT_INDEX];

		pool->thpool = http_cerver->cerver->thpool;
		pool->n_threads = http_cerver->cerver->n_thpool_threads;
	}

}

unsigned int http_cerver_pools_start (struct _HttpCerver *http_cerver) {

	unsigned int errors = 0;

	http_cerver->pools_started = true;

	http_cerver_pools_start_default (http_cerver);

	if (http_cerver->cerver->handler_type == CERVER_HANDLER_TYPE_EVENTS) {
		errors = http_cerver_pools_start_named (http_cerver);
	}

	else {
		http_cerver_pools_unused (http_cerver);
	}

	return errors;

}

// what is left of the time that the pools share
static time_t http_cerver_pools_drain_timeout (const time_t timeout, const time_t deadline) {

	time_t left = timeout;

	if (timeout != THPOOL_DRAIN_TIMEOUT_NONE) {
		left = deadline - time (NULL);
		if (left < HTTP_POOL_DRAIN_TIMEOUT_MINIMUM) left = HTTP_POOL_DRAIN_TIMEOUT_MINIMUM;
	}

	return left;

}

unsigned int http_cerver_pools_drain (
	struct _HttpCerver *http_cerver, const bool run_queued, const time_t timeout
) {

	unsigned int working = 0;

	const time_t deadline = time (NULL) + timeout;

	for (unsigned int idx = HTTP_POOL_DEFAULT_INDEX + 1; idx < http_cerver->n_pools; idx++) {
		const HttpPool *pool = http_cerver->pools[idx];

		if (
			pool->owns_thpool
			&& thpool_drain (pool->thpool, run_queued, http_cerver_pools_drain_timeout (timeout, deadline))
		) {
			working += 1;
		}
	}

	return working;

}

void http_cerver_pools_end (struct _HttpCerver *http_cerver) {

	for (unsigned int idx = HTTP_POOL_DEFAULT_INDEX + 1; idx < http_cerver->n_pools; idx++) {
		HttpPool *pool = http_cerver->pools[idx];

		if (pool->owns_thpool) thpool_destroy (pool->thpool);

		pool->thpool = NULL;
		pool->owns_thpool = false;
	}

	// the cerver releases its thpool
	if (http_cerver->n_pools) http_cerver->pools[HTTP_POOL_DEFAULT_INDEX]->thpool = NULL;

}

unsigned int http_cerver_pools_working (const struct _HttpCerver *http_cerver) {

	unsigned int working = 0;

	for (unsigned int idx = HTTP_POOL_DEFAULT_INDEX + 1; idx < http_cerver->n_pools; idx++) {
		const HttpPool *pool = http_cerver->pools[idx];

		if (pool->owns_thpool) working += thpool_get_num_threads_working (pool->thpool);
	}

	return working;

}

unsigned int http_cerver_pools_queued (const struct _HttpCerver *http_cerver) {

	unsigned int queued = 0;

	for (unsigned int idx = HTTP_POOL_DEFAULT_INDEX + 1; idx < http_cerver->n_pools; idx++) {
		const HttpPool *pool = http_cerver->pools[idx];

		if (pool->owns_thpool) queued += thpool_get_queued (pool->thpool);
	}

	return queued;

}

#pragma endregion

#pragma region health

// the path of a route that the cerver answers by itself
static void http_cerver_set_own_route (String **route, const char *path) {

	str_delete (*route);

	*route = path ? str_new (path) : NULL;

}

void http_cerver_set_health_route (
	HttpCerver *http_cerver, const char *path
) {

	if (http_cerver) http_cerver_set_own_route (&http_cerver->health_route, path);

}

void http_cerver_set_ready_route (
	HttpCerver *http_cerver, const char *path
) {

	if (http_cerver) http_cerver_set_own_route (&http_cerver->ready_route, path);

}

// a pool can take a request when its queue has a place and no request waits for one
// a pool without threads (a named one without an event loop) takes none: it is not counted
static bool http_pool_is_ready (const HttpPool *pool) {

	return !pool->thpool || (
		!thpool_is_full (pool->thpool)
		&& !event_loop_group_lane_parked (pool->http_cerver->cerver, pool->index)
	);

}

bool http_cerver_is_ready (const HttpCerver *http_cerver) {

	bool ready = false;

	if (http_cerver && !cerver_is_stopping (http_cerver->cerver)) {
		ready = true;

		for (unsigned int idx = 0; (idx < http_cerver->n_pools) && ready; idx++) {
			ready = http_pool_is_ready (http_cerver->pools[idx]);
		}
	}

	return ready;

}

#pragma endregion
