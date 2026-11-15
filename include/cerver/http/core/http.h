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
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
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

#define HTTP_CERVER_MULTI_PARTS_POOL_INIT			32

#define HTTP_CERVER_UPLOADS_PATH_SIZE				256

#define HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE		0755
#define HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE		0644

// requests limits, a value of 0 disables the limit,
// except the url and the headers limits, that are always active
#define HTTP_CERVER_DEFAULT_MAX_BODY_SIZE			(1 << 29)
#define HTTP_CERVER_DEFAULT_MAX_URL_SIZE			(1 << 13)
#define HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE			(1 << 16)
#define HTTP_CERVER_DEFAULT_MAX_HEADERS				(1 << 7)
#define HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE	(1 << 13)
#define HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS		(1 << 10)
#define HTTP_CERVER_DEFAULT_MAX_FILE_SIZE			(1 << 29)

// the most that is reserved at once for a body from its Content-Length,
// a bigger body grows geometrically as its bytes arrive
#define HTTP_REQUEST_BODY_RESERVE_LIMIT				(1 << 24)

#define HTTP_MILLISECONDS_PER_SECOND				1000

// secs to receive a request's headers, a slower request is answered with 408
#define HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT			(1 << 4)
#define HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

// secs to receive a complete request, a slower one is answered with 408:
// a body that arrives a few bytes at a time can not keep its worker forever
// 0 disables the timeout, so a body can take any time while it keeps making progress
#define HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT			300
#define HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

// secs that a connection can wait without receiving anything,
// a request that stops arriving for longer ends (with 408 if it had started)
#define HTTP_CERVER_DEFAULT_IDLE_TIMEOUT			5
#define HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS	\
	(HTTP_CERVER_DEFAULT_IDLE_TIMEOUT * HTTP_MILLISECONDS_PER_SECOND)

// a client that closed its side of the connection (EOF) is taken as gone,
// although a half-close (shutdown (SHUT_WR)) client still waits for the response
#define HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE		true

// the most bytes read from a connection before closing it
// when a request was rejected before its body was received
#define HTTP_RECEIVE_DRAIN_SIZE						(1 << 16)
#define HTTP_RECEIVE_DRAIN_BUFFER_SIZE				(1 << 12)
#define HTTP_RECEIVE_DRAIN_TIMEOUT					1

// joins the values of a repeated header (RFC 9110 section 5.3)
#define HTTP_HEADER_VALUES_SEPARATOR				", "
#define HTTP_HEADER_VALUES_SEPARATOR_LEN			2

// the room of a joined header in the arena of its request, for each byte that it needs
#define HTTP_HEADER_VALUES_GROWTH					2

#define HTTP_CERVER_DEFAULT_UPLOADS_DELETE			false

#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN			false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO		true
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS		false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS	false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH		false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS		false

#define HTTP_CERVER_STATIC_FILE_SIZE				256

// every response has a Date header (RFC 9110 section 6.6.1)
#define HTTP_CERVER_DEFAULT_DATE_HEADER				true

// the requests are not identified unless the cerver enables it
#define HTTP_CERVER_DEFAULT_REQUEST_ID				false

// the responses are counted by their status code, from 100 to 599
#define HTTP_CERVER_RESPONSES_STATUS_SIZE			600

// the longest bearer token that is decoded, 0 has no limit
#define HTTP_CERVER_DEFAULT_JWT_MAX_SIZE			(1 << 13)

// a token without "exp" is accepted unless the cerver requires it
#define HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION	false

// the seconds of clock skew accepted in "exp" and "nbf"
#define HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS		0

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

#pragma region parser

// the lenient flags of the parser (llhttp), the values of LLHTTP_LENIENT_MAP,
// all of them are disabled by default (the strict mode),
// each one can open the cerver to request smuggling or cache poisoning attacks
typedef enum HttpParserLenient {

	HTTP_PARSER_LENIENT_NONE = 0,

	#define XX(num, name, string, description) HTTP_PARSER_LENIENT_##name = num,
	LLHTTP_LENIENT_MAP(XX)
	#undef XX

} HttpParserLenient;

// every lenient flag of LLHTTP_LENIENT_MAP
#define HTTP_PARSER_LENIENT_ALL				LLHTTP_LENIENT_ALL

// the value that a parser callback returns when it stops the parser,
// because it rejected the request or because its client is gone
#define HTTP_RECEIVE_PARSER_STOP			HPE_USER

// the name of a lenient flag ("headers", "chunked_length", ...)
CERVER_PUBLIC const char *http_parser_lenient_string (
	const HttpParserLenient lenient
);

// what a lenient flag accepts
CERVER_PUBLIC const char *http_parser_lenient_description (
	const HttpParserLenient lenient
);

#pragma endregion

#pragma region stages

// the stage of a request where cerver noticed that its client left,
// the label is the value of the stage label of the metrics
#define HTTP_STAGE_MAP(XX)									\
	XX(0, QUEUED,		Queued,			queued)				\
	XX(1, REQUEST_LINE,	Request Line,	request_line)		\
	XX(2, HEADERS,		Headers,		headers)			\
	XX(3, BODY,			Body,			body)				\
	XX(4, UPLOAD,		Upload,			upload)				\
	XX(5, HANDLER,		Handler,		handler)			\
	XX(6, RESPONSE,		Response,		response)

typedef enum HttpStage {

	#define XX(num, name, string, label) HTTP_STAGE_##name = num,
	HTTP_STAGE_MAP(XX)
	#undef XX

} HttpStage;

#define HTTP_STAGES									7

CERVER_PUBLIC const char *http_stage_string (
	const HttpStage stage
);

CERVER_PUBLIC const char *http_stage_label (
	const HttpStage stage
);

// how a client that left ended its connection
#define HTTP_ABORT_REASON_MAP(XX)							\
	XX(0, CLOSED,		Closed,			closed)				\
	XX(1, RESET,		Reset,			reset)

typedef enum HttpAbortReason {

	#define XX(num, name, string, label) HTTP_ABORT_REASON_##name = num,
	HTTP_ABORT_REASON_MAP(XX)
	#undef XX

} HttpAbortReason;

#define HTTP_ABORT_REASONS							2

CERVER_PUBLIC const char *http_abort_reason_string (
	const HttpAbortReason reason
);

CERVER_PUBLIC const char *http_abort_reason_label (
	const HttpAbortReason reason
);

// the state of the client of a request, while its handler runs
// CLOSED is an EOF, a full close and a half-close are the same in TCP
#define HTTP_CLIENT_STATE_MAP(XX)							\
	XX(0, CONNECTED,	Connected)							\
	XX(1, CLOSED,		Closed)								\
	XX(2, RESET,		Reset)

typedef enum HttpClientState {

	#define XX(num, name, string) HTTP_CLIENT_STATE_##name = num,
	HTTP_CLIENT_STATE_MAP(XX)
	#undef XX

} HttpClientState;

CERVER_PUBLIC const char *http_client_state_string (
	const HttpClientState state
);

#pragma endregion

#pragma region main

struct _HttpCerver {

	struct _Cerver *cerver;

	// paths to serve static files
	DoubleList *static_paths;

	// list of top level routes of the first routes API,
	// they are translated to endpoints of the routes tree when the cerver starts
	DoubleList *routes;

	const HttpRoute *main_route;

	// the groups, the endpoints and the compiled routes tree
	HttpTree *tree;

	// catch all route (/*)
	void (*default_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	// returns not found on not matching requests 
	bool not_found_handler;
	void (*not_found)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	// uploads
	int uploads_path_len;
	char uploads_path[HTTP_CERVER_UPLOADS_PATH_SIZE];

	unsigned int uploads_file_mode;
	void (*uploads_filename_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	unsigned int uploads_dir_mode;
	void (*uploads_dirname_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	// delete uploaded files when the request ends
	bool uploads_delete_when_done;

	// the last number given to a request by the default uploads generators,
	// so their names are unique even if a fd is reused in the same second
	size_t uploads_sequence;

	// auth
	jwt_alg_t jwt_alg;

	String *jwt_opt_key_name;		// jwt private key filename
	struct JwtKey *jwt_private_key;	// signs the tokens, loaded when the cerver starts

	String *jwt_opt_pub_key_name;	// jwt public key filename
	struct JwtKey *jwt_public_key;	// verifies the tokens, loaded when the cerver starts

	size_t jwt_max_size;			// the longest token that is decoded, 0 has no limit
	bool jwt_require_expiration;	// a token without "exp" is rejected
	unsigned int jwt_leeway_seconds;// clock skew accepted in "exp" and "nbf"
	String *jwt_issuer;				// the "iss" that a token must have, if it is set
	String *jwt_audience;			// the "aud" that a token must have, if it is set

	u8 n_origins;
	HttpOrigin origins_whitelist[HTTP_ORIGINS_SIZE];

	// data
	void *custom_data;
	void (*delete_custom_data)(void *);

	// the headers that are added to every response that does not have them
	HttpHeaders response_headers;

	// holds a reference to the state shared by every HTTP cerver of the process:
	// the jwt pool, the responses producer, the common responses and the multi-parts pool,
	// created by the first HTTP cerver that starts and released by the last one that ends
	bool global_reference;

	// stats
	size_t n_incompleted_requests;	// the parser could not parse the request
	size_t n_unhandled_requests;	// failed to get matching route

	size_t n_catch_all_requests;	// redirected to catch all route
	size_t n_failed_auth_requests;	// failed to auth with private route 

	// admins
	bool enable_admin_routes;
	bool admin_routes_ready;		// the admin routes were added to the routes
	bool enable_admin_info_route;
	bool enable_admin_head_handlers;
	bool enable_admin_options_handlers;

	bool enable_admin_routes_auth;
	HttpRouteAuthType admin_auth_type;
	void *(*admin_decode_data)(void *);
	void (*admin_delete_decoded_data)(void *);
	unsigned int (*admin_auth_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	);

	void *admin_routes_custom_data;
	void (*delete_admin_routes_custom_data)(void *);

	bool enable_admin_cors_headers;
	HttpOrigin admin_origin;
	
	DoubleList *admin_file_systems_stats;
	DoubleList *admin_workers;
	pthread_mutex_t *admin_mutex;

	// used to correctly update stats
	pthread_mutex_t *mutex;

	// the lenient flags (HttpParserLenient) of the parser of each request, 0 is the strict mode
	unsigned int parser_lenient;

	// requests limits, a value of 0 disables the limit (the url and the headers limits are always active)
	size_t max_body_size;
	size_t max_url_size;
	size_t max_header_size;
	unsigned int max_headers;
	unsigned int max_multipart_parts;
	size_t max_file_size;
	size_t max_custom_header_size;

	size_t n_limited_requests;		// rejected because they exceeded a limit

	// requests timeouts in milliseconds, a value of 0 disables the timeout
	unsigned int headers_timeout;
	unsigned int request_timeout;

	// the most milliseconds without receiving anything, it is never disabled
	unsigned int idle_timeout;

	// the most milliseconds that all the sends of a response take together,
	// 0 uses the send timeout of the cerver (secs)
	unsigned int send_timeout;

	// the WebSocket connections, their limit and their stats
	struct _HttpWebSocketRegistry *web_sockets;

	size_t n_timeout_requests;		// answered with 408

	size_t n_unanswered_requests;	// answered with 500 because the handler did not respond

	// the headers that cerver adds to every response
	bool date_header;
	bool request_id;

	// the compression of the endpoints with the compression setting
	size_t compression_min_size;
	int compression_level;

	// the peers whose X-Forwarded-For and X-Real-IP are used to find the client's address
	HttpTrustedProxies trusted_proxies;
	unsigned int n_trusted_proxies;
	HttpProxyNetwork trusted_proxies_networks[HTTP_TRUSTED_PROXIES_SIZE];

	// the responses sent by their status code, updated with atomic operations
	size_t n_responses_by_status[HTTP_CERVER_RESPONSES_STATUS_SIZE];

	// http_receive_is_client_gone () takes a client that closed its side as gone
	bool half_close_is_gone;

	// the requests whose client left, by the stage where cerver noticed it and how it left,
	// updated with atomic operations
	size_t n_aborted_requests[HTTP_STAGES][HTTP_ABORT_REASONS];

	// the requests whose client stopped sending or reading, ended by a timeout, by stage
	size_t n_vanished_requests[HTTP_STAGES];

	// the requests whose response could not be sent completely
	size_t n_failed_responses;

};

typedef struct _HttpCerver HttpCerver;

CERVER_PRIVATE HttpCerver *http_cerver_new (void);

CERVER_PRIVATE void http_cerver_delete (
	void *http_cerver_ptr
);

CERVER_EXPORT HttpCerver *http_cerver_get (
	struct _Cerver *cerver
);

CERVER_PRIVATE HttpCerver *http_cerver_create (
	struct _Cerver *cerver
);

CERVER_PRIVATE u8 http_cerver_init (
	HttpCerver *http_cerver
);

// destroy values allocated in http_cerver_init ()
CERVER_PRIVATE void http_cerver_end (
	HttpCerver *http_cerver
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
