/**
 * @file http/core/internal.h
 * @internal
 * @brief What the parts of the HttpCerver share and never export: the admin stats, the responses of the errors,
 * and the callbacks of the parser of a request.
 */

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
#include "cerver/http/parser/http_parser.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/response.h"

/** @internal @brief The size of the path of a file system of the admin stats, with its terminator. */
#define HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE				128

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpGroup;
struct _HttpRoute;

/**
 * @internal
 * @brief A file system whose stats the admin routes report.
 */
typedef struct HttpAdminFileSystemStats {

	/** @internal @brief The length of #path. */
	size_t path_len;
	/** @internal @brief The path of the file system. */
	char path[HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE];

	/** @internal @brief Its stats, read when they are requested. */
	FileSystemStats stats;

} HttpAdminFileSystemStats;

/**
 * @internal
 * @brief Allocates the stats of a file system with an empty path.
 *
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_PRIVATE HttpAdminFileSystemStats *http_admin_file_system_stats_new (void);

/**
 * @internal
 * @brief Releases the stats of a file system.
 *
 * @param[in] stats_ptr the stats, they can be NULL
 */
CERVER_PRIVATE void http_admin_file_system_stats_delete (void *stats_ptr);

/**
 * @internal
 * @brief Creates the stats of a file system.
 *
 * @param[in] path the path, truncated to HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE - 1 characters; it must not be NULL
 * @return the new stats, NULL if they could not be allocated
 */
CERVER_PRIVATE HttpAdminFileSystemStats *http_admin_file_system_stats_create (
	const char *path
);

/**
 * @internal
 * @brief Generates the json of the info of the cerver: its name, its alias, its uptime and its version.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the json, released with free (); NULL on error
 */
CERVER_PRIVATE char *http_cerver_admin_generate_info_json (
	const struct _HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Generates the json of the stats of the routes of the first routes API.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the json, released with free (); NULL on error
 */
CERVER_PRIVATE char *http_cerver_admin_generate_routes_stats_json (
	const struct _HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Generates the json of the stats of the registered file systems.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the json, released with free (); NULL on error
 */
CERVER_PRIVATE char *http_cerver_admin_generate_file_systems_stats_json (
	const struct _HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Generates the json of the states of the registered workers.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the json, released with free (); NULL on error
 */
CERVER_PRIVATE char *http_cerver_admin_generate_workers_json (
	const struct _HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Adds the admin routes as children of a top level route of the first routes API.
 *
 * @param[in] http_cerver the HttpCerver, with its admin settings
 * @param[in,out] top_level_route the route
 * @retval 0 the routes were added
 * @retval 1 a NULL route
 */
CERVER_PRIVATE u8 http_cerver_admin_init (
	const struct _HttpCerver *http_cerver,
	struct _HttpRoute *top_level_route
);

/**
 * @internal
 * @brief Adds the admin routes as endpoints of a group of the routes tree.
 *
 * @param[in] http_cerver the HttpCerver, with its admin settings
 * @param[in,out] group the group
 * @retval 0 the routes were added
 * @retval 1 a NULL group
 */
CERVER_PRIVATE u8 http_cerver_admin_group_init (
	const struct _HttpCerver *http_cerver,
	struct _HttpGroup *group
);

/** @internal @brief The response of the catch all route, created by http_cerver_init (). */
CERVER_HIDDEN HttpResponse *catch_all;

/** @internal @brief The 413 of a body, a header or a part that is too large. */
CERVER_HIDDEN HttpResponse *payload_too_large_error;

/** @internal @brief The 414 of a url that is too long. */
CERVER_HIDDEN HttpResponse *uri_too_long_error;

/** @internal @brief The 431 of headers that are too large. */
CERVER_HIDDEN HttpResponse *header_fields_too_large_error;

/** @internal @brief The 408 of a request that did not arrive in time. */
CERVER_HIDDEN HttpResponse *request_timeout_error;

/** @internal @brief The 405 of a method that the route does not have. */
CERVER_HIDDEN HttpResponse *method_not_allowed_error;

/** @internal @brief The 501 of a method that cerver does not serve. */
CERVER_HIDDEN HttpResponse *not_implemented_error;

/**
 * @internal
 * @brief Adds the admin routes once, when they are enabled: to the main route (the first route when it has none),
 * or else to the first group of the routes tree.
 *
 * @param[in,out] http_cerver the HttpCerver
 */
CERVER_HIDDEN void http_cerver_init_admin_routes (HttpCerver *http_cerver);

/**
 * @internal
 * @brief Releases a static path, the destroy of the list of the static paths.
 *
 * @param[in] http_static_path_ptr the static path, it can be NULL
 */
CERVER_HIDDEN void http_static_path_delete (void *http_static_path_ptr);

/**
 * @internal
 * @brief Compares two static paths by their path, the comparator of the list of the static paths.
 *
 * @param[in] a the first static path
 * @param[in] b the second static path
 * @return the result of strcmp () of their paths
 */
CERVER_HIDDEN int http_static_path_comparator (const void *a, const void *b);

/**
 * @internal
 * @brief Sends a response before the request was completed and marks the receive with a status, once: a receive
 * that already responded keeps its response and its status.
 *
 * A status of HTTP_RECEIVE_STATUS_LIMIT is counted in the limited requests of the HttpCerver.
 *
 * @param[in,out] http_receive the receive
 * @param[in] response the response that is sent
 * @param[in] status the status of the receive
 * @return always 1, the value that stops the parser
 */
CERVER_HIDDEN int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
);

/**
 * @internal
 * @brief Empties a buffer of the parser that is reused.
 *
 * @param[in,out] buffer the buffer, it can be NULL
 */
CERVER_HIDDEN void http_receive_buffer_reset (String *buffer);

/**
 * @internal
 * @brief Closes the url of the request that arrived in pieces, in on_headers_complete.
 *
 * @param[in,out] http_receive the receive
 * @retval 0 the request has its url, and its query when the target has one
 * @retval 1 the url or the query could not be allocated
 */
CERVER_HIDDEN u8 http_receive_close_url (HttpReceive *http_receive);

/**
 * @internal
 * @brief Saves the header whose field and value arrived in pieces, and empties both buffers.
 *
 * @param[in,out] http_receive the receive
 * @return 0 without a field, or the result of saving the header
 */
CERVER_HIDDEN int http_receive_close_header (HttpReceive *http_receive);

/**
 * @internal
 * @brief Adds the bytes of the body that arrived and checks them against the max body size of the request.
 *
 * @param[in,out] http_receive the receive
 * @param[in] length the bytes of the body that arrived
 * @return 0, or the value of the rejection with a 413 (http_receive_reject ()) when the body is too large
 */
CERVER_HIDDEN int http_receive_check_body_size (
	HttpReceive *http_receive, const size_t length
);

/**
 * @internal
 * @brief Returns whether a content type is a multipart form.
 *
 * @param[in] content the value of the Content-Type header, it must not be NULL
 * @return true if it starts with `multipart/form-data;`
 */
CERVER_HIDDEN bool is_multipart (const char *content);

/**
 * @internal
 * @brief Creates the pool of the multi parts that the HTTP cervers share.
 *
 * @return 0 on success, 1 on error
 */
CERVER_HIDDEN unsigned int http_multi_parts_init_pool (void);

/**
 * @internal
 * @brief Releases the pool of the multi parts.
 */
CERVER_HIDDEN void http_multi_parts_end_pool (void);

/**
 * @internal
 * @brief Copies the boundary of a multipart Content-Type header.
 *
 * @param[out] boundary where the boundary is copied
 * @param[in] content_type the value of the header
 * @param[in] content_type_len its length
 * @return 0 on success, 1 when the header does not have a valid boundary
 */
CERVER_HIDDEN unsigned int http_mpart_get_boundary (
	char *boundary,
	const char *content_type, const unsigned int content_type_len
);

/**
 * @internal
 * @brief Answers a request with the response of the catch all route.
 *
 * @param[in] http_receive the receive
 * @param[in] request the request
 */
CERVER_HIDDEN void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

/**
 * @internal
 * @brief Answers a request whose route was not found with a 404.
 *
 * @param[in] http_receive the receive
 * @param[in] request the request
 */
CERVER_HIDDEN void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

/**
 * @internal
 * @brief Creates the multipart parser of a request with the boundary of its Content-Type, the list of its parts,
 * and the directory of its uploads.
 *
 * @param[in,out] http_receive the receive
 * @param[in] boundary the boundary
 * @return 0 on success, 1 on error
 */
CERVER_HIDDEN int http_receive_init_mpart_parser (
	HttpReceive *http_receive,
	const char *boundary
);

/**
 * @internal
 * @brief Gives the bytes of a recv () to the parser of the receive.
 *
 * @param[in,out] http_receive the receive
 * @param[in] rc the bytes that were received
 * @param[in] packet_buffer the buffer with the bytes
 */
CERVER_HIDDEN void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
);

/**
 * @internal
 * @brief The `on_url` callback of the parser of a request: a piece of the url, closed in on_headers_complete.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_url (
	http_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_header_field` callback: a piece of the field of a header, a new field closes the previous header.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_header_field (
	http_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_header_value` callback: a piece of the value of a header.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_header_value (
	http_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_body` callback of a request that is not a multipart form.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece of the body
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_body (
	http_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_part_data_begin` callback of the multipart parser: a new part starts.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
);

/**
 * @internal
 * @brief The `on_header_field` callback of the multipart parser.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece of the field
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_header_value` callback of the multipart parser.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece of the value
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_headers_complete` callback of the multipart parser: the headers of a part ended.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_headers_completed (multipart_parser *parser);

/**
 * @internal
 * @brief The `on_part_data` callback of the multipart parser: a piece of the data of a part.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_data (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The `on_part_data_end` callback of the multipart parser: a part ended.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_data_end (multipart_parser *parser);

/**
 * @internal
 * @brief The `on_body` callback of a multipart form: it gives the body to the multipart parser.
 *
 * @param[in] parser the parser of the request, its data is the receive
 * @param[in] at the piece of the body
 * @param[in] length its length
 * @return 0 to go on, other value to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_body (
	http_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief Resets a multi part and returns it to the pool, the destroy of the list of the parts of a request.
 *
 * @param[in] multi_part_ptr the multi part, it can be NULL
 */
CERVER_HIDDEN void http_multi_parts_return (void *multi_part_ptr);

/**
 * @internal
 * @brief Compiles the header of a response with the default headers of an HttpCerver, the one of its request.
 *
 * http_response_compile_header () compiles it without them: the cerver that sends it adds its own ones.
 *
 * @param[in,out] response the response, it must not be NULL
 * @param[in] http_cerver the cerver of the request; NULL for none of the default headers
 */
CERVER_HIDDEN void http_response_compile_header_with_cerver (
	HttpResponse *response, const HttpCerver *http_cerver
);

/**
 * @internal
 * @brief Works like http_response_compile (), with the default headers of an HttpCerver, the one of its request.
 *
 * @param[in,out] response the response, it can be NULL
 * @param[in] http_cerver the cerver of the request; NULL for none of the default headers
 * @retval 0 the response was compiled
 * @retval 1 a NULL response or its buffer could not be allocated
 */
CERVER_HIDDEN u8 http_response_compile_with_cerver (
	HttpResponse *response, const HttpCerver *http_cerver
);

#ifdef __cplusplus
}
#endif

#endif
