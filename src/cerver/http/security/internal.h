/**
 * @file http/security/internal.h
 * @internal
 * @brief The requests of the remote authentication (cerver/http/security/remote.h) that wait in an event loop: what
 * a request keeps, and what the loop and the connection call; never installed.
 *
 * Without `CURL=1` the methods of the loop do nothing, so the loop never asks if cerver has libcurl.
 */

#ifndef _CERVER_HTTP_SECURITY_INTERNAL_H_
#define _CERVER_HTTP_SECURITY_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/events/internal.h"

#include "cerver/http/requests/method.h"
#include "cerver/http/security/remote.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;
struct HttpConnection;

/**
 * @internal
 * @brief Where the request of a connection was when its authentication sent its remote request, so its job
 * continues there when the answer arrives.
 */
typedef enum HttpRemoteStage {

	/** @internal @brief The authentication before the body (the job that authorizes, or the thread that follows). */
	HTTP_REMOTE_STAGE_AUTHORIZE		= 0,
	/** @internal @brief The authentication of a request without a body, in the job that handles it. */
	HTTP_REMOTE_STAGE_DISPATCH		= 1

} HttpRemoteStage;

/**
 * @internal
 * @brief A remote request that waits in an event loop, owned by its connection, and its answer.
 */
typedef struct HttpRemote {

	/** @internal @brief Where the job continues with the answer. */
	HttpRemoteStage stage;

	/** @internal @brief The method of the request. */
	HttpRequestMethod method;
	/** @internal @brief Its URL, owned. */
	char *url;
	/** @internal @brief Its headers, a curl_slist, owned. */
	void *headers;
	/** @internal @brief Its body, owned, NULL without one. */
	char *body;
	/** @internal @brief The bytes of its body. */
	size_t body_size;
	/** @internal @brief The milliseconds that it waits for its answer. */
	unsigned int timeout_milliseconds;

	/** @internal @brief What decides with the answer. */
	HttpAuthResponseHandler on_response;
	/** @internal @brief Its data. */
	void *data;

	/** @internal @brief The easy handle of libcurl while the request runs in the loop, NULL otherwise. */
	void *easy;

	/** @internal @brief The status of the answer, 0 when it failed. */
	unsigned int status;
	/** @internal @brief The body of the answer, with a terminator, owned. */
	char *response;
	/** @internal @brief The bytes of the body of the answer. */
	size_t response_size;
	/** @internal @brief The room of #response. */
	size_t response_capacity;
	/** @internal @brief The body of the answer did not fit in #HTTP_AUTH_RESPONSE_MAX_BODY_SIZE. */
	bool response_overflow;

} HttpRemote;

/**
 * @internal
 * @brief Releases a remote request and its answer.
 *
 * @param[in] remote the request, it can be NULL
 */
CERVER_HIDDEN void http_remote_delete (HttpRemote *remote);

/**
 * @internal
 * @brief Allows a remote request of the authentication of a request of an event loop to wait in its loop: only the
 * authentication of http_receive_headers_authorize () can wait there, every other one is synchronous.
 *
 * @param[in,out] http_receive the receive
 * @param[in] allowed true while the authentication runs
 */
CERVER_HIDDEN void http_remote_allow (struct _HttpReceive *http_receive, const bool allowed);

/**
 * @internal
 * @brief Returns whether the authentication of a request left a remote request to wait in its loop.
 *
 * @param[in] http_receive the receive
 * @return true when its connection has a remote request that was not answered
 */
CERVER_HIDDEN bool http_remote_is_waiting (const struct _HttpReceive *http_receive);

/**
 * @internal
 * @brief Releases the remote request that an authentication left without returning #HTTP_AUTHENTICATION_PENDING: it
 * is never sent.
 *
 * @param[in,out] http_receive the receive
 */
CERVER_HIDDEN void http_remote_discard (struct _HttpReceive *http_receive);

/**
 * @internal
 * @brief Runs the on_response () of the answer of the remote request of a connection, in a thread of its pool, and
 * releases the request.
 *
 * @param[in,out] http_receive the receive of the request, whose connection has the remote request
 * @return 0 when the request is authenticated, 1 when it is not (it was answered with 401)
 */
CERVER_HIDDEN unsigned int http_remote_answer (struct _HttpReceive *http_receive);

/**
 * @internal
 * @brief Releases the requests of the loop that still wait, and its multi of libcurl.
 *
 * @param[in,out] loop the loop
 */
CERVER_HIDDEN void http_remote_loop_delete (EventLoop *loop);

/**
 * @internal
 * @brief How long the poller of the loop can wait for the timers of libcurl.
 *
 * @param[in] loop the loop
 * @return the milliseconds, #EVENT_WHEEL_NEXT_NONE without a timer
 */
CERVER_HIDDEN int http_remote_loop_wait (const EventLoop *loop);

/**
 * @internal
 * @brief Runs the timers of libcurl that ended and gives each request that ended back to its connection.
 *
 * @param[in,out] loop the loop
 * @return the requests that ended
 */
CERVER_HIDDEN unsigned int http_remote_loop_step (EventLoop *loop);

/**
 * @internal
 * @brief A socket of the requests that the poller reported.
 *
 * @param[in,out] loop the loop
 * @param[in] source the source of the socket (#EVENT_SOURCE_TYPE_REMOTE)
 * @param[in] ready the EVENT_READY_* flags
 */
CERVER_HIDDEN void http_remote_loop_ready (EventLoop *loop, const EventSource *source, const unsigned int ready);

/**
 * @internal
 * @brief Starts the remote request of a connection in its loop; the connection waits without a thread until
 * http_connection_remote_answered ().
 *
 * @param[in,out] loop the loop of the connection
 * @param[in,out] connection the connection, with its remote request
 * @retval 0 the request runs
 * @retval 1 it could not start: its answer is a failure (status 0), the caller gives it to the pool at once
 */
CERVER_HIDDEN unsigned int http_remote_loop_start (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Stops the remote request of a connection that runs in its loop (the loop stops); its answer is a failure.
 *
 * @param[in,out] loop the loop of the connection
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_remote_loop_cancel (EventLoop *loop, struct HttpConnection *connection);

#ifdef __cplusplus
}
#endif

#endif
