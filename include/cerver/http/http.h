#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/headers.h"
#include "cerver/http/http_parser.h"
#include "cerver/http/multipart.h"
#include "cerver/http/origin.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"

#include "cerver/http/jwt/alg.h"

#define HTTP_CERVER_MULTI_PARTS_POOL_INIT			32

#define HTTP_CERVER_UPLOADS_PATH_SIZE				256

#define HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE		0777
#define HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE		0777

#define HTTP_CERVER_DEFAULT_UPLOADS_DELETE			false

#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN			false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO		true
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS		false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS	false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH		false
#define HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS		false

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Worker;

struct _HttpRouteFileStats;

struct _HttpReceive;
struct _HttpResponse;

struct jwt;

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

	// list of top level routes
	DoubleList *routes;

	const HttpRoute *main_route;

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

	// auth
	jwt_alg_t jwt_alg;

	String *jwt_opt_key_name;		// jwt private key filename
	String *jwt_private_key;		// jwt actual private key

	String *jwt_opt_pub_key_name;	// jwt public key filename
	String *jwt_public_key;			// jwt actual public key

	u8 n_origins;
	HttpOrigin origins_whitelist[HTTP_ORIGINS_SIZE];

	// data
	void *custom_data;
	void (*delete_custom_data)(void *);

	// responses
	u8 n_response_headers;
	String *response_headers[HTTP_HEADERS_SIZE];

	// stats
	size_t n_incompleted_requests;	// the request wasn't parsed completely
	size_t n_unhandled_requests;	// failed to get matching route

	size_t n_catch_all_requests;	// redirected to catch all route
	size_t n_failed_auth_requests;	// failed to auth with private route 

	// admins
	bool enable_admin_routes;
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

	bool enable_admin_cors_headers;
	HttpOrigin admin_origin;
	
	DoubleList *admin_file_systems_stats;
	DoubleList *admin_workers;
	pthread_mutex_t *admin_mutex;

	// used to correctly update stats
	pthread_mutex_t *mutex;

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

} HttpStaticPath;

// sets authentication requirenments for a whole static path
CERVER_EXPORT void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
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
);

// sets the mode_t to be used when creating uploads files
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE
CERVER_EXPORT void http_cerver_set_uploads_file_mode (
	HttpCerver *http_cerver, const unsigned int file_mode
);

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%s" using "sock_fd-time (NULL)-multi_part->filename"
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
// with format "%d-%ld" using "sock_fd-time (NULL)"
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

#pragma region auth

#define HTTP_JWT_POOL_INIT			32

#define HTTP_JWT_VALUE_KEY_SIZE		128
#define HTTP_JWT_VALUE_VALUE_SIZE	128

#define HTTP_JWT_VALUES_SIZE		16
#define HTTP_JWT_BEARER_SIZE		2048
#define HTTP_JWT_TOKEN_SIZE			4096

typedef struct HttpJwtValue {

	char key[HTTP_JWT_VALUE_KEY_SIZE];

	cerver_type_t type;

	union {
		bool value_bool;
		int value_int;
		char value_str[HTTP_JWT_VALUE_VALUE_SIZE];
	};

} HttpJwtValue;

typedef struct HttpJwt {

	struct jwt *jwt;

	u8 n_values;
	HttpJwtValue values[HTTP_JWT_VALUES_SIZE];

	size_t bearer_len;
	char bearer[HTTP_JWT_BEARER_SIZE];
	size_t json_len;
	char json[HTTP_JWT_TOKEN_SIZE];

} HttpJwt;

CERVER_PRIVATE void *http_jwt_new (void);

CERVER_PRIVATE void http_jwt_delete (void *http_jwt_ptr);

CERVER_EXPORT const char *http_jwt_get_bearer (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const size_t http_jwt_get_bearer_len (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const char *http_jwt_get_json (
	const HttpJwt *http_jwt
);

CERVER_EXPORT const size_t http_jwt_get_json_len (
	const HttpJwt *http_jwt
);

// loads a key from a filename that can be used for jwt
// returns a newly allocated c string on success, NULL on error
CERVER_PUBLIC char *http_cerver_auth_load_key (
	const char *filename, size_t *keylen
);

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
CERVER_EXPORT void http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, jwt_alg_t jwt_alg
);

// sets the filename from where the jwt private key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
);

// sets the filename from where the jwt public key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
);

CERVER_EXPORT HttpJwt *http_cerver_auth_jwt_new (void);

CERVER_EXPORT void http_cerver_auth_jwt_delete (HttpJwt *http_jwt);

CERVER_EXPORT void http_cerver_auth_jwt_add_value (
	HttpJwt *http_jwt,
	const char *key, const char *value
);

CERVER_EXPORT void http_cerver_auth_jwt_add_value_bool (
	HttpJwt *http_jwt,
	const char *key, const bool value
);

CERVER_EXPORT void http_cerver_auth_jwt_add_value_int (
	HttpJwt *http_jwt,
	const char *key, const int value
);

CERVER_PRIVATE char *http_cerver_auth_generate_jwt_actual (
	HttpJwt *http_jwt,
	jwt_alg_t alg, const unsigned char *key, int keylen
);

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
);

CERVER_PRIVATE u8 http_cerver_auth_generate_bearer_jwt_actual (
	HttpJwt *http_jwt,
	jwt_alg_t alg, const unsigned char *key, int keylen
);

// generates and signs a bearer jwt that is ready to be used
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

// generates and signs a bearer jwt
// and places it inside a json packet
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json (
	HttpCerver *http_cerver, HttpJwt *http_jwt
);

// works as http_cerver_auth_generate_bearer_jwt_json ()
// but with the ability to add an extra string value to
// the generated json
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_auth_generate_bearer_jwt_json_with_value (
	HttpCerver *http_cerver, HttpJwt *http_jwt,
	const char *key, const char *value
);

// returns TRUE if the jwt has been decoded and validate successfully
// returns FALSE if token is NOT valid or if an error has occurred
// option to pass the method to be used to create a decoded data
// that will be placed in the decoded data argument
CERVER_EXPORT bool http_cerver_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	void *(*decode_data)(void *), void **decoded_data
);

CERVER_PRIVATE void *http_decode_data_into_json (void *json_ptr);

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
	http_header type, const char *actual_header
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

#pragma region url

// returns a newly allocated url-encoded version of str
// that should be deleted after use
CERVER_PUBLIC char *http_url_encode (const char *str);

// returns a newly allocated url-decoded version of str
// that should be deleted after use
CERVER_PUBLIC char *http_url_decode (const char *str);

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
	XX(5,  UNHANDLED,		Unhandled)

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

	http_parser *parser;
	http_parser_settings settings;

	multipart_parser *mpart_parser;
	multipart_parser_settings mpart_settings;

	HttpRequest *request;

	// found
	HttpReceiveType type;
	HttpRoute *route;
	const char *served_file;

	http_status status;
	size_t sent;

	bool is_multi_part;
	struct _HttpRouteFileStats *file_stats;

};

typedef struct _HttpReceive HttpReceive;

CERVER_PRIVATE HttpReceive *http_receive_new (void);

CERVER_PRIVATE HttpReceive *http_receive_create (
	CerverReceive *cerver_receive
);

CERVER_PRIVATE void http_receive_delete (
	HttpReceive *http_receive
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

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif