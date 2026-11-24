/**
 * @file cerver/http/security/remote.h
 * @brief The request that a custom authentication sends to another service by HTTP, that waits in the event loop
 * of its connection without a thread.
 */

#ifndef _CERVER_HTTP_REMOTE_H_
#define _CERVER_HTTP_REMOTE_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/requests/method.h"
#include "cerver/http/requests/request.h"

/**
 * @defgroup http_remote Remote authentication
 * @ingroup http_security
 * @brief A custom authentication that asks another service by HTTP, with libcurl (a cerver built with `CURL=1`).
 *
 * The authentication handler of a route with HTTP_ROUTE_AUTH_TYPE_CUSTOM calls http_auth_request () and returns what
 * it returns. With CERVER_HANDLER_TYPE_EVENTS the request waits in the loop of its connection, without a thread of
 * the pool, and its on_response () runs in a thread of the pool when the answer arrives; with threads, and in an
 * authentication that needs the body of its request, the request is sent and answered in the thread of the
 * authentication.
 *
 * @since 2.1b-24
 * @{
 */

/**
 * @brief What an authentication handler returns while its request waits in the event loop: the value that
 * http_auth_request () returns then.
 */
#define HTTP_AUTHENTICATION_PENDING						2

/** @brief The default milliseconds that a request waits for its answer: 2^12. */
#define HTTP_AUTH_REQUEST_DEFAULT_TIMEOUT_MILLISECONDS	4096

/** @brief The most bytes of the body of an answer: 2^16, a longer one fails its request. */
#define HTTP_AUTH_RESPONSE_MAX_BODY_SIZE				65536

/** @brief The most headers of a request. */
#define HTTP_AUTH_REQUEST_MAX_HEADERS					16

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;

/**
 * @brief The request to the service; http_auth_request () copies it, so it can live in the stack of the
 * authentication handler.
 */
typedef struct HttpAuthRequest {

	/** @brief Its method. */
	HttpRequestMethod method;

	/** @brief Its URL, with http:// or https:// (libcurl verifies the certificate of the service). */
	const char *url;

	/** @brief Its headers, each one as "Name: value"; NULL without them. */
	const char *const *headers;
	/** @brief How many headers, up to #HTTP_AUTH_REQUEST_MAX_HEADERS. */
	size_t n_headers;

	/** @brief Its body, NULL without one. */
	const char *body;
	/** @brief The bytes of its body. */
	size_t body_size;

	/** @brief The milliseconds that it waits for its answer, 0 for #HTTP_AUTH_REQUEST_DEFAULT_TIMEOUT_MILLISECONDS. */
	unsigned int timeout_milliseconds;

} HttpAuthRequest;

/**
 * @brief The answer of the service, that only lives while on_response () runs.
 */
typedef struct HttpAuthResponse {

	/**
	 * @brief Its status; 0 when the request failed: no answer in time, a service that is not there, a body longer
	 * than #HTTP_AUTH_RESPONSE_MAX_BODY_SIZE, or a loop that stops.
	 */
	unsigned int status;

	/** @brief Its body with a terminator, empty without one. */
	const char *body;
	/** @brief The bytes of its body. */
	size_t body_size;

} HttpAuthResponse;

/**
 * @brief What decides with the answer of the service: it returns 0 when the request is authenticated and 1 when it
 * is not (answered with 401, like a custom authentication that fails).
 *
 * It runs in the thread of the authentication with threads, and in a thread of the pool of the endpoint with an
 * event loop; it can use the request, like the authentication handler.
 */
typedef unsigned int (*HttpAuthResponseHandler)(
	const struct _HttpReceive *http_receive,
	const HttpRequest *request,
	const HttpAuthResponse *response,
	void *data
);

/**
 * @brief Sends a request to another service from the authentication handler of a route, and decides with its answer.
 *
 * @param[in] http_receive the receive of the request that is authenticated
 * @param[in] request the request to the service, copied
 * @param[in] on_response what decides with the answer
 * @param[in] data the data of on_response (); it must live until on_response () runs
 * @retval HTTP_AUTHENTICATION_PENDING the request waits in the event loop: the authentication handler returns it
 * @retval 0 the answer authenticated the request (threads, or an authentication with the body)
 * @retval 1 it did not, or a NULL argument, or no memory, or a cerver built without `CURL=1`
 * @warning Only an authentication handler calls it; with an event loop, the handler returns at once with its value.
 * @since 2.1b-24
 */
CERVER_EXPORT unsigned int http_auth_request (
	const struct _HttpReceive *http_receive, const HttpAuthRequest *request,
	HttpAuthResponseHandler on_response, void *data
);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
