/**
 * @file cerver/http/core/pool.h
 * @brief The named pools of threads of an HttpCerver, and its health and ready routes.
 */

#ifndef _CERVER_HTTP_CORE_POOL_H_
#define _CERVER_HTTP_CORE_POOL_H_

#include <stdbool.h>

#include "cerver/config.h"

#include "cerver/core/types.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_pools Pools
 * @ingroup http_core
 * @brief The pools of threads that handle the requests of an HttpCerver with CERVER_HANDLER_TYPE_EVENTS.
 *
 * Every request is handled by the threads of the default pool, the thpool of its cerver, unless its endpoint or one
 * of its groups chose another one (http_group_set_pool (), http_endpoint_set_pool (), http_route_set_pool ()). Each
 * pool has its own threads, its own queue, its own overload policy and its own wait, so the handlers that take long
 * do not keep the threads that the fast ones need: the requests of a pool that is full wait or are rejected, and the
 * ones of the other pools are handled.
 *
 * The pools are created before the cerver starts. With another handler type the named pools are not started and
 * every request is handled like before; a warning is logged when the cerver starts.
 * @{
 */

/**
 * @brief The name of the pool of the requests that did not choose one: the thpool of the cerver.
 *
 * @since 3.0b-5
 */
#define HTTP_POOL_DEFAULT_NAME					"default"

/**
 * @brief A pool of threads that handles the requests of the endpoints that chose it.
 *
 * @since 3.0b-5
 */
typedef struct HttpPool HttpPool;

/**
 * @brief Creates a pool of threads in an HTTP cerver; its threads start with the cerver.
 *
 * @param[in,out] http_cerver the HttpCerver, it owns the pool
 * @param[in] name the name that the endpoints use to choose it, copied; a token of up to #HTTP_POOL_NAME_SIZE - 1
 * bytes that no other pool of the cerver has ("default" is the one of the thpool of the cerver)
 * @param[in] n_threads the threads of the pool, more than 0
 * @return the pool; NULL for a NULL argument, a name that is not valid or that another pool has, no threads, a
 * cerver that already started or one that has #HTTP_CERVER_POOLS_SIZE pools
 * @since 3.0b-5
 */
CERVER_EXPORT HttpPool *http_cerver_pool_create (
	HttpCerver *http_cerver, const char *name, const unsigned int n_threads
);

/**
 * @brief Returns the pool of an HTTP cerver with a name.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in] name the name, #HTTP_POOL_DEFAULT_NAME for the default pool
 * @return the pool, NULL for a NULL argument or when no pool has the name
 * @since 3.0b-5
 */
CERVER_EXPORT HttpPool *http_cerver_pool_get_with_name (
	const HttpCerver *http_cerver, const char *name
);

/**
 * @brief Returns the name of a pool.
 *
 * @param[in] pool the pool
 * @return the name, NULL for a NULL pool
 * @since 3.0b-5
 */
CERVER_EXPORT const char *http_pool_get_name (const HttpPool *pool);

/**
 * @brief Sets the jobs that can wait in the queue of a pool for one of its threads.
 *
 * The default pool takes its size from the cerver (cerver_set_thpool_queue_size ()), this call does not change it.
 *
 * @param[in,out] pool the pool
 * @param[in] queue_size the size, rounded up to a power of 2 by the thpool; 0 uses the default of the thpool
 * @since 3.0b-5
 */
CERVER_EXPORT void http_pool_set_queue_size (
	HttpPool *pool, const unsigned int queue_size
);

/**
 * @brief Sets what a pool does with a request when its queue is full: CERVER_OVERLOAD_POLICY_WAIT keeps the
 * request in its event loop until a place is free or its wait ends (http_pool_set_wait_timeout ()), then it is
 * answered with 503; CERVER_OVERLOAD_POLICY_REJECT answers it with 503 at once.
 *
 * Without this call a pool uses the policy of its cerver (cerver_set_overload_policy ()).
 *
 * @param[in,out] pool the pool
 * @param[in] policy the policy, a value that is not a policy is ignored
 * @since 3.0b-5
 */
CERVER_EXPORT void http_pool_set_overload_policy (
	HttpPool *pool, const CerverOverloadPolicy policy
);

/**
 * @brief Sets the seconds that a request waits for a place in a pool that is full, with
 * CERVER_OVERLOAD_POLICY_WAIT; then it is answered with 503.
 *
 * Without this call a pool uses the wait of its cerver (cerver_set_overload_wait_timeout ()).
 *
 * @param[in,out] pool the pool
 * @param[in] seconds the seconds, with 0 a request does not wait
 * @since 3.0b-5
 */
CERVER_EXPORT void http_pool_set_wait_timeout (
	HttpPool *pool, const unsigned int seconds
);

/** @} */

/**
 * @defgroup http_health Health
 * @ingroup http_core
 * @brief The routes that tell whether an HTTP cerver is alive and whether it can take more requests.
 *
 * With CERVER_HANDLER_TYPE_EVENTS they are answered by the event loop, without the pools: they are answered also
 * when every thread is busy. With another handler type they are answered by the thread that has the connection, so
 * they wait for a thread like any request; a warning is logged when the cerver starts. They take GET and HEAD,
 * they have no authentication and they are not counted in the stats of the routes.
 * @{
 */

/**
 * @brief Sets the path of the health route of an HTTP cerver: it is answered with 200 while the cerver runs.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] path the complete path, like "/api/health", copied; NULL removes the route
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_health_route (
	HttpCerver *http_cerver, const char *path
);

/**
 * @brief Sets the path of the ready route of an HTTP cerver: it is answered with 200 when the cerver can take a
 * request, and with 503 (with Retry-After) when one of its pools has no place for a request or has requests that
 * wait for one, or when the cerver is stopping.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] path the complete path, like "/api/ready", copied; NULL removes the route
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_ready_route (
	HttpCerver *http_cerver, const char *path
);

/**
 * @brief Returns whether an HTTP cerver can take a request: what its ready route answers.
 *
 * @param[in] http_cerver the HttpCerver
 * @return false for a NULL cerver, a cerver that is stopping, or one with a pool that has no place for a request or
 * that has requests that wait for one
 * @since 3.0b-5
 */
CERVER_EXPORT bool http_cerver_is_ready (const HttpCerver *http_cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
