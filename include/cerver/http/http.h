#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include <stdbool.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/http_parser.h"
#include "cerver/http/multipart.h"
#include "cerver/http/route.h"
#include "cerver/http/request.h"
#include "cerver/http/socket.h"
#include "cerver/http/jwt/alg.h"

struct _Cerver;

#pragma region content

#define CONTENT_TYPE_MAP(XX)								\
	XX(0, HTML, html, text/html; charset=UTF-8)				\
	XX(1, CSS, css, text/css)								\
	XX(2, JS, js, application/javascript)					\
	XX(3, JPG, jpg, image/jpg)								\
	XX(4, PNG, png, image/png)								\
	XX(5, MP3, mp3, audio/mp3)								\
	XX(6, ICO, ico, image/x-icon)							\
	XX(7, GIF, gif, image/gif)								\
	XX(8, OCTET, octet, application/octet-stream)			\

typedef enum ContentType {

	#define XX(num, name, string, description) CONTENT_TYPE_##name = num,
	CONTENT_TYPE_MAP(XX)
	#undef XX

} ContentType;

CERVER_PUBLIC const char *http_content_type_string (ContentType content_type);

CERVER_PUBLIC const char *http_content_type_description (ContentType content_type);

CERVER_PUBLIC const char *http_content_type_by_extension (const char *ext);

#pragma endregion

#pragma region kvp

typedef struct KeyValuePair { 

	String *key;
	String *value;

} KeyValuePair;

CERVER_PUBLIC KeyValuePair *key_value_pair_new (void);

CERVER_PUBLIC void key_value_pair_delete (void *kvp_ptr);

CERVER_PUBLIC KeyValuePair *key_value_pair_create (const char *key, const char *value);

CERVER_PUBLIC const String *key_value_pairs_get_value (DoubleList *pairs, const char *key);

CERVER_PUBLIC void key_value_pairs_print (DoubleList *pairs);

#pragma endregion

#pragma region main

struct _HttpCerver {

	struct _Cerver *cerver;

	// paths to serve static files
	DoubleList *static_paths;

	// list of top level routes
	DoubleList *routes;

	// catch all route (/*)
	void (*default_handler)(CerverReceive *cr, HttpRequest *request);

	// uploads
	String *uploads_path;              // default uploads path
	String *(*uploads_dirname_generator)(CerverReceive *cr);

	// auth
	jwt_alg_t jwt_alg;

	String *jwt_opt_key_name;          // jwt private key filename
	String *jwt_private_key;           // jwt actual private key

	String *jwt_opt_pub_key_name;      // jwt public key filename
	String *jwt_public_key;            // jwt actual public key

	// stats
	size_t n_cath_all_requests;        // failed to match a route
	size_t n_failed_auth_requests;     // failed to auth with private route 

};

typedef struct _HttpCerver HttpCerver;

CERVER_PRIVATE HttpCerver *http_cerver_new (void);

CERVER_PRIVATE void http_cerver_delete (void *http_cerver_ptr);

CERVER_PRIVATE HttpCerver *http_cerver_create (struct _Cerver *cerver);

CERVER_PRIVATE void http_cerver_init (HttpCerver *http_cerver);

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

// set a route to catch any requet that didn't match any registered route
CERVER_EXPORT void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver, 
	void (*catch_all_route)(CerverReceive *cr, HttpRequest *request)
);

#pragma endregion

#pragma region uploads

// sets the default uploads path where any multipart file request will be saved
// this method will replace the previous value with the new one
CERVER_EXPORT void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
);

// sets a method to be called on every new request & that will be used to generate a new directory
// inside the uploads path to save all the files from each request
CERVER_EXPORT void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver, String *(*dirname_generator)(CerverReceive *)
);

#pragma endregion

#pragma region auth

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

// generates and signs a jwt token that is ready to be sent
// returns a newly allocated string that should be deleted after use
CERVER_EXPORT char *http_cerver_auth_generate_jwt (
	HttpCerver *http_cerver, DoubleList *values
);

#pragma endregion

#pragma region stats

// print number of routes & handlers
CERVER_PUBLIC void http_cerver_routes_stats_print (HttpCerver *http_cerver);

// print route's stats
CERVER_PUBLIC void http_cerver_route_stats_print (HttpRoute *route);

// print all http cerver stats, general & by route
CERVER_PUBLIC void http_cerver_all_stats_print (HttpCerver *http_cerver);

#pragma endregion

#pragma region url

// returns a newly allocated url-encoded version of str that should be deleted after use
CERVER_PUBLIC char *http_url_encode (const char *str);

// returns a newly allocated url-decoded version of str that should be deleted after use
CERVER_PUBLIC char *http_url_decode (const char *str);

#pragma endregion

#pragma region parser

// parses a url query into key value pairs for better manipulation
CERVER_PUBLIC DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
);

// gets the matching value for the requested key from a list of pairs
CERVER_PUBLIC const String *http_query_pairs_get_value (
	DoubleList *pairs, const char *key
);

CERVER_PUBLIC void http_query_pairs_print (DoubleList *pairs);

#pragma endregion

#pragma region handler

typedef struct HttpReceive {

	CerverReceive *cr;

	// keep connection alive - don't close after request has ended
	bool keep_alive;

	void (*handler)(struct HttpReceive *, ssize_t, char *);

	HttpCerver *http_cerver;

	http_parser *parser;
	http_parser_settings settings;

	multipart_parser *mpart_parser;
	multipart_parser_settings mpart_settings;
	
	HttpRequest *request;

	// websockets
	unsigned char fin_rsv_opcode;
	size_t fragmented_message_len;
	char *fragmented_message;
	void (*ws_on_open)(struct _Cerver *, struct _Connection *);
	void (*ws_on_close)(struct _Cerver *, const char *reason);
	void (*ws_on_ping)(struct _Cerver *, struct _Connection *);
	void (*ws_on_pong)(struct _Cerver *, struct _Connection *);
	void (*ws_on_message)(struct _Cerver *, struct _Connection *, const char *msg, const size_t msg_len);
	void (*ws_on_error)(struct _Cerver *, enum _HttpWebSocketError);

} HttpReceive;

CERVER_PRIVATE HttpReceive *http_receive_new (void);

CERVER_PRIVATE void http_receive_delete (HttpReceive *http_receive);

#pragma endregion

#pragma region websockets

// the default tmeout for a websocket sonnection
#define DEFAULT_WEB_SOCKET_RECV_TIMEOUT         5

// sends a ws message to the selected connection
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_sockets_send (
	Cerver *cerver, Connection *connection,
	const char *msg, const size_t msg_len
);

#pragma endregion

#endif