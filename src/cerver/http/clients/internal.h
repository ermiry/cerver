/**
 * @internal
 * @file cerver/http/clients/internal.h
 * @brief What the files of the HTTP client share: a client, its requests, their responses and its connections,
 * that the event loops drive as sources.
 */

#ifndef _CERVER_HTTP_CLIENTS_INTERNAL_H_
#define _CERVER_HTTP_CLIENTS_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include <pthread.h>

#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/events.h"
#include "cerver/events/internal.h"

#include "cerver/http/clients/client.h"
#include "cerver/http/core/http.h"
#include "cerver/http/parser/parser.h"
#include "cerver/http/requests/method.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @defgroup http_clients_internal Client internals
 * @ingroup http_clients
 * @brief The structures of the HTTP client and what its files share.
 *
 * A request and a connection are sources of an event loop (struct EventSource): only the thread of their loop
 * uses them from the moment that the request is given to it until its handler was called.
 * @{
 */

/** @internal @brief The bytes of the host of a request, with its end. */
#define HTTP_CLIENT_HOST_SIZE					256

/** @internal @brief The bytes of the destination of a request, its host and its port (`host:65535`), with its end. */
#define HTTP_CLIENT_DESTINATION_SIZE			(HTTP_CLIENT_HOST_SIZE + 8)

/** @internal @brief The fields that the head of a response has at most. */
#define HTTP_CLIENT_FIELDS						CERVER_HTTP_PARSER_DEFAULT_FIELDS

/** @internal @brief The bytes that the head of a response has at most. */
#define HTTP_CLIENT_HEAD_SIZE					(1 << 16)

/** @internal @brief The first bytes that the buffer of the head or of the body of a response takes. */
#define HTTP_CLIENT_BUFFER_SIZE					(1 << 12)

/** @internal @brief The addresses of a host that a request tries at most, one after the other. */
#define HTTP_CLIENT_ADDRESSES					4

/** @internal @brief An address of the host of a request. */
typedef struct HttpClientAddress {

	/** @internal @brief The address, with its port. */
	struct sockaddr_storage address;
	/** @internal @brief The bytes of #address. */
	socklen_t length;

} HttpClientAddress;

/** @internal @brief A connection of a client, see struct HttpClientConnection. */
typedef struct HttpClientConnection HttpClientConnection;

/**
 * @internal
 * @brief The connections of a client that an event loop keeps for its next requests, and the message that
 * closes them when their client is deleted.
 */
typedef struct HttpClientPool {

	/** @internal @brief Gives the pool to its loop to close its connections; the first member. */
	EventSource source;
	/** @internal @brief The client. */
	struct _CerverHttpClient *client;
	/** @internal @brief The loop of the pool, set by the first request that it handles. */
	EventLoop *loop;
	/** @internal @brief The connection that was kept last. */
	HttpClientConnection *idle;
	/** @internal @brief The connections in #idle. */
	unsigned int n_idle;
	/**
	 * @internal
	 * @brief Whether the pool was given to its loop to be closed, a value of HttpClientPoolState; atomic: the
	 * thread that deletes the client waits until the loop does not use the pool anymore.
	 */
	unsigned int state;

} HttpClientPool;

/** @internal @brief What the end of its client did to a pool. */
typedef enum HttpClientPoolState {

	/** @internal @brief Its client goes on. */
	HTTP_CLIENT_POOL_STATE_OPEN			= 0,
	/** @internal @brief It was given to its loop, that closes the connections that it keeps. */
	HTTP_CLIENT_POOL_STATE_CLOSING		= 1,
	/** @internal @brief Its loop closed its connections and does not use it anymore. */
	HTTP_CLIENT_POOL_STATE_CLOSED		= 2

} HttpClientPoolState;

/** @internal @brief Where a request is. */
typedef enum HttpClientState {

	/** @internal @brief It has not been sent. */
	HTTP_CLIENT_STATE_NONE				= 0,
	/** @internal @brief It is in the queue of its loop. */
	HTTP_CLIENT_STATE_GIVEN				= 1,
	/** @internal @brief A thread looks for the address of its host. */
	HTTP_CLIENT_STATE_RESOLVE			= 2,
	/** @internal @brief Its connection is connecting. */
	HTTP_CLIENT_STATE_CONNECT			= 3,
	/** @internal @brief Its bytes are being sent and its response is being read. */
	HTTP_CLIENT_STATE_EXCHANGE			= 4,
	/** @internal @brief It ended: its response is complete or it has an error. */
	HTTP_CLIENT_STATE_DONE				= 5

} HttpClientState;

/**
 * @internal
 * @brief The response of a request, in the memory of its request.
 */
struct _CerverHttpClientResponse {

	/** @internal @brief Why the request failed, #CERVER_HTTP_CLIENT_ERROR_NONE for a complete response. */
	CerverHttpClientError error;
	/** @internal @brief The errno of the system call that failed, or the code of getaddrinfo (); 0 without one. */
	int error_code;

	/** @internal @brief The parser of the response. */
	CerverHttpParser parser;
	/** @internal @brief The fields of the head of the response. */
	CerverHttpField fields[HTTP_CLIENT_FIELDS];

	/** @internal @brief The bytes of the head, that its spans point to; the values of its fields end with a '\0'. */
	char *head;
	/** @internal @brief The bytes in #head. */
	size_t head_length;
	/** @internal @brief The bytes that #head takes. */
	size_t head_capacity;
	/** @internal @brief The head is complete: its status and its fields can be read. */
	bool head_complete;

	/** @internal @brief The body, without its transfer coding; it ends with a '\0'. */
	char *body;
	/** @internal @brief The bytes in #body, without its end. */
	size_t body_length;
	/** @internal @brief The bytes that #body takes. */
	size_t body_capacity;

	/** @internal @brief Bytes of the response were read: a request that failed before them can be sent again. */
	bool started;
	/** @internal @brief The message is complete. */
	bool complete;
	/** @internal @brief Bytes arrived after the end of the message: nobody asked for them, its connection is not kept. */
	bool exceeded;
	/** @internal @brief Its server ended the connection. */
	bool ended;

};

/**
 * @internal
 * @brief A request of a client.
 */
struct _CerverHttpClientRequest {

	/** @internal @brief Gives the request to its loop, and its timer; the first member. */
	EventSource source;
	/** @internal @brief The client. */
	struct _CerverHttpClient *client;

	/** @internal @brief The method. */
	CerverHttpMethod method;
	/** @internal @brief The host of its url, without the brackets of an IPv6. */
	char host[HTTP_CLIENT_HOST_SIZE];
	/** @internal @brief Its host came in brackets: it is written like that in its Host header. */
	bool ipv6;
	/** @internal @brief The port of its url. */
	u16 port;
	/** @internal @brief Its host and its port: the requests of the same destination share the connections that are kept. */
	char destination[HTTP_CLIENT_DESTINATION_SIZE];
	/** @internal @brief The addresses of its host, in the order that they are tried. */
	HttpClientAddress addresses[HTTP_CLIENT_ADDRESSES];
	/** @internal @brief The addresses in #addresses, 0 until they are known. */
	unsigned int n_addresses;
	/** @internal @brief The address that its next connection connects to: the next one after each one that failed. */
	unsigned int next_address;
	/** @internal @brief What getaddrinfo () returned for its host, 0 when it found its addresses. */
	int resolve_code;

	/** @internal @brief The target of its request line: the path and the query of its url. */
	char *target;
	/** @internal @brief The lines of the headers that were added, each one with its line break. */
	char *headers;
	/** @internal @brief The bytes in #headers. */
	size_t headers_length;
	/** @internal @brief The bytes that #headers takes. */
	size_t headers_capacity;
	/** @internal @brief The value of its Content-Type, a copy; NULL without one. */
	char *content_type;
	/** @internal @brief The body of the request, a copy; NULL without one. */
	char *body;
	/** @internal @brief The bytes of #body. */
	size_t body_length;
	/** @internal @brief The request has a body, that can be empty: its Content-Length is sent. */
	bool has_body;

	/** @internal @brief The bytes that are sent: the head and, after it, the body; written when the request is sent. */
	char *output;
	/** @internal @brief The bytes in #output. */
	size_t output_length;
	/** @internal @brief The bytes of #output that were sent. */
	size_t sent;

	/** @internal @brief The milliseconds that the request has, 0 for the ones of its client. */
	u64 timeout;

	/** @internal @brief Called with the response; NULL for a request that its caller waits for. */
	CerverHttpClientHandler handler;
	/** @internal @brief Passed to #handler. */
	void *data;

	/** @internal @brief Its caller waits for it (cerver_http_client_request_perform ()). */
	bool blocking;
	/** @internal @brief Protects #finished. */
	pthread_mutex_t mutex;
	/** @internal @brief Told when the request is finished. */
	pthread_cond_t condition;
	/** @internal @brief The loop does not use the request anymore. */
	bool finished;

	/** @internal @brief Where the request is. */
	HttpClientState state;
	/** @internal @brief Its connection, NULL without one. */
	HttpClientConnection *connection;
	/** @internal @brief It was sent again in a new connection: the one that it took had been closed by its server. */
	bool retried;
	/** @internal @brief Its handler was called, or its error was set for its caller. */
	bool completed;
	/** @internal @brief The milliseconds of the clock of its loop when its time ends, 0 without a limit. */
	u64 deadline;

	/** @internal @brief Its response. */
	struct _CerverHttpClientResponse response;

};

/**
 * @internal
 * @brief A connection of a client to a destination.
 */
struct HttpClientConnection {

	/** @internal @brief Its socket and the timer of the time that it is kept; the first member. */
	EventSource source;
	/** @internal @brief The client. */
	struct _CerverHttpClient *client;
	/** @internal @brief The pool of its loop, NULL when its loop keeps no connections of its client. */
	HttpClientPool *pool;
	/** @internal @brief The destination that it is connected to. */
	char destination[HTTP_CLIENT_DESTINATION_SIZE];

	/** @internal @brief The request that it serves, NULL while it is kept. */
	struct _CerverHttpClientRequest *request;
	/** @internal @brief It served a request before the current one. */
	bool reused;
	/** @internal @brief It is connected. */
	bool connected;
	/** @internal @brief It is being closed: it waits for the end of the operations of its socket. */
	bool closing;

	/** @internal @brief The next connection that its pool keeps. */
	HttpClientConnection *next;
	/** @internal @brief The previous connection that its pool keeps. */
	HttpClientConnection *previous;
	/** @internal @brief It is in the list of its pool. */
	bool idle;

};

/**
 * @internal
 * @brief An HTTP client.
 */
struct _CerverHttpClient {

	/** @internal @brief The HTTP cerver whose event loops run its requests, NULL for a client with its own loop. */
	CerverHttp *http_cerver;

	/** @internal @brief Its own loop, NULL for the client of a cerver. */
	EventLoop *loop;
	/** @internal @brief The thread of #loop. */
	pthread_t thread;
	/** @internal @brief #thread was created. */
	bool started;

	/** @internal @brief The milliseconds that a connection has to connect. */
	u64 connect_timeout;
	/** @internal @brief The milliseconds that a request has, from the moment it is sent until its response is complete. */
	u64 request_timeout;
	/** @internal @brief The milliseconds that a connection is kept without a request. */
	u64 idle_timeout;
	/** @internal @brief The connections that each loop keeps for each destination, 0 keeps none. */
	unsigned int max_idle;
	/** @internal @brief The bytes that the body of a response has at most. */
	size_t max_response_size;

	/** @internal @brief The pools of its loops, each one in the position of its loop; atomic until it is set. */
	HttpClientPool *pools;
	/** @internal @brief The pools in #pools; atomic. */
	unsigned int n_pools;

	/** @internal @brief The client is being deleted: it takes no more requests; atomic. */
	bool deleting;
	/** @internal @brief The requests that were sent and have not finished; atomic. */
	unsigned int n_requests;
	/** @internal @brief Its connections, also the ones that are being closed; atomic. */
	unsigned int n_alive;
	/**
	 * @internal
	 * @brief The threads that look for the address of a host of its requests: each one uses the loop of its
	 * request, and the client, until it leaves; atomic.
	 */
	unsigned int n_resolving;
	/** @internal @brief The loop of the cerver that takes its next request from a thread that is not a loop; atomic. */
	unsigned int next_loop;

	/** @internal @brief Every connection that it created; atomic. */
	u64 n_connections;
	/** @internal @brief Every request that took a connection that was kept; atomic. */
	u64 n_reused;

};

/**
 * @internal
 * @brief Gives a request to an event loop of its client.
 *
 * @param[in,out] request the request, with its bytes ready
 * @retval 0 the request was given: its loop calls its handler, or wakes its caller up
 * @retval 1 the client is being deleted, or its cerver is not running
 */
CERVER_HIDDEN u8 http_client_request_give (struct _CerverHttpClientRequest *request);

/** @internal @brief The bytes of a response were read, and it needs more (http_client_response_read ()). */
#define HTTP_CLIENT_READ_MORE					0
/** @internal @brief The response is complete. */
#define HTTP_CLIENT_READ_COMPLETE				1
/** @internal @brief The response failed: its error says why. */
#define HTTP_CLIENT_READ_FAILED					2

/**
 * @internal
 * @brief Starts the response of a request: its parser, that takes no body for the response of a HEAD.
 *
 * @param[in,out] request the request
 * @note A request that is sent starts its response by itself; the tests and the fuzzers that read a response
 * without a connection call it first.
 */
CERVER_PRIVATE void http_client_response_start (struct _CerverHttpClientRequest *request);

/**
 * @internal
 * @brief Reads bytes of the response of a request, like the ones that its connection receives: the head (also
 * the informational ones before it) and the body, with the limits of its client.
 *
 * @param[in,out] request the request, after http_client_response_start ()
 * @param[in] data the bytes
 * @param[in] length how many
 * @retval HTTP_CLIENT_READ_MORE the response needs more bytes
 * @retval HTTP_CLIENT_READ_COMPLETE the response is complete
 * @retval HTTP_CLIENT_READ_FAILED the response is not valid, it is over a limit or there is no memory: its error
 * was set
 * @note For the tests and the fuzzers: a request that is sent reads its response from its connection.
 */
CERVER_PRIVATE unsigned int http_client_response_read (
	struct _CerverHttpClientRequest *request, const char *data, const size_t length
);

/**
 * @internal
 * @brief Releases a request and what it has.
 *
 * @param[in] request the request, that no loop uses
 */
CERVER_HIDDEN void http_client_request_free (struct _CerverHttpClientRequest *request);

/**
 * @internal
 * @brief The methods of a request as a source of its loop.
 */
CERVER_HIDDEN const EventSourceMethods http_client_request_methods;

/**
 * @internal
 * @brief The methods of a connection as a source of its loop.
 */
CERVER_HIDDEN const EventSourceMethods http_client_connection_methods;

/**
 * @internal
 * @brief The methods of the pool of a loop as a source: it closes the connections that it keeps.
 */
CERVER_HIDDEN const EventSourceMethods http_client_pool_methods;

/**
 * @internal
 * @brief The event loops of an HTTP cerver exist: its clients can send their requests.
 *
 * @param[in,out] http_cerver the HTTP cerver, it can be NULL
 */
CERVER_HIDDEN void http_clients_open (CerverHttp *http_cerver);

/**
 * @internal
 * @brief The event loops of an HTTP cerver are about to be released: its clients send no more requests, and the
 * threads that were sending one have left the loops when this returns.
 *
 * @param[in,out] http_cerver the HTTP cerver, it can be NULL
 */
CERVER_HIDDEN void http_clients_close (CerverHttp *http_cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
