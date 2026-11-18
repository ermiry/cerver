/**
 * @file cerver/http/core/http.h
 * @brief The CerverHttp: the defaults of HTTP, the lenient flags of the parser, the stages of a request and the
 * methods of the HTTP data of a web cerver.
 */

#ifndef _CERVER_HTTP_CORE_HTTP_H_
#define _CERVER_HTTP_CORE_HTTP_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/handler/handler.h"

#include "cerver/http/core/context.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/parser/multipart.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/responses/status.h"
#include "cerver/http/routes/middleware.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/routes/validator.h"
#include "cerver/http/security/origin.h"
#include "cerver/http/security/proxy.h"

#include "cerver/jwt/jwt.h"

/**
 * @defgroup http_core Core
 * @ingroup http
 * @brief The CerverHttp and what it shares with the rest of HTTP.
 */

/**
 * @defgroup http_defaults Defaults
 * @ingroup http_core
 * @brief The values that a new CerverHttp starts with, and the sizes of its buffers.
 * @{
 */

/** @brief The initial size of the pool of the multi-parts that every HTTP cerver of the process shares. */
#define CERVER_HTTP_MULTI_PARTS_POOL_INIT			32

/** @brief The size of the path of the uploads, with its terminator. */
#define CERVER_HTTP_UPLOADS_PATH_SIZE				256

/** @brief The default mode of the directories of the uploads. */
#define CERVER_HTTP_DEFAULT_UPLOADS_DIR_MODE		0755
/** @brief The default mode of the uploaded files. */
#define CERVER_HTTP_DEFAULT_UPLOADS_FILE_MODE		0644

/** @brief The default max bytes of a body; 0 disables the limit. */
#define CERVER_HTTP_DEFAULT_MAX_BODY_SIZE			(1 << 29)
/** @brief The default max bytes of an url; the limit is always active. */
#define CERVER_HTTP_DEFAULT_MAX_URL_SIZE			(1 << 13)
/** @brief The default max bytes of the headers of a request; the limit is always active. */
#define CERVER_HTTP_DEFAULT_MAX_HEADER_SIZE			(1 << 16)
/** @brief The default max headers of a request; 0 disables the limit. */
#define CERVER_HTTP_DEFAULT_MAX_HEADERS				(1 << 7)
/** @brief The default max bytes of a custom header; 0 disables the limit. */
#define CERVER_HTTP_DEFAULT_MAX_CUSTOM_HEADER_SIZE	(1 << 13)
/** @brief The default max parts of a multipart body; 0 disables the limit. */
#define CERVER_HTTP_DEFAULT_MAX_MULTIPART_PARTS		(1 << 10)
/** @brief The default max bytes of an uploaded file; 0 disables the limit. */
#define CERVER_HTTP_DEFAULT_MAX_FILE_SIZE			(1 << 29)

/**
 * @brief The most bytes that are reserved at once for a body from its Content-Length; a bigger body grows
 * geometrically as its bytes arrive.
 */
#define HTTP_REQUEST_BODY_RESERVE_LIMIT				(1 << 24)

/** @brief The milliseconds of a second. */
#define HTTP_MILLISECONDS_PER_SECOND				1000

/** @brief The default seconds to receive the headers of a request; a slower request is answered with 408. */
#define CERVER_HTTP_DEFAULT_HEADERS_TIMEOUT			(1 << 4)
/** @brief #CERVER_HTTP_DEFAULT_HEADERS_TIMEOUT in milliseconds. */
#define CERVER_HTTP_DEFAULT_HEADERS_TIMEOUT_MILLISECONDS	\
	(CERVER_HTTP_DEFAULT_HEADERS_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds to receive a complete request; a slower one is answered with 408.
 *
 * A body that arrives a few bytes at a time can not keep its worker forever; 0 disables the timeout, so a body can
 * take any time while it keeps making progress.
 */
#define CERVER_HTTP_DEFAULT_REQUEST_TIMEOUT			300
/** @brief #CERVER_HTTP_DEFAULT_REQUEST_TIMEOUT in milliseconds. */
#define CERVER_HTTP_DEFAULT_REQUEST_TIMEOUT_MILLISECONDS	\
	(CERVER_HTTP_DEFAULT_REQUEST_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds that a connection can wait without receiving anything; a request that stops arriving
 * for longer ends, with 408 if it had started.
 */
#define CERVER_HTTP_DEFAULT_IDLE_TIMEOUT			5
/** @brief #CERVER_HTTP_DEFAULT_IDLE_TIMEOUT in milliseconds. */
#define CERVER_HTTP_DEFAULT_IDLE_TIMEOUT_MILLISECONDS	\
	(CERVER_HTTP_DEFAULT_IDLE_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default seconds that a connection that is kept waits for its next request, more than the time that
 * a proxy keeps its idle connections: the proxy closes them first.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT		(1 << 7)
/**
 * @brief #CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT in milliseconds.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS	\
	(CERVER_HTTP_DEFAULT_KEEP_ALIVE_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

/**
 * @brief The default requests that a connection handles before it is closed, more than the ones that a proxy
 * sends through a connection.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_DEFAULT_KEEP_ALIVE_REQUESTS		(1 << 10)

/**
 * @brief The pools of threads that an HTTP cerver has at most (http/core/pool.h), with the default one.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_POOLS_SIZE						(1 << 4)

/**
 * @brief The exponent of the last bucket with a limit of the requests that a connection handled: 2^10 requests,
 * the default of #CERVER_HTTP_DEFAULT_KEEP_ALIVE_REQUESTS.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_CONNECTION_REQUESTS_EXPONENT	10

/**
 * @brief The buckets of the requests that a connection handled: one for each power of two from 2^0 to
 * 2^#CERVER_HTTP_CONNECTION_REQUESTS_EXPONENT, and one for the connections with more.
 *
 * @since 3.0b-5
 */
#define CERVER_HTTP_CONNECTION_REQUESTS_BUCKETS		(CERVER_HTTP_CONNECTION_REQUESTS_EXPONENT + 2)

/**
 * @brief A client that closed its side of the connection (EOF) is taken as gone by default, although a half-close
 * (shutdown (SHUT_WR)) client still waits for the response.
 */
#define CERVER_HTTP_DEFAULT_HALF_CLOSE_IS_GONE		true

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
#define CERVER_HTTP_DEFAULT_UPLOADS_DELETE			false

/** @brief The admin routes are disabled by default. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN			false
/** @brief The path of the group of the admin routes by default: they are `/cerver/info`, `/cerver/stats`… */
#define CERVER_HTTP_DEFAULT_ADMIN_PATH				"/"
/** @brief The info admin route is enabled by default, when the admin routes are. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN_INFO		true
/** @brief The admin routes do not have HEAD handlers by default. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN_HEADS		false
/** @brief The admin routes do not have OPTIONS handlers by default. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN_OPTIONS	false
/** @brief The admin routes do not require authentication by default. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN_AUTH		false
/** @brief The admin routes do not add the CORS headers by default. */
#define CERVER_HTTP_DEFAULT_ENABLE_ADMIN_CORS		false

/** @brief The size of the path of a static file, with its terminator. */
#define CERVER_HTTP_STATIC_FILE_SIZE				256

/** @brief Every response has a Date header by default (RFC 9110 section 6.6.1). */
#define CERVER_HTTP_DEFAULT_DATE_HEADER				true

/** @brief The requests are not identified unless the cerver enables it. */
#define CERVER_HTTP_DEFAULT_REQUEST_ID				false

/** @brief The responses are counted by their status code, from 100 to 599. */
#define CERVER_HTTP_RESPONSES_STATUS_SIZE			600

/**
 * @brief The default algorithm of the tokens of a cerver.
 *
 * @since 3.0b-8
 */
#define CERVER_HTTP_DEFAULT_JWT_ALGORITHM			CERVER_JWT_ALGORITHM_HS256

/** @brief The default longest bearer token that is decoded; 0 is the limit of every token (#CERVER_JWT_SIZE_MAX). */
#define CERVER_HTTP_DEFAULT_JWT_MAX_SIZE			(1 << 13)

/** @brief A token without "exp" is accepted by default, unless the cerver requires it. */
#define CERVER_HTTP_DEFAULT_JWT_REQUIRE_EXPIRATION	false

/** @brief The default seconds of clock skew accepted in "exp", "nbf" and "iat". */
#define CERVER_HTTP_DEFAULT_JWT_LEEWAY_SECONDS		0

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
typedef enum CerverHttpStage {

	#define XX(num, name, string, label) HTTP_STAGE_##name = num,
	HTTP_STAGE_MAP(XX)
	#undef XX

} CerverHttpStage;

/** @brief The number of stages. */
#define HTTP_STAGES									7

/**
 * @brief Returns the string of a stage.
 *
 * @param[in] stage the stage
 * @return the string of its row in #HTTP_STAGE_MAP, or the one of HTTP_STAGE_QUEUED for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_stage_string (
	const CerverHttpStage stage
);

/**
 * @brief Returns the label of a stage, the value of the stage label of the metrics.
 *
 * @param[in] stage the stage
 * @return the label of its row in #HTTP_STAGE_MAP, or the one of HTTP_STAGE_QUEUED for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_stage_label (
	const CerverHttpStage stage
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
typedef enum CerverHttpAbortReason {

	#define XX(num, name, string, label) HTTP_ABORT_REASON_##name = num,
	HTTP_ABORT_REASON_MAP(XX)
	#undef XX

} CerverHttpAbortReason;

/** @brief The number of abort reasons. */
#define HTTP_ABORT_REASONS							2

/**
 * @brief Returns the string of an abort reason.
 *
 * @param[in] reason the reason
 * @return the string of its row in #HTTP_ABORT_REASON_MAP, or the one of HTTP_ABORT_REASON_CLOSED for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_abort_reason_string (
	const CerverHttpAbortReason reason
);

/**
 * @brief Returns the label of an abort reason, the value of the reason label of the metrics.
 *
 * @param[in] reason the reason
 * @return the label of its row in #HTTP_ABORT_REASON_MAP, or the one of HTTP_ABORT_REASON_CLOSED for a value out of
 * the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_abort_reason_label (
	const CerverHttpAbortReason reason
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
typedef enum CerverHttpClientState {

	#define XX(num, name, string) HTTP_CLIENT_STATE_##name = num,
	HTTP_CLIENT_STATE_MAP(XX)
	#undef XX

} CerverHttpClientState;

/**
 * @brief Returns the string of a client state.
 *
 * @param[in] state the state
 * @return the string of its row in #HTTP_CLIENT_STATE_MAP, or the one of HTTP_CLIENT_STATE_CONNECTED for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_client_state_string (
	const CerverHttpClientState state
);

/**
 * @brief Checks, without waiting and without reading its data, if the client of a request is still connected.
 *
 * A handler that takes long can call it to stop the work of a client that left; cerver_http_context_client_is_gone ()
 * is the same question without the difference between a close and a reset.
 *
 * @param[in] context the context of the request
 * @return HTTP_CLIENT_STATE_CONNECTED, HTTP_CLIENT_STATE_CLOSED when the client closed its side (an EOF), or
 * HTTP_CLIENT_STATE_RESET when its connection failed (a RST) and for a NULL context; a gone client never comes back
 * @since 3.0b-12 it replaces http_receive_get_client_state ().
 */
CERVER_EXPORT CerverHttpClientState cerver_http_context_client_state (
	const CerverHttpContext *context
);

/** @} */

/**
 * @defgroup http_main CerverHttp
 * @ingroup http_core
 * @brief The HTTP data of a web cerver.
 * @{
 */

/**
 * @brief Allocates a CerverHttp with the default values.
 *
 * @return the new CerverHttp, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverHttp *cerver_http_new (void);

/**
 * @brief Releases a CerverHttp: its paths, its routes, its keys, its headers, its admin lists and its WebSocket
 * registry, and its data with its delete method.
 *
 * @param[in] cerver_http_ptr the CerverHttp, it can be NULL
 * @note For the internal methods of cerver: the cerver releases it in its teardown.
 */
CERVER_PRIVATE void cerver_http_delete (
	void *cerver_http_ptr
);

/**
 * @brief Returns the CerverHttp of a web cerver.
 *
 * @param[in] cerver the cerver
 * @return its CerverHttp, NULL for a NULL cerver; the data of the cerver as a CerverHttp, so the cerver must be
 * of type CERVER_TYPE_WEB
 */
CERVER_EXPORT CerverHttp *cerver_http_get (
	struct _Cerver *cerver
);

/**
 * @brief Creates the CerverHttp of a cerver, with its lists, its routes tree, its default handlers and its
 * WebSocket registry.
 *
 * @param[in] cerver the cerver
 * @return the new CerverHttp, NULL if it could not be allocated
 * @note For the internal methods of cerver: cerver_create () calls it for CERVER_TYPE_WEB.
 */
CERVER_PRIVATE CerverHttp *cerver_http_create (
	struct _Cerver *cerver
);

/**
 * @brief Initializes a CerverHttp when its cerver starts: the state that the HTTP cervers of the process share,
 * the keys and the routes.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @retval 0 the CerverHttp is ready
 * @retval 1 a NULL CerverHttp or it failed to initialize
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_http_init (
	CerverHttp *http_cerver
);

/**
 * @brief Releases what cerver_http_init () took: the reference to the state that the HTTP cervers of the process
 * share.
 *
 * @param[in,out] http_cerver the CerverHttp, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_end (
	CerverHttp *http_cerver
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
