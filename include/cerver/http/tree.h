#ifndef _CERVER_HTTP_TREE_H_
#define _CERVER_HTTP_TREE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/middleware.h"
#include "cerver/http/request.h"
#include "cerver/http/route.h"
#include "cerver/http/validator.h"

// the most params of a route, a route with more is a registration error
#define HTTP_TREE_DEFAULT_MAX_PARAMS			16

#define HTTP_TREE_ERROR_SIZE					256

#define HTTP_TREE_ALLOW_SIZE					HTTP_ROUTE_METHODS_STRING_SIZE

#define HTTP_TREE_PATH_SEPARATOR				'/'
#define HTTP_TREE_PARAM_PREFIX					':'
#define HTTP_TREE_WILDCARD_PREFIX				'*'
#define HTTP_TREE_VALIDATOR_START				'<'
#define HTTP_TREE_VALIDATOR_END					'>'

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpReceive;

struct _HttpGroup;
struct _HttpTree;

#define HTTP_TRAILING_SLASH_MAP(XX)																								\
	XX(0,	NONE, 			None,			Undefined)																				\
	XX(1,	TOLERANT, 		Tolerant,		A path with a trailing slash matches the same route as the path without it)				\
	XX(2,	STRICT, 		Strict,			A path with a trailing slash does not match a route without it)							\
	XX(3,	REDIRECT, 		Redirect,		A path with a trailing slash is redirected with 308 to the path without it)

typedef enum HttpTrailingSlash {

	#define XX(num, name, string, description) HTTP_TRAILING_SLASH_##name = num,
	HTTP_TRAILING_SLASH_MAP (XX)
	#undef XX

	HTTP_TRAILING_SLASH_MAX

} HttpTrailingSlash;

CERVER_PUBLIC const char *http_trailing_slash_to_string (
	const HttpTrailingSlash trailing_slash
);

CERVER_PUBLIC const char *http_trailing_slash_description (
	const HttpTrailingSlash trailing_slash
);

#define HTTP_TREE_DEFAULT_TRAILING_SLASH		HTTP_TRAILING_SLASH_TOLERANT

#pragma region settings

// the settings that a group passes to its subgroups and endpoints,
// each one is inherited from the nearest group that set it
#define HTTP_ROUTE_SETTING_MODIFIER					(1 << 0)
#define HTTP_ROUTE_SETTING_AUTH_TYPE				(1 << 1)
#define HTTP_ROUTE_SETTING_DECODE_DATA				(1 << 2)
#define HTTP_ROUTE_SETTING_AUTHENTICATION_HANDLER	(1 << 3)
#define HTTP_ROUTE_SETTING_AUTHENTICATION_WITH_BODY	(1 << 4)
#define HTTP_ROUTE_SETTING_CUSTOM_DATA				(1 << 5)
#define HTTP_ROUTE_SETTING_MAX_BODY_SIZE			(1 << 6)
#define HTTP_ROUTE_SETTING_CORS						(1 << 7)
#define HTTP_ROUTE_SETTING_ETAG						(1 << 8)
#define HTTP_ROUTE_SETTING_COMPRESSION				(1 << 9)

struct _HttpRouteSettings {

	// the settings that were set
	unsigned int set;

	HttpRouteModifier modifier;

	HttpRouteAuthType auth_type;

	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	HttpAuthenticationHandler authentication_handler;

	bool authentication_with_body;

	// owned by the group or the endpoint that set it,
	// the ones that inherit it only keep a reference
	void *custom_data;
	void (*delete_custom_data)(void *);

	// 0 uses the cerver's max body size
	size_t max_body_size;

	// answers the CORS preflight requests (OPTIONS) with the origins whitelist,
	// and adds the Access-Control-Allow-Origin of a whitelisted origin to every response
	bool cors;

	// the GET and HEAD responses with 200 have the entity tag of their body,
	// and they are answered with 304 when the request's If-None-Match matches it
	bool etag;

	// the responses are compressed with gzip when the request accepts it
	// (only when cerver was compiled with zlib)
	bool compression;

	HttpMiddlewares middlewares;

};

typedef struct _HttpRouteSettings HttpRouteSettings;

#pragma endregion

#pragma region endpoint

// the handler of a method in a route path
struct _HttpEndpoint {

	// the complete path, eg. "/api/users/:id<objectid>"
	String *path;

	RequestMethod method;
	HttpHandler handler;

	struct _HttpGroup *group;

	// the route of the first routes API that created the endpoint, NULL for the others
	HttpRoute *route;

	HttpRouteSettings settings;

	// the settings with the inherited values, resolved when the cerver starts
	HttpRouteSettings resolved;

	// the names of the params, NULL for a param without name
	unsigned int n_params;
	char **params_names;

	HttpRouteStats *stats;
	HttpRouteFileStats *file_stats;
	bool owns_stats;

};

typedef struct _HttpEndpoint HttpEndpoint;

// creates an endpoint with its complete path
CERVER_PRIVATE HttpEndpoint *http_endpoint_create (
	const char *path, const RequestMethod method, const HttpHandler handler
);

CERVER_PRIVATE void http_endpoint_delete (void *endpoint_ptr);

CERVER_EXPORT const String *http_endpoint_get_path (
	const HttpEndpoint *endpoint
);

CERVER_EXPORT RequestMethod http_endpoint_get_method (
	const HttpEndpoint *endpoint
);

CERVER_EXPORT void http_endpoint_set_modifier (
	HttpEndpoint *endpoint, const HttpRouteModifier modifier
);

CERVER_EXPORT void http_endpoint_set_auth (
	HttpEndpoint *endpoint, const HttpRouteAuthType auth_type
);

CERVER_EXPORT void http_endpoint_set_decode_data (
	HttpEndpoint *endpoint,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

CERVER_EXPORT void http_endpoint_set_decode_data_into_json (
	HttpEndpoint *endpoint
);

CERVER_EXPORT void http_endpoint_set_authentication_handler (
	HttpEndpoint *endpoint, const HttpAuthenticationHandler authentication_handler
);

CERVER_EXPORT void http_endpoint_set_authentication_with_body (
	HttpEndpoint *endpoint, const bool authentication_with_body
);

// the endpoint owns the data, it is deleted with delete_custom_data
CERVER_EXPORT void http_endpoint_set_custom_data (
	HttpEndpoint *endpoint, void *custom_data, void (*delete_custom_data)(void *)
);

// the custom data of the endpoint, or the one that it inherits from its groups
CERVER_EXPORT const void *http_endpoint_get_custom_data (
	const HttpEndpoint *endpoint
);

CERVER_EXPORT void http_endpoint_set_max_body_size (
	HttpEndpoint *endpoint, const size_t max_body_size
);

CERVER_EXPORT void http_endpoint_set_cors (
	HttpEndpoint *endpoint, const bool cors
);

CERVER_EXPORT void http_endpoint_set_etag (
	HttpEndpoint *endpoint, const bool etag
);

CERVER_EXPORT void http_endpoint_set_compression (
	HttpEndpoint *endpoint, const bool compression
);

// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_endpoint_add_before (
	HttpEndpoint *endpoint, const HttpBeforeMiddleware middleware
);

// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_endpoint_add_after (
	HttpEndpoint *endpoint, const HttpAfterMiddleware middleware
);

#pragma endregion

#pragma region group

// a prefix with the settings that its endpoints and subgroups inherit
struct _HttpGroup {

	// the complete prefix, eg. "/api/users", "/" for the root
	String *prefix;

	struct _HttpGroup *parent;
	struct _HttpTree *tree;

	HttpRouteSettings settings;

	DoubleList *groups;
	DoubleList *endpoints;

};

typedef struct _HttpGroup HttpGroup;

CERVER_PRIVATE void http_group_delete (void *group_ptr);

// creates a top level group in the cerver, the prefix can be "" or "/" for the root
// returns NULL on error (the error is kept in the tree and the cerver will not start)
CERVER_EXPORT HttpGroup *http_cerver_group_create (
	struct _HttpCerver *http_cerver, const char *prefix
);

// creates a group inside another one
CERVER_EXPORT HttpGroup *http_group_create (
	HttpGroup *parent, const char *prefix
);

CERVER_EXPORT const String *http_group_get_prefix (
	const HttpGroup *group
);

// adds an endpoint to the group, the path is relative to the group's prefix
// ":name" is a param, ":name<validator>" a param with a validator
// and "*name" (only in the last segment) matches the rest of the path
// returns NULL on error (the error is kept in the tree and the cerver will not start)
CERVER_EXPORT HttpEndpoint *http_group_route_add (
	HttpGroup *group,
	const RequestMethod method, const char *path, const HttpHandler handler
);

// adds the endpoint of the method in the path of the group itself, eg. GET /api/users
// returns NULL on error (the error is kept in the tree and the cerver will not start)
CERVER_EXPORT HttpEndpoint *http_group_root_add (
	HttpGroup *group,
	const RequestMethod method, const HttpHandler handler
);

CERVER_EXPORT HttpEndpoint *http_group_get (
	HttpGroup *group, const char *path, const HttpHandler handler
);

CERVER_EXPORT HttpEndpoint *http_group_post (
	HttpGroup *group, const char *path, const HttpHandler handler
);

CERVER_EXPORT HttpEndpoint *http_group_put (
	HttpGroup *group, const char *path, const HttpHandler handler
);

CERVER_EXPORT HttpEndpoint *http_group_patch (
	HttpGroup *group, const char *path, const HttpHandler handler
);

CERVER_EXPORT HttpEndpoint *http_group_delete_route (
	HttpGroup *group, const char *path, const HttpHandler handler
);

CERVER_EXPORT void http_group_set_modifier (
	HttpGroup *group, const HttpRouteModifier modifier
);

CERVER_EXPORT void http_group_set_auth (
	HttpGroup *group, const HttpRouteAuthType auth_type
);

CERVER_EXPORT void http_group_set_decode_data (
	HttpGroup *group,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

CERVER_EXPORT void http_group_set_decode_data_into_json (
	HttpGroup *group
);

CERVER_EXPORT void http_group_set_authentication_handler (
	HttpGroup *group, const HttpAuthenticationHandler authentication_handler
);

CERVER_EXPORT void http_group_set_authentication_with_body (
	HttpGroup *group, const bool authentication_with_body
);

// the group owns the data, it is deleted with delete_custom_data
CERVER_EXPORT void http_group_set_custom_data (
	HttpGroup *group, void *custom_data, void (*delete_custom_data)(void *)
);

CERVER_EXPORT void http_group_set_max_body_size (
	HttpGroup *group, const size_t max_body_size
);

CERVER_EXPORT void http_group_set_cors (
	HttpGroup *group, const bool cors
);

CERVER_EXPORT void http_group_set_etag (
	HttpGroup *group, const bool etag
);

CERVER_EXPORT void http_group_set_compression (
	HttpGroup *group, const bool compression
);

// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_group_add_before (
	HttpGroup *group, const HttpBeforeMiddleware middleware
);

// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_group_add_after (
	HttpGroup *group, const HttpAfterMiddleware middleware
);

#pragma endregion

#pragma region tree

typedef enum HttpRouteNodeType {

	HTTP_ROUTE_NODE_TYPE_NONE			= 0,
	HTTP_ROUTE_NODE_TYPE_STATIC			= 1,
	HTTP_ROUTE_NODE_TYPE_PARAM			= 2,
	HTTP_ROUTE_NODE_TYPE_WILDCARD		= 3,

	HTTP_ROUTE_NODE_TYPE_MAX			= 4

} HttpRouteNodeType;

// a segment of the routes tree
// a segment of a url is matched with the static children first (a binary search),
// then with the params (the ones with a validator first) and at last with the wildcard,
// so the order in which the routes were registered never changes the result
struct _HttpRouteNode {

	HttpRouteNodeType type;

	// the text of a static segment, the validator's name of a param (NULL without one)
	String *segment;
	const HttpParamValidator *validator;

	size_t n_statics;
	struct _HttpRouteNode **statics;

	size_t n_params;
	struct _HttpRouteNode **params;

	struct _HttpRouteNode *wildcard;

	size_t n_endpoints;
	HttpEndpoint *endpoints[REQUEST_METHOD_SIZE];

	// the methods that the path accepts, for the Allow header
	size_t allow_len;
	char allow[HTTP_TREE_ALLOW_SIZE];

	// an endpoint of the node answers CORS preflights
	bool cors;

};

typedef struct _HttpRouteNode HttpRouteNode;

#pragma endregion

#pragma region tree

struct _HttpTree {

	struct _HttpCerver *http_cerver;

	DoubleList *groups;

	// the endpoints that translate the routes of the first routes API
	DoubleList *compatibility_endpoints;

	// the custom param validators
	DoubleList *validators;

	HttpRouteNode *root;
	bool compiled;
	size_t n_endpoints;

	unsigned int max_params;
	HttpTrailingSlash trailing_slash;

	// a method without a handler is answered with 405 instead of the catch all handler
	bool method_not_allowed;
	HttpHandler method_not_allowed_handler;

	// the middlewares of every endpoint
	HttpMiddlewares middlewares;

	// the registration errors, the cerver does not start if there is any
	size_t n_errors;
	char error[HTTP_TREE_ERROR_SIZE];

};

typedef struct _HttpTree HttpTree;

CERVER_PRIVATE HttpTree *http_tree_create (struct _HttpCerver *http_cerver);

CERVER_PRIVATE void http_tree_delete (void *tree_ptr);

// keeps the error of a registration, the first one is the one that is reported
CERVER_PRIVATE void http_tree_error (
	HttpTree *tree, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

// a warning of the first routes API, it is logged but it is not an error
CERVER_PRIVATE void http_tree_warning (
	const char *format, ...
) CERVER_PRINTF_FORMAT (1, 2);

// checks the syntax of a path relative to a group
// returns 0 if it is valid, 1 if it is not (the reason is written in error)
CERVER_PUBLIC u8 http_tree_path_validate (
	const char *path, char *error, const size_t error_size
);

// joins a path relative to a prefix, eg. "/api" and "users" is "/api/users"
CERVER_PRIVATE String *http_tree_path_join (
	const char *prefix, const char *path
);

// adds a resolved endpoint to the routes tree
// a conflict of the first routes API (compatibility) is only a warning and the first route is kept
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_tree_insert (
	HttpTree *tree, HttpEndpoint *endpoint, const bool compatibility
);

// builds the routes tree with the groups and the routes of the first API
// returns 0 on success, 1 on error (the cerver does not start)
CERVER_PRIVATE u8 http_tree_compile (HttpTree *tree);

// the result of matching a url with the routes tree
typedef struct HttpRouteMatch {

	const HttpRouteNode *node;

	unsigned int n_params;

} HttpRouteMatch;

// matches a path with the tree, the params are written in params (max_params + 1 slots)
// returns true if a node with endpoints matched the path
CERVER_PRIVATE bool http_tree_match (
	const HttpTree *tree,
	const char *path, const size_t path_len,
	HttpRequestParam *params, HttpRouteMatch *match
);

// prints the compiled routes tree
CERVER_PUBLIC void http_tree_print (const HttpTree *tree);

// the number of endpoints in the compiled tree
CERVER_PUBLIC size_t http_tree_get_n_endpoints (const HttpTree *tree);

// the compiled endpoints as a json string, NULL if the tree is not compiled
// {"n_endpoints": N, "endpoints": [{"method", "path", "auth", "modifier", "compatibility"}]}
CERVER_PRIVATE char *http_tree_generate_json (const HttpTree *tree);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
