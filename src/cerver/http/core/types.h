/**
 * @file cerver/http/core/types.h
 * @internal
 * @brief The HttpCerver: the struct of the HTTP data of a web cerver, that an application only reaches as a
 * CerverHttp through the methods of HTTP.
 */

#ifndef _CERVER_HTTP_CORE_TYPES_H_
#define _CERVER_HTTP_CORE_TYPES_H_

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/core/context.h"
#include "cerver/http/core/http.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/routes/internal.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/origin.h"
#include "cerver/http/security/proxy.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _HttpWebSocketRegistry;

struct HttpPool;

/**
 * @defgroup http_main_internal HttpCerver internals
 * @ingroup http_core
 * @brief The struct of the HTTP data of a web cerver.
 * @{
 */

/**
 * @brief The HTTP data of a cerver of type CERVER_TYPE_WEB: its routes, its limits, its timeouts, its security and
 * its stats.
 *
 * The fields are set with the setters of HTTP before the cerver starts and read by the parts of HTTP.
 */
struct _HttpCerver {

	/** @brief The cerver that owns it. */
	struct _Cerver *cerver;

	/** @brief The paths of the static files. */
	DoubleList *static_paths;

	/** @brief The groups, the endpoints and the compiled routes tree. */
	HttpTree *tree;

	/** @brief The catch all route (/\*). */
	CerverHttpHandler default_handler;

	/** @brief The requests that do not match a route are answered by #not_found. */
	bool not_found_handler;
	/** @brief The handler of the requests that do not match a route. */
	CerverHttpHandler not_found;

	/** @brief The length of #uploads_path. */
	int uploads_path_len;
	/** @brief The directory of the uploads. */
	char uploads_path[CERVER_HTTP_UPLOADS_PATH_SIZE];

	/** @brief The mode of the uploaded files. */
	unsigned int uploads_file_mode;
	/** @brief Generates the name of each uploaded file. */
	CerverHttpHandler uploads_filename_generator;

	/** @brief The mode of the directories of the uploads. */
	unsigned int uploads_dir_mode;
	/** @brief Generates the directory of the uploads of a request. */
	CerverHttpHandler uploads_dirname_generator;

	/** @brief The uploaded files are deleted when their request ends. */
	bool uploads_delete_when_done;

	/** @brief The last number given to a request by the default uploads generators, so their names are unique even if a fd is reused in the same second. */
	size_t uploads_sequence;

	/** @brief The algorithm of the tokens. */
	CerverJwtAlgorithm jwt_alg;

	/** @brief The filename of the private key of the tokens. */
	String *jwt_opt_key_name;
	/** @brief Signs the tokens, loaded when the cerver starts. */
	CerverJwtKey *jwt_private_key;

	/** @brief The filename of the public key of the tokens. */
	String *jwt_opt_pub_key_name;
	/** @brief Verifies the tokens, loaded when the cerver starts. */
	CerverJwtKey *jwt_public_key;

	/** @brief The longest token that is decoded, 0 is the limit of every token (#CERVER_JWT_SIZE_MAX). */
	size_t jwt_max_size;
	/** @brief A token without "exp" is rejected. */
	bool jwt_require_expiration;
	/** @brief The seconds of clock skew accepted in "exp", "nbf" and "iat". */
	unsigned int jwt_leeway_seconds;
	/** @brief The "iss" that a token must have, if it is set. */
	String *jwt_issuer;
	/** @brief The "sub" that a token must have, if it is set. */
	String *jwt_subject;
	/** @brief The "aud" that a token must have, if it is set. */
	String *jwt_audience;

	/** @brief The origins of #origins_whitelist. */
	u8 n_origins;
	/** @brief The origins that the cerver accepts. */
	HttpOrigin origins_whitelist[HTTP_ORIGINS_SIZE];

	/** @brief The data of the application. */
	void *custom_data;
	/** @brief Releases the data of the application when the HttpCerver is deleted. */
	void (*delete_custom_data)(void *);

	/** @brief The headers that are added to every response that does not have them. */
	HttpHeaders response_headers;

	/**
	 * @brief Holds a reference to the state shared by every HTTP cerver of the process: the jwt pool, the responses
	 * producer, the common responses and the multi-parts pool, created by the first HTTP cerver that starts and
	 * released by the last one that ends.
	 */
	bool global_reference;

	/** @brief The requests that the parser could not parse. */
	size_t n_incompleted_requests;
	/** @brief The requests without a matching route. */
	size_t n_unhandled_requests;

	/** @brief The requests redirected to the catch all route. */
	size_t n_catch_all_requests;
	/** @brief The requests that failed the authentication of a private route. */
	size_t n_failed_auth_requests;

	/** @brief The admin routes are enabled. */
	bool enable_admin_routes;
	/** @brief The admin routes were added to the routes. */
	bool admin_routes_ready;
	/**
	 * @brief The path of the group of the admin routes (cerver_http_admin_set_path ()), NULL for
	 * #CERVER_HTTP_DEFAULT_ADMIN_PATH.
	 */
	String *admin_path;
	/** @brief The info admin route is enabled. */
	bool enable_admin_info_route;
	/** @brief The admin routes have HEAD handlers. */
	bool enable_admin_head_handlers;
	/** @brief The admin routes have OPTIONS handlers. */
	bool enable_admin_options_handlers;

	/** @brief The admin routes require authentication. */
	bool enable_admin_routes_auth;
	/** @brief How the admin routes authenticate. */
	CerverHttpRouteAuthType admin_auth_type;
	/** @brief Decodes the data of the token of an admin request, from its claims. */
	CerverHttpDecodeData admin_decode_data;
	/** @brief Releases the decoded data of an admin request. */
	void (*admin_delete_decoded_data)(void *);
	/** @brief The custom authentication of the admin routes. */
	CerverHttpAuthenticationHandler admin_auth_handler;

	/** @brief The data of the admin routes. */
	void *admin_routes_custom_data;
	/** @brief Releases the data of the admin routes. */
	void (*delete_admin_routes_custom_data)(void *);

	/** @brief The admin routes add the CORS headers. */
	bool enable_admin_cors_headers;
	/** @brief The origin of the CORS headers of the admin routes. */
	HttpOrigin admin_origin;

	/** @brief The file systems that the admin routes report. */
	DoubleList *admin_file_systems_stats;
	/** @brief The workers that the admin routes report. */
	DoubleList *admin_workers;
	/** @brief Protects the lists of the admin routes. */
	pthread_mutex_t *admin_mutex;

	/** @brief Protects the stats. */
	pthread_mutex_t *mutex;

	/** @brief The lenient flags (HttpParserLenient) of the parser of each request, 0 is the strict mode. */
	unsigned int parser_lenient;

	/** @brief The max bytes of a body, 0 disables the limit. */
	size_t max_body_size;
	/** @brief The max bytes of an url, always active. */
	size_t max_url_size;
	/** @brief The max bytes of the headers of a request, always active. */
	size_t max_header_size;
	/** @brief The max headers of a request, 0 disables the limit. */
	unsigned int max_headers;
	/** @brief The max parts of a multipart body, 0 disables the limit. */
	unsigned int max_multipart_parts;
	/** @brief The max bytes of an uploaded file, 0 disables the limit. */
	size_t max_file_size;
	/** @brief The max bytes of a custom header, 0 disables the limit. */
	size_t max_custom_header_size;

	/** @brief The requests rejected because they exceeded a limit. */
	size_t n_limited_requests;

	/** @brief The milliseconds to receive the headers of a request, 0 disables the timeout. */
	unsigned int headers_timeout;
	/** @brief The milliseconds to receive a complete request, 0 disables the timeout. */
	unsigned int request_timeout;

	/** @brief The most milliseconds without receiving anything, it is never disabled. */
	unsigned int idle_timeout;

	/** @brief The most milliseconds that all the sends of a response take together, 0 uses the send timeout of the cerver (seconds). */
	unsigned int send_timeout;

	/** @brief The WebSocket connections, their limit and their stats. */
	struct _HttpWebSocketRegistry *web_sockets;

	/** @brief The requests answered with 408. */
	size_t n_timeout_requests;

	/** @brief The requests answered with 500 because their handler did not respond. */
	size_t n_unanswered_requests;

	/** @brief cerver adds the Date header to every response. */
	bool date_header;
	/** @brief cerver identifies each request. */
	bool request_id;

	/** @brief The smallest body that the endpoints with the compression setting compress. */
	size_t compression_min_size;
	/** @brief The level of the compression. */
	int compression_level;

	/** @brief The peers whose X-Forwarded-For and X-Real-IP are used to find the address of the client. */
	CerverHttpTrustedProxies trusted_proxies;
	/** @brief The networks of #trusted_proxies_networks. */
	unsigned int n_trusted_proxies;
	/** @brief The networks of the trusted proxies. */
	HttpProxyNetwork trusted_proxies_networks[HTTP_TRUSTED_PROXIES_SIZE];

	/** @brief The responses sent by their status code, updated with atomic operations. */
	size_t n_responses_by_status[CERVER_HTTP_RESPONSES_STATUS_SIZE];

	/** @brief http_receive_is_client_gone () takes a client that closed its side as gone. */
	bool half_close_is_gone;

	/** @brief The requests whose client left, by the stage where cerver noticed it and how it left, updated with atomic operations. */
	size_t n_aborted_requests[HTTP_STAGES][HTTP_ABORT_REASONS];

	/** @brief The requests whose client stopped sending or reading, ended by a timeout, by stage. */
	size_t n_vanished_requests[HTTP_STAGES];

	/** @brief The requests whose response could not be sent completely. */
	size_t n_failed_responses;

	/**
	 * @brief A connection is kept after a request for the next one of its client. When it was not configured, it
	 * is set when the cerver starts: true with CERVER_HANDLER_TYPE_EVENTS, false with the other handler types.
	 *
	 * @since 3.0b-5
	 */
	bool keep_alive;
	/**
	 * @brief cerver_http_set_keep_alive () was called, the handler type does not choose.
	 *
	 * @since 3.0b-5
	 */
	bool keep_alive_configured;
	/**
	 * @brief The most milliseconds that a connection that is kept waits for its next request.
	 *
	 * @since 3.0b-5
	 */
	unsigned int keep_alive_timeout;
	/**
	 * @brief The most requests that a connection handles, the last one is answered with "Connection: close";
	 * 0 disables the limit.
	 *
	 * @since 3.0b-5
	 */
	unsigned int keep_alive_requests;

	/**
	 * @brief The requests that arrived in a connection that had already handled another one, updated with atomic
	 * operations.
	 *
	 * @since 3.0b-5
	 */
	size_t n_reused_connections;

	/**
	 * @brief The pools of threads that handle the requests (http/core/pool.h); the first one is the default one,
	 * the thpool of the cerver.
	 *
	 * @since 3.0b-5
	 */
	struct HttpPool *pools[CERVER_HTTP_POOLS_SIZE];
	/**
	 * @brief The pools in #pools.
	 *
	 * @since 3.0b-5
	 */
	unsigned int n_pools;
	/**
	 * @brief The cerver started its pools: a pool can not be created anymore.
	 *
	 * @since 3.0b-5
	 */
	bool pools_started;
	/**
	 * @brief The pool of the requests that their event loop handles by itself (#HTTP_POOL_LOOP_NAME,
	 * http/core/pool.h): it has no threads and it is not one of #pools.
	 *
	 * @since 3.0b-11
	 */
	struct HttpPool *loop_pool;
	/**
	 * @brief The pool of the requests whose endpoint and groups did not choose one
	 * (cerver_http_set_handlers_pool ()); NULL for the default one.
	 *
	 * @since 3.0b-11
	 */
	struct HttpPool *handlers_pool;

	/**
	 * @brief The path of the health route (cerver_http_set_health_route ()), NULL without one.
	 *
	 * @since 3.0b-5
	 */
	String *health_route;
	/**
	 * @brief The path of the ready route (cerver_http_set_ready_route ()), NULL without one.
	 *
	 * @since 3.0b-5
	 */
	String *ready_route;

	/**
	 * @brief The requests of the health route, updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t n_health_requests;
	/**
	 * @brief The requests of the ready route that were answered with 200, updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t n_ready_requests;
	/**
	 * @brief The requests of the ready route that were answered with 503: a pool had no place, or the cerver was
	 * stopping; updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t n_not_ready_requests;

	/**
	 * @brief The connections that ended, by the requests that they handled: the bucket `i` has the ones with up to
	 * 2^i requests (and more than 2^(i - 1)), the last one the ones with more; a connection that ended without a
	 * request is not counted. Updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t connection_requests[CERVER_HTTP_CONNECTION_REQUESTS_BUCKETS];
	/**
	 * @brief The requests that the connections of #connection_requests handled, updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t n_connection_requests;

	/**
	 * @brief The microseconds that the cerver spent writing the files of its uploads, updated with atomic
	 * operations.
	 *
	 * @since 3.0b-5
	 */
	size_t uploads_write_microseconds;
	/**
	 * @brief The bytes of the files of the uploads that the cerver wrote, updated with atomic operations.
	 *
	 * @since 3.0b-5
	 */
	size_t uploads_write_bytes;

};

/** @brief The HTTP data of a web cerver, see struct _HttpCerver. */
typedef struct _HttpCerver HttpCerver;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
