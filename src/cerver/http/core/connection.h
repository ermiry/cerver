/**
 * @file http/core/connection.h
 * @internal
 * @brief The HTTP connection of an event loop: its state machine, what it lends to its receive, the output that
 * its loop sends and the job that a thread of the pool runs for it. Never installed.
 */

#ifndef _CERVER_HTTP_CORE_CONNECTION_H_
#define _CERVER_HTTP_CORE_CONNECTION_H_

#include <stdbool.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/events/internal.h"
#include "cerver/handler/handler.h"

#include "cerver/http/core/handler.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;
struct EventLoop;

/** @internal @brief The bytes that the buffer of the pending bytes of a connection starts with. */
#define HTTP_CONNECTION_BUFFER_SIZE					(1 << 10)

/** @internal @brief The most bytes that a connection keeps of what follows a paused request. */
#define HTTP_CONNECTION_PENDING_MAX_SIZE			(1 << 16)

/**
 * @internal
 * @brief The bytes of its responses that a connection keeps to be sent before the thread of a pool that writes
 * them has to wait for its client: a handler that writes faster than its client reads does not fill the memory.
 */
#define HTTP_CONNECTION_OUTPUT_LIMIT				(1 << 20)

/** @internal @brief The bytes to be sent that a thread that waited for the client of its connection goes on with. */
#define HTTP_CONNECTION_OUTPUT_RESUME				(1 << 19)

/**
 * @internal
 * @brief The requests of a pipeline that a loop handles in a row, with their responses sent together, before it
 * handles its other connections.
 */
#define HTTP_CONNECTION_PIPELINE_BATCH				(1 << 5)

/**
 * @internal
 * @brief The bytes of the part of the output that a loop starts for the responses of a pipeline: the ones that
 * follow are written in the same part while they fit, and leave in one send.
 */
#define HTTP_CONNECTION_OUTPUT_BATCH_SIZE			(1 << 14)

/** @internal @brief The first millisecond of a loop that can be a deadline: 0 is the one of no deadline. */
#define HTTP_CONNECTION_DEADLINE_FIRST				1

/** @internal @brief The most bytes of a file that the loop gives to the socket of a connection each time. */
#define HTTP_CONNECTION_FILE_CHUNK					(1 << 20)

/** @internal @brief The milliseconds in a second, the timeouts of a connection are deadlines in milliseconds. */
#define HTTP_CONNECTION_MILLISECONDS_PER_SECOND		1000

/**
 * @internal
 * @brief Where a connection of an event loop is. Only the thread of its loop changes it.
 */
typedef enum HttpConnectionState {

	/** @internal @brief Not in use. */
	HTTP_CONNECTION_STATE_NONE				= 0,
	/** @internal @brief The loop reads its request: its headers or its body, its receive tells which. */
	HTTP_CONNECTION_STATE_READ				= 1,
	/** @internal @brief A thread of the pool authorizes it before its body is read; the pool owns its request. */
	HTTP_CONNECTION_STATE_AUTH				= 2,
	/** @internal @brief Its job waits in the loop for a place in the pool. */
	HTTP_CONNECTION_STATE_PARKED			= 3,
	/** @internal @brief A thread of the pool handles its request; the pool owns its request, the loop sends what the thread writes. */
	HTTP_CONNECTION_STATE_DISPATCHED		= 4,
	/** @internal @brief The loop sends what is left of a response, and then does what follows it. */
	HTTP_CONNECTION_STATE_WRITE				= 5,
	/** @internal @brief Its writes were shut down, the loop reads what the client still sends before closing it. */
	HTTP_CONNECTION_STATE_CLOSING			= 6,
	/** @internal @brief It was kept after a request and its client has not sent the next one. */
	HTTP_CONNECTION_STATE_IDLE				= 7,
	/** @internal @brief It ended, and the backend of its loop still ends its operations: its socket is closed after them. */
	HTTP_CONNECTION_STATE_ENDING			= 8

} HttpConnectionState;

/** @internal @brief What a thread of the pool does for a connection. */
typedef enum HttpConnectionJob {

	/** @internal @brief Nothing. */
	HTTP_CONNECTION_JOB_NONE				= 0,
	/** @internal @brief The second part of its headers, before its body is read. */
	HTTP_CONNECTION_JOB_AUTHORIZE			= 1,
	/** @internal @brief Its complete request. */
	HTTP_CONNECTION_JOB_DISPATCH			= 2

} HttpConnectionJob;

/** @internal @brief How the job of a connection ended, written by the thread that ran it before it gives it back. */
typedef enum HttpConnectionOutcome {

	/** @internal @brief The job has not ended. */
	HTTP_CONNECTION_OUTCOME_NONE			= 0,
	/** @internal @brief It was authorized: the loop reads its body. */
	HTTP_CONNECTION_OUTCOME_RESUME			= 1,
	/** @internal @brief It was not authorized and it was answered, or its client is gone: the loop closes it. */
	HTTP_CONNECTION_OUTCOME_REJECTED		= 2,
	/** @internal @brief The pool dropped the job without running it: the loop answers 503. */
	HTTP_CONNECTION_OUTCOME_DROPPED			= 3,
	/**
	 * @internal
	 * @brief Its request was handled: the loop sends what is left of its response, ends the request, and keeps or
	 * closes the connection.
	 */
	HTTP_CONNECTION_OUTCOME_ANSWERED		= 4,
	/**
	 * @internal
	 * @brief The loop ended its request and gave the connection to itself: its next request is the bytes that
	 * followed, in the next step of the loop.
	 */
	HTTP_CONNECTION_OUTCOME_KEPT			= 5

} HttpConnectionOutcome;

/** @internal @brief What the loop does with a connection once its output was sent. */
typedef enum HttpConnectionNext {

	/** @internal @brief Nothing. */
	HTTP_CONNECTION_NEXT_NONE				= 0,
	/** @internal @brief Close it: its request was complete. */
	HTTP_CONNECTION_NEXT_CLOSE				= 1,
	/** @internal @brief Close it after reading what the client still sends: its request was rejected. */
	HTTP_CONNECTION_NEXT_LINGER				= 2,
	/** @internal @brief Keep reading its request (a "100 Continue" was written). */
	HTTP_CONNECTION_NEXT_READ				= 3,
	/** @internal @brief Keep it for its next request: its complete request was answered. */
	HTTP_CONNECTION_NEXT_KEEP				= 4,
	/**
	 * @internal
	 * @brief Give the job of its request (HttpConnection::job) to its pool: the responses of the requests of
	 * its pipeline that the loop handled before it were written, and they leave first.
	 */
	HTTP_CONNECTION_NEXT_JOB				= 5

} HttpConnectionNext;

/**
 * @internal
 * @brief A buffer of bytes of a connection that grows up to a limit and keeps its memory between requests.
 */
typedef struct HttpConnectionBuffer {

	/** @internal @brief The bytes. */
	char *data;
	/** @internal @brief The bytes in use. */
	size_t length;
	/** @internal @brief The bytes that were already consumed from the start. */
	size_t offset;
	/** @internal @brief The bytes allocated. */
	size_t size;

} HttpConnectionBuffer;

struct HttpConnection;

/**
 * @internal
 * @brief A part of the output of a connection: bytes of a response, followed by the bytes of a file when it has
 * one. Its bytes are in the same memory, after it.
 *
 * The thread that writes a response creates it and gives it to the loop of its connection, that sends it and
 * releases it: only one thread uses it at a time.
 */
typedef struct HttpConnectionOutput {

	/** @internal @brief Its node in the queue of its loop, when a thread of a pool gives it. */
	EventNode node;

	/** @internal @brief The part that is sent after it. */
	struct HttpConnectionOutput *next;
	/** @internal @brief Its connection. */
	struct HttpConnection *connection;

	/** @internal @brief The capacity of the memory that holds it, to give it back. */
	size_t capacity;
	/** @internal @brief Its bytes. */
	size_t length;
	/** @internal @brief The bytes that were sent. */
	size_t offset;

	/** @internal @brief The file whose bytes are sent after its own, that it owns; -1 without one. */
	int file;
	/** @internal @brief The position of the next byte of the file. */
	off_t file_offset;
	/** @internal @brief The bytes of the file that are left to send. */
	size_t file_length;

} HttpConnectionOutput;

/**
 * @internal
 * @brief A connection of an event loop with the request that it is receiving.
 *
 * Its loop owns the connection and its socket: only the loop reads, sends and closes. The thread of a pool that
 * runs its job (the states #HTTP_CONNECTION_STATE_AUTH and #HTTP_CONNECTION_STATE_DISPATCHED) owns its request
 * (HttpConnection::receive, its arena, its pending bytes): the loop does not use them until the thread gives the
 * connection back through the queue of the loop. What the thread writes of a response goes to the loop through
 * the same queue, as parts of its output, and the loop sends them while the handler runs.
 *
 * A connection handles one request at a time, and a request ends when its response was sent: then the loop logs
 * it, releases its data and, when the cerver keeps the connection (HttpReceive::keep_alive), starts the next one
 * with the bytes that followed it (a pipelined request) or waits for it in #HTTP_CONNECTION_STATE_IDLE.
 *
 * The requests of a pipeline that the loop handles by itself (the ones of the pool of the event loops) are the
 * exception: each one ends when its response is written, the loop goes on with the next one, and their responses
 * leave in one send. A send that fails then is counted once, in the request that the connection has at that time.
 */
struct HttpConnection {

	/** @internal @brief The loop of the connection. */
	struct EventLoop *loop;

	/** @internal @brief Where the connection is. */
	HttpConnectionState state;

	/** @internal @brief Its socket in the backend of its loop. */
	EventHandle handle;

	/** @internal @brief Its timer in the wheel of the loop. */
	EventTimer timer;
	/** @internal @brief The deadline that applies now, in the milliseconds of the loop; its timer can be armed earlier. */
	u64 deadline;
	/** @internal @brief The deadline of its request and of its headers, from the start of the request. */
	u64 request_deadline;
	/** @internal @brief The deadline of its headers. */
	u64 headers_deadline;

	/** @internal @brief The connection of the cerver: its socket, its address and its stats. */
	Connection *peer;

	/** @internal @brief The cerver receive that its receive and the WebSocket use, never deleted by itself. */
	CerverReceive cerver_receive;

	/** @internal @brief Its receive, used by all its requests. */
	HttpReceive receive;

	/** @internal @brief The arena of the data of its request. */
	struct Arena *arena;
	/** @internal @brief The buffer of the head of a request that does not arrive in one piece. */
	String *head_buffer;

	/** @internal @brief The bytes that followed a paused request in the buffer of the backend. */
	HttpConnectionBuffer pending;

	/** @internal @brief The first part of its output that has bytes to send; of its loop. */
	HttpConnectionOutput *output_head;
	/** @internal @brief The last part of its output. */
	HttpConnectionOutput *output_tail;
	/** @internal @brief The bytes of its responses that were written and not sent; atomic: the thread that writes adds, its loop takes. */
	size_t output_queued;
	/** @internal @brief A send failed, nothing more is sent and the connection ends; atomic: its loop sets it, the thread that writes reads it. */
	bool output_failed;
	/** @internal @brief The errno of the send that failed; atomic, like #output_failed. */
	int output_error;
	/**
	 * @internal
	 * @brief The milliseconds that the sends of the response of its request still have: the send timeout when the
	 * request starts, less the time that its output has waited for its client since then.
	 */
	u64 output_budget;
	/** @internal @brief The milliseconds of the loop when its output started to wait for its client, while #output_waits. */
	u64 output_since;
	/** @internal @brief Its output waits for its client: that time is taken from #output_budget when it is sent. */
	bool output_waits;
	/**
	 * @internal
	 * @brief The bytes of its output that its client took since its request started: a response that fails
	 * after some of them failed in the response stage, one that fails without any, in the stage of its handler.
	 * Atomic: its loop adds, the thread that owns its request reads.
	 */
	size_t output_sent;
	/** @internal @brief What follows when the output was sent, in #HTTP_CONNECTION_STATE_WRITE. */
	HttpConnectionNext next;
	/**
	 * @internal
	 * @brief The requests of a pipeline that its loop handled in a row in this step, up to
	 * #HTTP_CONNECTION_PIPELINE_BATCH: their responses wait in its output and leave together.
	 */
	unsigned int batch;

	/** @internal @brief The bytes that were read from a connection that is closing. */
	size_t drained;

	/** @internal @brief The job that the pool runs, or that waits for a place while it is parked. */
	HttpConnectionJob job;
	/** @internal @brief How the job ended. */
	HttpConnectionOutcome outcome;

	/**
	 * @internal
	 * @brief The pool that runs its job, the one of the endpoint of its request (http/core/pool.h); NULL for a
	 * cerver without pools. It is chosen each time that a job is submitted.
	 */
	struct HttpPool *pool;
	/** @internal @brief The lane of its loop for that pool, the position of the pool in its cerver. */
	unsigned int lane;

	/** @internal @brief The connection is in the parked ones of its lane. */
	bool parked;
	/** @internal @brief When its job started to wait for a place in its pool, in the milliseconds of its loop. */
	u64 parked_since;

	/** @internal @brief Its node in the queue of its loop, when the thread that ran its job gives it back. */
	EventNode node;

	/** @internal @brief The next connection of the list where it is: the parked ones or the recycled ones. */
	struct HttpConnection *list_next;

	/** @internal @brief The connection is one of the ones that its loop owns. */
	bool attached;
	/** @internal @brief The next connection that its loop owns. */
	struct HttpConnection *active_next;
	/** @internal @brief The previous connection that its loop owns. */
	struct HttpConnection *active_previous;

	/** @internal @brief The connection is in the idle ones of its loop. */
	bool idle;
	/** @internal @brief The connection that became idle after it. */
	struct HttpConnection *idle_next;
	/** @internal @brief The connection that became idle before it. */
	struct HttpConnection *idle_previous;

};

/** @internal @brief A connection of an event loop, see struct HttpConnection. */
typedef struct HttpConnection HttpConnection;

/**
 * @internal
 * @brief The connection of a node of the queue of a loop.
 *
 * @param[in] node a node of type #EVENT_NODE_TYPE_CONNECTION
 * @return its connection
 */
static inline HttpConnection *http_connection_of_node (EventNode *node) {

	return (HttpConnection *) (void *) ((char *) node - offsetof (HttpConnection, node));

}

/**
 * @internal
 * @brief Allocates a connection with its arena and its buffers.
 *
 * @return the connection, NULL on error
 */
CERVER_HIDDEN HttpConnection *http_connection_new (void);

/**
 * @internal
 * @brief Releases a connection that is not in use with its arena and its buffers.
 *
 * @param[in] connection the connection, it can be NULL
 */
CERVER_HIDDEN void http_connection_delete (HttpConnection *connection);

/**
 * @internal
 * @brief Starts the connection of a socket that a loop accepted: it prepares its receive and asks for what the
 * client sent.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in] peer the connection of the cerver, with a non blocking socket; the connection owns it from now on
 * @return 0 on success, 1 on error (the peer was dropped)
 */
CERVER_HIDDEN unsigned int http_connection_accepted (struct EventLoop *loop, Connection *peer);

/**
 * @internal
 * @brief Handles an operation of the socket of a connection that completed in the backend of its loop.
 *
 * @param[in,out] connection the connection
 * @param[in] completion the completion
 */
CERVER_HIDDEN void http_connection_completion (HttpConnection *connection, const EventCompletion *completion);

/**
 * @internal
 * @brief Handles the timer of a connection that expired: its deadline is checked, a later one arms it again.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_connection_timeout (HttpConnection *connection);

/**
 * @internal
 * @brief Handles a connection that the thread that ran its job gave back to its loop.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_connection_completed (HttpConnection *connection);

/**
 * @internal
 * @brief Takes a part of the output of a connection that a thread of a pool gave to its loop, and sends it after
 * the ones before it.
 *
 * @param[in,out] node the node of the part, of type #EVENT_NODE_TYPE_OUTPUT
 */
CERVER_HIDDEN void http_connection_output_given (EventNode *node);

/**
 * @internal
 * @brief Gives the job of a parked connection to the pool.
 *
 * @param[in,out] connection the first parked connection of its loop, already out of that list
 * @return 0 when the pool took it, 1 when there is still no place (the caller keeps it parked)
 */
CERVER_HIDDEN unsigned int http_connection_unpark (HttpConnection *connection);

/**
 * @internal
 * @brief Ends a connection that the loop owns because its cerver stops: one that has not been answered gets a 503
 * when it already sent bytes of a request, and one that waits idle for its next request is closed once its
 * client closes it too.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_connection_abort (HttpConnection *connection);

/**
 * @internal
 * @brief Closes at once an idle connection of a loop, to give its place to a new connection when the cerver is
 * at its limit of connections; nothing is done with a connection that is not idle.
 *
 * @param[in,out] connection the connection, owned by its loop
 */
CERVER_HIDDEN void http_connection_evict (HttpConnection *connection);

/**
 * @internal
 * @brief Returns whether the caller is in the loop of the connection and it owns its request.
 *
 * @param[in] connection the connection
 * @return false while a thread of the pool runs its job
 */
CERVER_HIDDEN bool http_connection_is_loop_owned (const HttpConnection *connection);

/**
 * @internal
 * @brief Counts the response of the request of a connection as one that failed, once: the bytes of it that its
 * client took, without its status, and where its client left (the response stage when some of its bytes left,
 * the one of its handler when none did).
 *
 * @param[in,out] connection the connection, whose output failed
 * @threadsafety Called by the thread that owns its request: the one of a pool whose write was refused, or its
 * loop.
 */
CERVER_HIDDEN void http_connection_response_failed (HttpConnection *connection);

/**
 * @internal
 * @brief Answers 503 to a request whose headers are complete when the cerver would reject it once its body
 * arrives: the pool has no place and its policy does not wait for one, or the cerver is draining and rejects its
 * requests. The body of a request that will not be handled is not received.
 *
 * @param[in,out] connection the connection, owned by its loop
 * @return true when the request was refused and answered, the parser must stop
 */
CERVER_HIDDEN bool http_connection_refuse (HttpConnection *connection);

/**
 * @internal
 * @brief Takes the parts of a response to be sent by the loop of the connection, without waiting for its socket:
 * their bytes are copied, so they only have to be valid during the call.
 *
 * The thread of a pool that has written more than #HTTP_CONNECTION_OUTPUT_LIMIT bytes that its client has not
 * read waits here until the loop sent some, at most the send timeout of the cerver.
 *
 * @param[in,out] connection the connection
 * @param[in] parts the parts
 * @param[in] n_parts the number of parts
 * @param[out] sent the bytes that were taken
 * @param[out] error the errno when they were not: the one of the send that failed before, or ENOMEM
 * @return 0 on success, 1 on error
 * @threadsafety The thread that owns the request of the connection: its loop, or the thread of a pool that runs
 * its job.
 */
CERVER_HIDDEN u8 http_connection_send (
	HttpConnection *connection, const struct iovec *parts, const int n_parts,
	size_t *sent, int *error
);

/**
 * @internal
 * @brief Takes a part of a file to be sent by the loop of the connection after what was written before.
 *
 * @param[in,out] connection the connection
 * @param[in] file the fd of the file; the caller keeps it, the connection uses its own
 * @param[in] offset the position of its first byte
 * @param[in] length the bytes to send
 * @param[out] error the errno when it was not taken
 * @return 0 on success, 1 on error
 * @threadsafety The thread that owns the request of the connection.
 * @warning The loop reads the file as it sends it: a file that is not in the memory of the system keeps the
 * loop until it is read.
 */
CERVER_HIDDEN u8 http_connection_send_file (
	HttpConnection *connection, const int file, const off_t offset, const size_t length, int *error
);

/**
 * @internal
 * @brief The job of a connection, run by a thread of the pool (or by the dispatcher of a test): it authorizes or
 * handles its request and gives the connection back to its loop.
 *
 * @param[in] connection_ptr the connection
 */
CERVER_PRIVATE void http_connection_work (void *connection_ptr);

/**
 * @internal
 * @brief The job of a connection that the pool drops without running it: the connection goes back to its loop to
 * be answered with 503.
 *
 * @param[in] connection_ptr the connection
 */
CERVER_PRIVATE void http_connection_work_dropped (void *connection_ptr);

/**
 * @internal
 * @brief The name of a state, for the logs and the tests.
 *
 * @param[in] state the state
 * @return its name
 */
CERVER_PRIVATE const char *http_connection_state_string (const HttpConnectionState state);

#ifdef __cplusplus
}
#endif

#endif
