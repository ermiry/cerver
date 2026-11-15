/**
 * @file cerver/http/core/stats.h
 * @brief The stats of the routes of an HttpCerver, printed with the log.
 */

#ifndef _CERVER_HTTP_CORE_STATS_H_
#define _CERVER_HTTP_CORE_STATS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_stats Stats
 * @ingroup http_core
 * @brief The stats of the routes of the first routes API and of the requests of an HttpCerver.
 * @{
 */

/**
 * @brief Counts the children routes and the handlers of the top level routes of the first routes API.
 *
 * @param[in] http_cerver the HttpCerver
 * @param[in,out] handlers the handlers of the top level routes and of their children are added to it
 * @return the children routes
 * @note For the internal methods of cerver. The endpoints of the routes tree are not counted.
 */
CERVER_PRIVATE size_t http_cerver_stats_get_children_routes (
	const HttpCerver *http_cerver, size_t *handlers
);

/**
 * @brief Prints the number of routes, of children routes and of handlers of the first routes API.
 *
 * @param[in] http_cerver the HttpCerver, NULL prints nothing
 */
CERVER_PUBLIC void http_cerver_routes_stats_print (
	const HttpCerver *http_cerver
);

/**
 * @brief Prints the stats of each handler of a route, of its uploads and of its children.
 *
 * @param[in] route the route, NULL prints nothing
 */
CERVER_PUBLIC void http_cerver_route_stats_print (
	const HttpRoute *route
);

/**
 * @brief Prints every HTTP stat of the cerver: of each route of the first routes API and of its requests.
 *
 * @param[in] http_cerver the HttpCerver, NULL prints nothing
 */
CERVER_PUBLIC void http_cerver_all_stats_print (
	const HttpCerver *http_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
