#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/routes.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region routes

// sets the HTTP cerver's main route (top level route)
// used to enable admin routes
// if no route has been set, the first top level route
// will be used
void http_cerver_set_main_route (
	HttpCerver *http_cerver, const HttpRoute *route
) {

	if (http_cerver) {
		http_cerver->main_route = route;
	}

}

void http_cerver_route_register (
	HttpCerver *http_cerver, HttpRoute *route
) {

	if (http_cerver && route) {
		(void) dlist_insert_at_end (http_cerver->routes, route);
	}

}

void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver,
	void (*catch_all_route)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver && catch_all_route) {
		http_cerver->default_handler = catch_all_route;
	}

}

// enables the use of the default not found handler
// which returns 404 status on no matching routes
// by default this option is disabled
// as the catch all handler is used instead
void http_cerver_set_not_found_handler (
	HttpCerver *http_cerver
) {

	if (http_cerver)
		http_cerver->not_found_handler = true;

}

// sets a custom handler to be executed on no matching routes
// it should return status 404
void http_cerver_set_not_found_route (
	HttpCerver *http_cerver,
	void (*not_found)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver && not_found) {
		http_cerver->not_found = not_found;
		http_cerver->not_found_handler = true;
	}

}

void http_cerver_set_method_not_allowed (
	HttpCerver *http_cerver, const bool method_not_allowed
) {

	if (http_cerver) {
		http_cerver->tree->method_not_allowed = method_not_allowed;
	}

}

void http_cerver_set_method_not_allowed_handler (
	HttpCerver *http_cerver, const HttpHandler handler
) {

	if (http_cerver) {
		http_cerver->tree->method_not_allowed_handler = handler;
	}

}

void http_cerver_set_trailing_slash (
	HttpCerver *http_cerver, const HttpTrailingSlash trailing_slash
) {

	if (
		http_cerver
		&& (trailing_slash > HTTP_TRAILING_SLASH_NONE)
		&& (trailing_slash < HTTP_TRAILING_SLASH_MAX)
	) {
		http_cerver->tree->trailing_slash = trailing_slash;
	}

}

void http_cerver_set_max_route_params (
	HttpCerver *http_cerver, const unsigned int max_params
) {

	if (http_cerver && max_params) {
		http_cerver->tree->max_params = max_params;
	}

}

u8 http_cerver_register_param_validator (
	HttpCerver *http_cerver, const char *name, const HttpParamValidate validate
) {

	u8 retval = 1;

	if (http_cerver) {
		HttpParamValidator *validator = http_param_validator_create (name, validate);
		if (validator) {
			retval = (u8) dlist_insert_at_end_unsafe (http_cerver->tree->validators, validator);
		}
	}

	return retval;

}

u8 http_cerver_add_before (
	HttpCerver *http_cerver, const HttpBeforeMiddleware middleware
) {

	return http_cerver ? http_middlewares_add_before (&http_cerver->tree->middlewares, middleware) : 1;

}

u8 http_cerver_add_after (
	HttpCerver *http_cerver, const HttpAfterMiddleware middleware
) {

	return http_cerver ? http_middlewares_add_after (&http_cerver->tree->middlewares, middleware) : 1;

}

const char *http_cerver_routes_error (
	const HttpCerver *http_cerver
) {

	return (http_cerver && http_cerver->tree->n_errors) ? http_cerver->tree->error : NULL;

}

size_t http_cerver_routes_errors_count (
	const HttpCerver *http_cerver
) {

	return http_cerver ? http_cerver->tree->n_errors : 0;

}

u8 http_cerver_routes_compile (
	HttpCerver *http_cerver
) {

	u8 retval = 1;

	if (http_cerver) {
		http_cerver_init_admin_routes (http_cerver);

		retval = http_tree_compile (http_cerver->tree);
	}

	return retval;

}

void http_cerver_routes_print (
	const HttpCerver *http_cerver
) {

	if (http_cerver) http_tree_print (http_cerver->tree);

}

char *http_cerver_routes_generate_json (
	const HttpCerver *http_cerver
) {

	return http_cerver ? http_tree_generate_json (http_cerver->tree) : NULL;

}

void http_cerver_routes_json_delete (char *json) {

	free (json);

}

#pragma endregion
