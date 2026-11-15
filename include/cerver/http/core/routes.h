/**
 * @file cerver/http/core/routes.h
 * @brief The routes of an HttpCerver: how they are registered, matched, compiled and reported.
 */

#ifndef _CERVER_HTTP_CORE_ROUTES_H_
#define _CERVER_HTTP_CORE_ROUTES_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_routes Routes
 * @ingroup http_core
 * @brief The routes of the first routes API, the routes tree that matches them and its settings.
 *
 * The setters ignore a NULL HttpCerver.
 * @{
 */

/**
 * @brief Sets the main route (a top level route), the parent of the admin routes.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] route the route, NULL so the first top level route is used
 */
CERVER_EXPORT void http_cerver_set_main_route (
	HttpCerver *http_cerver, const HttpRoute *route
);

/**
 * @brief Registers a top level route of the first routes API.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] route the route, the HttpCerver owns it from now on; NULL is ignored
 */
CERVER_EXPORT void http_cerver_route_register (
	HttpCerver *http_cerver, HttpRoute *route
);

/**
 * @brief Sets the handler of the requests that do not match any route.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] catch_all_route the handler, NULL keeps the current one
 */
CERVER_EXPORT void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver,
	void (*catch_all_route)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

/**
 * @brief Enables the default not found handler, that answers the requests that do not match any route with 404.
 *
 * By default it is disabled and the catch all handler is used instead.
 *
 * @param[in,out] http_cerver the HttpCerver
 */
CERVER_EXPORT void http_cerver_set_not_found_handler (
	HttpCerver *http_cerver
);

/**
 * @brief Sets and enables a custom handler of the requests that do not match any route; it should answer with 404.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] not_found the handler, NULL keeps the current one
 */
CERVER_EXPORT void http_cerver_set_not_found_route (
	HttpCerver *http_cerver,
	void (*not_found)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

/**
 * @brief Answers a request whose path exists but has no handler for its method with 405 and the Allow header,
 * instead of the catch all handler.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] method_not_allowed true to answer with 405, the default is false
 */
CERVER_EXPORT void http_cerver_set_method_not_allowed (
	HttpCerver *http_cerver, const bool method_not_allowed
);

/**
 * @brief Sets the handler that sends the 405 response, with any body.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] handler the handler, it gets the value of the Allow header with http_receive_get_allow ()
 */
CERVER_EXPORT void http_cerver_set_method_not_allowed_handler (
	HttpCerver *http_cerver, const HttpHandler handler
);

/**
 * @brief Sets how a path with a trailing slash is matched.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] trailing_slash the policy, a value out of the table keeps the current one; the default is
 * HTTP_TRAILING_SLASH_TOLERANT
 */
CERVER_EXPORT void http_cerver_set_trailing_slash (
	HttpCerver *http_cerver, const HttpTrailingSlash trailing_slash
);

/**
 * @brief Sets the most params of a route; a route with more is a registration error.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_params the params, 0 keeps the current value; the default is HTTP_TREE_DEFAULT_MAX_PARAMS
 */
CERVER_EXPORT void http_cerver_set_max_route_params (
	HttpCerver *http_cerver, const unsigned int max_params
);

/**
 * @brief Registers a validator that a route param uses with ":name<validator>".
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] name the name of the validator, copied; shorter than HTTP_PARAM_VALIDATOR_NAME_SIZE
 * @param[in] validate the method that validates a param
 * @retval 0 the validator was registered
 * @retval 1 a NULL argument, a name that is too long, or it could not be allocated
 */
CERVER_EXPORT u8 http_cerver_register_param_validator (
	HttpCerver *http_cerver, const char *name, const HttpParamValidate validate
);

/**
 * @brief Adds a middleware that runs before the handler of every endpoint.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL HttpCerver or it could not be added
 */
CERVER_EXPORT u8 http_cerver_add_before (
	HttpCerver *http_cerver, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds a middleware that runs after the handler of every endpoint.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL HttpCerver or it could not be added
 */
CERVER_EXPORT u8 http_cerver_add_after (
	HttpCerver *http_cerver, const HttpAfterMiddleware middleware
);

/**
 * @brief Returns the first error of the registration of the routes.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the message, that belongs to the HttpCerver; NULL if there is none
 */
CERVER_EXPORT const char *http_cerver_routes_error (
	const HttpCerver *http_cerver
);

/**
 * @brief Returns the number of errors of the registration of the routes.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the errors, 0 for a NULL HttpCerver
 */
CERVER_EXPORT size_t http_cerver_routes_errors_count (
	const HttpCerver *http_cerver
);

/**
 * @brief Builds the routes tree with the admin routes; the cerver does it when it starts.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @retval 0 the routes were compiled
 * @retval 1 a NULL HttpCerver or a registration error
 */
CERVER_EXPORT u8 http_cerver_routes_compile (
	HttpCerver *http_cerver
);

/**
 * @brief Prints the compiled routes tree.
 *
 * @param[in] http_cerver the HttpCerver, NULL prints nothing
 */
CERVER_EXPORT void http_cerver_routes_print (
	const HttpCerver *http_cerver
);

/**
 * @brief Returns the compiled endpoints as a json string.
 *
 * The json is `{"n_endpoints": N, "endpoints": [{"method", "path", "auth", "modifier", "compatibility", "cors",
 * "etag", "compression"}]}`.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the json, deleted with http_cerver_routes_json_delete (); NULL if the routes are not compiled
 */
CERVER_EXPORT char *http_cerver_routes_generate_json (
	const HttpCerver *http_cerver
);

/**
 * @brief Releases the json of http_cerver_routes_generate_json ().
 *
 * @param[in] json the json, it can be NULL
 */
CERVER_EXPORT void http_cerver_routes_json_delete (char *json);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
