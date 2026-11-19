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

#include "cerver/http/core/context.h"
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
 * validators.
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

struct _HttpReceive;

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
typedef enum CerverHttpTrailingSlash {

	#define XX(num, name, string, description) HTTP_TRAILING_SLASH_##name = num,
	HTTP_TRAILING_SLASH_MAP (XX)
	#undef XX

	/** @brief The number of policies, the first value that is not valid. */
	HTTP_TRAILING_SLASH_MAX

} CerverHttpTrailingSlash;

/**
 * @brief Returns the string of a trailing slash policy.
 *
 * @param[in] trailing_slash the policy
 * @return the string of its row in #HTTP_TRAILING_SLASH_MAP, or the one of HTTP_TRAILING_SLASH_NONE for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_trailing_slash_to_string (
	const CerverHttpTrailingSlash trailing_slash
);

/**
 * @brief Returns the description of a trailing slash policy.
 *
 * @param[in] trailing_slash the policy
 * @return the description of its row in #HTTP_TRAILING_SLASH_MAP, or the one of HTTP_TRAILING_SLASH_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_trailing_slash_description (
	const CerverHttpTrailingSlash trailing_slash
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

/**
 * @brief The size of the name of a pool of threads (http/core/pool.h), with its end.
 *
 * @since 3.0b-5
 */
#define HTTP_POOL_NAME_SIZE							32

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
 * @brief Returns the complete path of an endpoint.
 *
 * @param[in] endpoint the endpoint
 * @return the path, NULL for a NULL endpoint
 */
CERVER_EXPORT const String *cerver_http_endpoint_get_path (
	const CerverHttpEndpoint *endpoint
);

/**
 * @brief Returns the method of an endpoint.
 *
 * @param[in] endpoint the endpoint
 * @return the method, REQUEST_METHOD_UNDEFINED for a NULL endpoint
 */
CERVER_EXPORT CerverHttpMethod cerver_http_endpoint_get_method (
	const CerverHttpEndpoint *endpoint
);

/**
 * @brief Sets the modifier of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] modifier the modifier
 */
CERVER_EXPORT void cerver_http_endpoint_set_modifier (
	CerverHttpEndpoint *endpoint, const CerverHttpRouteModifier modifier
);

/**
 * @brief Sets the authentication type of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] auth_type the authentication type
 */
CERVER_EXPORT void cerver_http_endpoint_set_auth (
	CerverHttpEndpoint *endpoint, const CerverHttpRouteAuthType auth_type
);

/**
 * @brief Sets the methods that decode the data of the JWT of a request and release it.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] decode_data the method that decodes the data, from the claims of the token
 * @param[in] delete_decoded_data the method that releases it, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 * @since 3.0b-7 decode_data is a CerverHttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT void cerver_http_endpoint_set_decode_data (
	CerverHttpEndpoint *endpoint,
	CerverHttpDecodeData decode_data, void (*delete_decoded_data)(void *)
);

/**
 * @brief Decodes the data of the JWT of a request into a json string, released with free ().
 *
 * @param[in,out] endpoint the endpoint
 */
CERVER_EXPORT void cerver_http_endpoint_set_decode_data_into_json (
	CerverHttpEndpoint *endpoint
);

/**
 * @brief Sets the authentication handler of an endpoint with HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] authentication_handler the handler, it returns 0 on success and 1 on error
 */
CERVER_EXPORT void cerver_http_endpoint_set_authentication_handler (
	CerverHttpEndpoint *endpoint, const HttpAuthenticationHandler authentication_handler
);

/**
 * @brief Sets whether the authentication handler of an endpoint needs the body of the request, like
 * http_route_set_authentication_with_body ().
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] authentication_with_body true to authenticate after the body was received
 */
CERVER_EXPORT void cerver_http_endpoint_set_authentication_with_body (
	CerverHttpEndpoint *endpoint, const bool authentication_with_body
);

/**
 * @brief Sets the custom data of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] custom_data the custom data, the endpoint owns it
 * @param[in] delete_custom_data releases it when the endpoint is deleted, NULL to keep it
 * @note A different custom data releases the one that the endpoint had with its delete; the same one is kept.
 */
CERVER_EXPORT void cerver_http_endpoint_set_custom_data (
	CerverHttpEndpoint *endpoint, void *custom_data, void (*delete_custom_data)(void *)
);

/**
 * @brief Returns the custom data of an endpoint, or the one that it inherits from its groups.
 *
 * @param[in] endpoint the endpoint
 * @return the custom data, NULL for a NULL endpoint; the inherited one is known after the cerver starts
 */
CERVER_EXPORT const void *cerver_http_endpoint_get_custom_data (
	const CerverHttpEndpoint *endpoint
);

/**
 * @brief Sets the max body size of the requests of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] max_body_size the size in bytes, 0 uses the one of the cerver
 */
CERVER_EXPORT void cerver_http_endpoint_set_max_body_size (
	CerverHttpEndpoint *endpoint, const size_t max_body_size
);

/**
 * @brief Sets the max size of each file of the multipart requests of an endpoint; a bigger one is answered
 * with 413.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] max_file_size the size in bytes, 0 uses the one of the cerver (cerver_http_set_max_file_size ())
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_endpoint_set_max_file_size (
	CerverHttpEndpoint *endpoint, const size_t max_file_size
);

/**
 * @brief Sets the max parts of the multipart requests of an endpoint; a request with more is answered with 413.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] max_multipart_parts the parts, 0 uses the ones of the cerver (cerver_http_set_max_multipart_parts ())
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_endpoint_set_max_multipart_parts (
	CerverHttpEndpoint *endpoint, const unsigned int max_multipart_parts
);

/**
 * @brief Sets the methods that take the parts of the multipart requests of an endpoint while their body arrives:
 * cerver saves none of their files and keeps none of their values (#CerverHttpPartHandlers).
 *
 * The limits of the endpoint still apply: its max parts, its max file size (for the bytes of each part with a
 * filename) and its max body size.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] handlers the methods, they are copied; NULL for cerver to take the parts again
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_endpoint_set_part_handlers (
	CerverHttpEndpoint *endpoint, const CerverHttpPartHandlers *handlers
);

/**
 * @brief Sets whether an endpoint answers the CORS preflights and adds the Access-Control-Allow-Origin of a
 * whitelisted origin to its responses.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] cors true to enable it
 */
CERVER_EXPORT void cerver_http_endpoint_set_cors (
	CerverHttpEndpoint *endpoint, const bool cors
);

/**
 * @brief Sets whether the 200 responses to GET and HEAD of an endpoint have an entity tag, and are answered with
 * 304 when it matches.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] etag true to enable it
 */
CERVER_EXPORT void cerver_http_endpoint_set_etag (
	CerverHttpEndpoint *endpoint, const bool etag
);

/**
 * @brief Sets whether the responses of an endpoint are compressed with gzip when the request accepts it.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] compression true to enable it, it only compresses when cerver was compiled with zlib
 */
CERVER_EXPORT void cerver_http_endpoint_set_compression (
	CerverHttpEndpoint *endpoint, const bool compression
);

/**
 * @brief Sets the smallest body of the responses of an endpoint that is compressed.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] min_size the size in bytes, 0 uses the one of the cerver (cerver_http_set_compression_min_size ())
 * @note It only applies to an endpoint whose responses are compressed (cerver_http_endpoint_set_compression ()).
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_endpoint_set_compression_min_size (
	CerverHttpEndpoint *endpoint, const size_t min_size
);

/**
 * @brief Sets the level of the compression of the responses of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] level from #HTTP_COMPRESSION_MIN_LEVEL to #HTTP_COMPRESSION_MAX_LEVEL; 0 uses the one of the cerver
 * (cerver_http_set_compression_level ())
 * @retval 0 the level was set
 * @retval 1 a NULL endpoint or a level out of that range, that is not set
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_endpoint_set_compression_level (
	CerverHttpEndpoint *endpoint, const int level
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
 * @param[in] name the name of the pool, copied: "default" for the thpool of the cerver, "loop" for the event loop
 * that reads the request (#HTTP_POOL_LOOP_NAME: its handler must never wait); NULL for none, the endpoint takes the
 * one of its groups or the one of its cerver (cerver_http_set_handlers_pool ()). A name of more than
 * #HTTP_POOL_NAME_SIZE - 1 bytes is an error of the routes
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_endpoint_set_pool (
	CerverHttpEndpoint *endpoint, const char *name
);

/**
 * @brief Adds a before middleware to an endpoint, after the ones of its groups.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 cerver_http_endpoint_add_before (
	CerverHttpEndpoint *endpoint, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds an after middleware to an endpoint; the ones of the endpoint run before the ones of its groups.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 cerver_http_endpoint_add_after (
	CerverHttpEndpoint *endpoint, const HttpAfterMiddleware middleware
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
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket (
	CerverHttpEndpoint *endpoint, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the most bytes of a message (and of each frame); a larger one closes the connection with 1009.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] max_message the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE (the default)
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_max_message (
	CerverHttpEndpoint *endpoint, const size_t max_message
);

/**
 * @brief Sets the subprotocols of an endpoint, in the order of preference of the server.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] protocols the subprotocols, copied
 * @param[in] n_protocols the number of subprotocols
 * @retval 0 the subprotocols were set
 * @retval 1 a NULL endpoint or they could not be set (see http_web_socket_settings_set_protocols ())
 */
CERVER_EXPORT u8 cerver_http_endpoint_set_web_socket_protocols (
	CerverHttpEndpoint *endpoint, const char * const *protocols, const unsigned int n_protocols
);

/**
 * @brief Enables or disables permessage-deflate (RFC 7692) with the default options, when cerver was built with
 * zlib.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] compression true to enable it
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_compression (
	CerverHttpEndpoint *endpoint, const bool compression
);

/**
 * @brief Sets the options of permessage-deflate of an endpoint.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] options the options, copied
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_compression_options (
	CerverHttpEndpoint *endpoint, const HttpWebSocketDeflateOptions *options
);

/**
 * @brief Sets whether the Origin of the upgrade must be in the origins whitelist; 403 if it is not.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] origin_check true to check it
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_origin_check (
	CerverHttpEndpoint *endpoint, const bool origin_check
);

/**
 * @brief Sets the milliseconds without receiving anything before a ping, and the ones to wait for its pong.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] ping_interval the milliseconds before a ping, 0 disables them
 * @param[in] pong_timeout the milliseconds to wait for the pong
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_ping_milliseconds (
	CerverHttpEndpoint *endpoint, const unsigned int ping_interval, const unsigned int pong_timeout
);

/**
 * @brief Sets the milliseconds that the closing handshake waits for the peer.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] close_timeout the milliseconds
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_close_timeout_milliseconds (
	CerverHttpEndpoint *endpoint, const unsigned int close_timeout
);

/**
 * @brief Sets the milliseconds of each send of a WebSocket connection.
 *
 * @param[in,out] endpoint the endpoint
 * @param[in] send_timeout the milliseconds, 0 uses the send timeout of the HTTP cerver
 */
CERVER_EXPORT void cerver_http_endpoint_set_web_socket_send_timeout_milliseconds (
	CerverHttpEndpoint *endpoint, const unsigned int send_timeout
);

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
 * @brief Creates a top level group in an HTTP cerver.
 *
 * @param[in,out] http_cerver the HTTP cerver, it owns the group
 * @param[in] prefix the prefix, "" or "/" (or NULL) for the root
 * @return the group, NULL for a NULL cerver or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT CerverHttpGroup *cerver_http_group_create (
	CerverHttp *http_cerver, const char *prefix
);

/**
 * @brief Creates a group inside another one.
 *
 * @param[in,out] parent the parent group, it owns the new one
 * @param[in] prefix the prefix, relative to the one of the parent
 * @return the group, NULL for a NULL parent or on error (the error is kept in the tree and the cerver does not
 * start)
 */
CERVER_EXPORT CerverHttpGroup *cerver_http_group_subgroup_add (
	CerverHttpGroup *parent, const char *prefix
);

/**
 * @brief Returns the complete prefix of a group.
 *
 * @param[in] group the group
 * @return the prefix, NULL for a NULL group
 */
CERVER_EXPORT const String *cerver_http_group_get_prefix (
	const CerverHttpGroup *group
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
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_route_add (
	CerverHttpGroup *group,
	const CerverHttpMethod method, const char *path, const HttpHandler handler
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
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_root_add (
	CerverHttpGroup *group,
	const CerverHttpMethod method, const HttpHandler handler
);

/**
 * @brief Adds a GET endpoint to a group, like cerver_http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_get (
	CerverHttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a POST endpoint to a group, like cerver_http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_post (
	CerverHttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a PUT endpoint to a group, like cerver_http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_put (
	CerverHttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a PATCH endpoint to a group, like cerver_http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_patch (
	CerverHttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Adds a DELETE endpoint to a group, like cerver_http_group_route_add ().
 *
 * @param[in,out] group the group
 * @param[in] path the path, relative to the prefix of the group
 * @param[in] handler the handler
 * @return the endpoint, NULL on error
 */
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_delete_route (
	CerverHttpGroup *group, const char *path, const HttpHandler handler
);

/**
 * @brief Sets the modifier that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] modifier the modifier
 */
CERVER_EXPORT void cerver_http_group_set_modifier (
	CerverHttpGroup *group, const CerverHttpRouteModifier modifier
);

/**
 * @brief Sets the authentication type that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] auth_type the authentication type
 */
CERVER_EXPORT void cerver_http_group_set_auth (
	CerverHttpGroup *group, const CerverHttpRouteAuthType auth_type
);

/**
 * @brief Sets the methods that decode and release the data of the JWT, inherited by the endpoints of a group.
 *
 * @param[in,out] group the group
 * @param[in] decode_data the method that decodes the data, from the claims of the token
 * @param[in] delete_decoded_data the method that releases it, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 * @since 3.0b-7 decode_data is a CerverHttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT void cerver_http_group_set_decode_data (
	CerverHttpGroup *group,
	CerverHttpDecodeData decode_data, void (*delete_decoded_data)(void *)
);

/**
 * @brief Decodes the data of the JWT into a json string (released with free ()) in the endpoints of a group.
 *
 * @param[in,out] group the group
 */
CERVER_EXPORT void cerver_http_group_set_decode_data_into_json (
	CerverHttpGroup *group
);

/**
 * @brief Sets the authentication handler that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] authentication_handler the handler, it returns 0 on success and 1 on error
 */
CERVER_EXPORT void cerver_http_group_set_authentication_handler (
	CerverHttpGroup *group, const HttpAuthenticationHandler authentication_handler
);

/**
 * @brief Sets whether the authentication of the endpoints of a group needs the body of the request.
 *
 * @param[in,out] group the group
 * @param[in] authentication_with_body true to authenticate after the body was received
 */
CERVER_EXPORT void cerver_http_group_set_authentication_with_body (
	CerverHttpGroup *group, const bool authentication_with_body
);

/**
 * @brief Sets the custom data that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] custom_data the custom data, the group owns it
 * @param[in] delete_custom_data releases it when the group is deleted, NULL to keep it
 * @note A different custom data releases the one that the group had with its delete; the same one is kept.
 */
CERVER_EXPORT void cerver_http_group_set_custom_data (
	CerverHttpGroup *group, void *custom_data, void (*delete_custom_data)(void *)
);

/**
 * @brief Sets the max body size that the endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] max_body_size the size in bytes, 0 uses the one of the cerver
 */
CERVER_EXPORT void cerver_http_group_set_max_body_size (
	CerverHttpGroup *group, const size_t max_body_size
);

/**
 * @brief Sets the max size of each file that the endpoints of a group inherit, like
 * cerver_http_endpoint_set_max_file_size ().
 *
 * @param[in,out] group the group
 * @param[in] max_file_size the size in bytes, 0 uses the one of the cerver
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_group_set_max_file_size (
	CerverHttpGroup *group, const size_t max_file_size
);

/**
 * @brief Sets the max parts of a multipart request that the endpoints of a group inherit, like
 * cerver_http_endpoint_set_max_multipart_parts ().
 *
 * @param[in,out] group the group
 * @param[in] max_multipart_parts the parts, 0 uses the ones of the cerver
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_group_set_max_multipart_parts (
	CerverHttpGroup *group, const unsigned int max_multipart_parts
);

/**
 * @brief Sets the methods that take the parts of the multipart requests that the endpoints of a group inherit,
 * like cerver_http_endpoint_set_part_handlers ().
 *
 * @param[in,out] group the group
 * @param[in] handlers the methods, they are copied; NULL for cerver to take the parts
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_group_set_part_handlers (
	CerverHttpGroup *group, const CerverHttpPartHandlers *handlers
);

/**
 * @brief Sets the CORS that the endpoints of a group inherit, like cerver_http_endpoint_set_cors ().
 *
 * @param[in,out] group the group
 * @param[in] cors true to enable it
 */
CERVER_EXPORT void cerver_http_group_set_cors (
	CerverHttpGroup *group, const bool cors
);

/**
 * @brief Sets the entity tags that the endpoints of a group inherit, like cerver_http_endpoint_set_etag ().
 *
 * @param[in,out] group the group
 * @param[in] etag true to enable them
 */
CERVER_EXPORT void cerver_http_group_set_etag (
	CerverHttpGroup *group, const bool etag
);

/**
 * @brief Sets the compression that the endpoints of a group inherit, like cerver_http_endpoint_set_compression ().
 *
 * @param[in,out] group the group
 * @param[in] compression true to enable it
 */
CERVER_EXPORT void cerver_http_group_set_compression (
	CerverHttpGroup *group, const bool compression
);

/**
 * @brief Sets the smallest body that is compressed that the endpoints of a group inherit, like
 * cerver_http_endpoint_set_compression_min_size ().
 *
 * @param[in,out] group the group
 * @param[in] min_size the size in bytes, 0 uses the one of the cerver
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_group_set_compression_min_size (
	CerverHttpGroup *group, const size_t min_size
);

/**
 * @brief Sets the level of the compression that the endpoints of a group inherit, like
 * cerver_http_endpoint_set_compression_level ().
 *
 * @param[in,out] group the group
 * @param[in] level from #HTTP_COMPRESSION_MIN_LEVEL to #HTTP_COMPRESSION_MAX_LEVEL; 0 uses the one of the cerver
 * @retval 0 the level was set
 * @retval 1 a NULL group or a level out of that range, that is not set
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_group_set_compression_level (
	CerverHttpGroup *group, const int level
);

/**
 * @brief Sets the pool of threads that the endpoints of a group inherit, like cerver_http_endpoint_set_pool ().
 *
 * @param[in,out] group the group
 * @param[in] name the name of the pool, copied: "default" for the thpool of the cerver, "loop" for the event loop
 * that reads each request (#HTTP_POOL_LOOP_NAME); NULL for none
 * @since 3.0b-5
 */
CERVER_EXPORT void cerver_http_group_set_pool (
	CerverHttpGroup *group, const char *name
);

/**
 * @brief Adds a before middleware to a group; the ones of the outer groups run first.
 *
 * @param[in,out] group the group
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 cerver_http_group_add_before (
	CerverHttpGroup *group, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds an after middleware to a group; the after middlewares run in the opposite order of the before ones.
 *
 * @param[in,out] group the group
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 cerver_http_group_add_after (
	CerverHttpGroup *group, const HttpAfterMiddleware middleware
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
CERVER_EXPORT CerverHttpEndpoint *cerver_http_group_web_socket_add (
	CerverHttpGroup *group, const char *path, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the WebSocket handlers that the WebSocket endpoints of a group inherit; the other endpoints of the
 * group are not changed.
 *
 * @param[in,out] group the group
 * @param[in] handlers the handlers, copied; NULL changes nothing
 */
CERVER_EXPORT void cerver_http_group_set_web_socket (
	CerverHttpGroup *group, const HttpWebSocketHandlers *handlers
);

/**
 * @brief Sets the max message that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] max_message the bytes, 0 for #HTTP_WEB_SOCKET_DEFAULT_MAX_MESSAGE
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_max_message (
	CerverHttpGroup *group, const size_t max_message
);

/**
 * @brief Sets the subprotocols that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] protocols the subprotocols, copied
 * @param[in] n_protocols the number of subprotocols
 * @retval 0 the subprotocols were set
 * @retval 1 a NULL group or they could not be set
 */
CERVER_EXPORT u8 cerver_http_group_set_web_socket_protocols (
	CerverHttpGroup *group, const char * const *protocols, const unsigned int n_protocols
);

/**
 * @brief Enables or disables permessage-deflate with the default options in the WebSocket endpoints of a group.
 *
 * @param[in,out] group the group
 * @param[in] compression true to enable it
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_compression (
	CerverHttpGroup *group, const bool compression
);

/**
 * @brief Sets the options of permessage-deflate that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] options the options, copied
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_compression_options (
	CerverHttpGroup *group, const HttpWebSocketDeflateOptions *options
);

/**
 * @brief Sets the check of the Origin that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] origin_check true to check it
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_origin_check (
	CerverHttpGroup *group, const bool origin_check
);

/**
 * @brief Sets the pings that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] ping_interval the milliseconds before a ping, 0 disables them
 * @param[in] pong_timeout the milliseconds to wait for the pong
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_ping_milliseconds (
	CerverHttpGroup *group, const unsigned int ping_interval, const unsigned int pong_timeout
);

/**
 * @brief Sets the close timeout that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] close_timeout the milliseconds
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_close_timeout_milliseconds (
	CerverHttpGroup *group, const unsigned int close_timeout
);

/**
 * @brief Sets the send timeout that the WebSocket endpoints of a group inherit.
 *
 * @param[in,out] group the group
 * @param[in] send_timeout the milliseconds, 0 uses the send timeout of the HTTP cerver
 */
CERVER_EXPORT void cerver_http_group_set_web_socket_send_timeout_milliseconds (
	CerverHttpGroup *group, const unsigned int send_timeout
);

/** @} */

/**
 * @defgroup http_routes_tree Tree
 * @ingroup http_routes
 * @brief The routes tree: its nodes, how it is compiled and how a url is matched.
 * @{
 */

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
 * @brief Prints the compiled routes tree.
 *
 * @param[in] tree the tree, nothing is printed before it is compiled
 */
CERVER_PUBLIC void http_tree_print (const struct _HttpTree *tree);

/**
 * @brief Returns the number of endpoints in the compiled tree.
 *
 * @param[in] tree the tree
 * @return the number of endpoints, 0 for a NULL tree
 */
CERVER_PUBLIC size_t http_tree_get_n_endpoints (const struct _HttpTree *tree);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
