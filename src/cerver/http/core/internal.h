/**
 * @file http/core/internal.h
 * @internal
 * @brief What the parts of the HttpCerver share and never export: the admin stats, the custom headers, the
 * responses of the errors, and the callbacks of the parsers of a request.
 */

#ifndef _CERVER_HTTP_CORE_INTERNAL_H_
#define _CERVER_HTTP_CORE_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <sys/types.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/types.h"
#include "cerver/system/system.h"

#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/pool.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/routes/tree.h"

#include "cerver/threads/thpool.h"

/** @internal @brief The size of the path of a file system of the admin stats, with its terminator. */
#define HTTP_ADMIN_FILE_SYSTEM_STATS_PATH_SIZE				128

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _HttpCerver;
struct _HttpGroup;
struct _HttpRoute;

/**
 * @internal
 * @brief A pool of threads of an HTTP cerver (http/core/pool.h).
 *
 * The default pool, the first one of its cerver, is the thpool of the cerver: it takes its threads, its queue, its
 * policy and its wait from the cerver, and the cerver starts it and releases it.
 */
struct HttpPool {

	/** @internal @brief The name of the pool. */
	char name[HTTP_POOL_NAME_SIZE];
	/** @internal @brief Its position in the pools of its cerver, 0 for the default one. */
	unsigned int index;

	/** @internal @brief The HTTP cerver of the pool. */
	struct _HttpCerver *http_cerver;

	/** @internal @brief The threads of the pool. */
	unsigned int n_threads;
	/** @internal @brief The size of its queue, 0 for the default of the thpool. */
	unsigned int queue_size;

	/** @internal @brief The policy of the pool was set, without it the pool uses the one of its cerver. */
	bool policy_configured;
	/** @internal @brief What the pool does with a request when its queue is full. */
	CerverOverloadPolicy overload_policy;

	/** @internal @brief The wait of the pool was set, without it the pool uses the one of its cerver. */
	bool wait_configured;
	/** @internal @brief The seconds that a request waits for a place in the pool. */
	unsigned int wait_timeout;

	/** @internal @brief The thpool: the one of the cerver for the default pool, its own for the others; NULL until the cerver starts. */
	Thpool *thpool;
	/** @internal @brief The pool created its thpool and releases it. */
	bool owns_thpool;

	/** @internal @brief Every request that waited in its event loop for a place in the pool; atomic. */
	u64 n_parked;
	/** @internal @brief Every request that was answered with 503 because the pool had no place for it, at once or when its wait ended; atomic. */
	u64 n_unavailable;

	/**
	 * @internal
	 * @brief The requests that stopped waiting in their event loop for a place in the pool (they got it, their wait
	 * ended, or the cerver stopped) by the time that they waited, in buckets of up to 2^i ms like the histograms of
	 * a thpool; atomic.
	 *
	 * @since 2.1b-19
	 */
	u64 parked_histogram[THPOOL_HISTOGRAM_BUCKETS];
	/** @internal @brief The microseconds that those requests waited; atomic. */
	u64 parked_total_microseconds;

};

/**
 * @internal
 * @brief Creates the default pool of an HTTP cerver, when it is created.
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @return 0 on success, 1 on error
 */
CERVER_HIDDEN unsigned int http_cerver_pools_create (struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Releases the pools of an HTTP cerver, their threads must have ended.
 *
 * @param[in,out] http_cerver the HTTP cerver
 */
CERVER_HIDDEN void http_cerver_pools_delete (struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Starts the pools of an HTTP cerver when it starts: the default one takes the thpool of the cerver, and
 * the others create their own with CERVER_HANDLER_TYPE_EVENTS; with another handler type they are not started and
 * a warning is logged.
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @return 0 on success, 1 when the thpool of a pool could not be started
 */
CERVER_HIDDEN unsigned int http_cerver_pools_start (struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Drains the named pools of an HTTP cerver, like the cerver drains its thpool (thpool_drain ()); the pools
 * share the timeout.
 *
 * @param[in,out] http_cerver the HTTP cerver
 * @param[in] run_queued true to run the jobs that wait in their queues, false to drop them
 * @param[in] timeout the seconds for all the pools, #THPOOL_DRAIN_TIMEOUT_NONE waits until every job has finished
 * @return the pools that still had threads working when the time ended, 0 when every pool was drained
 */
CERVER_HIDDEN unsigned int http_cerver_pools_drain (
	struct _HttpCerver *http_cerver, const bool run_queued, const time_t timeout
);

/**
 * @internal
 * @brief Ends the threads of the named pools of an HTTP cerver and releases their thpools, after they were drained.
 *
 * @param[in,out] http_cerver the HTTP cerver
 */
CERVER_HIDDEN void http_cerver_pools_end (struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Returns the threads of the named pools of an HTTP cerver that are running a job.
 *
 * @param[in] http_cerver the HTTP cerver
 * @return the threads
 */
CERVER_HIDDEN unsigned int http_cerver_pools_working (const struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Returns the jobs that wait in the queues of the named pools of an HTTP cerver.
 *
 * @param[in] http_cerver the HTTP cerver
 * @return the jobs
 */
CERVER_HIDDEN unsigned int http_cerver_pools_queued (const struct _HttpCerver *http_cerver);

/**
 * @internal
 * @brief Counts a connection that ended by the requests that it handled (HttpCerver::connection_requests).
 *
 * The owner of the connection calls it once, when the connection ends: its event loop or the thread that had it.
 * A connection of a thread that goes back to the thpool to wait for its next request has not ended.
 *
 * @param[in,out] http_cerver the HTTP cerver, NULL does nothing
 * @param[in] n_requests the requests that the connection handled, a connection without one is not counted
 * @threadsafety Any thread, the counters are atomic.
 */
CERVER_HIDDEN void http_cerver_connection_ended (struct _HttpCerver *http_cerver, const size_t n_requests);

/**
 * @internal
 * @brief Returns what a pool does when its queue is full: its own policy, or the one of the cerver.
 *
 * @param[in] cerver the cerver
 * @param[in] pool the pool, NULL for the default one
 * @return the policy
 */
CERVER_PRIVATE CerverOverloadPolicy http_pool_overload_policy (
	const struct _Cerver *cerver, const struct HttpPool *pool
);

/**
 * @internal
 * @brief Returns the seconds that a request waits for a place in a pool: its own wait, or the one of the cerver.
 *
 * @param[in] cerver the cerver
 * @param[in] pool the pool, NULL for the default one
 * @return the seconds
 */
CERVER_PRIVATE unsigned int http_pool_wait_timeout (
	const struct _Cerver *cerver, const struct HttpPool *pool
);

/**
 * @internal
 * @brief Counts a request that stopped waiting in its event loop for a place in a pool, with the time that it waited.
 *
 * @param[in,out] pool the pool, nothing is done with NULL
 * @param[in] milliseconds the time that it waited
 * @threadsafety Safe from any thread, it only uses atomic operations.
 * @since 2.1b-19
 */
CERVER_HIDDEN void http_pool_parked_count (struct HttpPool *pool, const u64 milliseconds);

struct Arena;
struct KeyValuePair;

/**
 * @internal
 * @brief Parses the query of an url into an array of key value pairs in an arena, like
 * http_parse_query_into_pairs () does into a list: the pairs, their keys and their values are released with it.
 *
 * @param[in,out] arena the arena
 * @param[in] first the first character of the query
 * @param[in] last the character after the last one of the query
 * @param[out] n_pairs the pairs
 * @return the pairs, NULL without them or without memory
 * @since 2.1b-19
 */
CERVER_HIDDEN struct KeyValuePair *http_query_pairs_arena (
	struct Arena *arena, const char *first, const char *last, size_t *n_pairs
);

/**
 * @internal
 * @brief Returns the value of the first pair of an array with a key.
 *
 * @param[in] pairs the pairs, from http_query_pairs_arena ()
 * @param[in] n_pairs the pairs
 * @param[in] key the key
 * @return the value, that belongs to the pair; NULL for a NULL argument or a missing key
 * @since 2.1b-19
 */
CERVER_HIDDEN const String *http_query_pairs_find (
	const struct KeyValuePair *pairs, const size_t n_pairs, const char *key
);

/**
 * @internal
 * @brief Creates a list of KeyValuePair with a copy of each pair of an array.
 *
 * @param[in] pairs the pairs, from http_query_pairs_arena ()
 * @param[in] n_pairs the pairs
 * @return the list, released with dlist_delete (); NULL without pairs or without memory
 * @since 2.1b-19
 */
CERVER_HIDDEN DoubleList *http_query_pairs_list (const struct KeyValuePair *pairs, const size_t n_pairs);

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

/**
 * @internal
 * @brief Creates a custom header with an empty value in an arena.
 *
 * @param[in] arena the arena of the request
 * @param[in] header the name of the header
 * @param[in] header_len the length of the name
 * @return the new custom header, NULL on error
 */
CERVER_PRIVATE HttpCustomHeader *http_custom_header_create (
	struct Arena *arena, const char *header, const size_t header_len
);

/**
 * @internal
 * @brief Sets the value of a custom header, a new string in the arena.
 *
 * @param[in] arena the arena of the request
 * @param[in,out] custom_header the custom header
 * @param[in] value the value
 * @param[in] value_len the length of the value
 * @retval 0 the value was set
 * @retval 1 the string could not be created
 */
CERVER_PRIVATE unsigned int http_custom_header_set_value (
	struct Arena *arena, HttpCustomHeader *custom_header,
	const char *value, const size_t value_len
);

/** @internal @brief The response of the catch all route, created by the first HTTP cerver. */
CERVER_HIDDEN HttpResponse *catch_all;

/** @internal @brief The 413 response. */
CERVER_HIDDEN HttpResponse *payload_too_large_error;
/** @internal @brief The 414 response. */
CERVER_HIDDEN HttpResponse *uri_too_long_error;
/** @internal @brief The 431 response. */
CERVER_HIDDEN HttpResponse *header_fields_too_large_error;
/** @internal @brief The 408 response. */
CERVER_HIDDEN HttpResponse *request_timeout_error;
/** @internal @brief The 405 response. */
CERVER_HIDDEN HttpResponse *method_not_allowed_error;
/** @internal @brief The 501 response. */
CERVER_HIDDEN HttpResponse *not_implemented_error;
/** @internal @brief The 505 response. */
CERVER_HIDDEN HttpResponse *http_version_not_supported_error;

/** @internal @brief The 426 response of a request that has to upgrade to a WebSocket. */
CERVER_HIDDEN HttpResponse *upgrade_required_error;
/** @internal @brief The 403 response. */
CERVER_HIDDEN HttpResponse *forbidden_error;
/** @internal @brief The 503 response of a WebSocket that the cerver can not take. */
CERVER_HIDDEN HttpResponse *web_socket_unavailable_error;

/**
 * @internal
 * @brief The 503 response of the ready route of a cerver that can not take a request, with its Retry-After; it
 * does not close its connection like the one of a request that was rejected.
 */
CERVER_HIDDEN HttpResponse *not_ready_error;

/**
 * @internal
 * @brief Keeps the memory of a string but empties its contents.
 *
 * @param[in,out] buffer the string, it can be NULL
 */
static inline void http_receive_buffer_reset (String *buffer) {

	if (buffer) {
		buffer->len = 0;
		if (buffer->str) buffer->str[0] = '\0';
	}

}

/**
 * @internal
 * @brief The value that a parser callback returns: the parser stops when the request was rejected or its client is
 * gone.
 *
 * @param[in] rejected not 0 when the request was rejected or its client is gone
 * @return #HTTP_RECEIVE_PARSER_STOP or 0
 */
static inline int http_receive_parser_result (const int rejected) {

	return rejected ? HTTP_RECEIVE_PARSER_STOP : 0;

}

/**
 * @internal
 * @brief Adds the admin routes to the routes, only once, when they are enabled.
 *
 * @param[in,out] http_cerver the HttpCerver
 */
CERVER_HIDDEN void http_cerver_init_admin_routes (HttpCerver *http_cerver);

/**
 * @internal
 * @brief Releases a static path.
 *
 * @param[in] http_static_path_ptr the static path, it can be NULL
 */
CERVER_HIDDEN void http_static_path_delete (void *http_static_path_ptr);

/**
 * @internal
 * @brief Compares two static paths by their paths.
 *
 * @param[in] a the first static path
 * @param[in] b the second static path
 * @return the result of strcmp () of their paths
 */
CERVER_HIDDEN int http_static_path_comparator (const void *a, const void *b);

/**
 * @internal
 * @brief Answers a request that is rejected, once: it sends the response and sets the status of the receive.
 *
 * @param[in,out] http_receive the receive
 * @param[in] response the response
 * @param[in] status the new status; HTTP_RECEIVE_STATUS_LIMIT also counts a limited request
 * @return always 1, so a parser callback stops
 */
CERVER_HIDDEN int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
);

/**
 * @internal
 * @brief Adds bytes to the body of the request and rejects it with 413 when it exceeds its limit.
 *
 * @param[in,out] http_receive the receive
 * @param[in] length the bytes
 * @return 0 while it is below the limit, 1 when it was rejected
 */
CERVER_HIDDEN int http_receive_check_body_size (
	HttpReceive *http_receive, const size_t length
);

/**
 * @internal
 * @brief The callback of the parser for the pieces of the headers that are not kept, only counted for the limit of
 * the headers.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP when the headers exceed their limit
 */
CERVER_HIDDEN int http_receive_handle_discarded (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the parser for a piece of the url.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_url (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the parser when the url is complete.
 *
 * @param[in] parser the parser, its data is the receive
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_url_complete (llhttp_t *parser);

/**
 * @internal
 * @brief The callback of the parser for a piece of the protocol of the request line.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_protocol (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the parser when the protocol of the request line is complete.
 *
 * @param[in] parser the parser, its data is the receive
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_protocol_complete (llhttp_t *parser);

/**
 * @internal
 * @brief The callback of the parser when the version of the request line is complete.
 *
 * @param[in] parser the parser, its data is the receive
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_version_complete (llhttp_t *parser);

/**
 * @internal
 * @brief The callback of the parser for a piece of the name of a header.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_header_field (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the parser for a piece of the value of a header.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_header_value (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the parser when the value of a header is complete.
 *
 * @param[in] parser the parser, its data is the receive
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_header_value_complete (llhttp_t *parser);

/**
 * @internal
 * @brief The callback of the parser for a piece of a body that is not multipart.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_body (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief Returns whether a content type starts with the one of a multipart form.
 *
 * @param[in] content the value of the Content-Type, it must not be NULL
 * @return true for a multipart form
 */
CERVER_HIDDEN bool is_multipart (const char *content);

/**
 * @internal
 * @brief Creates the pool of the multi-parts that every HTTP cerver of the process shares, with
 * #HTTP_CERVER_MULTI_PARTS_POOL_INIT multi-parts.
 *
 * @retval 0 the pool is ready
 * @retval 1 it could not be created
 */
CERVER_HIDDEN unsigned int http_multi_parts_init_pool (void);

/**
 * @internal
 * @brief Releases the pool of the multi-parts.
 */
CERVER_HIDDEN void http_multi_parts_end_pool (void);

/**
 * @internal
 * @brief Gives a multi-part back to its pool.
 *
 * @param[in] multi_part_ptr the multi-part
 */
CERVER_HIDDEN void http_multi_parts_return (void *multi_part_ptr);

/**
 * @internal
 * @brief Copies the boundary of a multipart Content-Type, with its leading "--".
 *
 * @param[out] boundary the buffer of HTTP_MULTI_PART_BOUNDARY_MAX_SIZE bytes; a longer boundary is truncated
 * @param[in] content_type the value of the Content-Type
 * @param[in] content_type_len its length
 * @retval 0 the boundary was copied
 * @retval 1 the Content-Type has no boundary
 */
CERVER_HIDDEN unsigned int http_mpart_get_boundary (
	char *boundary,
	const char *content_type, const unsigned int content_type_len
);

/**
 * @internal
 * @brief The callback of the multipart parser when a part begins.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
);

/**
 * @internal
 * @brief The callback of the multipart parser for a piece of the name of a header of a part.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the multipart parser for a piece of the value of a header of a part.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the multipart parser when the headers of a part are complete: a file part opens its file.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_headers_completed (
	multipart_parser *parser
);

/**
 * @internal
 * @brief The callback of the multipart parser for a piece of the data of a part.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_data (
	multipart_parser *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The callback of the multipart parser when the data of a part ends.
 *
 * @param[in] parser the multipart parser, its data is the receive
 * @return 0 to continue, not 0 to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_data_end (multipart_parser *parser);

/**
 * @internal
 * @brief The callback of the parser for a piece of a multipart body, that goes to the multipart parser.
 *
 * @param[in] parser the parser, its data is the receive
 * @param[in] at the piece
 * @param[in] length the bytes of the piece
 * @return 0, or #HTTP_RECEIVE_PARSER_STOP to stop the parser
 */
CERVER_HIDDEN int http_receive_handle_mpart_body (
	llhttp_t *parser, const char *at, size_t length
);

/**
 * @internal
 * @brief The default catch all handler.
 *
 * @param[in] http_receive the receive
 * @param[in] request the request
 */
CERVER_HIDDEN void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

/**
 * @internal
 * @brief The default not found handler, that answers with 404.
 *
 * @param[in] http_receive the receive
 * @param[in] request the request
 */
CERVER_HIDDEN void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

/**
 * @internal
 * @brief Prepares the multipart parser of a request and the directory of its uploads.
 *
 * @param[in,out] http_receive the receive
 * @param[in] boundary the boundary of the body
 * @retval 0 the parser is ready
 * @retval 1 the parser, the list of the parts or the directory could not be created
 */
CERVER_HIDDEN int http_receive_init_mpart_parser (
	HttpReceive *http_receive, const char *boundary
);

/**
 * @internal
 * @brief Handles a buffer that the connection received: it parses it and answers the requests that it completes.
 *
 * @param[in,out] http_receive the receive
 * @param[in] rc the bytes that were received
 * @param[in] packet_buffer the buffer
 */
CERVER_HIDDEN void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
);

/**
 * @internal
 * @brief Gives a buffer to the parser of the receive, without closing its connection: what
 * http_receive_handle () does before it handles the result of the parser.
 *
 * The request of an event loop (HttpReceive::connection) is not handled by the parser: it stops with
 * HPE_PAUSED and HttpReceive::pause tells why.
 *
 * @param[in,out] http_receive the receive
 * @param[in] buffer the bytes, a valid pointer also when length is 0
 * @param[in] length the bytes of the buffer
 * @param[out] n_parsed the bytes of the buffer that the parser consumed
 * @return the result of the parser: HPE_OK when it needs more bytes, HPE_PAUSED after a pause, or its error
 */
CERVER_HIDDEN llhttp_errno_t http_receive_execute (
	HttpReceive *http_receive,
	const char *buffer, const size_t length, size_t *n_parsed
);

/**
 * @internal
 * @brief Lets the parser of a receive continue after a pause in its headers (#HTTP_RECEIVE_PAUSE_HEADERS), the
 * next http_receive_execute () gives it the bytes of the body.
 *
 * @param[in,out] http_receive the receive
 */
CERVER_HIDDEN void http_receive_resume (HttpReceive *http_receive);

/**
 * @internal
 * @brief Answers with 400 a request that the parser could not parse, unless it was answered or its client is gone.
 *
 * @param[in,out] http_receive the receive
 * @param[in] parser_errno the error of the parser
 * @param[in] n_parsed the bytes that the parser consumed
 * @param[in] received the bytes that it was given
 */
CERVER_HIDDEN void http_receive_parser_failed (
	HttpReceive *http_receive, const llhttp_errno_t parser_errno,
	const size_t n_parsed, const ssize_t received
);

/**
 * @internal
 * @brief Counts a request that did not arrive in time and answers it with 408, without closing its connection;
 * when the client did not send anything only the status of the receive changes.
 *
 * @param[in,out] http_receive the receive
 * @param[in] received the client sent bytes of the request
 */
CERVER_HIDDEN void http_receive_timed_out (
	HttpReceive *http_receive, const bool received
);

/**
 * @internal
 * @brief Runs the second part of the complete headers: the authentication of the endpoint or of the static path,
 * the validation of an upgrade, the directory of the uploads and "100 Continue".
 *
 * It runs inside the parser, or in a thread of the pool for the request of an event loop that set
 * HttpReceive::authorize_pending.
 *
 * @param[in,out] http_receive the receive
 * @return 0 on success, #HTTP_RECEIVE_PARSER_STOP when the request was rejected (it was answered) or its client
 * is gone
 */
CERVER_HIDDEN int http_receive_headers_authorize (HttpReceive *http_receive);

/**
 * @internal
 * @brief Handles a complete request: it serves its file, runs its endpoint or the handler of the requests that
 * no endpoint handles, and answers with 500 the one that was not answered.
 *
 * A request with HttpReceive::authorize_pending is authorized first, a rejected one is not handled.
 *
 * @param[in,out] http_receive the receive
 */
CERVER_HIDDEN void http_receive_dispatch (HttpReceive *http_receive);

/**
 * @internal
 * @brief Returns whether cerver answers the complete request by itself with a short response (a redirect, an
 * OPTIONS, a 405 or the default routes of a request without an endpoint), so an event loop can answer it
 * without the pool.
 *
 * @param[in] http_receive the receive
 * @return true when no method of the application nor a file is needed to answer it
 */
CERVER_HIDDEN bool http_receive_dispatch_is_inline (const HttpReceive *http_receive);

/**
 * @internal
 * @brief Returns whether the request was received completely and it was not rejected: its handler answers it, or
 * cerver when no route matched it.
 *
 * A response that is sent to a request that was not handled says that its connection is closed, whatever
 * HttpReceive::keep_alive has.
 *
 * @param[in] http_receive the receive
 * @return true with HTTP_RECEIVE_STATUS_COMPLETED or HTTP_RECEIVE_STATUS_UNHANDLED
 */
CERVER_HIDDEN bool http_receive_was_handled (const HttpReceive *http_receive);

/**
 * @internal
 * @brief Returns whether the connection of a request that was answered is kept for the next request of its
 * client, and updates HttpReceive::keep_alive with it.
 *
 * The cerver decided it when the request was completed; a request that was not answered, a response whose end
 * the client can not find or that could not be sent, an upgrade, a client that left or a cerver that stops
 * close the connection anyway.
 *
 * @param[in,out] http_receive the receive, after its request was handled
 * @return true when the owner of the connection starts its next request with http_receive_reset ()
 */
CERVER_HIDDEN bool http_receive_keeps_connection (HttpReceive *http_receive);

/**
 * @internal
 * @brief Starts the clock and the deadlines of a request again, when the first bytes of the next request of a
 * connection that was kept arrive: the time that the connection waited idle is not the one of the request.
 *
 * @param[in,out] http_receive the receive, after http_receive_reset ()
 */
CERVER_HIDDEN void http_receive_request_arrived (HttpReceive *http_receive);

/**
 * @internal
 * @brief Returns whether the connection of a receive is between two requests: it handled one and nothing of
 * the next one has arrived. A connection that ends there has no request to log or to count.
 *
 * @param[in] http_receive the receive
 * @return true for a connection that was kept and waits for its next request
 */
CERVER_HIDDEN bool http_receive_is_between_requests (const HttpReceive *http_receive);

/**
 * @internal
 * @brief Ends a connection that is not kept alive: it shuts down its writes, reads what the client still sends and
 * ends the connection.
 *
 * @param[in,out] http_receive the receive
 * @param[in] rejected the request was rejected, so the rest of it is read with a short timeout
 */
CERVER_HIDDEN void http_receive_close (
	HttpReceive *http_receive, const bool rejected
);

/**
 * @internal
 * @brief Returns whether an endpoint takes WebSocket connections: its modifier is the one of WebSockets and it has
 * its handlers.
 *
 * @param[in] endpoint the endpoint, it must not be NULL
 * @return true for a WebSocket endpoint
 */
CERVER_HIDDEN bool http_endpoint_is_web_socket (const HttpEndpoint *endpoint);

/**
 * @internal
 * @brief Releases the WebSocket handshake of a request, and its reservation in the registry if it had one.
 *
 * @param[in,out] http_receive the receive
 */
CERVER_HIDDEN void http_receive_web_socket_release (HttpReceive *http_receive);

/**
 * @internal
 * @brief Checks the headers of a request to a WebSocket endpoint and keeps its handshake.
 *
 * @param[in,out] http_receive the receive
 * @param[in] parser the parser of the request
 * @return 0 to continue, not 0 when the request was rejected
 */
CERVER_HIDDEN int http_receive_handle_headers_web_socket (
	HttpReceive *http_receive, const llhttp_t *parser
);

/**
 * @internal
 * @brief Keeps the bytes that arrived after the request of an upgrade, for its WebSocket.
 *
 * @param[in,out] http_receive the receive
 * @param[in] unparsed the bytes after the request
 */
CERVER_HIDDEN void http_receive_web_socket_pending (
	HttpReceive *http_receive, const char *unparsed
);

/**
 * @internal
 * @brief Upgrades the connection of a request to a WebSocket of its endpoint.
 *
 * @param[in,out] http_receive the receive
 * @param[in,out] request the request
 * @param[in] endpoint the WebSocket endpoint
 */
CERVER_HIDDEN void http_receive_web_socket_upgrade (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
);

#ifdef __cplusplus
}
#endif

#endif
