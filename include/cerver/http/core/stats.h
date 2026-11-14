#ifndef _CERVER_HTTP_CORE_STATS_H_
#define _CERVER_HTTP_CORE_STATS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region stats

CERVER_PRIVATE size_t http_cerver_stats_get_children_routes (
	const HttpCerver *http_cerver, size_t *handlers
); 

// print number of routes & handlers
CERVER_PUBLIC void http_cerver_routes_stats_print (
	const HttpCerver *http_cerver
);

// print route's stats
CERVER_PUBLIC void http_cerver_route_stats_print (
	const HttpRoute *route
);

// prints all HTTP cerver stats, general & by route
CERVER_PUBLIC void http_cerver_all_stats_print (
	const HttpCerver *http_cerver
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
