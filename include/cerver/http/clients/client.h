/**
 * @file cerver/http/clients/client.h
 * @brief The HTTP client of cerver: requests of HTTP/1.1 that the event loops send, inside and outside a cerver.
 */

#ifndef _CERVER_HTTP_CLIENTS_CLIENT_H_
#define _CERVER_HTTP_CLIENTS_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/events.h"

#include "cerver/http/core/context.h"
#include "cerver/http/core/http.h"
#include "cerver/http/requests/method.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_clients Client
 * @ingroup http
 * @brief An HTTP/1.1 client that the event loops drive: its requests wait in no thread.
 *
 * A client of a cerver (cerver_http_client_create ()) sends its requests through the event loops of that cerver:
 * a request that a handler of a loop sends is sent by that same loop, and its handler runs there. A client alone
 * (with a NULL cerver) has its own event loop and its own thread, for the programs that are not a cerver.
 *
 * A request is created, gets its headers and its body, and is sent once: with a handler that takes its response
 * (cerver_http_client_request_send ()), or by a thread that waits for it (cerver_http_client_request_perform ()).
 * The connections are kept for the next requests of the same destination, in each loop.
 *
 * It speaks `http://` only: TLS is not part of it yet.
 * @{
 */

/** @brief The milliseconds that a connection has to connect, by default. */
#define CERVER_HTTP_CLIENT_DEFAULT_CONNECT_TIMEOUT			5000

/** @brief The milliseconds that a request has until its response is complete, by default. */
#define CERVER_HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT			30000

/** @brief The milliseconds that a connection is kept without a request, by default. */
#define CERVER_HTTP_CLIENT_DEFAULT_IDLE_TIMEOUT				30000

/** @brief The connections that each event loop keeps for each destination, by default. */
#define CERVER_HTTP_CLIENT_DEFAULT_MAX_IDLE_CONNECTIONS		8

/** @brief The bytes that the body of a response has at most, by default. */
#define CERVER_HTTP_CLIENT_DEFAULT_MAX_RESPONSE_SIZE		(1 << 24)

/** @brief The port of a url without one. */
#define CERVER_HTTP_CLIENT_DEFAULT_PORT						80

/** @brief The scheme of the urls that the client takes. */
#define CERVER_HTTP_CLIENT_SCHEME							"http://"

/**
 * @brief The X-macro table of the errors of a request of a client: the number, the name, the string and the
 * description of each one.
 */
#define CERVER_HTTP_CLIENT_ERROR_MAP(XX)																			\
	XX(0,	NONE,			None,			The response is complete)												\
	XX(1,	RESOLVE,		Resolve,		The address of the host was not found)									\
	XX(2,	CONNECT,		Connect,		The connection to the host failed)										\
	XX(3,	SEND,			Send,			The request could not be sent)											\
	XX(4,	RECEIVE,		Receive,		The connection ended or failed before the response was complete)		\
	XX(5,	RESPONSE,		Response,		The response is not valid HTTP)											\
	XX(6,	SIZE,			Size,			The response is bigger than the limit of the client)					\
	XX(7,	TIMEOUT,		Timeout,		The response was not complete in time)									\
	XX(8,	STOPPED,		Stopped,		The event loop of the request ended before its response)				\
	XX(9,	MEMORY,			Memory,			The memory for the response could not be allocated)

/**
 * @brief The error of a request of a client, one `CERVER_HTTP_CLIENT_ERROR_<name>` for each row of
 * #CERVER_HTTP_CLIENT_ERROR_MAP.
 *
 * @since 3.0b-13
 */
typedef enum CerverHttpClientError {

	#define XX(num, name, string, description) CERVER_HTTP_CLIENT_ERROR_##name = num,
	CERVER_HTTP_CLIENT_ERROR_MAP (XX)
	#undef XX

	/** @brief The number of errors, the first value that is not valid. */
	CERVER_HTTP_CLIENT_ERROR_MAX

} CerverHttpClientError;

/**
 * @brief Returns the string of an error.
 *
 * @param[in] error the error
 * @return the string of its row in #CERVER_HTTP_CLIENT_ERROR_MAP, or the one of CERVER_HTTP_CLIENT_ERROR_NONE for
 * a value out of the table; a static string that must not be freed
 * @since 3.0b-13
 */
CERVER_EXPORT const char *cerver_http_client_error_to_string (
	const CerverHttpClientError error
);

/**
 * @brief Returns the description of an error.
 *
 * @param[in] error the error
 * @return the description of its row in #CERVER_HTTP_CLIENT_ERROR_MAP, or the one of
 * CERVER_HTTP_CLIENT_ERROR_NONE for a value out of the table; a static string that must not be freed
 * @since 3.0b-13
 */
CERVER_EXPORT const char *cerver_http_client_error_description (
	const CerverHttpClientError error
);

/** @brief An HTTP client; its fields are internal. */
typedef struct _CerverHttpClient CerverHttpClient;

/** @brief A request of an HTTP client; its fields are internal. */
typedef struct _CerverHttpClientRequest CerverHttpClientRequest;

/** @brief The response of a request of an HTTP client; its fields are internal. */
typedef struct _CerverHttpClientResponse CerverHttpClientResponse;

/**
 * @brief Takes the response of a request that was sent with cerver_http_client_request_send ().
 *
 * It is called once for each request, also for one that failed (cerver_http_client_response_error ()). The
 * response is valid only during the call.
 *
 * @param[in] response the response
 * @param[in] data what was passed with the request
 * @warning It runs in the thread of an event loop: it must not wait. It can send other requests.
 */
typedef void (*CerverHttpClientHandler) (
	const CerverHttpClientResponse *response, void *data
);

/**
 * @defgroup http_clients_client Clients
 * @ingroup http_clients
 * @brief A client, its event loops and its settings.
 * @{
 */

/**
 * @brief Creates a client.
 *
 * @param[in] http_cerver the HTTP cerver whose event loops send its requests; NULL for a client alone, that
 * creates its own event loop and its thread (with the backend of io_uring where it is available)
 * @return the client, NULL on error
 * @note The client of a cerver sends requests only while its cerver runs: from its handlers, or from any other
 * thread between the start and the end of the cerver.
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpClient *cerver_http_client_create (
	CerverHttp *http_cerver
);

/**
 * @brief Creates a client alone whose event loop uses a backend.
 *
 * @param[in] backend the backend of its event loop
 * @return the client, NULL on error: also when the backend is not available
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpClient *cerver_http_client_create_with_backend (
	const CerverEventsBackend backend
);

/**
 * @brief Deletes a client: it takes no more requests, the ones that were sent end, and its connections are
 * closed.
 *
 * The requests of a client alone that have not ended fail with CERVER_HTTP_CLIENT_ERROR_STOPPED, and their
 * handlers are called before this returns. The ones of the client of a cerver are waited for: each one ends with
 * its response, its error or its timeout.
 *
 * @param[in] client the client, it can be NULL
 * @warning It waits: it must not be called from a handler of the client, nor from a thread of an event loop.
 * @warning The client of a cerver is deleted before its cerver, or after its cerver ended.
 * @warning The client of a cerver whose requests do not end in the time of the client (its request timeout and
 * its connect timeout, and a few seconds more) is not released: its memory stays, with a warning in the log. A
 * request with a longer time of its own (cerver_http_client_request_set_timeout ()) is not waited for longer.
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_delete (
	CerverHttpClient *client
);

/**
 * @brief Sets the time that a connection of a client has to connect.
 *
 * @param[in,out] client the client, before its first request
 * @param[in] milliseconds the time; the default is #CERVER_HTTP_CLIENT_DEFAULT_CONNECT_TIMEOUT
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_set_connect_timeout (
	CerverHttpClient *client, const u32 milliseconds
);

/**
 * @brief Sets the time that a request of a client has, from the moment it is sent until its response is complete.
 *
 * @param[in,out] client the client, before its first request
 * @param[in] milliseconds the time, 0 for no limit; the default is #CERVER_HTTP_CLIENT_DEFAULT_REQUEST_TIMEOUT
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_set_request_timeout (
	CerverHttpClient *client, const u32 milliseconds
);

/**
 * @brief Sets the time that a connection of a client is kept without a request.
 *
 * @param[in,out] client the client, before its first request
 * @param[in] milliseconds the time; the default is #CERVER_HTTP_CLIENT_DEFAULT_IDLE_TIMEOUT
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_set_idle_timeout (
	CerverHttpClient *client, const u32 milliseconds
);

/**
 * @brief Sets the connections that each event loop of a client keeps for each destination (a host and a port).
 *
 * @param[in,out] client the client, before its first request
 * @param[in] connections the connections, 0 to close each one after its response; the default is
 * #CERVER_HTTP_CLIENT_DEFAULT_MAX_IDLE_CONNECTIONS
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_set_max_idle_connections (
	CerverHttpClient *client, const unsigned int connections
);

/**
 * @brief Sets the bytes that the body of a response of a client has at most: a request with a bigger one fails
 * with CERVER_HTTP_CLIENT_ERROR_SIZE.
 *
 * @param[in,out] client the client, before its first request
 * @param[in] size the bytes; the default is #CERVER_HTTP_CLIENT_DEFAULT_MAX_RESPONSE_SIZE
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_set_max_response_size (
	CerverHttpClient *client, const size_t size
);

/**
 * @brief Returns every connection that a client has created.
 *
 * @param[in] client the client
 * @return the count, 0 for a NULL client
 * @threadsafety Any thread.
 * @since 3.0b-13
 */
CERVER_EXPORT u64 cerver_http_client_get_connections (
	const CerverHttpClient *client
);

/**
 * @brief Returns every request of a client that took a connection that was kept, instead of a new one.
 *
 * @param[in] client the client
 * @return the count, 0 for a NULL client
 * @threadsafety Any thread.
 * @since 3.0b-13
 */
CERVER_EXPORT u64 cerver_http_client_get_reused (
	const CerverHttpClient *client
);

/** @} */

/**
 * @defgroup http_clients_request Requests
 * @ingroup http_clients
 * @brief A request: its url, its headers, its body and how it is sent.
 * @{
 */

/**
 * @brief Creates a request.
 *
 * The url is `http://host[:port][/path][?query]`: the host is a name, an IPv4 or an IPv6 in brackets, the port is
 * #CERVER_HTTP_CLIENT_DEFAULT_PORT without one, and the path is `/` without one. The request has its `Host`
 * header; a `Content-Length` is added with its body.
 *
 * @param[in] client the client
 * @param[in] method the method
 * @param[in] url the url, it is copied
 * @return the request, NULL for a NULL client, a method that is not valid, or a url that is not valid: without
 * the scheme #CERVER_HTTP_CLIENT_SCHEME, with a space or a control character, with a fragment, or with a host or a
 * port that is not valid
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpClientRequest *cerver_http_client_request_create (
	CerverHttpClient *client, const CerverHttpMethod method, const char *url
);

/**
 * @brief Deletes a request that was not sent, or one whose caller waited for it
 * (cerver_http_client_request_perform ()): its response is released with it.
 *
 * @param[in] request the request, it can be NULL
 * @warning A request that was sent with cerver_http_client_request_send () is of its client, that deletes it
 * after its handler.
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_request_delete (
	CerverHttpClientRequest *request
);

/**
 * @brief Adds a header to a request.
 *
 * @param[in,out] request the request, before it is sent
 * @param[in] name the name of the header, a token; it is copied
 * @param[in] value its value, without line breaks; it is copied
 * @retval 0 the header was added
 * @retval 1 a NULL argument, a name or a value that is not valid, a header that the client writes (`Host`,
 * `Content-Length`, `Content-Type`, `Transfer-Encoding`), a request that was sent, or no memory
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_client_request_add_header (
	CerverHttpClientRequest *request, const char *name, const char *value
);

/**
 * @brief Sets the body of a request, with its `Content-Type` and its `Content-Length`.
 *
 * @param[in,out] request the request, before it is sent
 * @param[in] content_type the value of its `Content-Type`, NULL to send none
 * @param[in] body the bytes, they are copied; NULL only with a length of 0
 * @param[in] length the bytes of the body
 * @retval 0 the body was set, instead of the one that the request had
 * @retval 1 a NULL request, a NULL body with a length, a content type that is not a valid value, a request that
 * was sent, or no memory: the request keeps the body that it had
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_client_request_set_body (
	CerverHttpClientRequest *request,
	const char *content_type, const char *body, const size_t length
);

/**
 * @brief Sets the time that a request has, instead of the one of its client.
 *
 * @param[in,out] request the request, before it is sent
 * @param[in] milliseconds the time, 0 for the one of its client
 * @since 3.0b-13
 */
CERVER_EXPORT void cerver_http_client_request_set_timeout (
	CerverHttpClientRequest *request, const u32 milliseconds
);

/**
 * @brief Sends a request: its handler takes the response.
 *
 * The request is of its client from this call, also when it fails: it must not be used again. The handler is
 * called once, by the thread of the event loop that sent the request: the one of the calling thread when it runs
 * a loop of the cerver of the client, any loop of that cerver otherwise, or the loop of a client alone.
 *
 * @param[in] request the request
 * @param[in] handler takes the response
 * @param[in] data passed to the handler
 * @retval 0 the request was given to an event loop: the handler will be called
 * @retval 1 a NULL request or handler, a request that was already sent, a client that is being deleted, or the
 * client of a cerver that is not running; the handler is not called, and a request that had not been sent was
 * deleted
 * @threadsafety Any thread; a request is used by one thread at a time.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_client_request_send (
	CerverHttpClientRequest *request, CerverHttpClientHandler handler, void *data
);

/**
 * @brief Sends a request and waits for its response, that stays in the request
 * (cerver_http_client_request_response ()) until it is deleted.
 *
 * @param[in,out] request the request
 * @retval 0 the request ended: its response is complete, or it has an error (cerver_http_client_response_error ())
 * @retval 1 a NULL request, a request that was already sent, a call from a thread of an event loop of the client,
 * a client that is being deleted, or the client of a cerver that is not running; a request that was not sent is
 * still of its caller, that deletes it
 * @warning It waits: it is for the threads of the pools and for the programs without event loops, never for a
 * handler that an event loop runs.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_client_request_perform (
	CerverHttpClientRequest *request
);

/**
 * @brief Returns the response of a request that its caller waited for.
 *
 * @param[in] request the request, after cerver_http_client_request_perform () returned 0
 * @return the response, valid until the request is deleted; NULL for a NULL request or one that has not ended
 * @since 3.0b-13
 */
CERVER_EXPORT const CerverHttpClientResponse *cerver_http_client_request_response (
	const CerverHttpClientRequest *request
);

/**
 * @brief Decides the authentication of a request of a cerver with the response of a request of a client.
 *
 * @param[in,out] context the context of the request that is being authenticated
 * @param[in] response the response of the request that was sent to decide, valid only during the call
 * @return true when the request of the context is authenticated; false rejects it, with the response that this
 * method wrote for it or with a 401 when it wrote none
 * @warning It runs in the thread of an event loop: it must not wait.
 * @since 3.0b-13
 */
typedef bool (*CerverHttpAuthenticationResponse) (
	CerverHttpContext *context, const CerverHttpClientResponse *response
);

/**
 * @brief Authenticates a request of a cerver with the response of another service: it defers the verdict of the
 * request (cerver_http_context_authentication_defer ()), sends a request of a client, and gives the verdict that
 * a method returns for its response. The authentication handler returns right after it: no thread waits.
 *
 * @param[in,out] context the context of the request, in its authentication handler
 * @param[in] request the request that is sent; it is of its client from this call, also when it fails
 * @param[in] on_response decides with the response, also with the one of a request that failed
 * (cerver_http_client_response_error ())
 * @retval 0 the request was sent: on_response will be called, and what the authentication handler returns is not
 * read
 * @retval 1 a NULL argument, a call that is not made from the authentication handler of the request, or a request
 * that could not be sent: the request of the context is rejected, and on_response is not called
 * @threadsafety The thread that runs the authentication handler of the request.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_context_authentication_request (
	CerverHttpContext *context,
	CerverHttpClientRequest *request, CerverHttpAuthenticationResponse on_response
);

/** @} */

/**
 * @defgroup http_clients_response Responses
 * @ingroup http_clients
 * @brief What a response has: its error, its status, its headers and its body, as views of its memory.
 * @{
 */

/**
 * @brief Returns why a request failed.
 *
 * @param[in] response the response
 * @return CERVER_HTTP_CLIENT_ERROR_NONE for a complete response, whatever its status; the error otherwise;
 * CERVER_HTTP_CLIENT_ERROR_MEMORY for a NULL response
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpClientError cerver_http_client_response_error (
	const CerverHttpClientResponse *response
);

/**
 * @brief Returns the status of a response.
 *
 * @param[in] response the response
 * @return the status, 0 for a NULL response or for one whose head did not arrive
 * @since 3.0b-13
 */
CERVER_EXPORT unsigned int cerver_http_client_response_status (
	const CerverHttpClientResponse *response
);

/**
 * @brief Returns the value of the first header of a response with a name.
 *
 * @param[in] response the response
 * @param[in] name the name of the header, case insensitive
 * @return a view of the value; one without data for a header that the response does not have, a NULL argument or
 * a response whose head did not arrive
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpView cerver_http_client_response_header (
	const CerverHttpClientResponse *response, const char *name
);

/**
 * @brief Returns the body of a response, without its transfer coding.
 *
 * @param[in] response the response
 * @return a view of the body, that has what arrived of it when the request failed; one without data for a NULL
 * response or one without a body
 * @since 3.0b-13
 */
CERVER_EXPORT CerverHttpView cerver_http_client_response_body (
	const CerverHttpClientResponse *response
);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif
