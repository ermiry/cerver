#ifndef _CERVER_HTTP_ADMIN_H_
#define _CERVER_HTTP_ADMIN_H_

#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"
#include "cerver/system.h"

#define HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE				128

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpRoute;

typedef struct HttpAdminFileSystemStats {

	size_t path_len;
	char path[HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE];

	FileSystemStats stats;

} HttpAdminFileSystemStats;

CERVER_PRIVATE HttpAdminFileSystemStats *http_admin_file_system_stats_new (void);

CERVER_PRIVATE void http_admin_file_system_stats_delete (void *stats_ptr);

CERVER_PRIVATE HttpAdminFileSystemStats *http_admin_file_system_stats_create (
	const char *path
);

CERVER_PRIVATE char *http_cerver_admin_generate_info_json (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE char *http_cerver_admin_generate_routes_stats_json (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE char *http_cerver_admin_generate_file_systems_stats_json (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE char *http_cerver_admin_generate_workers_json (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE u8 http_cerver_admin_init (
	const struct _HttpCerver *http_cerver,
	struct _HttpRoute *top_level_route
);

#ifdef __cplusplus
}
#endif

#endif