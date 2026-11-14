#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/http/routes/middleware.h"

const char *http_middleware_result_to_string (
	const HttpMiddlewareResult result
) {

	switch (result) {
		#define XX(num, name, string, description) case HTTP_MIDDLEWARE_RESULT_##name: return #string;
		HTTP_MIDDLEWARE_RESULT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_middleware_result_to_string (HTTP_MIDDLEWARE_RESULT_NONE);

}

const char *http_middleware_result_description (
	const HttpMiddlewareResult result
) {

	switch (result) {
		#define XX(num, name, string, description) case HTTP_MIDDLEWARE_RESULT_##name: return #description;
		HTTP_MIDDLEWARE_RESULT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_middleware_result_description (HTTP_MIDDLEWARE_RESULT_NONE);

}

void http_middlewares_init (HttpMiddlewares *middlewares) {

	middlewares->n_before = 0;
	middlewares->before = NULL;

	middlewares->n_after = 0;
	middlewares->after = NULL;

}

void http_middlewares_end (HttpMiddlewares *middlewares) {

	free (middlewares->before);
	free (middlewares->after);

	http_middlewares_init (middlewares);

}

// returns 0 on success, 1 on error
u8 http_middlewares_add_before (
	HttpMiddlewares *middlewares, const HttpBeforeMiddleware middleware
) {

	u8 retval = 1;

	if (middleware) {
		HttpBeforeMiddleware *before = (HttpBeforeMiddleware *) realloc (
			middlewares->before, (middlewares->n_before + 1) * sizeof (HttpBeforeMiddleware)
		);

		if (before) {
			before[middlewares->n_before] = middleware;
			middlewares->before = before;
			middlewares->n_before += 1;

			retval = 0;
		}
	}

	return retval;

}

// returns 0 on success, 1 on error
u8 http_middlewares_add_after (
	HttpMiddlewares *middlewares, const HttpAfterMiddleware middleware
) {

	u8 retval = 1;

	if (middleware) {
		HttpAfterMiddleware *after = (HttpAfterMiddleware *) realloc (
			middlewares->after, (middlewares->n_after + 1) * sizeof (HttpAfterMiddleware)
		);

		if (after) {
			after[middlewares->n_after] = middleware;
			middlewares->after = after;
			middlewares->n_after += 1;

			retval = 0;
		}
	}

	return retval;

}

// adds the middlewares of source at the end of destination
// returns 0 on success, 1 on error
u8 http_middlewares_append (
	HttpMiddlewares *destination, const HttpMiddlewares *source
) {

	u8 errors = 0;

	for (size_t idx = 0; idx < source->n_before; idx++) {
		errors |= http_middlewares_add_before (destination, source->before[idx]);
	}

	for (size_t idx = 0; idx < source->n_after; idx++) {
		errors |= http_middlewares_add_after (destination, source->after[idx]);
	}

	return errors;

}

// runs the before middlewares in order
// returns true if the request continues to the route handler
bool http_middlewares_run_before (
	const HttpMiddlewares *middlewares,
	const struct _HttpReceive *http_receive, const HttpRequest *request
) {

	bool next = true;

	for (size_t idx = 0; next && (idx < middlewares->n_before); idx++) {
		next = middlewares->before[idx] (http_receive, request) != HTTP_MIDDLEWARE_RESULT_STOP;
	}

	return next;

}

// runs the after middlewares in reverse order
void http_middlewares_run_after (
	const HttpMiddlewares *middlewares,
	const struct _HttpReceive *http_receive, const HttpRequest *request
) {

	for (size_t idx = middlewares->n_after; idx > 0; idx--) {
		middlewares->after[idx - 1] (http_receive, request);
	}

}
