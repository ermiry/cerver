/**
 * @file http/routes/compatibility.h
 * @internal
 * @brief The translation of the first routes API (HttpRoute) to the routes tree, never exported by libcerver.so.
 */

#ifndef _CERVER_HTTP_COMPATIBILITY_H_
#define _CERVER_HTTP_COMPATIBILITY_H_

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpTree;

/**
 * @internal
 * @brief Translates the routes of the first routes API (HttpRoute) to endpoints of the routes tree.
 *
 * Each method with a handler of a route, and of its children at any depth, is an endpoint with the settings of its
 * route, as http_route_set_handler () shares them between methods. A conflict (a repeated path and method, an
 * invalid path) is only a warning: the first registered route is kept, so the existing services keep starting.
 *
 * @param[in,out] tree the routes tree
 * @param[in] routes the routes
 * @return the number of endpoints that were added
 */
CERVER_PRIVATE size_t http_compatibility_translate (
	struct _HttpTree *tree, const DoubleList *routes
);

#ifdef __cplusplus
}
#endif

#endif
