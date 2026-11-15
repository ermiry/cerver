#ifndef _CERVER_HTTP_CORE_ROUTES_H_
#define _CERVER_HTTP_CORE_ROUTES_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif
