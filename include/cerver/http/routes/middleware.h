/**
 * @file cerver/http/routes/middleware.h
 * @brief The middlewares that run before and after the handler of a route.
 */

#ifndef _CERVER_HTTP_MIDDLEWARE_H_
#define _CERVER_HTTP_MIDDLEWARE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/core/context.h"
#include "cerver/http/requests/request.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

/**
 * @defgroup http_routes_middleware Middlewares
 * @ingroup http_routes
 * @brief The middlewares of a cerver, a group of routes or an endpoint.
 * @{
 */

/**
 * @brief The X-macro table of the results of a before middleware: the number, the name, the string and the
 * description of each one.
 */
#define HTTP_MIDDLEWARE_RESULT_MAP(XX)																	\
	XX(0,	NONE, 			None,			Undefined)													\
	XX(1,	CONTINUE, 		Continue,		The request continues with the next middleware)				\
	XX(2,	STOP, 			Stop,			The middleware answered the request and it ends here)

/**
 * @brief The result of a before middleware, one `HTTP_MIDDLEWARE_RESULT_<name>` for each row of
 * #HTTP_MIDDLEWARE_RESULT_MAP.
 */
typedef enum CerverHttpMiddlewareResult {

	#define XX(num, name, string, description) HTTP_MIDDLEWARE_RESULT_##name = num,
	HTTP_MIDDLEWARE_RESULT_MAP (XX)
	#undef XX

	/** @brief The number of results, the first value that is not valid. */
	HTTP_MIDDLEWARE_RESULT_MAX

} CerverHttpMiddlewareResult;

/**
 * @brief Returns the string of a result.
 *
 * @param[in] result the result
 * @return the string of its row in #HTTP_MIDDLEWARE_RESULT_MAP, or the one of HTTP_MIDDLEWARE_RESULT_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_middleware_result_to_string (
	const CerverHttpMiddlewareResult result
);

/**
 * @brief Returns the description of a result.
 *
 * @param[in] result the result
 * @return the description of its row in #HTTP_MIDDLEWARE_RESULT_MAP, or the one of HTTP_MIDDLEWARE_RESULT_NONE for
 * a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_middleware_result_description (
	const CerverHttpMiddlewareResult result
);

/**
 * @brief A middleware that runs after the request was authenticated and before the handler of the route.
 *
 * A middleware that returns HTTP_MIDDLEWARE_RESULT_STOP must send the response; any other value lets the request
 * continue.
 */
typedef CerverHttpMiddlewareResult (*HttpBeforeMiddleware)(
	CerverHttpContext *context
);

/**
 * @brief A middleware that runs after the handler of the route, when the response was already sent.
 */
typedef void (*HttpAfterMiddleware)(
	CerverHttpContext *context
);

/**
 * @brief The middlewares of a cerver, a group or an endpoint.
 *
 * The ones of an endpoint are compiled when the cerver starts: the cerver's, then its groups' from the outermost,
 * and then its own; the after middlewares run in the opposite order.
 */
struct _HttpMiddlewares {

	/** @brief The number of before middlewares. */
	size_t n_before;
	/** @brief The before middlewares, in the order they run. */
	HttpBeforeMiddleware *before;

	/** @brief The number of after middlewares. */
	size_t n_after;
	/** @brief The after middlewares, in the order they were added (they run in reverse). */
	HttpAfterMiddleware *after;

};

/** @brief The middlewares of a cerver, a group or an endpoint, see struct _HttpMiddlewares. */
typedef struct _HttpMiddlewares HttpMiddlewares;

/**
 * @brief Initializes empty middlewares.
 *
 * @param[out] middlewares the middlewares, they must not be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_middlewares_init (HttpMiddlewares *middlewares);

/**
 * @brief Releases the arrays of the middlewares and leaves them empty.
 *
 * @param[in,out] middlewares the middlewares, they must not be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_middlewares_end (HttpMiddlewares *middlewares);

/**
 * @brief Adds a before middleware at the end.
 *
 * @param[in,out] middlewares the middlewares, they must not be NULL
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL middleware or the array could not grow
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_middlewares_add_before (
	HttpMiddlewares *middlewares, const HttpBeforeMiddleware middleware
);

/**
 * @brief Adds an after middleware at the end.
 *
 * @param[in,out] middlewares the middlewares, they must not be NULL
 * @param[in] middleware the middleware
 * @retval 0 the middleware was added
 * @retval 1 a NULL middleware or the array could not grow
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_middlewares_add_after (
	HttpMiddlewares *middlewares, const HttpAfterMiddleware middleware
);

/**
 * @brief Adds the middlewares of a source at the end of a destination.
 *
 * @param[in,out] destination the middlewares that grow
 * @param[in] source the middlewares that are added
 * @retval 0 every middleware was added
 * @retval 1 a middleware could not be added
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_middlewares_append (
	HttpMiddlewares *destination, const HttpMiddlewares *source
);

/**
 * @brief Runs the before middlewares in order, until one of them returns HTTP_MIDDLEWARE_RESULT_STOP.
 *
 * @param[in] middlewares the middlewares
 * @param[in,out] http_receive the receive of the request
 * @return true if the request continues to the handler of the route
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool http_middlewares_run_before (
	const HttpMiddlewares *middlewares, struct _HttpReceive *http_receive
);

/**
 * @brief Runs the after middlewares in reverse order.
 *
 * @param[in] middlewares the middlewares
 * @param[in,out] http_receive the receive of the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_middlewares_run_after (
	const HttpMiddlewares *middlewares, struct _HttpReceive *http_receive
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
