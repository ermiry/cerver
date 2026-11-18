/**
 * @file cerver/http/core/stats.h
 * @brief The stats of the routes of a CerverHttp, printed with the log.
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
 * @brief The stats of the endpoints and of the requests of a CerverHttp.
 * @{
 */

/**
 * @brief Prints the number of top level groups and of handlers (endpoints) of the routes tree.
 *
 * @param[in] http_cerver the CerverHttp, NULL prints nothing
 */
CERVER_PUBLIC void cerver_http_routes_stats_print (
	const CerverHttp *http_cerver
);

/**
 * @brief Prints the stats of the requests of an endpoint, and of its uploads when it takes multipart requests.
 *
 * @param[in] endpoint the endpoint, NULL prints nothing
 * @since 3.0b-12 it replaces cerver_http_route_stats_print (), of the first routes API.
 */
CERVER_PUBLIC void cerver_http_endpoint_stats_print (
	const struct _HttpEndpoint *endpoint
);

/**
 * @brief Prints every HTTP stat of the cerver: of each endpoint of its groups and of its requests.
 *
 * @param[in] http_cerver the CerverHttp, NULL prints nothing
 */
CERVER_PUBLIC void cerver_http_all_stats_print (
	const CerverHttp *http_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
