#ifndef _CERVER_HTTP_MIDDLEWARE_H_
#define _CERVER_HTTP_MIDDLEWARE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/requests/request.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

#define HTTP_MIDDLEWARE_RESULT_MAP(XX)																	\
	XX(0,	NONE, 			None,			Undefined)													\
	XX(1,	CONTINUE, 		Continue,		The request continues with the next middleware)				\
	XX(2,	STOP, 			Stop,			The middleware answered the request and it ends here)

typedef enum HttpMiddlewareResult {

	#define XX(num, name, string, description) HTTP_MIDDLEWARE_RESULT_##name = num,
	HTTP_MIDDLEWARE_RESULT_MAP (XX)
	#undef XX

	HTTP_MIDDLEWARE_RESULT_MAX

} HttpMiddlewareResult;

CERVER_PUBLIC const char *http_middleware_result_to_string (
	const HttpMiddlewareResult result
);

CERVER_PUBLIC const char *http_middleware_result_description (
	const HttpMiddlewareResult result
);

// runs after the request was authenticated and before the route handler,
// a middleware that returns HTTP_MIDDLEWARE_RESULT_STOP must send the response,
// any other value lets the request continue
typedef HttpMiddlewareResult (*HttpBeforeMiddleware)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// runs after the route handler, the response was already sent
typedef void (*HttpAfterMiddleware)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
);

// the middlewares of a cerver, a group or an endpoint
// the ones of an endpoint are compiled when the cerver starts:
// the cerver's, then its groups' from the outermost, and then its own
// the after middlewares run in the opposite order
struct _HttpMiddlewares {

	size_t n_before;
	HttpBeforeMiddleware *before;

	size_t n_after;
	HttpAfterMiddleware *after;

};

typedef struct _HttpMiddlewares HttpMiddlewares;

CERVER_PRIVATE void http_middlewares_init (HttpMiddlewares *middlewares);

CERVER_PRIVATE void http_middlewares_end (HttpMiddlewares *middlewares);

// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_middlewares_add_before (
	HttpMiddlewares *middlewares, const HttpBeforeMiddleware middleware
);

// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_middlewares_add_after (
	HttpMiddlewares *middlewares, const HttpAfterMiddleware middleware
);

// adds the middlewares of source at the end of destination
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_middlewares_append (
	HttpMiddlewares *destination, const HttpMiddlewares *source
);

// runs the before middlewares in order
// returns true if the request continues to the route handler
CERVER_PRIVATE bool http_middlewares_run_before (
	const HttpMiddlewares *middlewares,
	const struct _HttpReceive *http_receive, const HttpRequest *request
);

// runs the after middlewares in reverse order
CERVER_PRIVATE void http_middlewares_run_after (
	const HttpMiddlewares *middlewares,
	const struct _HttpReceive *http_receive, const HttpRequest *request
);

#ifdef __cplusplus
}
#endif

#endif
