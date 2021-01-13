#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/http_parser.h"
#include "cerver/http/multipart.h"
#include "cerver/http/route.h"
#include "cerver/http/request.h"

#include "cerver/http/jwt/alg.h"

#define HTTP_CERVER_DEFAULT_UPLOADS_DELETE			false

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _HttpRouteFileStats;

struct jwt;

#pragma region content

#define CONTENT_TYPE_MAP(XX)								\
	XX(0, HTML,		html,	text/html; charset=UTF-8)		\
	XX(1, CSS,		css,	text/css)						\
	XX(2, JS,		js,		application/javascript)			\
	XX(3, JPG,		jpg,	image/jpg)						\
	XX(4, PNG,		png,	image/png)						\
	XX(5, MP3,		mp3,	audio/mp3)						\
	XX(6, ICO,		ico,	image/x-icon)					\
	XX(7, GIF,		gif,	image/gif)						\
	XX(8, OCTET,	octet,	application/octet-stream)

typedef enum ContentType {

	#define XX(num, name, string, description) CONTENT_TYPE_##name = num,
	CONTENT_TYPE_MAP(XX)
	#undef XX

} ContentType;

CERVER_PUBLIC const char *http_content_type_string (
	ContentType content_type
);

CERVER_PUBLIC const char *http_content_type_description (
	ContentType content_type
);

CERVER_PUBLIC const char *http_content_type_by_extension (
	const char *ext
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

	// list of top level routes
	DoubleList *routes;

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
	String *uploads_path;              // default uploads path
	void (*uploads_filename_generator)(
		const CerverReceive *,
		const char *original_filename,
		char *generated_filename
	);

	String *(*uploads_dirname_generator)(const CerverReceive *);

	// delete uploaded files when the request ends
	bool uploads_delete_when_done;

	// auth
	jwt_alg_t jwt_alg;

	String *jwt_opt_key_name;		// jwt private key filename
	String *jwt_private_key;		// jwt actual private key

	String *jwt_opt_pub_key_name;	// jwt public key filename
	String *jwt_public_key;			// jwt actual public key

	// stats
	size_t n_incompleted_requests;	// the request wasn't parsed completely
	size_t n_unhandled_requests;	// failed to get matching route

	size_t n_catch_all_requests;	// redirected to catch all route
	size_t n_failed_auth_requests;	// failed to auth with private route 

	// used to correctly update stats
	pthread_mutex_t *mutex;

};

typedef struct _HttpCerver HttpCerver;

CERVER_PRIVATE HttpCerver *http_cerver_new (void);

CERVER_PRIVATE void http_cerver_delete (
	void *http_cerver_ptr
);

CERVER_PRIVATE HttpCerver *http_cerver_create (
	struct _Cerver *cerver
);

CERVER_PRIVATE void http_cerver_init (
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

// add a new static path where static files can be served upon request
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

// register a new http to the http cerver
CERVER_EXPORT void http_cerver_route_register (
	HttpCerver *http_cerver, HttpRoute *route
);

// set a route to catch any requet
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

// sets the default uploads path where any multipart file request will be saved
// this method will replace the previous value with the new one
CERVER_EXPORT void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
);

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_LEN
extern void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const CerverReceive *,
		const char *original_filename,
		char *generated_filename
	)
);

// sets a method to be called on every new request & that will be used to generate a new directory
// inside the uploads path to save all the files from each request
CERVER_EXPORT void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	String *(*dirname_generator)(const CerverReceive *)
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

	char bearer[HTTP_JWT_BEARER_SIZE];
	char json[HTTP_JWT_TOKEN_SIZE];

} HttpJwt;

CERVER_PRIVATE void *http_jwt_new (void);

CERVER_PRIVATE void http_jwt_delete (void *http_jwt_ptr);

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

// generates and signs a jwt token that is ready to be used
// returns a newly allocated string that should be deleted after use
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	const HttpCerver *http_cerver, HttpJwt *http_jwt
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

#pragma endregion

#pragma region stats

// print number of routes & handlers
CERVER_PUBLIC void http_cerver_routes_stats_print (
	const HttpCerver *http_cerver
);

// print route's stats
CERVER_PUBLIC void http_cerver_route_stats_print (
	const HttpRoute *route
);

// print all http cerver stats, general & by route
CERVER_PUBLIC void http_cerver_all_stats_print (
	const HttpCerver *http_cerver
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
	XX(5,  UNHANDLED,		Unhandled)	\

typedef enum HttpReceiveStatus {

	#define XX(num, name, string) HTTP_RECEIVE_STATUS_##name = num,
	HTTP_RECEIVE_STATUS_MAP(XX)
	#undef XX

} HttpReceiveStatus;

CERVER_PUBLIC const char *http_receive_status_str (
	const HttpReceiveStatus status
);

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

	HttpRoute *route;

	http_status status;
	size_t sent;

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

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif