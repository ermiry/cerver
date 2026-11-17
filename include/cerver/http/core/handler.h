/**
 * @file cerver/http/core/handler.h
 * @brief The HttpReceive: the state of an HTTP connection and of the request that it is receiving.
 */

#ifndef _CERVER_HTTP_CORE_HANDLER_H_
#define _CERVER_HTTP_CORE_HANDLER_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_receive Receive
 * @ingroup http_core
 * @brief How a request is received and what a handler can ask about it.
 * @{
 */

/**
 * @brief The X-macro table of the status of a receive: the number, the name and the string of each one.
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
 * @brief The status of a receive, one `HTTP_RECEIVE_STATUS_<name>` for each row of #HTTP_RECEIVE_STATUS_MAP.
 */
typedef enum HttpReceiveStatus {

	#define XX(num, name, string) HTTP_RECEIVE_STATUS_##name = num,
	HTTP_RECEIVE_STATUS_MAP(XX)
	#undef XX

} HttpReceiveStatus;

/**
 * @brief Returns the string of the status of a receive.
 *
 * @param[in] status the status
 * @return the string of its row in #HTTP_RECEIVE_STATUS_MAP, or the one of HTTP_RECEIVE_STATUS_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_receive_status_str (
	const HttpReceiveStatus status
);

/**
 * @brief What a request found: a static file or a route.
 */
typedef enum HttpReceiveType {

	/** @brief Nothing was found yet. */
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

	/** @brief It was not dispatched yet. */
	HTTP_RECEIVE_DISPATCH_NONE					= 0,
	/** @brief The handler of its endpoint. */
	HTTP_RECEIVE_DISPATCH_ENDPOINT				= 1,
	/** @brief The automatic answer of an OPTIONS request. */
	HTTP_RECEIVE_DISPATCH_OPTIONS				= 2,
	/** @brief The path has no handler for its method: 405. */
	HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED	= 3,
	/** @brief The catch all handler. */
	HTTP_RECEIVE_DISPATCH_CATCH_ALL				= 4,
	/** @brief A redirect of its trailing slash. */
	HTTP_RECEIVE_DISPATCH_REDIRECT				= 5,
	/**
	 * @brief The health route of the cerver (http_cerver_set_health_route ()).
	 *
	 * @since 3.0b-5
	 */
	HTTP_RECEIVE_DISPATCH_HEALTH				= 6,
	/**
	 * @brief The ready route of the cerver (http_cerver_set_ready_route ()).
	 *
	 * @since 3.0b-5
	 */
	HTTP_RECEIVE_DISPATCH_READY					= 7,

	/** @brief The number of dispatches, the first value that is not valid. */
	HTTP_RECEIVE_DISPATCH_MAX					= 8

} HttpReceiveDispatch;

/**
 * @brief What the owner of a receive lends to it: the arena where the data of each request lives and the buffer
 * where the head of a request that arrives in pieces is gathered.
 *
 * They are used by one request at a time and they are never released by the receive.
 */
struct _HttpReceiveResources {

	/** @brief The arena of the data of each request. */
	struct Arena *arena;

	/**
	 * @brief The buffer of the head of a request that does not arrive in one piece.
	 *
	 * @since 3.0b-9
	 */
	String *head_buffer;

};

/** @brief What the owner of a receive lends to it, see struct _HttpReceiveResources. */
typedef struct _HttpReceiveResources HttpReceiveResources;

/**
 * @brief The recv () calls of a request, to explain why it ended before being completed.
 */
struct _HttpReceiveDiagnostics {

	/** @brief What the last recv () returned. */
	ssize_t last_rc;
	/** @brief The errno of the last recv (). */
	int last_errno;
	/** @brief The calls to recv (). */
	unsigned int n_recvs;

	/** @brief The last recv () timed out or a deadline expired. */
	bool timed_out;

};

/** @brief The recv () calls of a request, see struct _HttpReceiveDiagnostics. */
typedef struct _HttpReceiveDiagnostics HttpReceiveDiagnostics;

/**
 * @brief The fields of a request that a receive takes without an allocation.
 *
 * @since 3.0b-9
 */
#define HTTP_RECEIVE_FIELDS_INLINE			(1 << 5)

/**
 * @brief Where the parser of a request of an event loop stopped to give the request to a thread of the pool.
 *
 * @since 3.0b-5
 */
typedef enum HttpReceivePause {

	/** @brief The parser is not paused for the pool. */
	HTTP_RECEIVE_PAUSE_NONE				= 0,
	/** @brief The headers are complete: the request is authorized before its body is received. */
	HTTP_RECEIVE_PAUSE_HEADERS			= 1,
	/** @brief The request is complete: it is handled. */
	HTTP_RECEIVE_PAUSE_MESSAGE			= 2

} HttpReceivePause;

/**
 * @brief The state of an HTTP connection and of the request that it is receiving.
 *
 * What belongs to the connection (its cerver receive, the parser, the resources of its owner, the state of its
 * client) stays for all its requests, and what belongs to a request is started again by http_receive_reset ().
 * The fields are read by the parts of HTTP; a handler uses the getters.
 */
struct _HttpReceive {

	/** @brief The status of the receive. */
	HttpReceiveStatus receive_status;

	/** @brief The cerver receive of the connection. */
	CerverReceive *cr;

	/**
	 * @brief The connection is kept after the response of the request, for the next one of its client.
	 *
	 * A value of each request: false until the request is complete, when the cerver decides it before the response
	 * is sent (so the response says it in its Connection header); a response whose end the client can not find,
	 * one that failed, one with its own "Connection: close" or a cerver that stops close the connection anyway.
	 */
	bool keep_alive;

	/** @brief Handles each buffer that the connection receives. */
	void (*handler)(
		struct _HttpReceive *, ssize_t, char *
	);

	/** @brief The HttpCerver of the connection. */
	HttpCerver *http_cerver;

	/**
	 * @brief The parser of the requests of the connection (cerver/http/parser).
	 *
	 * @since 3.0b-9
	 */
	CerverHttpParser parser;

	/**
	 * @brief The fields of the head of the request: #fields_inline, or an array of the receive for a request with
	 * more fields than it takes, that stays for the next requests of the connection.
	 *
	 * @since 3.0b-9
	 */
	CerverHttpField *fields;
	/**
	 * @brief The fields that #fields takes.
	 *
	 * @since 3.0b-9
	 */
	unsigned int fields_capacity;
	/**
	 * @brief The fields of most requests, without an allocation.
	 *
	 * @since 3.0b-9
	 */
	CerverHttpField fields_inline[HTTP_RECEIVE_FIELDS_INLINE];

	/**
	 * @brief The head of a request that did not arrive in one piece, gathered until it is complete; lent by the
	 * owner of the receive or its own.
	 *
	 * @since 3.0b-9
	 */
	String *head_buffer;

	/**
	 * @brief What takes each piece of the body of the request: it is saved, it is discarded (a request that no
	 * endpoint handles) or it is a multipart body; it returns 0, or 1 when the request was rejected.
	 *
	 * @since 3.0b-9
	 */
	int (*body_handler)(
		struct _HttpReceive *, const char *, size_t
	);

	/** @brief What the client asked when its request was completed (cerver_http_parser_keep_alive ()): HTTP/1.1 without "Connection: close" or HTTP/1.0 with "Connection: keep-alive". */
	bool keep_alive_requested;

	/** @brief The bytes of the last buffer that were not parsed after the request was completed (a pipelined request). */
	size_t unparsed;

	/** @brief The parser of a multipart body: #multipart while the request is multipart, NULL otherwise. */
	CerverHttpMultipart *mpart_parser;
	/**
	 * @brief The memory of the parser of a multipart body, without an allocation.
	 *
	 * @since 3.0b-9
	 */
	CerverHttpMultipart multipart;

	/** @brief The request that is being received. */
	HttpRequest *request;

	/** @brief What the request found. */
	HttpReceiveType type;
	/** @brief How the request is answered. */
	HttpReceiveDispatch dispatch;
	/** @brief The node of the routes tree that matched the path. */
	const HttpRouteNode *node;
	/** @brief The endpoint that handles the request. */
	HttpEndpoint *endpoint;

	/** @brief The route of the first routes API of the endpoint, NULL for the new endpoints. */
	HttpRoute *route;

	/** @brief The static file that was served. */
	const char *served_file;

	/** @brief A HEAD request handled by the GET handler, its responses are sent without their body. */
	bool head;

	/** @brief The body limit of the request, the one of its endpoint or the one of its cerver. */
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

	/** @brief The bytes of the body, for the limits. */
	size_t body_size;
	/** @brief The parts of a multipart body, for the limits. */
	unsigned int n_parts;

	/**
	 * @brief cerver already tried to send a response, even if it failed.
	 *
	 * Before the request was completed, the rest of the request is not handled; after its handler, a request
	 * without one is answered with 500.
	 */
	bool responded;

	/**
	 * @brief The response that was sent has an end that its client can find without the close of the connection.
	 *
	 * It has a Content-Length, its status has no body (1xx, 204, 304), it answers a HEAD or it is a chunked stream
	 * that sent its last chunk; a connection can only take another request after a response like that.
	 */
	bool framed;

	/** @brief The response that the handler sends in pieces. */
	HttpStream stream;

	/**
	 * @brief The arena that the owner of the receive lends to it: the data of its request lives there and it is
	 * released all at once when the request ends; NULL in a receive that allocates the memory of its request.
	 */
	struct Arena *arena;

	/** @brief The buffer of the head belongs to the owner of the receive and it is reused between requests. */
	bool borrowed_buffers;

	/** @brief The fd of the connection when the receive was created, for the log after it is closed. */
	int sock_fd;

	/** @brief When the request started, to know the time that it took. */
	double start_time;

	/** @brief The bytes of the request that were received. */
	size_t received;

	/** @brief The recv () calls of the request, to explain why it ended before being completed. */
	HttpReceiveDiagnostics diagnostics;

	/** @brief The deadline of the headers, in monotonic seconds, 0 when disabled. */
	double headers_deadline;
	/** @brief The deadline of the whole request, in monotonic seconds, 0 when disabled. */
	double request_deadline;

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

	/** @brief The request of an upgrade to a WebSocket endpoint, until its connection starts. */
	struct HttpWebSocketHandshake *web_socket;

	/** @brief The 101 was sent, the connection belongs to its WebSocket now. */
	bool upgraded;

	/** @brief The last state of the client that was found, a gone client never comes back. */
	HttpClientState client_state;

	/**
	 * @brief The connection of the event loop that owns the receive, NULL with the other handler types.
	 *
	 * With it the parser does not handle the request: it pauses, and the loop answers it or gives it to the pool.
	 *
	 * @since 3.0b-5
	 */
	struct HttpConnection *connection;

	/**
	 * @brief Where the parser was paused for the pool, a value of a request.
	 *
	 * @since 3.0b-5
	 */
	HttpReceivePause pause;

	/**
	 * @brief The second part of the headers (the authentication, the upgrade, the uploads, the static path) has not
	 * run: a thread of the pool runs it before the body is received or before the request is handled.
	 *
	 * @since 3.0b-5
	 */
	bool authorize_pending;

	/**
	 * @brief The requests that were completed in the connection, with the one that is being handled; a value of
	 * the connection.
	 *
	 * @since 3.0b-5
	 */
	unsigned int n_requests;

};

/** @brief The state of an HTTP connection, see struct _HttpReceive. */
typedef struct _HttpReceive HttpReceive;

/**
 * @brief Allocates an empty receive.
 *
 * @return the new receive, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpReceive *http_receive_new (void);

/**
 * @brief Prepares a receive that lives in the memory of its owner (a worker thread, a connection) with the
 * resources that it lends.
 *
 * The data of each request lives in the arena and the token buffers are reused: they are emptied but never deleted.
 * The receive is ended with http_receive_delete (), that does not release it nor its resources.
 *
 * @param[out] http_receive the receive
 * @param[in] cerver_receive the cerver receive of the connection
 * @param[in] resources the resources, all of them are required
 * @retval 0 the receive is ready for its first request
 * @retval 1 a NULL argument, incomplete resources, or its request could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_receive_prepare (
	HttpReceive *http_receive,
	CerverReceive *cerver_receive, const HttpReceiveResources *resources
);

/**
 * @brief Creates a receive that allocates its own memory and the one of its request.
 *
 * @param[in] cerver_receive the cerver receive of the connection
 * @return the new receive, released with http_receive_delete (); NULL on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpReceive *http_receive_create (
	CerverReceive *cerver_receive
);

/**
 * @brief Ends the request of the receive and starts a new one in the same connection.
 *
 * Its uploads, its data and the arena that was lent are released; the parser, the clock, the deadlines, the stage
 * and the counters of the request start again, and what belongs to the connection is kept.
 *
 * @param[in,out] http_receive the receive
 * @retval 0 the new request started
 * @retval 1 a NULL receive or the new request could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_receive_reset (
	HttpReceive *http_receive
);

/**
 * @brief Ends the request of the receive; a receive that was created is released, a prepared one is not.
 *
 * @param[in] http_receive the receive, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_delete (
	HttpReceive *http_receive
);

/**
 * @brief Reads, without waiting, the data that the client already sent, up to #HTTP_RECEIVE_DRAIN_SIZE bytes, so a
 * close () does not answer with a RST.
 *
 * @param[in] sock_fd the socket
 * @retval 0 the data was read
 * @retval 1 an invalid socket
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_receive_drain_available (const int sock_fd);

/**
 * @brief Returns whether the headers of the request have not been completed.
 *
 * @param[in] http_receive the receive
 * @return true while the headers are being received
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool http_receive_is_receiving_headers (
	const HttpReceive *http_receive
);

/**
 * @brief Ends a request that did not arrive in time: it is answered with 408 and closed.
 *
 * @param[in,out] http_receive the receive
 * @param[in] received false if the client did not send anything, then it is only closed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
);

/**
 * @brief Returns the cerver receive of the connection of a request.
 *
 * @param[in] http_receive the receive
 * @return the cerver receive, NULL for a NULL receive
 */
CERVER_EXPORT const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the socket of the connection of a request.
 *
 * @param[in] http_receive the receive
 * @return the fd, -1 for a NULL receive
 */
CERVER_EXPORT const int http_receive_get_sock_fd (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the HttpCerver that received a request.
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
 * @return the data, NULL if it has none or for a NULL receive
 */
CERVER_EXPORT const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the custom data of the HttpCerver that received a request.
 *
 * @param[in] http_receive the receive
 * @return the data, NULL if it has none or for a NULL receive
 */
CERVER_EXPORT const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
);

/**
 * @brief Returns whether a request is a HEAD handled by the GET handler; its responses are sent without their body.
 *
 * @param[in] http_receive the receive
 * @return true for a HEAD, false for any other request or a NULL receive
 */
CERVER_EXPORT bool http_receive_is_head (
	const HttpReceive *http_receive
);

/**
 * @brief Returns the methods that the path of a request accepts, the value of the Allow header.
 *
 * @param[in] http_receive the receive
 * @return the value, that belongs to the routes tree; NULL if the path did not match any route
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
 * @brief Checks, without waiting and without reading its data, if the client of a request is still connected.
 *
 * A handler that takes long can call it to stop the work of a client that left.
 *
 * @param[in] http_receive the receive
 * @return HTTP_CLIENT_STATE_CONNECTED, HTTP_CLIENT_STATE_CLOSED when the client closed its side (an EOF), or
 * HTTP_CLIENT_STATE_RESET when its connection failed (a RST) and for a NULL receive; a gone client never comes back
 */
CERVER_EXPORT HttpClientState http_receive_get_client_state (
	const HttpReceive *http_receive
);

/**
 * @brief Returns whether the client of a request is gone.
 *
 * A client is gone when it reset its connection, when it closed its side and the cerver takes it as gone
 * (http_cerver_set_half_close_is_gone ()), or when a send () of the response failed: a stream whose write fails is
 * also gone, and its handler must stop writing.
 *
 * @param[in] http_receive the receive
 * @return true if the client is gone, and for a NULL receive
 */
CERVER_EXPORT bool http_receive_is_client_gone (
	const HttpReceive *http_receive
);

/**
 * @brief Records that the client left while the request was being received.
 *
 * @param[in,out] http_receive the receive
 * @param[in] error 0 for an EOF, or the errno of the recv () or the send () that failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_receive_client_left (
	HttpReceive *http_receive, const int error
);

/**
 * @brief Records that a send () of the response failed; the request is counted once as a failed response.
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
