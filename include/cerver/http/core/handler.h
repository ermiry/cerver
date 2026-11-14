#ifndef _CERVER_HTTP_CORE_HANDLER_H_
#define _CERVER_HTTP_CORE_HANDLER_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region handler

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

typedef enum HttpReceiveStatus {

	#define XX(num, name, string) HTTP_RECEIVE_STATUS_##name = num,
	HTTP_RECEIVE_STATUS_MAP(XX)
	#undef XX

} HttpReceiveStatus;

CERVER_PUBLIC const char *http_receive_status_str (
	const HttpReceiveStatus status
);

typedef enum HttpReceiveType {

	HTTP_RECEIVE_TYPE_NONE		= 0,
	HTTP_RECEIVE_TYPE_FILE		= 1,
	HTTP_RECEIVE_TYPE_ROUTE		= 2

} HttpReceiveType;

// how a request that matched a path of the routes tree is answered
typedef enum HttpReceiveDispatch {

	HTTP_RECEIVE_DISPATCH_NONE					= 0,
	HTTP_RECEIVE_DISPATCH_ENDPOINT				= 1,
	HTTP_RECEIVE_DISPATCH_OPTIONS				= 2,
	HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED	= 3,
	HTTP_RECEIVE_DISPATCH_CATCH_ALL				= 4,
	HTTP_RECEIVE_DISPATCH_REDIRECT				= 5,

	HTTP_RECEIVE_DISPATCH_MAX					= 6

} HttpReceiveDispatch;

// what the owner of a receive lends to it: the arena where the data of each request lives
// and the buffers of the tokens that the parser gives in pieces
// they are used by one request at a time and they are never released by the receive
struct _HttpReceiveResources {

	struct Arena *arena;

	String *url_buffer;
	String *field_buffer;
	String *value_buffer;

};

typedef struct _HttpReceiveResources HttpReceiveResources;

// keeps track of the recv () calls of a request
// to explain why it ended before being completed
struct _HttpReceiveDiagnostics {

	ssize_t last_rc;
	int last_errno;
	unsigned int n_recvs;

	// the last recv () timed out or a deadline expired
	bool timed_out;

};

typedef struct _HttpReceiveDiagnostics HttpReceiveDiagnostics;

// what belongs to the connection (its cerver receive, the parser, the resources
// of its owner, the state of its client) stays for all its requests,
// and what belongs to a request is started again by http_receive_reset ()
struct _HttpReceive {

	HttpReceiveStatus receive_status;

	CerverReceive *cr;

	// keep connection alive
	// don't close after request has ended
	bool keep_alive;

	void (*handler)(
		struct _HttpReceive *, ssize_t, char *
	);

	HttpCerver *http_cerver;

	// the parser of the request, embedded in the receive, with its own settings,
	// so the body callback can change for each request
	llhttp_t parser;
	llhttp_settings_t settings;

	// what the client asked when its request was completed (llhttp_should_keep_alive ()),
	// the connection is still closed after the response
	bool keep_alive_requested;

	// the bytes of the last buffer that were not parsed after the request was completed
	// (a pipelined request)
	size_t unparsed;

	// the protocol of the request line arrives in pieces: the bytes of it that are the ones of HTTP,
	// and whether it had any other one (llhttp also parses the request lines of RTSP and ICE)
	size_t protocol_length;
	bool protocol_other;

	multipart_parser *mpart_parser;
	multipart_parser_settings mpart_settings;

	HttpRequest *request;

	// found
	HttpReceiveType type;
	HttpReceiveDispatch dispatch;
	const HttpRouteNode *node;
	HttpEndpoint *endpoint;

	// the route of the first routes API of the endpoint, NULL for the new endpoints
	HttpRoute *route;

	const char *served_file;

	// a HEAD request handled by the GET handler, the responses are sent without their body
	bool head;

	// the body limit of the request, the endpoint's one or the cerver's
	size_t max_body_size;

	http_status status;
	size_t sent;

	bool is_multi_part;
	struct _HttpRouteFileStats *file_stats;

	// the number of the request in the default uploads generators, 0 until one asks for it
	size_t upload_sequence;

	// the request created the directory of its uploads, it is removed if it ends empty
	bool upload_directory_created;

	// the pieces of the url, the header field and the header value that are being received,
	// each one is closed by its complete callback of the parser
	String *url_buffer;
	String *field_buffer;
	String *value_buffer;

	// used to check the requests limits
	size_t header_size;
	unsigned int n_headers;
	size_t body_size;
	unsigned int n_parts;

	// cerver already tried to send a response, even if it failed:
	// before the request was completed, the rest of the request is not handled,
	// after its handler, a request without one is answered with 500
	bool responded;

	// the response that was sent has an end that its client can find without the close
	// of the connection: it has a Content-Length, its status has no body (1xx, 204, 304),
	// it answers a HEAD or it is a chunked stream that sent its last chunk
	// a connection can only take another request after a response like that
	bool framed;

	// the response that the handler sends in pieces
	HttpStream stream;

	// the arena that the owner of the receive lends to it, the data of its request
	// lives there and it is released all at once when the request ends,
	// NULL in a receive that allocates the memory of its request
	struct Arena *arena;

	// the token buffers belong to the owner of the receive and they are reused between requests
	bool borrowed_buffers;

	// the fd of the connection when the receive was created, for the log after it is closed
	int sock_fd;

	// when the request started, to know the time that it took
	double start_time;

	// the bytes of the request that were received
	size_t received;

	// the recv () calls of the request, to explain why it ended before being completed
	HttpReceiveDiagnostics diagnostics;

	// the deadlines of the request, in monotonic secs, 0 when disabled
	double headers_deadline;
	double request_deadline;

	// the stage that the request has reached
	HttpStage stage;

	// how the client left, when a recv () or a send () found that it was gone
	HttpAbortReason abort_reason;

	// a send () of the response failed, with its errno
	bool response_failed;
	int response_error;

	// the deadline of all the sends of the response, set by its first send,
	// each piece of a stream sets its own
	struct timespec send_deadline;
	bool send_deadline_set;

	// the request of an upgrade to a WebSocket endpoint, until its connection starts
	struct HttpWebSocketHandshake *web_socket;

	// the 101 was sent, the connection belongs to its WebSocket now
	bool upgraded;

	// the last state of the client that was found, a gone client never comes back
	HttpClientState client_state;

};

typedef struct _HttpReceive HttpReceive;

CERVER_PRIVATE HttpReceive *http_receive_new (void);

// prepares a receive that lives in the memory of its owner (a worker thread, a connection)
// with the resources that it lends: the data of each request lives in the arena
// and the token buffers are reused, they are emptied but never deleted
// the receive is ended with http_receive_delete (), that does not release it nor its resources
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_prepare (
	HttpReceive *http_receive,
	CerverReceive *cerver_receive, const HttpReceiveResources *resources
);

// creates a receive that allocates its own memory and the one of its request
CERVER_PRIVATE HttpReceive *http_receive_create (
	CerverReceive *cerver_receive
);

// ends the request of the receive (its uploads, its data, the arena that was lent)
// and starts a new one in the same connection: the parser, the clock, the deadlines,
// the stage and the counters of the request start again
// and what belongs to the connection is kept
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_reset (
	HttpReceive *http_receive
);

// ends the request of the receive, a receive that was created is released
CERVER_PRIVATE void http_receive_delete (
	HttpReceive *http_receive
);

// reads, without waiting, the data that the client already sent
// up to HTTP_RECEIVE_DRAIN_SIZE bytes, so a close () does not answer with a RST
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_receive_drain_available (const int sock_fd);

// returns true while the request's headers have not been completed
CERVER_PRIVATE bool http_receive_is_receiving_headers (
	const HttpReceive *http_receive
);

// a request that did not arrive in time is answered with 408 and closed,
// if the client did not send anything (received is false), it is only closed
CERVER_PRIVATE void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
);

CERVER_EXPORT const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
);

CERVER_EXPORT const int http_receive_get_sock_fd (
	const HttpReceive *http_receive
);

CERVER_EXPORT const HttpCerver *http_receive_get_cerver (
	const HttpReceive *http_receive
);

// the endpoint that handles the request, NULL if no endpoint matched
CERVER_EXPORT const HttpEndpoint *http_receive_get_endpoint (
	const HttpReceive *http_receive
);

// the custom data of the endpoint that handles the request
// (its own, the one that it inherits from its groups or the one of its first API route)
CERVER_EXPORT const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
);

// the custom data of the HTTP cerver that received the request
CERVER_EXPORT const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
);

// true if the request is a HEAD handled by the GET handler,
// its responses are sent without their body
CERVER_EXPORT bool http_receive_is_head (
	const HttpReceive *http_receive
);

// the methods that the request's path accepts (the value of the Allow header),
// NULL if the path did not match any route
CERVER_EXPORT const char *http_receive_get_allow (
	const HttpReceive *http_receive
);

// the stage that the request has reached
CERVER_EXPORT HttpStage http_receive_get_stage (
	const HttpReceive *http_receive
);

// checks, without waiting and without reading its data, if the client is still connected:
// HTTP_CLIENT_STATE_CLOSED when it closed its side (an EOF),
// HTTP_CLIENT_STATE_RESET when its connection failed (a RST)
// a handler that takes long can call it to stop the work of a client that left
CERVER_EXPORT HttpClientState http_receive_get_client_state (
	const HttpReceive *http_receive
);

// true if the client reset its connection, or if it closed its side
// and the cerver takes it as gone (http_cerver_set_half_close_is_gone ())
// a stream whose write fails is also gone, its handler must stop writing
CERVER_EXPORT bool http_receive_is_client_gone (
	const HttpReceive *http_receive
);

// the client left while the request was being received:
// error is 0 for an EOF, or the errno of the recv () or the send () that failed
CERVER_PRIVATE void http_receive_client_left (
	HttpReceive *http_receive, const int error
);

// marks that a send () of the response failed with error,
// the request is counted once as a failed response
CERVER_PRIVATE void http_receive_response_failed (
	const HttpReceive *http_receive, const int error
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
