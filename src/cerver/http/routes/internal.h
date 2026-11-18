/**
 * @file cerver/http/routes/internal.h
 * @internal
 * @brief The internals of the routes tree: the settings, the endpoints, the groups, the nodes and the tree, and the
 * methods that the parts of HTTP use to build the tree and to match a url with it.
 */

#ifndef _CERVER_HTTP_ROUTES_INTERNAL_H_
#define _CERVER_HTTP_ROUTES_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/requests/method.h"
#include "cerver/http/routes/middleware.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/routes/validator.h"
#include "cerver/http/websockets/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;

struct _HttpGroup;
struct _HttpTree;

struct HttpPool;

/**
 * @defgroup http_routes_internal Routes internals
 * @ingroup http_routes
 * @brief The settings, the endpoints, the groups and the nodes of the routes tree, and how it is built and matched.
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
 * @since 3.0b-5
 */
#define HTTP_ROUTE_SETTING_POOL						(1 << 10)

/**
 * @brief The settings of a group or an endpoint.
 */
struct _HttpRouteSettings {

	/** @brief The HTTP_ROUTE_SETTING_* flags of the settings that were set. */
	unsigned int set;

	/** @brief The modifier. */
	CerverHttpRouteModifier modifier;

	/** @brief The authentication type. */
	CerverHttpRouteAuthType auth_type;

	/** @brief Decodes the data of the JWT of a bearer authentication, from its claims. */
	CerverHttpDecodeData decode_data;
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
	 * @since 3.0b-5
	 */
	char pool_name[HTTP_POOL_NAME_SIZE];
	/**
	 * @brief The pool with that name, found when the cerver starts; NULL without a name: the default pool, the
	 * thpool of the cerver.
	 *
	 * @since 3.0b-5
	 */
	struct HttpPool *pool;

	/** @brief The middlewares. */
	HttpMiddlewares middlewares;

	/** @brief The settings of the connections of a WebSocket endpoint (HTTP_ROUTE_MODIFIER_WEB_SOCKET), each one inherited on its own. */
	HttpWebSocketSettings web_socket;

};

/** @brief The settings of a group or an endpoint, see struct _HttpRouteSettings. */
typedef struct _HttpRouteSettings HttpRouteSettings;

/**
 * @brief The handler of a method in a route path.
 */
struct _HttpEndpoint {

	/** @brief The complete path, like "/api/users/:id<objectid>". */
	String *path;

	/** @brief The request method. */
	CerverHttpMethod method;
	/** @brief The handler, NULL in a WebSocket endpoint without one. */
	HttpHandler handler;

	/** @brief The group of the endpoint. */
	struct _HttpGroup *group;

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
CERVER_PRIVATE HttpEndpoint *cerver_http_endpoint_create (
	const char *path, const CerverHttpMethod method, const HttpHandler handler
);

/**
 * @brief Releases an endpoint, its settings and the stats that it owns.
 *
 * @param[in] endpoint_ptr the endpoint, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_endpoint_delete (void *endpoint_ptr);

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
CERVER_PRIVATE void cerver_http_group_delete (void *group_ptr);

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
	CerverHttpTrailingSlash trailing_slash;

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
 * @param[in,out] tree the tree
 * @param[in] endpoint the endpoint
 * @retval 0 the endpoint was added
 * @retval 1 it was not added
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_tree_insert (
	HttpTree *tree, HttpEndpoint *endpoint
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
 * @since 3.0b-5
 */
CERVER_PRIVATE u8 http_tree_resolve_pool (
	HttpTree *tree, HttpEndpoint *endpoint
);

/**
 * @brief Builds the routes tree with the endpoints of the groups.
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

/** @brief A route param of a request, a slice of its url; it is defined with the internals of a request. */
struct HttpRequestParam;

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
	struct HttpRequestParam *params, HttpRouteMatch *match
);

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
