/**
 * @file cerver/http/core/context.h
 * @brief The context of a request: what a handler receives, the views of its values and the types of the handlers.
 */

#ifndef _CERVER_HTTP_CORE_CONTEXT_H_
#define _CERVER_HTTP_CORE_CONTEXT_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _HttpCerver;
struct _HttpEndpoint;
struct _HttpGroup;
struct _HttpReceive;

/**
 * @defgroup http_context Context
 * @ingroup http_core
 * @brief The context of a request and the views of its values.
 *
 * A handler receives one context: the functions of cerver/http/requests/request.h read the request from it and
 * the ones of cerver/http/responses/response.h write its response to it. The context belongs to cerver: it is
 * valid until the request ends, and what a handler takes from it (a view, the body, the JSON) ends with it.
 * @{
 */

/**
 * @brief The HTTP of a web cerver: its routes and its settings, see cerver/http/core/http.h.
 *
 * @since 3.0b-12 it replaces the old typedef of struct _HttpCerver, whose fields are not public anymore.
 */
typedef struct _HttpCerver CerverHttp;

/**
 * @brief The context of a request, the only argument of a handler.
 *
 * @since 3.0b-12 it replaces the HttpReceive and the HttpRequest that a handler received.
 */
typedef struct _HttpReceive CerverHttpContext;

/**
 * @brief An endpoint: the handler of a method in a path, see cerver/http/routes/tree.h.
 *
 * @since 3.0b-12 it replaces the old typedef of struct _HttpEndpoint, whose fields are not public anymore.
 */
typedef struct _HttpEndpoint CerverHttpEndpoint;

/**
 * @brief A group of endpoints that share a prefix and their settings, see cerver/http/routes/tree.h.
 *
 * @since 3.0b-12 it replaces the old typedef of struct _HttpGroup, whose fields are not public anymore.
 */
typedef struct _HttpGroup CerverHttpGroup;

/**
 * @brief A value of a request: its bytes and how many they are.
 *
 * The bytes belong to the request and they are valid until it ends; they always end with a '\0' that the length
 * does not count, so a view can be used as a C string. A value that the request does not have is an empty view,
 * with NULL data.
 *
 * @since 3.0b-12
 */
typedef struct CerverHttpView {

	/** @brief The bytes, that end with a '\0'; NULL for a value that the request does not have. */
	const char *data;

	/** @brief The bytes of #data, without its end. */
	size_t length;

} CerverHttpView;

/**
 * @brief Returns whether a view has no value.
 *
 * @param[in] view the view
 * @retval true the request does not have the value (its data is NULL)
 * @retval false it has it, even if it has no bytes
 * @since 3.0b-12
 */
CERVER_EXPORT bool cerver_http_view_is_missing (
	const CerverHttpView view
);

/**
 * @brief Compares a view with a string.
 *
 * @param[in] view the view
 * @param[in] string the string, that ends with '\0'
 * @retval true they have the same bytes
 * @retval false they differ, the view has no value or the string is NULL
 * @since 3.0b-12
 */
CERVER_EXPORT bool cerver_http_view_equals (
	const CerverHttpView view, const char *string
);

/**
 * @brief The handler of a request: it reads the request from its context and writes its response to it.
 *
 * By default it runs in the event loop that read the request, so it must not wait: an endpoint or a group whose
 * handlers wait (they read a database, they call a service) chooses a pool of threads, see cerver/http/core/pool.h.
 *
 * @param[in,out] context the context of the request, valid until the request ends
 * @since 3.0b-12 it replaces HttpHandler, that took a HttpReceive and a HttpRequest.
 */
typedef void (*CerverHttpHandler)(
	CerverHttpContext *context
);

/**
 * @brief The authentication handler of an endpoint with HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] context the context of the request
 * @return 0 when the request is authenticated; any other value rejects it, with the response that the handler
 * wrote or with a 401 when it wrote none
 * @since 3.0b-12 it replaces HttpAuthenticationHandler, that took a HttpReceive and a HttpRequest.
 */
typedef unsigned int (*CerverHttpAuthenticationHandler)(
	CerverHttpContext *context
);

/**
 * @brief Returns the cerver of a request.
 *
 * @param[in] context the context of the request
 * @return the cerver, NULL for a NULL context
 * @since 3.0b-12
 */
CERVER_EXPORT struct _Cerver *cerver_http_context_cerver (
	const CerverHttpContext *context
);

/**
 * @brief Returns the HTTP of the cerver of a request.
 *
 * @param[in] context the context of the request
 * @return the HTTP of its cerver, NULL for a NULL context
 * @since 3.0b-12 it replaces http_receive_get_cerver ().
 */
CERVER_EXPORT CerverHttp *cerver_http_context_http (
	const CerverHttpContext *context
);

/**
 * @brief Returns the endpoint that handles a request.
 *
 * @param[in] context the context of the request
 * @return the endpoint; NULL for a request that no endpoint handles (the catch all, a static file) and for a NULL
 * context
 * @since 3.0b-12 it replaces http_receive_get_endpoint ().
 */
CERVER_EXPORT const CerverHttpEndpoint *cerver_http_context_endpoint (
	const CerverHttpContext *context
);

/**
 * @brief Returns the custom data of the endpoint that handles a request: its own or the one that it inherited
 * from its groups.
 *
 * @param[in] context the context of the request
 * @return the custom data, NULL when there is none
 * @since 3.0b-12 it replaces http_receive_get_route_custom_data ().
 */
CERVER_EXPORT const void *cerver_http_context_endpoint_custom_data (
	const CerverHttpContext *context
);

/**
 * @brief Returns the custom data of the HTTP of the cerver of a request.
 *
 * @param[in] context the context of the request
 * @return the custom data, NULL when there is none
 * @since 3.0b-12 it replaces http_receive_get_cerver_custom_data ().
 */
CERVER_EXPORT const void *cerver_http_context_http_custom_data (
	const CerverHttpContext *context
);

/**
 * @brief Returns the methods that the path of a request handles, as the value of an Allow header.
 *
 * @param[in] context the context of the request
 * @return the methods separated with ", ", like "GET, HEAD, OPTIONS"; NULL when the path matched no route
 * @since 3.0b-12 it replaces http_receive_get_allow ().
 */
CERVER_EXPORT const char *cerver_http_context_allow (
	const CerverHttpContext *context
);

/**
 * @brief Returns whether the client of a request left: it closed its connection or it stopped answering.
 *
 * It asks the socket, so a handler that works for a long time can stop when nobody waits for its response. A
 * client whose response can not be written anymore is gone too: a send failed, it did not read in time, or its
 * deferred response was not completed before the request timeout.
 *
 * @param[in] context the context of the request
 * @retval true the client is gone
 * @retval false it is still there, or the context is NULL
 * @since 3.0b-12 it replaces http_receive_is_client_gone ().
 */
CERVER_EXPORT bool cerver_http_context_client_is_gone (
	const CerverHttpContext *context
);

/**
 * @brief Returns the descriptor of the socket of the connection of a request.
 *
 * It identifies the connection while the request lasts: no other open connection of the process has it, so it can
 * be part of a name that must not repeat between the requests that are being handled (the folder of an upload).
 *
 * @param[in] context the context of the request
 * @return the descriptor, -1 for a NULL context
 * @warning Only the event loop of the connection reads, writes and closes the socket: a handler must not use the
 * descriptor for anything else than telling its connection apart, and its number is taken by another connection
 * once this one is closed.
 * @since 3.0b-12 it replaces http_receive_get_sock_fd ().
 */
CERVER_EXPORT int cerver_http_context_socket (
	const CerverHttpContext *context
);

/** @} */

/**
 * @defgroup http_context_deferred Deferred
 * @ingroup http_context
 * @brief A handler that answers later, without keeping a thread while it waits.
 *
 * A handler that needs something that takes time (another service, a job of its own, a timer) defers the response
 * of its request and returns: its thread, or its event loop, goes on with other requests. When the response is
 * ready, any thread writes it to the context with the methods of the response and completes the request. The
 * context, the request and its views stay valid from the deferral until the request is completed.
 *
 * @code
 * void handler (CerverHttpContext *context) {
 *     if (!cerver_http_context_defer (context)) work_start (context);    // returns at once
 * }
 *
 * void work_done (CerverHttpContext *context, const char *json, size_t length) {    // any thread, later
 *     (void) cerver_http_response_send_json (context, json, length);
 *     (void) cerver_http_context_complete (context);
 * }
 * @endcode
 * @{
 */

/**
 * @brief Defers the response of a request: its handler returns without answering it, and the request waits until
 * cerver_http_context_complete ().
 *
 * What follows a handler (the after middlewares of its endpoint, the end of a stream that it left open, the 500 of
 * a request that was not answered) happens when the request is completed, in the thread that completes it.
 *
 * @param[in,out] context the context of the request, in its handler
 * @retval 0 the response is deferred
 * @retval 1 a NULL context, a call that is not made from the handler of the request, or a response that was
 * already deferred
 * @warning **A deferred request must be completed, once:** its connection and its memory are kept until then, also
 * when its client left, and a cerver that stops waits for it (its drain timeout).
 * @warning A request that is not completed before its request timeout (cerver_http_set_request_timeout ()) loses
 * its client: its connection is shut down and what is written of its response after that fails. It still has to be
 * completed. cerver_http_context_client_is_gone () tells it.
 * @threadsafety The thread that runs the handler of the request.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_context_defer (CerverHttpContext *context);

/**
 * @brief Completes a request whose response was deferred: what was written of its response is sent, the request
 * ends, and its connection is kept for the next one or closed.
 *
 * The response is written before this call, with the methods of the response and the same context. After it the
 * context must not be used: it is of another request.
 *
 * @param[in,out] context the context of the deferred request
 * @retval 0 the request was completed; when its handler has not returned yet, it ends when it does
 * @retval 1 a NULL context, or a request whose response is not deferred or that was already completed; also a
 * request that waits for the verdict of its authentication (cerver_http_context_authentication_complete ())
 * @threadsafety Any thread, also the handler of the request before it returns; one call for each deferred response.
 * A response is written by one thread at a time.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_context_complete (CerverHttpContext *context);

/** @} */

/**
 * @defgroup http_context_authentication Deferred authentication
 * @ingroup http_context
 * @brief An authentication handler that asks someone else: it defers its verdict and returns, and its request
 * waits without a thread until the verdict is given.
 *
 * The authentication handler of an endpoint (#CerverHttpAuthenticationHandler) calls
 * cerver_http_context_authentication_defer () and returns: what it returns is not read. The request waits, without
 * a thread of a pool and without stopping its event loop, until any thread calls
 * cerver_http_context_authentication_complete () with the verdict. Then the request goes on where it would have
 * (its event loop, or a thread of the pool of its endpoint): its body is read and its handler runs, or it is
 * answered with the response that was written for it, or with a 401 when none was.
 *
 * cerver_http_context_authentication_request () (cerver/http/clients/client.h) does it with a request of the
 * HTTP client of cerver: the response of another service decides.
 * @{
 */

/**
 * @brief Defers the verdict of the authentication of a request.
 *
 * @param[in,out] context the context of the request, in its authentication handler
 * @retval 0 the verdict is deferred: what the authentication handler returns is not read
 * @retval 1 a NULL context, a call that is not made from the authentication handler of the request, or a verdict
 * that was already deferred
 * @warning **A deferred verdict must be given, once:** the connection and the memory of the request are kept until
 * then, also when its client left, and a cerver that stops waits for it (its drain timeout).
 * @warning A request whose verdict is not given before its request timeout (cerver_http_set_request_timeout ())
 * loses its client: its connection is shut down, and the request is rejected when its verdict is given, whatever
 * it is.
 * @threadsafety The thread that runs the authentication handler of the request.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_context_authentication_defer (CerverHttpContext *context);

/**
 * @brief Gives the verdict of the authentication of a request that deferred it.
 *
 * A response for a request that is rejected is written before this call, with the methods of the response and
 * the same context; without one the request is answered with a 401. After the call the context must not be
 * used.
 *
 * @param[in,out] context the context of the request
 * @param[in] authenticated true when the request is authenticated, false to reject it
 * @retval 0 the verdict was given
 * @retval 1 a NULL context, or a request whose verdict is not deferred or was already given
 * @threadsafety Any thread, also the authentication handler of the request before it returns; one call for each
 * deferred verdict.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_context_authentication_complete (
	CerverHttpContext *context, const bool authenticated
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
