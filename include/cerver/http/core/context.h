/**
 * @file cerver/http/core/context.h
 * @brief The context of a request: what a handler receives, the views of its values and the types of the handlers.
 */

#ifndef _CERVER_HTTP_CORE_CONTEXT_H_
#define _CERVER_HTTP_CORE_CONTEXT_H_

#include <stdbool.h>
#include <stddef.h>

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
 * It asks the socket, so a handler that works for a long time can stop when nobody waits for its response.
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

#ifdef __cplusplus
}
#endif

#endif
