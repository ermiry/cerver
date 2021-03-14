#ifndef _CERVER_HTTP_ADMIN_H_
#define _CERVER_HTTP_ADMIN_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpRoute;

CERVER_PRIVATE char *http_cerver_admin_generate_routes_stats_json (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE u8 http_cerver_admin_init (
	struct _HttpRoute *top_level_route
);

#ifdef __cplusplus
}
#endif

#endif