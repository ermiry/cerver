/**
 * @file cerver/http/routes/tree.h
 * @brief The routes tree of an HTTP cerver: its groups, its endpoints, their settings and how a url is matched.
 */

#ifndef _CERVER_HTTP_TREE_H_
#define _CERVER_HTTP_TREE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/requests/request.h"
#include "cerver/http/routes/middleware.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/validator.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/settings.h"

/**
 * @defgroup http_routes Routes
 * @ingroup http
 * @brief The routes of an HTTP cerver: the groups and the endpoints of the routes tree, their middlewares and
 * validators, and the routes of the first routes API.
 */

/**
 * @defgroup http_routes_tree_values Values
 * @ingroup http_routes
 * @brief The limits and the characters of the paths of the routes tree.
 * @{
 */

/** @brief The most params of a route; a route with more is a registration error. */
#define HTTP_TREE_DEFAULT_MAX_PARAMS			16

/** @brief The size of the first registration error that the tree keeps. */
#define HTTP_TREE_ERROR_SIZE					256

/** @brief The size of the Allow header of a node. */
#define HTTP_TREE_ALLOW_SIZE					HTTP_ROUTE_METHODS_STRING_SIZE

/** @brief The separator of the segments of a path. */
#define HTTP_TREE_PATH_SEPARATOR				'/'

/** @brief The prefix of a param segment, like `:id`. */
#define HTTP_TREE_PARAM_PREFIX					':'

/** @brief The prefix of a wildcard segment, like `*path`, only in the last segment. */
#define HTTP_TREE_WILDCARD_PREFIX				'*'

/** @brief The start of the validator of a param, like `:id<objectid>`. */
#define HTTP_TREE_VALIDATOR_START				'<'

/** @brief The end of the validator of a param. */
#define HTTP_TREE_VALIDATOR_END					'>'

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpReceive;

struct _HttpGroup;
struct _HttpTree;

/**
 * @defgroup http_routes_trailing_slash Trailing slash
 * @ingroup http_routes
 * @brief How a path with a trailing slash is matched.
 * @{
 */

/**
 * @brief The X-macro table of the trailing slash policies: the number, the name, the string and the description
 * of each one.
 */
#define HTTP_TRAILING_SLASH_MAP(XX)																								\
	XX(0,	NONE, 			None,			Undefined)																				\
	XX(1,	TOLERANT, 		Tolerant,		A path with a trailing slash matches the same route as the path without it)				\
	XX(2,	STRICT, 		Strict,			A path with a trailing slash does not match a route without it)							\
	XX(3,	REDIRECT, 		Redirect,		A path with a trailing slash is redirected with 308 to the path without it)

/**
 * @brief The trailing slash policy, one `HTTP_TRAILING_SLASH_<name>` for each row of #HTTP_TRAILING_SLASH_MAP.
 */
typedef enum HttpTrailingSlash {

	#define XX(num, name, string, description) HTTP_TRAILING_SLASH_##name = num,
	HTTP_TRAILING_SLASH_MAP (XX)
	#undef XX

	/** @brief The number of policies, the first value that is not valid. */
	HTTP_TRAILING_SLASH_MAX

} HttpTrailingSlash;

/**
 * @brief Returns the string of a trailing slash policy.
 *
 * @param[in] trailing_slash the policy
 * @return the string of its row in #HTTP_TRAILING_SLASH_MAP, or the one of HTTP_TRAILING_SLASH_NONE for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_trailing_slash_to_string (
	const HttpTrailingSlash trailing_slash
);

/**
 * @brief Returns the description of a trailing slash policy.
 *
 * @param[in] trailing_slash the policy
 * @return the description of its row in #HTTP_TRAILING_SLASH_MAP, or the one of HTTP_TRAILING_SLASH_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_trailing_slash_description (
	const HttpTrailingSlash trailing_slash
);

/** @brief The default trailing slash policy of a tree. */
#define HTTP_TREE_DEFAULT_TRAILING_SLASH		HTTP_TRAILING_SLASH_TOLERANT

/** @} */

/**
 * @defgroup http_routes_settings Settings
 * @ingroup http_routes
 * @brief The settings that a group passes to its subgroups and endpoints; each one is inherited from the nearest
 * group that set it.
 * @{
 */

/** @brief The modifier was set. */
#define HTTP_ROUTE_SETTING_MODIFIER					(1 << 0)
/** @brief The authentication type was set. */
#define HTTP_ROUTE_SETTING_AUTH_TYPE				(1 << 1)
/** @brief The methods that decode the data of the JWT were set. */
#define HTTP_ROUTE_SETTING_DECODE_DATA				(1 << 2)
/** @brief The authentication handler was set. */
#define HTTP_ROUTE_SETTING_AUTHENTICATION_HANDLER	(1 << 3)
/** @brief The authentication with the body was set. */
#define HTTP_ROUTE_SETTING_AUTHENTICATION_WITH_BODY	(1 << 4)
/** @brief The custom data was set. */
#define HTTP_ROUTE_SETTING_CUSTOM_DATA				(1 << 5)
/** @brief The max body size was set. */
#define HTTP_ROUTE_SETTING_MAX_BODY_SIZE			(1 << 6)
/** @brief The CORS was set. */
#define HTTP_ROUTE_SETTING_CORS						(1 << 7)
/** @brief The entity tags were set. */
#define HTTP_ROUTE_SETTING_ETAG						(1 << 8)
/** @brief The compression was set. */
#define HTTP_ROUTE_SETTING_COMPRESSION				(1 << 9)
/**
 * @brief The pool of threads was set.
 *
 * @since 2.1b-17
 */
#define HTTP_ROUTE_SETTING_POOL						(1 << 10)
/**
 * @brief What is asked of the certificate of the client was set.
 *
 * @since 2.1b-22
 */
#define HTTP_ROUTE_SETTING_CLIENT_CERTIFICATE		(1 << 11)

/**
 * @brief The X-macro table of what an endpoint asks of the certificate of the client (mTLS): the number, the name and
 * the description of each one.
 *
 * TLS 1.3 never renegotiates (RFC 8740), so the cerver asks for the certificate in its handshake
 * (tls_settings_set_client_verify () with TLS_CLIENT_VERIFY_OPTIONAL) and an endpoint only checks that it came.
 */
#define HTTP_CLIENT_CERTIFICATE_MAP(XX)																	\
	XX(0,	NONE,		The endpoint does not need a certificate)											\
	XX(1,	REQUIRED,	A request without a certificate that verified is answered with 403)

/**
 * @brief What an endpoint asks of the certificate of the client, one `HTTP_CLIENT_CERTIFICATE_<name>` for each row
 * of #HTTP_CLIENT_CERTIFICATE_MAP.
 *
 * @since 2.1b-22
 */
typedef enum HttpClientCertificate {

	#define XX(num, name, description) HTTP_CLIENT_CERTIFICATE_##name = num,
	HTTP_CLIENT_CERTIFICATE_MAP (XX)
	#undef XX

} HttpClientCertificate;

/**
 * @brief The size of the name of a pool of threads (http/core/pool.h), with its end.
 *
 * @since 2.1b-17
 */
#define HTTP_POOL_NAME_SIZE							32

struct HttpPool;

/**
 * @brief The settings of a group or an endpoint.
 */
struct _HttpRouteSettings {

	/** @brief The HTTP_ROUTE_SETTING_* flags of the settings that were set. */
	unsigned int set;

	/** @brief The modifier. */
	HttpRouteModifier modifier;

	/** @brief The authentication type. */
	HttpRouteAuthType auth_type;

	/** @brief Decodes the data of the JWT of a bearer authentication. */
	void *(*decode_data)(void *);
	/** @brief Releases the decoded data, NULL to keep it. */
	void (*delete_decoded_data)(void *);

	/** @brief The authentication handler of HTTP_ROUTE_AUTH_TYPE_CUSTOM. */
	HttpAuthenticationHandler authentication_handler;

	/** @brief The authentication handler needs the body of the request. */
	bool authentication_with_body;

	/** @brief The custom data, owned by the group or the endpoint that set it; the ones that inherit it only keep a reference. */
	void *custom_data;
	/** @brief Releases the custom data, NULL to keep it. */
	void (*delete_custom_data)(void *);

	/** @brief The max body size of a request, 0 uses the one of the cerver. */
	size_t max_body_size;

	/**
	 * @brief Answers the CORS preflight requests (OPTIONS) with the origins whitelist, and adds the
	 * Access-Control-Allow-Origin of a whitelisted origin to every response.
	 */
	bool cors;

	/**
	 * @brief The GET and HEAD responses with 200 have the entity tag of their body, and they are answered with 304
	 * when the If-None-Match of the request matches it.
	 */
	bool etag;

	/** @brief The responses are compressed with gzip when the request accepts it (only when cerver was compiled with zlib). */
	bool compression;

	/**
	 * @brief The name of the pool of threads that handles the requests (http/core/pool.h).
	 *
	 * @since 2.1b-17
	 */
	char pool_name[HTTP_POOL_NAME_SIZE];
	/**
	 * @brief The pool with that name, found when the cerver starts; NULL without a name: the default pool, the
	 * thpool of the cerver.
	 *
	 * @since 2.1b-17
	 */
	struct HttpPool *pool;

	/**
	 * @brief What is asked of the certificate of the client.
	 *
	 * @since 2.1b-22
	 */
	HttpClientCertificate client_certificate;

	/** @brief The middlewares. */
	HttpMiddlewares middlewares;

	/** @brief The settings of the connections of a WebSocket endpoint (HTTP_ROUTE_MODIFIER_WEB_SOCKET), each one inherited on its own. */
	HttpWebSocketSettings web_socket;

};

/** @brief The settings of a group or an endpoint, see struct _HttpRouteSettings. */
typedef struct _HttpRouteSettings HttpRouteSettings;

/** @} */

/**
 * @defgroup http_routes_endpoint Endpoints
 * @ingroup http_routes
 * @brief The handler of a method in a path, and its settings.
 *
 * The settings of an endpoint and of its groups are resolved when the cerver starts; the setters ignore a NULL
 * endpoint.
 * @{
 */

/**
 * @brief The handler of a method in a route path.
 */
struct _HttpEndpoint {

	/** @brief The complete path, like "/api/users/:id<objectid>". */
	String *path;

	/** @brief The request method. */
	HttpRequestMethod method;
	/** @brief The handler, NULL in a WebSocket endpoint without one. */
	HttpHandler handler;

	/** @brief The group of the endpoint, NULL for the ones of the first routes API. */
	struct _HttpGroup *group;

	/** @brief The route of the first routes API that created the endpoint, NULL for the others. */
	HttpRoute *route;

	/** @brief The settings of the endpoint itself. */
	HttpRouteSettings settings;

	/** @brief The settings with the inherited values, resolved when the cerver starts. */
	HttpRouteSettings resolved;

	/** @brief The number of params of the path. */
	unsigned int n_params;
	/** @brief The names of the params, NULL for a param without name. */
	char **params_names;

	/** @brief The stats of the requests. */
	HttpRouteStats *stats;
	/** @brief The stats of the uploaded files. */
	HttpRouteFileStats *file_stats;
	/** @brief The endpoint owns its stats (the ones of a route of the first routes API are of the route). */
	bool owns_stats;

};

/** @brief The handler of a method in a route path, see struct _HttpEndpoint. */
typedef struct _HttpEndpoint HttpEndpoint;

/**
 * @brief Creates an endpoint with its complete path.
 *
 * @param[in] path the complete path, copied
 * @param[in] method the request method
 * @param[in] handler the handler
 * @return the new endpoint, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpEndpoint *http_endpoint_create (
	const char *path, const HttpRequestMethod method, const HttpHandler handler
);

/**
 * @brief Releases an endpoint, its settings and the stats that it owns.
 *
 * @param[in] endpoint_ptr the endpoint, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_endpoint_delete (void *endpoint_ptr);

/**
 * @brief Returns the complete path of an endpoint.
 *
 * @param[in] endpoint the endpoint
 * @return the path, NULL for a NULL endpoint
 */
CERVER_EXPORT const String *http_endpoint_get_path (
	const HttpEndpoint *endpoint
);

/**
 * @brief Returns the method of an endpoint.
 *
 * @param[in] endpoint the endpoint
 * @return the method, REQUEST_METHOD_UNDEFINED for a NULL endpoint
 */
CERVER_EXPORT HttpRequestMethod http_endpoint_get_method (
	const HttpEndpoint *endpoint
);

/**
 * @brief Sets the modifier of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] modifier the modifier
 */
CERVER_EXPORT void http_endpoint_set_modifier (
	HttpEndpoint *endpoint, const HttpRouteModifier modifier
);

/**
 * @brief Sets the authentication type of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] auth_type the authentication type
 */
CERVER_EXPORT void http_endpoint_set_auth (
	HttpEndpoint *endpoint, const HttpRouteAuthType auth_type
);

/**
 * @brief Sets the methods that decode the data of the JWT of a request and release it.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] decode_data the method that decodes the data
 * @param[in] delete_decoded_data the method that releases it, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 */
CERVER_EXPORT void http_endpoint_set_decode_data (
	HttpEndpoint *endpoint,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

/**
 * @brief Decodes the data of the JWT of a request into a json string, released with free ().
 *
 * @param[in,out] endpoint the endpoint
 */
CERVER_EXPORT void http_endpoint_set_decode_data_into_json (
	HttpEndpoint *endpoint
);

/**
 * @brief Sets the authentication handler of an endpoint with HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] authentication_handler the handler, it returns 0 on success and 1 on error
 */
CERVER_EXPORT void http_endpoint_set_authentication_handler (
	HttpEndpoint *endpoint, const HttpAuthenticationHandler authentication_handler
);

/**
 * @brief Sets whether the authentication handler of an endpoint needs the body of the request, like
 * http_route_set_authentication_with_body ().
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] authentication_with_body true to authenticate after the body was received
 */
CERVER_EXPORT void http_endpoint_set_authentication_with_body (
	HttpEndpoint *endpoint, const bool authentication_with_body
);

/**
 * @brief Sets the custom data of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] custom_data the custom data, the endpoint owns it
 * @param[in] delete_custom_data releases it when the endpoint is deleted, NULL to keep it
 * @note A different custom data releases the one that the endpoint had with its delete; the same one is kept.
 */
CERVER_EXPORT void http_endpoint_set_custom_data (
	HttpEndpoint *endpoint, void *custom_data, void (*delete_custom_data)(void *)
);

/**
 * @brief Returns the custom data of an endpoint, or the one that it inherits from its groups.
 *
 * @param[in] endpoint the endpoint
 * @return the custom data, NULL for a NULL endpoint; the inherited one is known after the cerver starts
 */
CERVER_EXPORT const void *http_endpoint_get_custom_data (
	const HttpEndpoint *endpoint
);

/**
 * @brief Sets the max body size of the requests of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] max_body_size the size in bytes, 0 uses the one of the cerver
 */
CERVER_EXPORT void http_endpoint_set_max_body_size (
	HttpEndpoint *endpoint, const size_t max_body_size
);

/**
 * @brief Sets whether an endpoint answers the CORS preflights and adds the Access-Control-Allow-Origin of a
 * whitelisted origin to its responses.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] cors true to enable it
 */
CERVER_EXPORT void http_endpoint_set_cors (
	HttpEndpoint *endpoint, const bool cors
);

/**
 * @brief Sets whether the 200 responses to GET and HEAD of an endpoint have an entity tag, and are answered with
 * 304 when it matches.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] etag true to enable it
 */
CERVER_EXPORT void http_endpoint_set_etag (
	HttpEndpoint *endpoint, const bool etag
);

/**
 * @brief Sets whether the responses of an endpoint are compressed with gzip when the request accepts it.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] compression true to enable it, it only compresses when cerver was compiled with zlib
 */
CERVER_EXPORT void http_endpoint_set_compression (
	HttpEndpoint *endpoint, const bool compression
);

/**
 * @brief Sets what an endpoint asks of the certificate of the client (mTLS).
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] certificate HTTP_CLIENT_CERTIFICATE_REQUIRED answers 403 to a request without a certificate that
 * verified; the cerver must ask for it (tls_settings_set_client_verify () with TLS_CLIENT_VERIFY_OPTIONAL)
 * @since 2.1b-22
 */
CERVER_EXPORT void http_endpoint_set_client_certificate (
	HttpEndpoint *endpoint, const HttpClientCertificate certificate
);

/**
 * @brief Sets the pool of threads that handles the requests of an endpoint (http/core/pool.h), with
 * CERVER_HANDLER_TYPE_EVENTS: its handler, its middlewares and its custom authentication run in the threads of
 * that pool.
 *
 * The pool is found when the cerver starts: a name that no pool of the cerver has is an error of the routes and
 * the cerver does not start.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] name the name of the pool, copied; NULL or "default" for the thpool of the cerver. A name of more than
 * #HTTP_POOL_NAME_SIZE - 1 bytes is an error of the routes
 * @since 2.1b-17
 */
CERVER_EXPORT void http_endpoint_set_pool (
	HttpEndpoint *endpoint, const char *name
);

/**
 * @brief Adds a before middleware to an endpoint, after the ones of its groups.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 http_endpoint_add_before (
	HttpEndpoint *endpoint, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds an after middleware to an endpoint; the ones of the endpoint run before the ones of its groups.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 http_endpoint_add_after (
	HttpEndpoint *endpoint, const HttpAfterMiddleware middleware
);

/** @} */

/**
 * @defgroup http_routes_web_socket WebSocket endpoints
 * @ingroup http_routes
 * @brief The settings of the WebSocket connections of an endpoint.
 * @{
 */

/**
 * @brief Makes a GET endpoint take WebSocket connections (RFC 6455) with the methods of the handlers.
 *
 * A request without an upgrade goes to its handler, or it is answered with 426 when it has none.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] handlers the handlers, copied; NULL changes nothing
 * @since 2.1b-21 (it was http_endpoint_set_web_socket ())
 */
CERVER_EXPORT void http_endpoint_set_web_socket_handlers (
	HttpEndpoint *endpoint, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Returns the settings of the WebSocket connections of an endpoint, which take the ones of its groups that
 * it does not set.
 *
 * They are set with the http_web_socket_settings_set_* () methods, before the cerver starts.
 *
 * @param[in] endpoint the endpoint
 * @return its settings, or NULL without an endpoint
 * @since 2.1b-21 (it replaces the http_endpoint_set_web_socket_* () methods of each setting)
 */
CERVER_EXPORT HttpWebSocketSettings *http_endpoint_get_web_socket_settings (HttpEndpoint *endpoint);

/** @} */

/**
 * @defgroup http_routes_group Groups
 * @ingroup http_routes
 * @brief A prefix with the settings that its endpoints and subgroups inherit.
 *
 * A registration error (an invalid path or method, a missing handler) returns NULL and is kept in the tree: the
 * cerver does not start. The setters ignore a NULL group.
 * @{
 */

/**
 * @brief A prefix with the settings that its endpoints and subgroups inherit.
 */
struct _HttpGroup {

	/** @brief The complete prefix, like "/api/users", "/" for the root. */
	String *prefix;

	/** @brief The parent group, NULL for a top level one. */
	struct _HttpGroup *parent;
	/** @brief The tree of the group. */
	struct _HttpTree *tree;

	/** @brief The settings of the group. */
	HttpRouteSettings settings;

	/** @brief The subgroups, the group deletes them. */
	DoubleList *groups;
	/** @brief The endpoints, the group deletes them. */
	DoubleList *endpoints;

};

/** @brief A group of routes, see struct _HttpGroup. */
typedef struct _HttpGroup HttpGroup;

/**
 * @brief Releases a group, its subgroups, its endpoints and its settings.
 *
 * @param[in] group_ptr the group, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_group_delete (void *group_ptr);

/**
 * @brief Creates a top level group in an HTTP cerver.
 *
 * @param[in,out] http_cerver the HTTP cerver, it owns the group
 * @param[in] prefix the prefix, "" or "/" (or NULL) for the root
 * @return the group, NULL for a NULL cerver or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT HttpGroup *http_cerver_group_create (
	struct _HttpCerver *http_cerver, const char *prefix
);

/**
 * @brief Creates a group inside another one.
 *
 * @param[in,out] parent the parent group, it owns the new one
 * @param[in] prefix the prefix, relative to the one of the parent
 * @return the group, NULL for a NULL parent or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT HttpGroup *http_group_create (
	HttpGroup *parent, const char *prefix
);

/**
 * @brief Returns the complete prefix of a group.
 *
 * @param[in] group the group
 * @return the prefix, NULL for a NULL group
 */
CERVER_EXPORT const String *http_group_get_prefix (
	const HttpGroup *group
);

/**
 * @brief Adds an endpoint to a group.
 *
 * ":name" is a param, ":name<validator>" a param with a validator, and "*name" (only in the last segment) matches
 * the rest of the path.
 *
 * @param[in,out] group the group, it owns the endpoint
 * @param[in] method the request method
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler, required
 * @return the endpoint, NULL for a NULL group or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT HttpEndpoint *http_group_route_add (
	HttpGroup *group,
	const HttpRequestMethod method, const char *path, const HttpHandler handler
);

/**
 * @brief Adds the endpoint of a method in the path of the group itself, like GET /api/users.
 *
 * @param[in,out] group the group, it owns the endpoint
 * @param[in] method the request method
 * @param[in] handler the handler, required
 * @return the endpoint, NULL for a NULL group or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT HttpEndpoint *http_group_root_add (
	HttpGroup *group,
	const HttpRequestMethod method, const HttpHandler handler
);

/**
 * @brief Adds a GET endpoint to a group, like http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT HttpEndpoint *http_group_get (
	HttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a POST endpoint to a group, like http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT HttpEndpoint *http_group_post (
	HttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a PUT endpoint to a group, like http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT HttpEndpoint *http_group_put (
	HttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a PATCH endpoint to a group, like http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT HttpEndpoint *http_group_patch (
	HttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a DELETE endpoint to a group, like http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT HttpEndpoint *http_group_delete_route (
	HttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Sets the modifier that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] modifier the modifier
 */
CERVER_EXPORT void http_group_set_modifier (
	HttpGroup *group, const HttpRouteModifier modifier
);

/**
 * @brief Sets the authentication type that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] auth_type the authentication type
 */
CERVER_EXPORT void http_group_set_auth (
	HttpGroup *group, const HttpRouteAuthType auth_type
);

/**
 * @brief Sets the methods that decode and release the data of the JWT, inherited by the endpoints of a group.
 *
 * @param[in,out] group the group
 * @param[in] decode_data the method that decodes the data
 * @param[in] delete_decoded_data the method that releases it, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 */
CERVER_EXPORT void http_group_set_decode_data (
	HttpGroup *group,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

/**
 * @brief Decodes the data of the JWT into a json string (released with free ()) in the endpoints of a group.
 *
 * @param[in,out] group the group
 */
CERVER_EXPORT void http_group_set_decode_data_into_json (
	HttpGroup *group
);

/**
 * @brief Sets the authentication handler that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] authentication_handler the handler, it returns 0 on success and 1 on error
 */
CERVER_EXPORT void http_group_set_authentication_handler (
	HttpGroup *group, const HttpAuthenticationHandler authentication_handler
);

/**
 * @brief Sets whether the authentication of the endpoints of a group needs the body of the request.
 *
 * @param[in,out] group the group
 * @param[in] authentication_with_body true to authenticate after the body was received
 */
CERVER_EXPORT void http_group_set_authentication_with_body (
	HttpGroup *group, const bool authentication_with_body
);

/**
 * @brief Sets the custom data that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] custom_data the custom data, the group owns it
 * @param[in] delete_custom_data releases it when the group is deleted, NULL to keep it
 * @note A different custom data releases the one that the group had with its delete; the same one is kept.
 */
CERVER_EXPORT void http_group_set_custom_data (
	HttpGroup *group, void *custom_data, void (*delete_custom_data)(void *)
);

/**
 * @brief Sets the max body size that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] max_body_size the size in bytes, 0 uses the one of the cerver
 */
CERVER_EXPORT void http_group_set_max_body_size (
	HttpGroup *group, const size_t max_body_size
);

/**
 * @brief Sets the CORS that the endpoints of a group inherit, like http_endpoint_set_cors ().
 *
 * @param[in,out] group the group
 * @param[in] cors true to enable it
 */
CERVER_EXPORT void http_group_set_cors (
	HttpGroup *group, const bool cors
);

/**
 * @brief Sets the entity tags that the endpoints of a group inherit, like http_endpoint_set_etag ().
 *
 * @param[in,out] group the group
 * @param[in] etag true to enable them
 */
CERVER_EXPORT void http_group_set_etag (
	HttpGroup *group, const bool etag
);

/**
 * @brief Sets the compression that the endpoints of a group inherit, like http_endpoint_set_compression ().
 *
 * @param[in,out] group the group
 * @param[in] compression true to enable it
 */
CERVER_EXPORT void http_group_set_compression (
	HttpGroup *group, const bool compression
);

/**
 * @brief Sets what the endpoints of a group ask of the certificate of the client, like
 * http_endpoint_set_client_certificate ().
 *
 * @param[in,out] group the group
 * @param[in] certificate what is asked
 * @since 2.1b-22
 */
CERVER_EXPORT void http_group_set_client_certificate (
	HttpGroup *group, const HttpClientCertificate certificate
);

/**
 * @brief Sets the pool of threads that the endpoints of a group inherit, like http_endpoint_set_pool ().
 *
 * @param[in,out] group the group
 * @param[in] name the name of the pool, copied; NULL or "default" for the thpool of the cerver
 * @since 2.1b-17
 */
CERVER_EXPORT void http_group_set_pool (
	HttpGroup *group, const char *name
);

/**
 * @brief Adds a before middleware to a group; the ones of the outer groups run first.
 *
 * @param[in,out] group the group
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 http_group_add_before (
	HttpGroup *group, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds an after middleware to a group; the after middlewares run in the opposite order of the before ones.
 *
 * @param[in,out] group the group
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 http_group_add_after (
	HttpGroup *group, const HttpAfterMiddleware middleware
);

/**
 * @brief Adds a WebSocket endpoint (GET) to a group, without a handler for the requests without an upgrade.
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handlers the handlers, copied; NULL inherits the ones of its groups
 * @return the endpoint, NULL for a NULL group or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT HttpEndpoint *http_group_web_socket_add (
	HttpGroup *group, const char *path, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the WebSocket handlers that the WebSocket endpoints of a group inherit; the other endpoints of the
 * group are not changed.
 *
 * @param[in,out] group the group
 * @param[in] handlers the handlers, copied; NULL changes nothing
 * @since 2.1b-21 (it was http_group_set_web_socket ())
 */
CERVER_EXPORT void http_group_set_web_socket_handlers (
	HttpGroup *group, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Returns the settings that the WebSocket endpoints of a group (and its subgroups) inherit; an endpoint or a
 * subgroup keeps the ones that it sets.
 *
 * They are set with the http_web_socket_settings_set_* () methods, before the cerver starts.
 *
 * @param[in] group the group
 * @return its settings, or NULL without a group
 * @since 2.1b-21 (it replaces the http_group_set_web_socket_* () methods of each setting)
 */
CERVER_EXPORT HttpWebSocketSettings *http_group_get_web_socket_settings (HttpGroup *group);

/** @} */

/**
 * @defgroup http_routes_tree Tree
 * @ingroup http_routes
 * @brief The routes tree: its nodes, how it is compiled and how a url is matched.
 * @{
 */

/**
 * @brief The type of a node of the routes tree.
 */
typedef enum HttpRouteNodeType {

	/** @brief Undefined. */
	HTTP_ROUTE_NODE_TYPE_NONE			= 0,
	/** @brief A segment with a fixed text. */
	HTTP_ROUTE_NODE_TYPE_STATIC			= 1,
	/** @brief A param segment, like `:id`. */
	HTTP_ROUTE_NODE_TYPE_PARAM			= 2,
	/** @brief A wildcard that matches the rest of the path, like `*path`. */
	HTTP_ROUTE_NODE_TYPE_WILDCARD		= 3,

	/** @brief The number of types, the first value that is not valid. */
	HTTP_ROUTE_NODE_TYPE_MAX			= 4

} HttpRouteNodeType;

/**
 * @brief A segment of the routes tree.
 *
 * A segment of a url is matched with the static children first (a binary search), then with the params (the ones
 * with a validator first) and at last with the wildcard, so the order in which the routes were registered never
 * changes the result.
 */
struct _HttpRouteNode {

	/** @brief The type of the node. */
	HttpRouteNodeType type;

	/** @brief The text of a static segment, the name of the validator of a param (NULL without one). */
	String *segment;
	/** @brief The validator of a param, NULL without one. */
	const HttpParamValidator *validator;

	/** @brief The number of static children. */
	size_t n_statics;
	/** @brief The static children, sorted by their segment. */
	struct _HttpRouteNode **statics;

	/** @brief The number of param children. */
	size_t n_params;
	/** @brief The param children, the ones with a validator first. */
	struct _HttpRouteNode **params;

	/** @brief The wildcard child, NULL without one. */
	struct _HttpRouteNode *wildcard;

	/** @brief The number of endpoints of the node. */
	size_t n_endpoints;
	/** @brief The endpoint of each request method, NULL for the methods without one. */
	HttpEndpoint *endpoints[REQUEST_METHOD_SIZE];

	/** @brief The length of #allow. */
	size_t allow_len;
	/** @brief The methods that the path accepts, for the Allow header. */
	char allow[HTTP_TREE_ALLOW_SIZE];

	/** @brief An endpoint of the node answers CORS preflights. */
	bool cors;

};

/** @brief A segment of the routes tree, see struct _HttpRouteNode. */
typedef struct _HttpRouteNode HttpRouteNode;

/**
 * @brief The routes tree of an HTTP cerver.
 */
struct _HttpTree {

	/** @brief The HTTP cerver of the tree. */
	struct _HttpCerver *http_cerver;

	/** @brief The top level groups. */
	DoubleList *groups;

	/** @brief The endpoints that translate the routes of the first routes API. */
	DoubleList *compatibility_endpoints;

	/** @brief The custom param validators. */
	DoubleList *validators;

	/** @brief The root node, NULL until the tree is compiled. */
	HttpRouteNode *root;
	/** @brief The tree was compiled. */
	bool compiled;
	/** @brief The number of endpoints of the compiled tree. */
	size_t n_endpoints;

	/** @brief The most params of a route. */
	unsigned int max_params;
	/** @brief The trailing slash policy. */
	HttpTrailingSlash trailing_slash;

	/** @brief A method without a handler is answered with 405 instead of the catch all handler. */
	bool method_not_allowed;
	/** @brief The handler of the 405 responses, NULL for the default one. */
	HttpHandler method_not_allowed_handler;

	/** @brief The middlewares of every endpoint. */
	HttpMiddlewares middlewares;

	/** @brief The registration errors; the cerver does not start if there is any. */
	size_t n_errors;
	/** @brief The first registration error, the one that is reported. */
	char error[HTTP_TREE_ERROR_SIZE];

};

/** @brief The routes tree of an HTTP cerver, see struct _HttpTree. */
typedef struct _HttpTree HttpTree;

/**
 * @brief Creates an empty routes tree with the default limits.
 *
 * @param[in] http_cerver the HTTP cerver
 * @return the new tree, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpTree *http_tree_create (struct _HttpCerver *http_cerver);

/**
 * @brief Releases a routes tree, its groups, its endpoints, its validators and its nodes.
 *
 * @param[in] tree_ptr the tree, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_tree_delete (void *tree_ptr);

/**
 * @brief Logs a registration error and keeps it in the tree; the first one is the one that is reported.
 *
 * @param[in,out] tree the tree, it can be NULL (then it is only logged)
 * @param[in] format the printf () format of the error
 * @param[in] ... the arguments of the format
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_tree_error (
	HttpTree *tree, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Logs a warning of the first routes API, that is not an error.
 *
 * @param[in] format the printf () format of the warning
 * @param[in] ... the arguments of the format
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_tree_warning (
	const char *format, ...
) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Checks the syntax of a path relative to a group.
 *
 * @param[in] path the path
 * @param[out] error where the reason is written when it is not valid
 * @param[in] error_size the size of error
 * @retval 0 the path is valid
 * @retval 1 it is not (a NULL path, a path that starts with "//", a segment that is not valid)
 */
CERVER_PUBLIC u8 http_tree_path_validate (
	const char *path, char *error, const size_t error_size
);

/**
 * @brief Joins a path relative to a prefix, like "/api" and "users" in "/api/users".
 *
 * @param[in] prefix the prefix, it must not be NULL
 * @param[in] path the path, NULL or "" for the prefix itself
 * @return the new string, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE String *http_tree_path_join (
	const char *prefix, const char *path
);

/**
 * @brief Adds a resolved endpoint to the routes tree.
 *
 * A conflict of the first routes API (compatibility) is only a warning, and the first route is kept.
 *
 * @param[in,out] tree the tree
 * @param[in] endpoint the endpoint
 * @param[in] compatibility true for an endpoint of the first routes API
 * @retval 0 the endpoint was added
 * @retval 1 it was not added
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_tree_insert (
	HttpTree *tree, HttpEndpoint *endpoint, const bool compatibility
);

/**
 * @brief Finds the pool of threads of an endpoint with the name of its resolved settings: the cerver of the tree
 * must have it, an unknown name is an error of the tree.
 *
 * @param[in,out] tree the tree
 * @param[in,out] endpoint the endpoint, with its resolved settings
 * @retval 0 the pool was found, or the endpoint did not choose one
 * @retval 1 no pool has the name (the cerver does not start)
 * @note For the internal methods of cerver.
 * @since 2.1b-17
 */
CERVER_PRIVATE u8 http_tree_resolve_pool (
	HttpTree *tree, HttpEndpoint *endpoint
);

/**
 * @brief Builds the routes tree with the groups and the routes of the first routes API.
 *
 * @param[in,out] tree the tree
 * @retval 0 the tree was compiled
 * @retval 1 it was not (the cerver does not start)
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_tree_compile (HttpTree *tree);

/**
 * @brief The result of matching a url with the routes tree.
 */
typedef struct HttpRouteMatch {

	/** @brief The node that matched. */
	const HttpRouteNode *node;

	/** @brief The params that were written. */
	unsigned int n_params;

} HttpRouteMatch;

/**
 * @brief Matches a path with the routes tree.
 *
 * @param[in] tree the compiled tree
 * @param[in] path the path of the url
 * @param[in] path_len the length of the path
 * @param[out] params where the params are written, with max_params + 1 slots
 * @param[out] match the node and the number of params
 * @return true if a node with endpoints matched the path
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool http_tree_match (
	const HttpTree *tree,
	const char *path, const size_t path_len,
	HttpRequestParam *params, HttpRouteMatch *match
);

/**
 * @brief Prints the compiled routes tree.
 *
 * @param[in] tree the tree, nothing is printed before it is compiled
 */
CERVER_PUBLIC void http_tree_print (const HttpTree *tree);

/**
 * @brief Returns the number of endpoints in the compiled tree.
 *
 * @param[in] tree the tree
 * @return the number of endpoints, 0 for a NULL tree
 */
CERVER_PUBLIC size_t http_tree_get_n_endpoints (const HttpTree *tree);

/**
 * @brief Returns the compiled endpoints as json.
 *
 * `{"n_endpoints": N, "endpoints": [{"method", "path", "auth", "modifier", "compatibility"}]}`
 *
 * @param[in] tree the tree
 * @return the json, allocated with malloc (); NULL if the tree is not compiled
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE char *http_tree_generate_json (const HttpTree *tree);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
