/**
 * @file cerver/http/core/http.h
 * @brief The HttpCerver: the defaults of HTTP, the stages of a request and the struct of the HTTP data of a web
 * cerver.
 */

#ifndef _CERVER_HTTP_CORE_HTTP_H_
#define _CERVER_HTTP_CORE_HTTP_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/handler/handler.h"

#include "cerver/http/headers/fields.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/status.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/middleware.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/routes/validator.h"
#include "cerver/http/security/origin.h"
#include "cerver/http/security/proxy.h"

/**
 * @defgroup http_core Core
 * @ingroup http
 * @brief The HttpCerver and what it shares with the rest of HTTP.
 */

/**
 * @defgroup http_defaults Defaults
 * @ingroup http_core
 * @brief The values that a new HttpCerver starts with, and the sizes of its buffers.
 * @{
 */

/** @brief The initial size of the pool of the multi-parts that every HTTP cerver of the process shares. */
#define HTTP_CERVER_MULTI_PARTS_POOL_INIT			32

/** @brief The size of the path of the uploads, with its terminator. */
#define HTTP_CERVER_UPLOADS_PATH_SIZE				256

/** @brief The default mode of the directories of the uploads. */
#define HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE		0755
/** @brief The default mode of the uploaded files. */
#define HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE		0644

/** @brief The default max bytes of a body; 0 disables the limit. */
#define HTTP_CERVER_DEFAULT_MAX_BODY_SIZE			(1 << 29)
/** @brief The default max bytes of an url; the limit is always active. */
#define HTTP_CERVER_DEFAULT_MAX_URL_SIZE			(1 << 13)
/** @brief The default max bytes of the headers of a request; the limit is always active. */
#define HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE			(1 << 16)
/** @brief The default max headers of a request; 0 disables the limit. */
#define HTTP_CERVER_DEFAULT_MAX_HEADERS				(1 << 7)
/** @brief The default max bytes of a custom header; 0 disables the limit. */
#define HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE	(1 << 13)
/** @brief The default max parts of a multipart body; 0 disables the limit. */
#define HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS		(1 << 10)
/** @brief The default max bytes of an uploaded file; 0 disables the limit. */
#define HTTP_CERVER_DEFAULT_MAX_FILE_SIZE			(1 << 29)

/**
 * @brief The most bytes that are reserved at once for a body from its Content-Length; a bigger body grows
 * geometrically as its bytes arrive.
 */
#define HTTP_REQUEST_BODY_RESERVE_LIMIT				(1 << 24)

/** @brief The milliseconds of a second. */
#define HTTP_MILLISECONDS_PER_SECOND				1000

/** @brief The default seconds to receive the headers of a request; a slower request is answered with 408. */
#define HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT			(1 << 4)
/** @brief #HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT in milliseconds. */
#define HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds to receive a complete request; a slower one is answered with 408.
 *
 * A body that arrives a few bytes at a time can not keep its worker forever; 0 disables the timeout, so a body can
 * take any time while it keeps making progress.
 */
#define HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT			300
/** @brief #HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT in milliseconds. */
#define HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds that a connection can wait without receiving anything; a request that stops arriving
 * for longer ends, with 408 if it had started.
 */
#define HTTP_CERVER_DEFAULT_IDLE_TIMEOUT			5
/** @brief #HTTP_CERVER_DEFAULT_IDLE_TIMEOUT in milliseconds. */
#define HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_IDLE_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds that a connection that is kept waits for its next request, more than the time that
 * a proxy keeps its idle connections: the proxy closes them first.
 *
 * @since 2.1b-16
 */
#define HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT		(1 << 7)
/**
 * @brief #HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT in milliseconds.
 *
 * @since 2.1b-16
 */
#define HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default requests that a connection handles before it is closed, more than the ones that a proxy
 * sends through a connection.
 *
 * @since 2.1b-16
 */
#define HTTP_CERVER_DEFAULT_KEEP_ALIVE_REQUESTS		(1 << 10)

/**
 * @brief The pools of threads that an HTTP cerver has at most (http/core/pool.h), with the default one.
 *
 * @since 2.1b-17
 */
#define HTTP_CERVER_POOLS_SIZE						(1 << 4)

/**
 * @brief The default microseconds that the thread of a pool that answered a request looks for the next one of its
 * connection, before it gives the connection back to its event loop: none, a cerver asks for it with
 * http_cerver_set_keep_alive_follow ().
 *
 * @since 2.1b-17
 */
#define HTTP_CERVER_DEFAULT_KEEP_ALIVE_FOLLOW		0

/**
 * @brief The microseconds that a cerver that asks its threads to follow their connections can start with: the
 * next request of a quick client arrives in less.
 *
 * @since 2.1b-17
 */
#define HTTP_CERVER_KEEP_ALIVE_FOLLOW_SHORT			(1 << 6)

/**
 * @brief By default the threads of the pools read the next request of a connection that is kept, without its
 * event loop: http_cerver_set_keep_alive_pool_read ().
 *
 * @since 2.1b-19
 */
#define HTTP_CERVER_DEFAULT_KEEP_ALIVE_POOL_READ	true

/**
 * @brief The exponent of the last bucket with a limit of the requests that a connection handled: 2^10 requests,
 * the default of #HTTP_CERVER_DEFAULT_KEEP_ALIVE_REQUESTS.
 *
 * @since 2.1b-17
 */
#define HTTP_CERVER_CONNECTION_REQUESTS_EXPONENT	10

/**
 * @brief The buckets of the requests that a connection handled: one for each power of two from 2^0 to
 * 2^#HTTP_CERVER_CONNECTION_REQUESTS_EXPONENT, and one for the connections with more.
 *
 * @since 2.1b-17
 */
#define HTTP_CERVER_CONNECTION_REQUESTS_BUCKETS		(HTTP_CERVER_CONNECTION_REQUESTS_EXPONENT + 2)

/**
 * @brief A client that closed its side of the connection (EOF) is taken as gone by default, although a half-close
 * (shutdown (SHUT_WR)) client still waits for the response.
 */
#define HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE		true

/** @brief The most bytes read from a connection before it is closed, when a request was rejected before its body was received. */
#define HTTP_RECEIVE_DRAIN_SIZE						(1 << 16)
/** @brief The size of the buffer that reads the body of a rejected request. */
#define HTTP_RECEIVE_DRAIN_BUFFER_SIZE				(1 << 12)
/** @brief The SO_RCVTIMEO in seconds while the body of a rejected request is read. */
#define HTTP_RECEIVE_DRAIN_TIMEOUT					1

/** @brief Joins the values of a repeated header (RFC 9110 section 5.3). */
#define HTTP_HEADER_VALUES_SEPARATOR				", "
/** @brief The length of #HTTP_HEADER_VALUES_SEPARATOR. */
#define HTTP_HEADER_VALUES_SEPARATOR_LEN			2

/** @brief The room of a joined header in the arena of its request, for each byte that it needs. */
#define HTTP_HEADER_VALUES_GROWTH					2

/** @brief The uploaded files are kept by default when their request ends. */
#define HTTP_CERVER_DEFAULT_UPLOADS_DELETE			false

/** @brief The admin routes are disabled by default. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN			false
/** @brief The info admin route is enabled by default, when the admin routes are. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO		true
/** @brief The admin routes do not have HEAD handlers by default. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS		false
/** @brief The admin routes do not have OPTIONS handlers by default. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS	false
/** @brief The admin routes do not require authentication by default. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH		false
/** @brief The admin routes do not add the CORS headers by default. */
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS		false

/** @brief The size of the path of a static file, with its terminator. */
#define HTTP_CERVER_STATIC_FILE_SIZE				256

/** @brief Every response has a Date header by default (RFC 9110 section 6.6.1). */
#define HTTP_CERVER_DEFAULT_DATE_HEADER				true

/** @brief The requests are not identified unless the cerver enables it. */
#define HTTP_CERVER_DEFAULT_REQUEST_ID				false

/** @brief The responses are counted by their status code, from 100 to 599. */
#define HTTP_CERVER_RESPONSES_STATUS_SIZE			600

/** @brief The default longest bearer token that is decoded; 0 has no limit. */
#define HTTP_CERVER_DEFAULT_JWT_MAX_SIZE			(1 << 13)

/** @brief A token without "exp" is accepted by default, unless the cerver requires it. */
#define HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION	false

/** @brief The default seconds of clock skew accepted in "exp" and "nbf". */
#define HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS		0

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Worker;

struct _HttpRouteFileStats;

struct _HttpReceive;
struct _HttpResponse;

struct _HttpWebSocketRegistry;
struct HttpWebSocketHandshake;

struct jwt;
struct JwtKey;

/**
 * @defgroup http_stages Stages
 * @ingroup http_core
 * @brief Where a request was when its client left, and how it left.
 * @{
 */

/**
 * @brief The X-macro table of the stages of a request where cerver noticed that its client left: the number, the
 * name, the string and the label of the metrics of each one.
 */
#define HTTP_STAGE_MAP(XX)									\
	XX(0, QUEUED,		Queued,			queued)				\
	XX(1, REQUEST_LINE,	Request Line,	request_line)		\
	XX(2, HEADERS,		Headers,		headers)			\
	XX(3, BODY,			Body,			body)				\
	XX(4, UPLOAD,		Upload,			upload)				\
	XX(5, HANDLER,		Handler,		handler)			\
	XX(6, RESPONSE,		Response,		response)

/**
 * @brief The stage of a request, one `HTTP_STAGE_<name>` for each row of #HTTP_STAGE_MAP.
 */
typedef enum HttpStage {

	#define XX(num, name, string, label) HTTP_STAGE_##name = num,
	HTTP_STAGE_MAP(XX)
	#undef XX

} HttpStage;

/** @brief The number of stages. */
#define HTTP_STAGES									7

/**
 * @brief Returns the string of a stage.
 *
 * @param[in] stage the stage
 * @return the string of its row in #HTTP_STAGE_MAP, or "Undefined" for a value out of the table; a static string
 * that must not be freed
 * @since 2.1b-21 (it was http_stage_string ())
 */
CERVER_PUBLIC const char *http_stage_to_string (
	const HttpStage stage
);

/**
 * @brief Returns the label of a stage, the value of the stage label of the metrics.
 *
 * @param[in] stage the stage
 * @return the label of its row in #HTTP_STAGE_MAP, or the one of HTTP_STAGE_QUEUED for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *http_stage_label (
	const HttpStage stage
);

/**
 * @brief The X-macro table of how a client that left ended its connection: the number, the name, the string and
 * the label of each reason.
 */
#define HTTP_ABORT_REASON_MAP(XX)							\
	XX(0, CLOSED,		Closed,			closed)				\
	XX(1, RESET,		Reset,			reset)

/**
 * @brief How a client that left ended its connection, one `HTTP_ABORT_REASON_<name>` for each row of
 * #HTTP_ABORT_REASON_MAP.
 */
typedef enum HttpAbortReason {

	#define XX(num, name, string, label) HTTP_ABORT_REASON_##name = num,
	HTTP_ABORT_REASON_MAP(XX)
	#undef XX

} HttpAbortReason;

/** @brief The number of abort reasons. */
#define HTTP_ABORT_REASONS							2

/**
 * @brief Returns the string of an abort reason.
 *
 * @param[in] reason the reason
 * @return the string of its row in #HTTP_ABORT_REASON_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 * @since 2.1b-21 (it was http_abort_reason_string ())
 */
CERVER_PUBLIC const char *http_abort_reason_to_string (
	const HttpAbortReason reason
);

/**
 * @brief Returns the label of an abort reason, the value of the reason label of the metrics.
 *
 * @param[in] reason the reason
 * @return the label of its row in #HTTP_ABORT_REASON_MAP, or the one of HTTP_ABORT_REASON_CLOSED for a value out of
 * the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_abort_reason_label (
	const HttpAbortReason reason
);

/**
 * @brief The X-macro table of the states of the client of a request while its handler runs: the number, the name
 * and the string of each one.
 *
 * CLOSED is an EOF: a full close and a half-close are the same in TCP.
 */
#define HTTP_CLIENT_STATE_MAP(XX)							\
	XX(0, CONNECTED,	Connected)							\
	XX(1, CLOSED,		Closed)								\
	XX(2, RESET,		Reset)

/**
 * @brief The state of the client of a request, one `HTTP_CLIENT_STATE_<name>` for each row of
 * #HTTP_CLIENT_STATE_MAP.
 */
typedef enum HttpClientState {

	#define XX(num, name, string) HTTP_CLIENT_STATE_##name = num,
	HTTP_CLIENT_STATE_MAP(XX)
	#undef XX

} HttpClientState;

/**
 * @brief Returns the string of a client state.
 *
 * @param[in] state the state
 * @return the string of its row in #HTTP_CLIENT_STATE_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 * @since 2.1b-21 (it was http_client_state_string ())
 */
CERVER_PUBLIC const char *http_client_state_to_string (
	const HttpClientState state
);

/** @} */

/**
 * @defgroup http_main HttpCerver
 * @ingroup http_core
 * @brief The HTTP data of a web cerver.
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

	/** @brief The top level routes of the first routes API, translated to endpoints of the routes tree when the cerver starts. */
	DoubleList *routes;

	/** @brief The main route. */
	const HttpRoute *main_route;

	/** @brief The groups, the endpoints and the compiled routes tree. */
	HttpTree *tree;

	/** @brief The catch all route (/\*). */
	void (*default_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	/** @brief The requests that do not match a route are answered by #not_found. */
	bool not_found_handler;
	/** @brief The handler of the requests that do not match a route. */
	void (*not_found)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	/** @brief The length of #uploads_path. */
	int uploads_path_len;
	/** @brief The directory of the uploads. */
	char uploads_path[HTTP_CERVER_UPLOADS_PATH_SIZE];

	/** @brief The mode of the uploaded files. */
	unsigned int uploads_file_mode;
	/** @brief Generates the name of each uploaded file. */
	void (*uploads_filename_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	/** @brief The mode of the directories of the uploads. */
	unsigned int uploads_dir_mode;
	/** @brief Generates the directory of the uploads of a request. */
	void (*uploads_dirname_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	/** @brief The uploaded files are deleted when their request ends. */
	bool uploads_delete_when_done;

	/** @brief The last number given to a request by the default uploads generators, so their names are unique even if a fd is reused in the same second. */
	size_t uploads_sequence;

	/** @brief The algorithm of the tokens. */
	jwt_alg_t jwt_alg;

	/** @brief The filename of the private key of the tokens. */
	String *jwt_opt_key_name;
	/** @brief Signs the tokens, loaded when the cerver starts. */
	struct JwtKey *jwt_private_key;

	/** @brief The filename of the public key of the tokens. */
	String *jwt_opt_pub_key_name;
	/** @brief Verifies the tokens, loaded when the cerver starts. */
	struct JwtKey *jwt_public_key;

	/** @brief The longest token that is decoded, 0 has no limit. */
	size_t jwt_max_size;
	/** @brief A token without "exp" is rejected. */
	bool jwt_require_expiration;
	/** @brief The seconds of clock skew accepted in "exp" and "nbf". */
	unsigned int jwt_leeway_seconds;
	/** @brief The "iss" that a token must have, if it is set. */
	String *jwt_issuer;
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
	/** @brief The info admin route is enabled. */
	bool enable_admin_info_route;
	/** @brief The admin routes have HEAD handlers. */
	bool enable_admin_head_handlers;
	/** @brief The admin routes have OPTIONS handlers. */
	bool enable_admin_options_handlers;

	/** @brief The admin routes require authentication. */
	bool enable_admin_routes_auth;
	/** @brief How the admin routes authenticate. */
	HttpRouteAuthType admin_auth_type;
	/** @brief Decodes the data of the token of an admin request. */
	void *(*admin_decode_data)(void *);
	/** @brief Releases the decoded data of an admin request. */
	void (*admin_delete_decoded_data)(void *);
	/** @brief The custom authentication of the admin routes. */
	unsigned int (*admin_auth_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

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
	HttpTrustedProxies trusted_proxies;
	/** @brief The networks of #trusted_proxies_networks. */
	unsigned int n_trusted_proxies;
	/** @brief The networks of the trusted proxies. */
	HttpProxyNetwork trusted_proxies_networks[HTTP_TRUSTED_PROXIES_SIZE];

	/** @brief The responses sent by their status code, updated with atomic operations. */
	size_t n_responses_by_status[HTTP_CERVER_RESPONSES_STATUS_SIZE];

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
	 * @since 2.1b-16
	 */
	bool keep_alive;
	/**
	 * @brief http_cerver_set_keep_alive () was called, the handler type does not choose.
	 *
	 * @since 2.1b-16
	 */
	bool keep_alive_configured;
	/**
	 * @brief The most milliseconds that a connection that is kept waits for its next request.
	 *
	 * @since 2.1b-16
	 */
	unsigned int keep_alive_timeout;
	/**
	 * @brief The most requests that a connection handles, the last one is answered with "Connection: close";
	 * 0 disables the limit.
	 *
	 * @since 2.1b-16
	 */
	unsigned int keep_alive_requests;

	/**
	 * @brief The requests that arrived in a connection that had already handled another one, updated with atomic
	 * operations.
	 *
	 * @since 2.1b-16
	 */
	size_t n_reused_connections;

	/**
	 * @brief The pools of threads that handle the requests (http/core/pool.h); the first one is the default one,
	 * the thpool of the cerver.
	 *
	 * @since 2.1b-17
	 */
	struct HttpPool *pools[HTTP_CERVER_POOLS_SIZE];
	/**
	 * @brief The pools in #pools.
	 *
	 * @since 2.1b-17
	 */
	unsigned int n_pools;
	/**
	 * @brief The cerver started its pools: a pool can not be created anymore.
	 *
	 * @since 2.1b-17
	 */
	bool pools_started;

	/**
	 * @brief The path of the health route (http_cerver_set_health_route ()), NULL without one.
	 *
	 * @since 2.1b-17
	 */
	String *health_route;
	/**
	 * @brief The path of the ready route (http_cerver_set_ready_route ()), NULL without one.
	 *
	 * @since 2.1b-17
	 */
	String *ready_route;

	/**
	 * @brief The requests of the health route, updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t n_health_requests;
	/**
	 * @brief The requests of the ready route that were answered with 200, updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t n_ready_requests;
	/**
	 * @brief The requests of the ready route that were answered with 503: a pool had no place, or the cerver was
	 * stopping; updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t n_not_ready_requests;

	/**
	 * @brief The connections that ended, by the requests that they handled: the bucket `i` has the ones with up to
	 * 2^i requests (and more than 2^(i - 1)), the last one the ones with more; a connection that ended without a
	 * request is not counted. Updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t connection_requests[HTTP_CERVER_CONNECTION_REQUESTS_BUCKETS];
	/**
	 * @brief The requests that the connections of #connection_requests handled, updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t n_connection_requests;

	/**
	 * @brief The microseconds that the cerver spent writing the files of its uploads, updated with atomic
	 * operations.
	 *
	 * @since 2.1b-17
	 */
	size_t uploads_write_microseconds;
	/**
	 * @brief The bytes of the files of the uploads that the cerver wrote, updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t uploads_write_bytes;

	/**
	 * @brief The microseconds that the thread of a pool that answered a request of a connection that is kept looks
	 * for its next request, to handle it by itself; 0 disables it (http_cerver_set_keep_alive_follow ()). Only with
	 * event loops.
	 *
	 * @since 2.1b-17
	 */
	unsigned int keep_alive_follow;
	/**
	 * @brief The requests that the thread that answered the one before them handled by itself, without their event
	 * loop; updated with atomic operations.
	 *
	 * @since 2.1b-17
	 */
	size_t n_followed_requests;

	/**
	 * @brief The threads of the pools read, parse and handle the next request of a connection that is kept, and
	 * its event loop only reads it when its pool has no place (http_cerver_set_keep_alive_pool_read ()). Only with
	 * event loops.
	 *
	 * @since 2.1b-19
	 */
	bool keep_alive_pool_read;
	/**
	 * @brief The requests of connections that were kept that a thread of a pool read and handled, without their
	 * event loop; updated with atomic operations.
	 *
	 * @since 2.1b-19
	 */
	size_t n_pool_read_requests;

};

/** @brief The HTTP data of a web cerver, see struct _HttpCerver. */
typedef struct _HttpCerver HttpCerver;

/**
 * @brief Allocates an HttpCerver with the default values.
 *
 * @return the new HttpCerver, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpCerver *http_cerver_new (void);

/**
 * @brief Releases an HttpCerver: its paths, its routes, its keys, its headers, its admin lists and its WebSocket
 * registry, and its data with its delete method.
 *
 * @param[in] http_cerver_ptr the HttpCerver, it can be NULL
 * @note For the internal methods of cerver: the cerver releases it in its teardown.
 */
CERVER_PRIVATE void http_cerver_delete (
	void *http_cerver_ptr
);

/**
 * @brief Returns the HttpCerver of a web cerver.
 *
 * @param[in] cerver the cerver
 * @return its HttpCerver, NULL for a NULL cerver; the data of the cerver as an HttpCerver, so the cerver must be
 * of type CERVER_TYPE_WEB
 */
CERVER_EXPORT HttpCerver *http_cerver_get (
	struct _Cerver *cerver
);

/**
 * @brief Creates the HttpCerver of a cerver, with its lists, its routes tree, its default handlers and its
 * WebSocket registry.
 *
 * @param[in] cerver the cerver
 * @return the new HttpCerver, NULL if it could not be allocated
 * @note For the internal methods of cerver: cerver_create () calls it for CERVER_TYPE_WEB.
 */
CERVER_PRIVATE HttpCerver *http_cerver_create (
	struct _Cerver *cerver
);

/**
 * @brief Initializes an HttpCerver when its cerver starts: the state that the HTTP cervers of the process share,
 * the keys and the routes.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @retval 0 the HttpCerver is ready
 * @retval 1 a NULL HttpCerver or it failed to initialize
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_cerver_init (
	HttpCerver *http_cerver
);

/**
 * @brief Releases what http_cerver_init () took: the reference to the state that the HTTP cervers of the process
 * share.
 *
 * @param[in,out] http_cerver the HttpCerver, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_cerver_end (
	HttpCerver *http_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
