/**
 * @file cerver/http/core/pool.h
 * @brief The named pools of threads of a CerverHttp, and its health and ready routes.
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
 * @brief Where the handlers of the requests of a CerverHttp run: its event loops or a pool of threads.
 *
 * Every request is handled by the event loop that read it (the pool #HTTP_POOL_LOOP_NAME), unless its endpoint or
 * one of its groups chose a pool of threads (cerver_http_group_set_pool (), cerver_http_endpoint_set_pool ()) or its
 * cerver chose one for all of them (cerver_http_set_handlers_pool ()). A handler that waits (a database, another
 * service, a file) belongs to a pool of threads: #HTTP_POOL_DEFAULT_NAME, the thpool of the cerver, or one that
 * the application creates. Each pool has its own threads, its own queue, its own overload policy and its own wait,
 * so the handlers that take long do not keep the threads that the others need: the requests of a pool that is full
 * wait or are rejected, and the ones of the other pools are handled.
 *
 * The pools are created before the cerver starts.
 *
 * The pool #HTTP_POOL_LOOP_NAME has no threads: the event loop that read a request of that pool handles it by
 * itself, and its response leaves in the same thread, so the request crosses no thread at all. It is the pool of
 * the handlers that never wait.
 *
 * The files of the static paths are always served by #HTTP_POOL_DEFAULT_NAME, whatever pool the cerver chose: an
 * event loop does not read the disk.
 * @{
 */

/**
 * @brief The name of the pool of threads that every cerver has: the thpool of the cerver. It serves the files of the
 * static paths, and the requests of the endpoints, the groups or the cerver that chose it.
 *
 * @since 3.0b-5; since the 3.0b-12 it is not the pool of the requests that did not choose one
 */
#define HTTP_POOL_DEFAULT_NAME					"default"

/**
 * @brief The name of the pool of the requests that their event loop handles by itself, without a thread of a
 * pool: every cerver has it, it can not be created, and it is the one of the requests that did not choose a pool.
 *
 * @warning A handler of this pool runs in an I/O thread: while it runs, that thread handles no other connection.
 * It must never wait (a database, another service, a lock that another request holds, a sleep): the handlers that
 * do belong to a pool of threads. What it writes is sent when it returns: the parts of a stream stay in memory
 * until then, so a response that is long or that is written over time also belongs to a pool of threads.
 * @since 3.0b-11
 */
#define HTTP_POOL_LOOP_NAME						"loop"

/**
 * @brief A pool of threads that handles the requests of the endpoints that chose it.
 *
 * @since 3.0b-5
 */
typedef struct HttpPool HttpPool;

/**
 * @brief Creates a pool of threads in an HTTP cerver; its threads start with the cerver.
 *
 * @param[in,out] http_cerver the CerverHttp, it owns the pool
 * @param[in] name the name that the endpoints use to choose it, copied; a token of up to #HTTP_POOL_NAME_SIZE - 1
 * bytes that no other pool of the cerver has ("default" is the one of the thpool of the cerver)
 * @param[in] n_threads the threads of the pool, more than 0
 * @return the pool; NULL for a NULL argument, a name that is not valid or that another pool has, no threads, a
 * cerver that already started or one that has #CERVER_HTTP_POOLS_SIZE pools
 * @since 3.0b-5
 */
CERVER_EXPORT HttpPool *cerver_http_pool_create (
	CerverHttp *http_cerver, const char *name, const unsigned int n_threads
);

/**
 * @brief Returns the pool of an HTTP cerver with a name.
 *
 * @param[in] http_cerver the CerverHttp
 * @param[in] name the name, #HTTP_POOL_DEFAULT_NAME for the default pool and #HTTP_POOL_LOOP_NAME for the one of
 * the event loops
 * @return the pool, NULL for a NULL argument or when no pool has the name
 * @since 3.0b-5
 */
CERVER_EXPORT HttpPool *cerver_http_pool_get_with_name (
	const CerverHttp *http_cerver, const char *name
);

/**
 * @brief Sets the pool of the requests whose endpoint and groups did not choose one, also the ones without an
 * endpoint (the catch all handler, the routes of the admin).
 *
 * Without this call it is #HTTP_POOL_LOOP_NAME: every handler of the cerver runs in its event loops, except the
 * ones of the endpoints and the groups that chose a pool of threads (cerver_http_endpoint_set_pool (),
 * cerver_http_group_set_pool ()). With #HTTP_POOL_DEFAULT_NAME they run in the thpool of the cerver.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] name the name of the pool; NULL for #HTTP_POOL_LOOP_NAME, the one of a cerver that set none
 * @retval 0 the pool was set
 * @retval 1 the CerverHttp is NULL or it has no pool with that name
 * @note The files of the static paths are served by #HTTP_POOL_DEFAULT_NAME whatever this pool is.
 * @warning With #HTTP_POOL_LOOP_NAME, a handler that waits stops every connection of its I/O thread while it
 * does: see #HTTP_POOL_LOOP_NAME.
 * @since 3.0b-11; since the 3.0b-12 the pool of a cerver that set none, and the one of a NULL name, is
 * #HTTP_POOL_LOOP_NAME (it was #HTTP_POOL_DEFAULT_NAME)
 */
CERVER_EXPORT u8 cerver_http_set_handlers_pool (
	CerverHttp *http_cerver, const char *name
);

/**
 * @brief Returns the name of a pool.
 *
 * @param[in] pool the pool
 * @return the name, NULL for a NULL pool
 * @since 3.0b-5
 */
CERVER_EXPORT const char *cerver_http_pool_get_name (const HttpPool *pool);

/**
 * @brief Sets the jobs that can wait in the queue of a pool for one of its threads.
 *
 * The default pool takes its size from the cerver (cerver_set_thpool_queue_size ()), this call does not change it.
 *
 * @param[in,out] pool the pool
 * @param[in] queue_size the size, rounded up to a power of 2 by the thpool; 0 uses the default of the thpool
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_pool_set_queue_size (
	HttpPool *pool, const unsigned int queue_size
);

/**
 * @brief Sets what a pool does with a request when its queue is full: CERVER_OVERLOAD_POLICY_WAIT keeps the
 * request in its event loop until a place is free or its wait ends (cerver_http_pool_set_wait_timeout ()), then it is
 * answered with 503; CERVER_OVERLOAD_POLICY_REJECT answers it with 503 at once.
 *
 * Without this call a pool uses the policy of its cerver (cerver_set_overload_policy ()).
 *
 * @param[in,out] pool the pool
 * @param[in] policy the policy, a value that is not a policy is ignored
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_pool_set_overload_policy (
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
CERVER_EXPORT void cerver_http_pool_set_wait_timeout (
	HttpPool *pool, const unsigned int seconds
);

/** @} */

/**
 * @defgroup http_health Health
 * @ingroup http_core
 * @brief The routes that tell whether an HTTP cerver is alive and whether it can take more requests.
 *
 * They are answered by the event loop, without the pools: they are answered also when every thread is busy. They
 * take GET and HEAD, they have no authentication and they are not counted in the stats of the routes.
 * @{
 */

/**
 * @brief Sets the path of the health route of an HTTP cerver: it is answered with 200 while the cerver runs.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] path the complete path, like "/api/health", copied; NULL removes the route
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_set_health_route (
	CerverHttp *http_cerver, const char *path
);

/**
 * @brief Sets the path of the ready route of an HTTP cerver: it is answered with 200 when the cerver can take a
 * request, and with 503 (with Retry-After) when one of its pools has no place for a request or has requests that
 * wait for one, or when the cerver is stopping.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] path the complete path, like "/api/ready", copied; NULL removes the route
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_set_ready_route (
	CerverHttp *http_cerver, const char *path
);

/**
 * @brief Returns whether an HTTP cerver can take a request: what its ready route answers.
 *
 * @param[in] http_cerver the CerverHttp
 * @return false for a NULL cerver, a cerver that is stopping, or one with a pool that has no place for a request or
 * that has requests that wait for one
 * @since 3.0b-5
 */
CERVER_EXPORT bool cerver_http_is_ready (const CerverHttp *http_cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
