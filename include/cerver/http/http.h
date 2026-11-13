#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/headers.h"
#include "cerver/http/middleware.h"
#include "cerver/http/multipart.h"
#include "cerver/http/origin.h"
#include "cerver/http/proxy.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"
#include "cerver/http/status.h"
#include "cerver/http/stream.h"
#include "cerver/http/tree.h"
#include "cerver/http/validator.h"

#include "cerver/http/jwt/alg.h"

#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"

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

#pragma region kvp

typedef struct KeyValuePair { 

	String *key;
	String *value;

} KeyValuePair;

CERVER_PUBLIC KeyValuePair *key_value_pair_new (void);

CERVER_PUBLIC void key_value_pair_delete (void *kvp_ptr);

CERVER_PUBLIC KeyValuePair *key_value_pair_create (
	const char *key, const char *value
);

CERVER_PUBLIC const String *key_value_pairs_get_value (
	const DoubleList *pairs, const char *key
);

CERVER_PUBLIC void key_value_pairs_print (
	const DoubleList *pairs
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

#pragma region public

typedef struct HttpStaticPath {

	String *path;

	HttpRouteAuthType auth_type;

	// the data of a bearer JWT, for the request of a file
	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	// the method of HTTP_ROUTE_AUTH_TYPE_CUSTOM
	HttpAuthenticationHandler authentication_handler;

} HttpStaticPath;

// sets the authentication of the files of a whole static path,
// a request is authenticated with the static path that has its file,
// or with the first one that has authentication when none has it,
// so a file that does not exist is answered like a protected one
// a request that fails it is answered with 401, before its file is looked for
CERVER_EXPORT void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
);

// sets the method that decodes the data of the bearer JWT of a static path
// (HTTP_ROUTE_AUTH_TYPE_BEARER), and the one that deletes it after the request
CERVER_EXPORT void http_static_path_set_decode_data (
	HttpStaticPath *static_path,
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
);

// sets the method that authenticates the requests of a static path
// with HTTP_ROUTE_AUTH_TYPE_CUSTOM, it returns 0 on success and 1 on error
CERVER_EXPORT void http_static_path_set_authentication_handler (
	HttpStaticPath *static_path,
	HttpAuthenticationHandler authentication_handler
);

// adds a new static path where static files can be served upon request
// it is recomened to set absoulute paths
CERVER_EXPORT HttpStaticPath *http_cerver_static_path_add (
	HttpCerver *http_cerver, const char *static_path
);

// removes a path from the served public paths
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_receive_public_path_remove (
	HttpCerver *http_cerver, const char *static_path
);

#pragma endregion

#pragma region routes

// sets the HTTP cerver's main route (top level route)
// used to enable admin routes
// if no route has been set, the first top level route
// will be used
CERVER_EXPORT void http_cerver_set_main_route (
	HttpCerver *http_cerver, const HttpRoute *route
);

// registers a new HTTP route to the HTTP cerver
CERVER_EXPORT void http_cerver_route_register (
	HttpCerver *http_cerver, HttpRoute *route
);

// sets a route to catch any requet
// that didn't match any registered route
CERVER_EXPORT void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver, 
	void (*catch_all_route)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// enables the use of the default not found handler
// which returns 404 status on no matching routes
// by default this option is disabled
// as the catch all handler is used instead
CERVER_EXPORT void http_cerver_set_not_found_handler (
	HttpCerver *http_cerver
);

// sets a custom handler to be executed on no matching routes
// it should return status 404
CERVER_EXPORT void http_cerver_set_not_found_route (
	HttpCerver *http_cerver, 
	void (*not_found)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// answers with 405 and the Allow header a request whose path exists
// but has no handler for its method, instead of the catch all handler
// the default value is false
CERVER_EXPORT void http_cerver_set_method_not_allowed (
	HttpCerver *http_cerver, const bool method_not_allowed
);

// sets a handler that sends the 405 response (with any body),
// it gets the value of the Allow header with http_receive_get_allow ()
CERVER_EXPORT void http_cerver_set_method_not_allowed_handler (
	HttpCerver *http_cerver, const HttpHandler handler
);

// sets how a path with a trailing slash is matched
// the default value is HTTP_TRAILING_SLASH_TOLERANT
CERVER_EXPORT void http_cerver_set_trailing_slash (
	HttpCerver *http_cerver, const HttpTrailingSlash trailing_slash
);

// sets the most params of a route, a route with more is a registration error
// the default value is HTTP_TREE_DEFAULT_MAX_PARAMS
CERVER_EXPORT void http_cerver_set_max_route_params (
	HttpCerver *http_cerver, const unsigned int max_params
);

// registers a validator that a route param uses with ":name<validator>"
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_register_param_validator (
	HttpCerver *http_cerver, const char *name, const HttpParamValidate validate
);

// adds a middleware that runs before the handler of every endpoint
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_before (
	HttpCerver *http_cerver, const HttpBeforeMiddleware middleware
);

// adds a middleware that runs after the handler of every endpoint
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_after (
	HttpCerver *http_cerver, const HttpAfterMiddleware middleware
);

// returns the first routes registration error, NULL if there is none
CERVER_EXPORT const char *http_cerver_routes_error (
	const HttpCerver *http_cerver
);

// returns the number of routes registration errors
CERVER_EXPORT size_t http_cerver_routes_errors_count (
	const HttpCerver *http_cerver
);

// builds the routes tree with the admin routes, it is done when the cerver starts
// returns 0 on success, 1 on a registration error
CERVER_EXPORT u8 http_cerver_routes_compile (
	HttpCerver *http_cerver
);

// prints the compiled routes tree
CERVER_EXPORT void http_cerver_routes_print (
	const HttpCerver *http_cerver
);

// the compiled endpoints as a json string, NULL if the routes are not compiled
// {"n_endpoints": N, "endpoints": [{"method", "path", "auth", "modifier", "compatibility", "cors", "etag", "compression"}]}
// it is deleted with http_cerver_routes_json_delete ()
CERVER_EXPORT char *http_cerver_routes_generate_json (
	const HttpCerver *http_cerver
);

CERVER_EXPORT void http_cerver_routes_json_delete (char *json);

#pragma endregion

#pragma region uploads

// sets the default uploads path where any multipart file will be saved
// this method will replace the previous value with the new one
CERVER_EXPORT void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
);

// works like http_cerver_set_uploads_path () but can generate
// a custom path on the fly using variable arguments
CERVER_EXPORT void http_cerver_generate_uploads_path (
	HttpCerver *http_cerver, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

// sets the mode_t to be used when creating uploads files
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE
CERVER_EXPORT void http_cerver_set_uploads_file_mode (
	HttpCerver *http_cerver, const unsigned int file_mode
);

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%zu-%u-%s" using "sock_fd-time (NULL)-sequence-file index-multi_part->filename"
// the sequence is unique for each request of the cerver, even if its fd is reused in the same second,
// and the file index keeps unique the files of a request that have the same filename
CERVER_EXPORT void http_cerver_default_uploads_filename_generator (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_SIZE
CERVER_EXPORT void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets the HTTP cerver's uploads filename generator
// to be http_cerver_default_uploads_filename_generator ()
CERVER_EXPORT void http_cerver_set_default_uploads_filename_generator (
	HttpCerver *http_cerver
);

// sets the mode_t value to be used when creating uploads dirs
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE
CERVER_EXPORT void http_cerver_set_uploads_dir_mode (
	HttpCerver *http_cerver, const unsigned int dir_mode
);

// method that can be used to generate multi-part uploads dirnames
// with format "%d-%ld-%zu" using "sock_fd-time (NULL)-sequence"
// the files of the request share its sequence
CERVER_EXPORT void http_cerver_default_uploads_dirname_generator (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// sets a method to be called on every new multi-part request
// that will be used to generate a new directory
// inside the uploads path to save all the files from the request
CERVER_EXPORT void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	void (*uploads_dirname_generator)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets the HTTP cerver's uploads dirname generator
// to be http_cerver_default_uploads_dirname_generator ()
CERVER_EXPORT void http_cerver_set_default_uploads_dirname_generator (
	HttpCerver *http_cerver
);

// specifies whether uploads are deleted after the requested has ended
// unless the request files have been explicitly saved using
// http_request_multi_part_keep_files ()
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DELETE
CERVER_EXPORT void http_cerver_set_uploads_delete_when_done (
	HttpCerver *http_cerver, bool value
);

#pragma endregion

#pragma region limits

// sets the max size in bytes of a request's body
// a bigger request is answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_BODY_SIZE, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_body_size (
	HttpCerver *http_cerver, const size_t max_body_size
);

// sets the max size in bytes of a request's url (path and query)
// a bigger url is answered with 414
// the default value is HTTP_CERVER_DEFAULT_MAX_URL_SIZE,
// the limit is always active, 0 sets the default value
CERVER_EXPORT void http_cerver_set_max_url_size (
	HttpCerver *http_cerver, const size_t max_url_size
);

// sets the max size in bytes of all of a request's headers
// bigger headers are answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE,
// the limit is always active (the parser does not have one of its own), 0 sets the default value
CERVER_EXPORT void http_cerver_set_max_header_size (
	HttpCerver *http_cerver, const size_t max_header_size
);

// sets the max number of headers of a request
// more headers are answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_HEADERS, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_headers (
	HttpCerver *http_cerver, const unsigned int max_headers
);

// sets the max size in bytes of the value of a header that is not one of the HTTP headers map
// (a custom header, like the ones of a trace or the ones that a proxy adds),
// with the values of its repetitions joined; a bigger value is answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE, 0 disables the limit,
// and the size of all the headers is still limited by http_cerver_set_max_header_size ()
CERVER_EXPORT void http_cerver_set_max_custom_header_size (
	HttpCerver *http_cerver, const size_t max_custom_header_size
);

// sets the max number of parts of a multi-part request
// more parts are answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_multipart_parts (
	HttpCerver *http_cerver, const unsigned int max_multipart_parts
);

// sets the max size in bytes of each file of a multi-part request
// a bigger file is answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_FILE_SIZE, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_file_size (
	HttpCerver *http_cerver, const size_t max_file_size
);

// sets the secs to receive a request's headers
// a client that is slower (like a slowloris) is answered with 408
// the default value is HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT, 0 disables the timeout
CERVER_EXPORT void http_cerver_set_headers_timeout (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

// the same as http_cerver_set_headers_timeout (), in milliseconds
CERVER_EXPORT void http_cerver_set_headers_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

// sets the secs to receive a complete request (headers and body)
// a slower request is answered with 408
// while it is disabled, a body only needs to make progress
// before the idle timeout (http_cerver_set_idle_timeout_milliseconds ())
// the default value is HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT, 0 disables the timeout
CERVER_EXPORT void http_cerver_set_request_timeout (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

// the same as http_cerver_set_request_timeout (), in milliseconds
CERVER_EXPORT void http_cerver_set_request_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

// sets the most milliseconds that a connection waits without receiving anything,
// a request that had started is answered with 408, an empty connection is only closed
// the default value is HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS, 0 keeps the default
CERVER_EXPORT void http_cerver_set_idle_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int idle_timeout
);

// sets the most milliseconds that all the sends of a response take together,
// headers, body and files, also in a blocking socket with a longer SO_SNDTIMEO,
// so a client that stops reading costs that time in total and not in each send,
// each piece of a stream has its own deadline
// the default value is 0, the send timeout of the cerver (cerver_set_send_timeout (), in secs)
CERVER_EXPORT void http_cerver_set_send_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int send_timeout
);

// sets if http_receive_is_client_gone () takes a client that closed its side
// of the connection (HTTP_CLIENT_STATE_CLOSED) as gone,
// a client that uses a half-close to end its request still waits for the response
// the default value is HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE
CERVER_EXPORT void http_cerver_set_half_close_is_gone (
	HttpCerver *http_cerver, const bool half_close_is_gone
);

// sets the lenient flags (HttpParserLenient, combined with |) of the parser of every request,
// each flag accepts requests that the strict mode answers with 400,
// and it can open the cerver to request smuggling or cache poisoning attacks,
// so a warning is logged for each flag that is enabled
// the default value is HTTP_PARSER_LENIENT_NONE (the strict mode)
CERVER_EXPORT void http_cerver_set_parser_lenient (
	HttpCerver *http_cerver, const unsigned int lenient
);

#pragma endregion

#pragma region headers

// adds the Date header to every response
// the default value is HTTP_CERVER_DEFAULT_DATE_HEADER
CERVER_EXPORT void http_cerver_set_date_header (
	HttpCerver *http_cerver, const bool date_header
);

// identifies every request with the X-Request-Id that it has
// (up to HTTP_REQUEST_ID_LENGTH characters of [A-Za-z0-9._-]) or with a new UUID v4,
// the id is sent back in the X-Request-Id of the response and it is logged with the request
// the default value is HTTP_CERVER_DEFAULT_REQUEST_ID
CERVER_EXPORT void http_cerver_set_request_id (
	HttpCerver *http_cerver, const bool request_id
);

// the smallest body that is compressed
// the default value is HTTP_COMPRESSION_DEFAULT_MIN_SIZE
CERVER_EXPORT void http_cerver_set_compression_min_size (
	HttpCerver *http_cerver, const size_t min_size
);

// the zlib level, from HTTP_COMPRESSION_MIN_LEVEL to HTTP_COMPRESSION_MAX_LEVEL
// the default value is HTTP_COMPRESSION_DEFAULT_LEVEL
CERVER_EXPORT void http_cerver_set_compression_level (
	HttpCerver *http_cerver, const int level
);

#pragma endregion

#pragma region proxies

// sets from which peers the X-Forwarded-For and X-Real-IP headers are used
// to find the client's address (http_request_get_client_ip ())
// the default value is HTTP_CERVER_DEFAULT_TRUSTED_PROXIES
CERVER_EXPORT void http_cerver_set_trusted_proxies (
	HttpCerver *http_cerver, const HttpTrustedProxies trusted_proxies
);

// adds a network in CIDR notation ("172.16.0.0/12", "fc00::/7" or a single address)
// to the trusted proxies of HTTP_TRUSTED_PROXIES_LIST
// an invalid network or more than HTTP_TRUSTED_PROXIES_SIZE stop the cerver when it starts
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_trusted_proxy (
	HttpCerver *http_cerver, const char *cidr
);

#pragma endregion

#pragma region origins

// adds a new domain to the HTTP cerver's origins whitelist
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_origin_to_whitelist (
	HttpCerver *http_cerver, const char *domain
);

CERVER_EXPORT void http_cerver_print_origins_whitelist (
	const HttpCerver *http_cerver
);

#pragma endregion

#pragma region data

// gets the HTTP cerver's custom data
// this data can be safely set by the user and accessed at any time
CERVER_EXPORT const void *http_cerver_get_custom_data (
	const HttpCerver *http_cerver
);

// sets the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
);

// sets a custom method to delete the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
);

// sets free () as the method to delete the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_default_delete_custom_data (
	HttpCerver *http_cerver
);

#pragma endregion

#pragma region responses

CERVER_PUBLIC struct _HttpResponse *oki_doki;
CERVER_PUBLIC struct _HttpResponse *bad_request_error;
CERVER_PUBLIC struct _HttpResponse *bad_user_error;
CERVER_PUBLIC struct _HttpResponse *bad_auth_error;
CERVER_PUBLIC struct _HttpResponse *not_found_error;
CERVER_PUBLIC struct _HttpResponse *server_error;

// adds a new global responses header
// this header will be added to all the responses
// if the response has the same header type,
// it will be used instead of the global header
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 http_cerver_add_responses_header (
	HttpCerver *http_cerver,
	const http_header type, const char *actual_header
);

// works like http_cerver_add_responses_header () with the name of any header,
// it replaces the ones with the same name
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 http_cerver_add_responses_header_by_name (
	HttpCerver *http_cerver, const char *name, const char *value
);

#pragma endregion

#pragma region stats

CERVER_PRIVATE size_t http_cerver_stats_get_children_routes (
	const HttpCerver *http_cerver, size_t *handlers
); 

// print number of routes & handlers
CERVER_PUBLIC void http_cerver_routes_stats_print (
	const HttpCerver *http_cerver
);

// print route's stats
CERVER_PUBLIC void http_cerver_route_stats_print (
	const HttpRoute *route
);

// prints all HTTP cerver stats, general & by route
CERVER_PUBLIC void http_cerver_all_stats_print (
	const HttpCerver *http_cerver
);

#pragma endregion

#pragma region admin

// enables the ability to have admin routes
// to fetch cerver's HTTP stats
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN
CERVER_EXPORT void http_cerver_enable_admin_routes (
	HttpCerver *http_cerver, const bool enable
);

// enables the ability to have an admin info route
// to fetch cerver's information
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO
CERVER_EXPORT void http_cerver_enable_admin_info_route (
	HttpCerver *http_cerver, const bool enable
);

// enables HTTP admin routes to handle HEAD requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS
CERVER_EXPORT void http_cerver_enable_admin_head_handlers (
	HttpCerver *http_cerver, const bool enable
);

// enables HTTP admin routes to handle OPTIONS requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS
CERVER_EXPORT void http_cerver_enable_admin_options_handlers (
	HttpCerver *http_cerver, const bool enable
);

// enables authentication in admin routes
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH
CERVER_EXPORT void http_cerver_enable_admin_routes_authentication (
	HttpCerver *http_cerver, const HttpRouteAuthType auth_type
);

// sets the method to be used to decode incoming data from JWT
// and sets a method to delete it after use
// if no delete method is set, data won't be freed
CERVER_EXPORT void http_cerver_admin_routes_auth_set_decode_data (
	HttpCerver *http_cerver,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

// works like http_cerver_enable_admin_routes_authentication ()
// but sets a method to decode data from a JWT into a json string
CERVER_EXPORT void http_cerver_admin_routes_auth_decode_to_json (
	HttpCerver *http_cerver
);

// sets a method to be used to handle auth in admin routes
// HTTP cerver must had been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
CERVER_EXPORT void http_cerver_admin_routes_set_authentication_handler (
	HttpCerver *http_cerver,
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets a custom data reference to be set in all admin routes
CERVER_EXPORT void http_cerver_admin_routes_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
);

// sets how the custom data will be disposed when the service gets destroyed
CERVER_EXPORT void http_cerver_admin_routes_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
);

// enables CORS headers in admin routes responses
// always uses admin origin's value
// if there is no dedicated origin, it will dynamically
// set the header based on the origins whitelist
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS
CERVER_EXPORT void http_cerver_enable_admin_cors_headers (
	HttpCerver *http_cerver, const bool enable
);

// sets the dedicated domain that will be walways set
// in the admin responses CORS headers
CERVER_EXPORT void http_cerver_admin_set_origin (
	HttpCerver *http_cerver, const char *domain
);

// registers a new file system to be handled
// when requesting for fs stats
CERVER_EXPORT void http_cerver_register_admin_file_system (
	HttpCerver *http_cerver, const char *path
);

// registers an existing worker to be handled
// when requisting for workers states
CERVER_EXPORT void http_cerver_register_admin_worker (
	HttpCerver *http_cerver, const struct _Worker *worker
);

#pragma endregion

#pragma region parser

// parses a url query into key value pairs for better manipulation
CERVER_PUBLIC DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
);

// gets the matching value for the requested key from a list of pairs
CERVER_PUBLIC const String *http_query_pairs_get_value (
	const DoubleList *pairs, const char *key
);

CERVER_PUBLIC void http_query_pairs_print (
	const DoubleList *pairs
);

#pragma endregion

#pragma region handler

#define HTTP_RECEIVE_STATUS_MAP(XX)			\
	XX(0,  NONE,			Undefined)		\
	XX(1,  HEADERS,			Headers)		\
	XX(2,  BODY,			Body)			\
	XX(3,  COMPLETED,		Completed)		\
	XX(4,  INCOMPLETED,		Incompleted)	\
	XX(5,  UNHANDLED,		Unhandled)		\
	XX(6,  TIMEOUT,			Timeout)		\
	XX(7,  CLOSED,			Closed)			\
	XX(8,  PARSER_ERROR,	Parser Error)	\
	XX(9,  LIMIT,			Limit)			\
	XX(10, REJECTED,		Rejected)		\
	XX(11, UPLOAD_ERROR,	Upload Error)	\
	XX(12, RESPONSE_FAILED,	Response Failed)

typedef enum HttpReceiveStatus {

	#define XX(num, name, string) HTTP_RECEIVE_STATUS_##name = num,
	HTTP_RECEIVE_STATUS_MAP(XX)
	#undef XX

} HttpReceiveStatus;

CERVER_PUBLIC const char *http_receive_status_str (
	const HttpReceiveStatus status
);

typedef enum HttpReceiveType {

	HTTP_RECEIVE_TYPE_NONE		= 0,
	HTTP_RECEIVE_TYPE_FILE		= 1,
	HTTP_RECEIVE_TYPE_ROUTE		= 2

} HttpReceiveType;

// how a request that matched a path of the routes tree is answered
typedef enum HttpReceiveDispatch {

	HTTP_RECEIVE_DISPATCH_NONE					= 0,
	HTTP_RECEIVE_DISPATCH_ENDPOINT				= 1,
	HTTP_RECEIVE_DISPATCH_OPTIONS				= 2,
	HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED	= 3,
	HTTP_RECEIVE_DISPATCH_CATCH_ALL				= 4,
	HTTP_RECEIVE_DISPATCH_REDIRECT				= 5,

	HTTP_RECEIVE_DISPATCH_MAX					= 6

} HttpReceiveDispatch;

// what the owner of a receive lends to it: the arena where the data of each request lives
// and the buffers of the tokens that the parser gives in pieces
// they are used by one request at a time and they are never released by the receive
struct _HttpReceiveResources {

	struct Arena *arena;

	String *url_buffer;
	String *field_buffer;
	String *value_buffer;

};

typedef struct _HttpReceiveResources HttpReceiveResources;

// keeps track of the recv () calls of a request
// to explain why it ended before being completed
struct _HttpReceiveDiagnostics {

	ssize_t last_rc;
	int last_errno;
	unsigned int n_recvs;

	// the last recv () timed out or a deadline expired
	bool timed_out;

};

typedef struct _HttpReceiveDiagnostics HttpReceiveDiagnostics;

// what belongs to the connection (its cerver receive, the parser, the resources
// of its owner, the state of its client) stays for all its requests,
// and what belongs to a request is started again by http_receive_reset ()
struct _HttpReceive {

	HttpReceiveStatus receive_status;

	CerverReceive *cr;

	// keep connection alive
	// don't close after request has ended
	bool keep_alive;

	void (*handler)(
		struct _HttpReceive *, ssize_t, char *
	);

	HttpCerver *http_cerver;

	// the parser of the request, embedded in the receive, with its own settings,
	// so the body callback can change for each request
	llhttp_t parser;
	llhttp_settings_t settings;

	// what the client asked when its request was completed (llhttp_should_keep_alive ()),
	// the connection is still closed after the response
	bool keep_alive_requested;

	// the bytes of the last buffer that were not parsed after the request was completed
	// (a pipelined request)
	size_t unparsed;

	// the protocol of the request line arrives in pieces: the bytes of it that are the ones of HTTP,
	// and whether it had any other one (llhttp also parses the request lines of RTSP and ICE)
	size_t protocol_length;
	bool protocol_other;

	multipart_parser *mpart_parser;
	multipart_parser_settings mpart_settings;

	HttpRequest *request;

	// found
	HttpReceiveType type;
	HttpReceiveDispatch dispatch;
	const HttpRouteNode *node;
	HttpEndpoint *endpoint;

	// the route of the first routes API of the endpoint, NULL for the new endpoints
	HttpRoute *route;

	const char *served_file;

	// a HEAD request handled by the GET handler, the responses are sent without their body
	bool head;

	// the body limit of the request, the endpoint's one or the cerver's
	size_t max_body_size;

	http_status status;
	size_t sent;

	bool is_multi_part;
	struct _HttpRouteFileStats *file_stats;

	// the number of the request in the default uploads generators, 0 until one asks for it
	size_t upload_sequence;

	// the request created the directory of its uploads, it is removed if it ends empty
	bool upload_directory_created;

	// the pieces of the url, the header field and the header value that are being received,
	// each one is closed by its complete callback of the parser
	String *url_buffer;
	String *field_buffer;
	String *value_buffer;

	// used to check the requests limits
	size_t header_size;
	unsigned int n_headers;
	size_t body_size;
	unsigned int n_parts;

	// cerver already tried to send a response, even if it failed:
	// before the request was completed, the rest of the request is not handled,
	// after its handler, a request without one is answered with 500
	bool responded;

	// the response that was sent has an end that its client can find without the close
	// of the connection: it has a Content-Length, its status has no body (1xx, 204, 304),
	// it answers a HEAD or it is a chunked stream that sent its last chunk
	// a connection can only take another request after a response like that
	bool framed;

	// the response that the handler sends in pieces
	HttpStream stream;

	// the arena that the owner of the receive lends to it, the data of its request
	// lives there and it is released all at once when the request ends,
	// NULL in a receive that allocates the memory of its request
	struct Arena *arena;

	// the token buffers belong to the owner of the receive and they are reused between requests
	bool borrowed_buffers;

	// the fd of the connection when the receive was created, for the log after it is closed
	int sock_fd;

	// when the request started, to know the time that it took
	double start_time;

	// the bytes of the request that were received
	size_t received;

	// the recv () calls of the request, to explain why it ended before being completed
	HttpReceiveDiagnostics diagnostics;

	// the deadlines of the request, in monotonic secs, 0 when disabled
	double headers_deadline;
	double request_deadline;

	// the stage that the request has reached
	HttpStage stage;

	// how the client left, when a recv () or a send () found that it was gone
	HttpAbortReason abort_reason;

	// a send () of the response failed, with its errno
	bool response_failed;
	int response_error;

	// the deadline of all the sends of the response, set by its first send,
	// each piece of a stream sets its own
	struct timespec send_deadline;
	bool send_deadline_set;

	// the request of an upgrade to a WebSocket endpoint, until its connection starts
	struct HttpWebSocketHandshake *web_socket;

	// the 101 was sent, the connection belongs to its WebSocket now
	bool upgraded;

	// the last state of the client that was found, a gone client never comes back
	HttpClientState client_state;

};

typedef struct _HttpReceive HttpReceive;

CERVER_PRIVATE HttpReceive *http_receive_new (void);

// prepares a receive that lives in the memory of its owner (a worker thread, a connection)
// with the resources that it lends: the data of each request lives in the arena
// and the token buffers are reused, they are emptied but never deleted
// the receive is ended with http_receive_delete (), that does not release it nor its resources
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_prepare (
	HttpReceive *http_receive,
	CerverReceive *cerver_receive, const HttpReceiveResources *resources
);

// creates a receive that allocates its own memory and the one of its request
CERVER_PRIVATE HttpReceive *http_receive_create (
	CerverReceive *cerver_receive
);

// ends the request of the receive (its uploads, its data, the arena that was lent)
// and starts a new one in the same connection: the parser, the clock, the deadlines,
// the stage and the counters of the request start again
// and what belongs to the connection is kept
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_reset (
	HttpReceive *http_receive
);

// ends the request of the receive, a receive that was created is released
CERVER_PRIVATE void http_receive_delete (
	HttpReceive *http_receive
);

// reads, without waiting, the data that the client already sent
// up to HTTP_RECEIVE_DRAIN_SIZE bytes, so a close () does not answer with a RST
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_drain_available (const int sock_fd);

// returns true while the request's headers have not been completed
CERVER_PRIVATE bool http_receive_is_receiving_headers (
	const HttpReceive *http_receive
);

// a request that did not arrive in time is answered with 408 and closed,
// if the client did not send anything (received is false), it is only closed
CERVER_PRIVATE void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
);

CERVER_EXPORT const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
);

CERVER_EXPORT const int http_receive_get_sock_fd (
	const HttpReceive *http_receive
);

CERVER_EXPORT const HttpCerver *http_receive_get_cerver (
	const HttpReceive *http_receive
);

// the endpoint that handles the request, NULL if no endpoint matched
CERVER_EXPORT const HttpEndpoint *http_receive_get_endpoint (
	const HttpReceive *http_receive
);

// the custom data of the endpoint that handles the request
// (its own, the one that it inherits from its groups or the one of its first API route)
CERVER_EXPORT const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
);

// the custom data of the HTTP cerver that received the request
CERVER_EXPORT const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
);

// true if the request is a HEAD handled by the GET handler,
// its responses are sent without their body
CERVER_EXPORT bool http_receive_is_head (
	const HttpReceive *http_receive
);

// the methods that the request's path accepts (the value of the Allow header),
// NULL if the path did not match any route
CERVER_EXPORT const char *http_receive_get_allow (
	const HttpReceive *http_receive
);

// the stage that the request has reached
CERVER_EXPORT HttpStage http_receive_get_stage (
	const HttpReceive *http_receive
);

// checks, without waiting and without reading its data, if the client is still connected:
// HTTP_CLIENT_STATE_CLOSED when it closed its side (an EOF),
// HTTP_CLIENT_STATE_RESET when its connection failed (a RST)
// a handler that takes long can call it to stop the work of a client that left
CERVER_EXPORT HttpClientState http_receive_get_client_state (
	const HttpReceive *http_receive
);

// true if the client reset its connection, or if it closed its side
// and the cerver takes it as gone (http_cerver_set_half_close_is_gone ())
// a stream whose write fails is also gone, its handler must stop writing
CERVER_EXPORT bool http_receive_is_client_gone (
	const HttpReceive *http_receive
);

// the client left while the request was being received:
// error is 0 for an EOF, or the errno of the recv () or the send () that failed
CERVER_PRIVATE void http_receive_client_left (
	HttpReceive *http_receive, const int error
);

// marks that a send () of the response failed with error,
// the request is counted once as a failed response
CERVER_PRIVATE void http_receive_response_failed (
	const HttpReceive *http_receive, const int error
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
