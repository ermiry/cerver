#ifndef _CERVER_HTTP_CORE_INTERNAL_H_
#define _CERVER_HTTP_CORE_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include <sys/types.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/system/system.h"

#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/routes/tree.h"

#define HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE				128

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpGroup;
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

// the admin routes as endpoints of a group of the routes tree
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_cerver_admin_group_init (
	const struct _HttpCerver *http_cerver,
	struct _HttpGroup *group
);

// creates a custom header with an empty value in the arena
CERVER_PRIVATE HttpCustomHeader *http_custom_header_create (
	struct Arena *arena, const char *header, const size_t header_len
);

// sets the value of the custom header, a new string in the arena
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int http_custom_header_set_value (
	struct Arena *arena, HttpCustomHeader *custom_header,
	const char *value, const size_t value_len
);

// the responses that the parts of the HTTP cerver send, created by the first HTTP cerver
CERVER_HIDDEN HttpResponse *catch_all;

CERVER_HIDDEN HttpResponse *payload_too_large_error;
CERVER_HIDDEN HttpResponse *uri_too_long_error;
CERVER_HIDDEN HttpResponse *header_fields_too_large_error;
CERVER_HIDDEN HttpResponse *request_timeout_error;
CERVER_HIDDEN HttpResponse *method_not_allowed_error;
CERVER_HIDDEN HttpResponse *not_implemented_error;
CERVER_HIDDEN HttpResponse *http_version_not_supported_error;

CERVER_HIDDEN HttpResponse *upgrade_required_error;
CERVER_HIDDEN HttpResponse *forbidden_error;
CERVER_HIDDEN HttpResponse *web_socket_unavailable_error;

// keeps the string's memory but empties its contents
static inline void http_receive_buffer_reset (String *buffer) {

	if (buffer) {
		buffer->len = 0;
		if (buffer->str) buffer->str[0] = '\0';
	}

}

// the value that a parser callback returns when it rejected the request
// or when it found that its client is gone, the parser stops with it
static inline int http_receive_parser_result (const int rejected) {

	return rejected ? HTTP_RECEIVE_PARSER_STOP : 0;

}

// the methods that the parts of the HTTP cerver share, never exported

CERVER_HIDDEN void http_cerver_init_admin_routes (HttpCerver *http_cerver);

CERVER_HIDDEN void http_static_path_delete (void *http_static_path_ptr);

CERVER_HIDDEN int http_static_path_comparator (const void *a, const void *b);

CERVER_HIDDEN int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
);

CERVER_HIDDEN int http_receive_check_body_size (
	HttpReceive *http_receive, const size_t length
);

CERVER_HIDDEN int http_receive_handle_discarded (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_url (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_url_complete (llhttp_t *parser);

CERVER_HIDDEN int http_receive_handle_protocol (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_protocol_complete (llhttp_t *parser);

CERVER_HIDDEN int http_receive_handle_version_complete (llhttp_t *parser);

CERVER_HIDDEN int http_receive_handle_header_field (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_header_value (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_header_value_complete (llhttp_t *parser);

CERVER_HIDDEN int http_receive_handle_body (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN bool is_multipart (const char *content);

CERVER_HIDDEN unsigned int http_multi_parts_init_pool (void);

CERVER_HIDDEN void http_multi_parts_end_pool (void);

CERVER_HIDDEN void http_multi_parts_return (void *multi_part_ptr);

CERVER_HIDDEN unsigned int http_mpart_get_boundary (
	char *boundary,
	const char *content_type, const unsigned int content_type_len
);

CERVER_HIDDEN int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
);

CERVER_HIDDEN int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_mpart_headers_completed (
	multipart_parser *parser
);

CERVER_HIDDEN int http_receive_handle_mpart_data (
	multipart_parser *parser, const char *at, size_t length
);

CERVER_HIDDEN int http_receive_handle_mpart_data_end (multipart_parser *parser);

CERVER_HIDDEN int http_receive_handle_mpart_body (
	llhttp_t *parser, const char *at, size_t length
);

CERVER_HIDDEN void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

CERVER_HIDDEN void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

CERVER_HIDDEN int http_receive_init_mpart_parser (
	HttpReceive *http_receive, const char *boundary
);

CERVER_HIDDEN void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
);

CERVER_HIDDEN void http_receive_close (
	HttpReceive *http_receive, const bool rejected
);

CERVER_HIDDEN bool http_endpoint_is_web_socket (const HttpEndpoint *endpoint);

CERVER_HIDDEN void http_receive_web_socket_release (HttpReceive *http_receive);

CERVER_HIDDEN int http_receive_handle_headers_web_socket (
	HttpReceive *http_receive, const llhttp_t *parser
);

CERVER_HIDDEN void http_receive_web_socket_pending (
	HttpReceive *http_receive, const char *unparsed
);

CERVER_HIDDEN void http_receive_web_socket_upgrade (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
);

#ifdef __cplusplus
}
#endif

#endif
