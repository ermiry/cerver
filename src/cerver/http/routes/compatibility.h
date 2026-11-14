#ifndef _CERVER_HTTP_COMPATIBILITY_H_
#define _CERVER_HTTP_COMPATIBILITY_H_

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpTree;

// translates the routes of the first routes API (HttpRoute) to endpoints of the routes tree
// each method with a handler of a route, and of its children at any depth, is an endpoint
// with the settings of its route, as http_route_set_handler () shares them between methods
// a conflict (a repeated path and method, an invalid path) is only a warning:
// the first registered route is kept, so the existing services keep starting
// returns the number of endpoints that were added
CERVER_PRIVATE size_t http_compatibility_translate (
	struct _HttpTree *tree, const DoubleList *routes
);

#ifdef __cplusplus
}
#endif

#endif
