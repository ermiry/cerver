/**
 * @file cerver/http/core/handler.h
 * @brief The HttpReceive: the state of a request while it is received and handled, and its getters.
 */

#ifndef _CERVER_HTTP_CORE_HANDLER_H_
#define _CERVER_HTTP_CORE_HANDLER_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_core_handler Receive
 * @ingroup http_core
 * @brief The receive of a request and what a handler can ask of it.
 *
 * The getters for the handlers take a NULL receive.
 * @{
 */

/**
 * @brief The X-macro table of the states of a receive: the number, the name and the string of each one.
 */
#define HTTP_RECEIVE_STATUS_MAP(XX)			\
	XX(0,  NONE,			Undefined)		\
	XX(1,  HEADERS,			Headers)		\
	XX(2,  BODY,			Body)			\
	XX(3,  COMPLETED,		Completed)		\
	XX(4,  INCOMPLETED,		Incompleted)	\
	XX(5,  UNHANDLED,		Unhandled)		\
	XX(6,  TIMEOUT,			Timeout)		\
	XX(7,  CLOSED,			Closed)			\
	XX(8,  PARSER_ERROR,	Parser Error)	\
	XX(9,  LIMIT,			Limit)			\
	XX(10, REJECTED,		Rejected)		\
	XX(11, UPLOAD_ERROR,	Upload Error)	\
	XX(12, RESPONSE_FAILED,	Response Failed)

/**
 * @brief The state of a receive, one `HTTP_RECEIVE_STATUS_<name>` for each row of #HTTP_RECEIVE_STATUS_MAP.
 */
typedef enum HttpReceiveStatus {

	#define XX(num, name, string) HTTP_RECEIVE_STATUS_##name = num,
	HTTP_RECEIVE_STATUS_MAP(XX)
	#undef XX

} HttpReceiveStatus;

/**
 * @brief Returns the string of the state of a receive.
 *
 * @param[in] status the state
 * @return the string of its row in #HTTP_RECEIVE_STATUS_MAP, or the one of HTTP_RECEIVE_STATUS_NONE for a value out
 * of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_receive_status_str (
	const HttpReceiveStatus status
);

/**
 * @brief What answered a request: a file of a static path or a route.
 */
typedef enum HttpReceiveType {

	/** @brief Nothing yet. */
	HTTP_RECEIVE_TYPE_NONE		= 0,
	/** @brief A file of a static path. */
	HTTP_RECEIVE_TYPE_FILE		= 1,
	/** @brief A route. */
	HTTP_RECEIVE_TYPE_ROUTE		= 2

} HttpReceiveType;

/**
 * @brief How a request that matched a path of the routes tree is answered.
 */
typedef enum HttpReceiveDispatch {

	/** @brief Not dispatched yet. */
	HTTP_RECEIVE_DISPATCH_NONE					= 0,
	/** @brief The handler of the endpoint. */
	HTTP_RECEIVE_DISPATCH_ENDPOINT				= 1,
	/** @brief The automatic answer to OPTIONS. */
	HTTP_RECEIVE_DISPATCH_OPTIONS				= 2,
	/** @brief A 405, the path does not have the method. */
	HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED	= 3,
	/** @brief The catch all route. */
	HTTP_RECEIVE_DISPATCH_CATCH_ALL				= 4,
	/** @brief A redirect of the path. */
	HTTP_RECEIVE_DISPATCH_REDIRECT				= 5,

	/** @brief The number of dispatches, the first value that is not valid. */
	HTTP_RECEIVE_DISPATCH_MAX					= 6

} HttpReceiveDispatch;

/**
 * @brief The token that the parser is building between callbacks; a token can arrive in many pieces when it is split
 * between two recv ().
 */
typedef enum HttpReceiveToken {

	/** @brief No token. */
	HTTP_RECEIVE_TOKEN_NONE			= 0,
	/** @brief The url. */
	HTTP_RECEIVE_TOKEN_URL			= 1,
	/** @brief The field of a header. */
	HTTP_RECEIVE_TOKEN_FIELD		= 2,
	/** @brief The value of a header. */
	HTTP_RECEIVE_TOKEN_VALUE		= 3,

	/** @brief The number of tokens, the first value that is not valid. */
	HTTP_RECEIVE_TOKEN_MAX			= 4

} HttpReceiveToken;

/**
 * @brief The state of a request while it is received and handled.
 */
struct _HttpReceive {

	/** @brief The state of the receive. */
	HttpReceiveStatus receive_status;

	/** @brief The receive of the connection in the cerver. */
	CerverReceive *cr;

	/** @brief Keep the connection alive: do not close it after the request has ended. */
	bool keep_alive;

	/** @brief The method that handles the bytes of each recv (). */
	void (*handler)(
		struct _HttpReceive *, ssize_t, char *
	);

	/** @brief The HttpCerver of the request. */
	HttpCerver *http_cerver;

	/** @brief The parser of the request. */
	http_parser *parser;
	/** @brief The callbacks of the parser. */
	http_parser_settings settings;

	/** @brief The parser of a multipart body, NULL for the other requests. */
	multipart_parser *mpart_parser;
	/** @brief The callbacks of the multipart parser. */
	multipart_parser_settings mpart_settings;

	/** @brief The request. */
	HttpRequest *request;

	/** @brief What answered the request. */
	HttpReceiveType type;
	/** @brief How the request is answered. */
	HttpReceiveDispatch dispatch;
	/** @brief The node of the routes tree that matched. */
	const HttpRouteNode *node;
	/** @brief The endpoint that handles the request. */
	HttpEndpoint *endpoint;

	/** @brief The route of the first routes API of the endpoint, NULL for the new endpoints. */
	HttpRoute *route;

	/** @brief The file of a static path that is served. */
	const char *served_file;

	/** @brief A HEAD request handled by the GET handler: the responses are sent without their body. */
	bool head;

	/** @brief The body limit of the request, the one of the endpoint or the one of the cerver. */
	size_t max_body_size;

	/** @brief The status of the response. */
	http_status status;
	/** @brief The bytes of the response that were sent. */
	size_t sent;

	/** @brief The request has a multipart body. */
	bool is_multi_part;
	/** @brief The stats of the files of the request. */
	struct _HttpRouteFileStats *file_stats;

	/** @brief The number of the request in the default uploads generators, 0 until one asks for it. */
	size_t upload_sequence;

	/** @brief The request created the directory of its uploads, it is removed if it ends empty. */
	bool upload_directory_created;

	/** @brief The token that is being built. */
	HttpReceiveToken token;
	/** @brief The pieces of the url. */
	String *url_buffer;
	/** @brief The pieces of the field of a header. */
	String *field_buffer;
	/** @brief The pieces of the value of a header. */
	String *value_buffer;

	/** @brief The bytes of the headers, for the limits. */
	size_t header_size;
	/** @brief The headers, for the limits. */
	unsigned int n_headers;
	/** @brief The bytes of the body, for the limits. */
	size_t body_size;
	/** @brief The parts of a multipart body, for the limits. */
	unsigned int n_parts;

	/**
	 * @brief cerver already tried to send a response, even if it failed: before the request was completed, the rest of
	 * the request is not handled; after its handler, a request without one is answered with 500.
	 */
	bool responded;

	/** @brief The response that the handler sends in pieces. */
	HttpStream stream;

	/** @brief The arena of the thread that handles the request: the receive, its parser and its request are released with it. */
	struct Arena *arena;

	/** @brief The token buffers belong to the thread and they are reused between requests. */
	bool borrowed_buffers;

	/** @brief The stage that the request has reached. */
	HttpStage stage;

	/** @brief How the client left, when a recv () or a send () found that it was gone. */
	HttpAbortReason abort_reason;

	/** @brief A send () of the response failed. */
	bool response_failed;
	/** @brief The errno of the send () that failed. */
	int response_error;

	/** @brief The deadline of all the sends of the response, set by its first send; each piece of a stream sets its own. */
	struct timespec send_deadline;
	/** @brief #send_deadline was set. */
	bool send_deadline_set;

	/** @brief The last state of the client that was found, a gone client never comes back. */
	HttpClientState client_state;

};

/** @brief The receive of a request, see struct _HttpReceive. */
typedef struct _HttpReceive HttpReceive;

/**
 * @brief Allocates a receive with the default values.
 *
 * @return the new receive, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpReceive *http_receive_new (void);

/**
 * @brief Creates the receive, its parser and its request in the arena of the thread.
 *
 * @param[in] cerver_receive the receive of the connection
 * @param[in] arena the arena of the thread
 * @param[in] url_buffer the buffer of the url of the thread, emptied but never deleted
 * @param[in] field_buffer the buffer of the fields of the thread, emptied but never deleted
 * @param[in] value_buffer the buffer of the values of the thread, emptied but never deleted
 * @return the new receive, NULL on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpReceive *http_receive_create_arena (
	CerverReceive *cerver_receive, struct Arena *arena,
	String *url_buffer, String *field_buffer, String *value_buffer
);

/**
 * @brief Creates a receive with its parser, its request and its file stats, allocated on their own.
 *
 * @param[in] cerver_receive the receive of the connection
 * @return the new receive, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpReceive *http_receive_create (
	CerverReceive *cerver_receive
);

/**
 * @brief Releases a receive: its uploads, its parsers, its request and its buffers; what lives in an arena is
 * released with it.
 *
 * @param[in] http_receive the receive, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_delete (
	HttpReceive *http_receive
);

/**
 * @brief Reads, without waiting, the data that the client already sent, so a close () does not answer with a RST.
 *
 * @param[in] sock_fd the socket
 * @retval 0 the data was read
 * @retval 1 a socket that is not valid
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_receive_drain_available (const int sock_fd);

/**
 * @brief Returns whether the headers of the request have not been completed.
 *
 * @param[in] http_receive the receive, it must not be NULL
 * @return true while its state is HTTP_RECEIVE_STATUS_NONE
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool http_receive_is_receiving_headers (
	const HttpReceive *http_receive
);

/**
 * @brief Answers a request that did not arrive in time with 408 and closes it; if the client did not send anything,
 * it is only closed.
 *
 * @param[in,out] http_receive the receive
 * @param[in] received whether the client sent something
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
);

/**
 * @brief Returns the receive of the connection of a request in the cerver.
 *
 * @param[in] http_receive the receive
 * @return the receive of the connection, NULL for a NULL receive
 */
CERVER_EXPORT const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the socket of the connection of a request.
 *
 * @param[in] http_receive the receive
 * @return the socket, -1 for a NULL receive
 */
CERVER_EXPORT int http_receive_get_sock_fd (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the HttpCerver of a request.
 *
 * @param[in] http_receive the receive
 * @return the HttpCerver, NULL for a NULL receive
 */
CERVER_EXPORT const HttpCerver *http_receive_get_cerver (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the endpoint that handles a request.
 *
 * @param[in] http_receive the receive
 * @return the endpoint, NULL if no endpoint matched or for a NULL receive
 */
CERVER_EXPORT const HttpEndpoint *http_receive_get_endpoint (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the custom data of the endpoint that handles a request: its own, the one that it inherits from its
 * groups, or the one of its route of the first routes API.
 *
 * @param[in] http_receive the receive
 * @return the data, NULL without one or for a NULL receive
 */
CERVER_EXPORT const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the custom data of the HttpCerver that received a request.
 *
 * @param[in] http_receive the receive
 * @return the data, NULL without one or for a NULL receive
 */
CERVER_EXPORT const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
);

/**
 * @brief Returns whether a request is a HEAD handled by the GET handler; its responses are sent without their body.
 *
 * @param[in] http_receive the receive
 * @return true for a HEAD handled by the GET handler, false for a NULL receive
 */
CERVER_EXPORT bool http_receive_is_head (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the methods that the path of a request accepts, the value of the Allow header.
 *
 * @param[in] http_receive the receive
 * @return the methods, NULL if the path did not match any route or for a NULL receive
 */
CERVER_EXPORT const char *http_receive_get_allow (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the stage that a request has reached.
 *
 * @param[in] http_receive the receive
 * @return the stage, HTTP_STAGE_QUEUED for a NULL receive
 */
CERVER_EXPORT HttpStage http_receive_get_stage (
	const HttpReceive *http_receive
);

/**
 * @brief Checks, without waiting and without reading its data, whether the client of a request is still connected.
 *
 * A handler that takes long can call it to stop the work of a client that left.
 *
 * @param[in] http_receive the receive
 * @return HTTP_CLIENT_STATE_CLOSED when it closed its side (an EOF), HTTP_CLIENT_STATE_RESET when its connection
 * failed (a RST) or for a NULL receive; a gone client never comes back
 */
CERVER_EXPORT HttpClientState http_receive_get_client_state (
	const HttpReceive *http_receive
);

/**
 * @brief Returns whether the client of a request is gone: it reset its connection, it closed its side and the cerver
 * takes it as gone (http_cerver_set_half_close_is_gone ()), or a send of its response failed.
 *
 * @param[in] http_receive the receive
 * @return true if the client is gone, also for a NULL receive
 * @note A stream whose write fails is also gone, its handler must stop writing.
 */
CERVER_EXPORT bool http_receive_is_client_gone (
	const HttpReceive *http_receive
);

/**
 * @brief Marks that the client left while the request was being received.
 *
 * @param[in,out] http_receive the receive
 * @param[in] error 0 for an EOF, or the errno of the recv () or the send () that failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_client_left (
	HttpReceive *http_receive, const int error
);

/**
 * @brief Marks that a send () of the response failed; the request is counted once as a failed response.
 *
 * @param[in] http_receive the receive
 * @param[in] error the errno of the send ()
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_response_failed (
	const HttpReceive *http_receive, const int error
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
