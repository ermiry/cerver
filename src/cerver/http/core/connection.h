/**
 * @file http/core/connection.h
 * @internal
 * @brief The HTTP connection of an event loop: its state machine, what it lends to its receive and the job that a
 * thread of the pool runs for it. Never installed.
 */

#ifndef _CERVER_HTTP_CORE_CONNECTION_H_
#define _CERVER_HTTP_CORE_CONNECTION_H_

#include <stdbool.h>
#include <stddef.h>

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

/** @internal @brief The bytes that the buffers of a connection (the pending bytes, the output) start with. */
#define HTTP_CONNECTION_BUFFER_SIZE					(1 << 10)

/** @internal @brief The most bytes that a connection keeps of what follows a paused request. */
#define HTTP_CONNECTION_PENDING_MAX_SIZE			(1 << 16)

/** @internal @brief The most bytes of the short responses of cerver that a connection keeps to be written. */
#define HTTP_CONNECTION_OUTPUT_MAX_SIZE				(1 << 16)

/** @internal @brief The milliseconds in a second, the timeouts of a connection are deadlines in milliseconds. */
#define HTTP_CONNECTION_MILLISECONDS_PER_SECOND		1000

/**
 * @internal
 * @brief The most misses in a row that a connection counts: a connection with them is followed once in
 * 2^6 requests.
 */
#define HTTP_CONNECTION_FOLLOW_MISSES				6

/** @internal @brief The bytes of the buffer of a thread that follows its connection, the ones that it reads at once. */
#define HTTP_CONNECTION_FOLLOW_BUFFER_SIZE			(1 << 12)

/**
 * @internal
 * @brief Where a connection of an event loop is. Only the thread of its loop changes it.
 */
typedef enum HttpConnectionState {

	/** @internal @brief Not in use. */
	HTTP_CONNECTION_STATE_NONE				= 0,
	/** @internal @brief The loop reads its request: its headers or its body, its receive tells which. */
	HTTP_CONNECTION_STATE_READ				= 1,
	/** @internal @brief A thread of the pool authorizes it before its body is read; the pool owns it. */
	HTTP_CONNECTION_STATE_AUTH				= 2,
	/** @internal @brief Its job waits in the loop for a place in the pool. */
	HTTP_CONNECTION_STATE_PARKED			= 3,
	/** @internal @brief A thread of the pool handles its request, and ends it or gives it back to be kept; the pool owns it. */
	HTTP_CONNECTION_STATE_DISPATCHED		= 4,
	/** @internal @brief The loop writes what is left of a short response of cerver. */
	HTTP_CONNECTION_STATE_WRITE				= 5,
	/** @internal @brief Its writes were shut down, the loop reads what the client still sends before closing it. */
	HTTP_CONNECTION_STATE_CLOSING			= 6,
	/** @internal @brief It was kept after a request and its client has not sent the next one. */
	HTTP_CONNECTION_STATE_IDLE				= 7

} HttpConnectionState;

/** @internal @brief What a thread of the pool does for a connection. */
typedef enum HttpConnectionJob {

	/** @internal @brief Nothing. */
	HTTP_CONNECTION_JOB_NONE				= 0,
	/** @internal @brief The second part of its headers, before its body is read. */
	HTTP_CONNECTION_JOB_AUTHORIZE			= 1,
	/** @internal @brief Its complete request, and the end of its connection when it is not kept. */
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
	/** @internal @brief Its request was handled and its connection is kept: the loop starts its next request. */
	HTTP_CONNECTION_OUTCOME_KEPT			= 4,
	/** @internal @brief The thread that followed it read a part of its next request: the loop reads the rest. */
	HTTP_CONNECTION_OUTCOME_READING			= 5,
	/**
	 * @internal
	 * @brief The thread that followed it parsed its next request up to where it needs another job: its
	 * authentication before its body, or a complete request that the thread does not handle. The loop submits it.
	 */
	HTTP_CONNECTION_OUTCOME_PAUSED			= 6,
	/** @internal @brief The thread that followed it could not parse its next request, or it was rejected and answered: the loop closes it. */
	HTTP_CONNECTION_OUTCOME_FAILED			= 7

} HttpConnectionOutcome;

/** @internal @brief What the loop does with a connection once its output was written. */
typedef enum HttpConnectionNext {

	/** @internal @brief Nothing. */
	HTTP_CONNECTION_NEXT_NONE				= 0,
	/** @internal @brief Close it: its request was complete. */
	HTTP_CONNECTION_NEXT_CLOSE				= 1,
	/** @internal @brief Close it after reading what the client still sends: its request was rejected. */
	HTTP_CONNECTION_NEXT_LINGER				= 2,
	/** @internal @brief Keep reading its request (a "100 Continue" was written). */
	HTTP_CONNECTION_NEXT_READ				= 3,
	/** @internal @brief Keep it for its next request: the loop answered its complete request. */
	HTTP_CONNECTION_NEXT_KEEP				= 4

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

/**
 * @internal
 * @brief A connection of an event loop with the request that it is receiving.
 *
 * One thread owns it at a time: its loop, or the thread of the pool that runs its job (the states
 * #HTTP_CONNECTION_STATE_AUTH and #HTTP_CONNECTION_STATE_DISPATCHED). It goes to the pool through the queue of
 * the pool and it comes back through the queue of completed connections of its loop, both with a mutex, so the
 * two threads never use it at the same time.
 *
 * A connection handles one request at a time. When the cerver keeps it (HttpReceive::keep_alive) the thread
 * that answered its request starts the next one (http_receive_reset ()): the bytes that followed the request
 * that ended are a pipelined request, and the connection goes back through the queue; without them the
 * connection waits in #HTTP_CONNECTION_STATE_IDLE, and the thread of the pool gives it to its loop by itself
 * (event_loop_idle_enter ()): it adds it to the idle connections of the loop and arms its socket, with the mutex
 * of that list and without waking the loop up. The loop owns an idle connection again when it takes it out of
 * that list (event_loop_idle_claim ()), with the same mutex.
 */
struct HttpConnection {

	/** @internal @brief The loop of the connection. */
	struct EventLoop *loop;

	/** @internal @brief Where the connection is. */
	HttpConnectionState state;

	/** @internal @brief Its socket in the poller of the loop, registered only while the loop waits for it. */
	EventSource source;

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

	/** @internal @brief The bytes that followed a paused request in the buffer of the loop. */
	HttpConnectionBuffer pending;

	/** @internal @brief What is left to write of the short responses of cerver. */
	HttpConnectionBuffer output;
	/** @internal @brief A write of the loop failed, the connection is closed. */
	bool output_failed;
	/** @internal @brief What follows when the output was written. */
	HttpConnectionNext next;

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

	/** @internal @brief The next connection of the list where it is: the completed ones, the parked ones or the recycled ones. */
	struct HttpConnection *list_next;

	/** @internal @brief The connection is one of the ones that its loop can end. */
	bool attached;
	/** @internal @brief The next connection that its loop can end. */
	struct HttpConnection *active_next;
	/** @internal @brief The previous connection that its loop can end. */
	struct HttpConnection *active_previous;

	/** @internal @brief The connection is in the idle ones of its loop; with the mutex of that list, like the three fields that follow. */
	bool idle;
	/** @internal @brief The connection that became idle after it. */
	struct HttpConnection *idle_next;
	/** @internal @brief The connection that became idle before it. */
	struct HttpConnection *idle_previous;
	/** @internal @brief When the time that it can wait idle ends, in the milliseconds of the clock of its loop. */
	u64 idle_deadline;

	/**
	 * @internal
	 * @brief The times in a row that the thread that followed it found no request in time, up to
	 * #HTTP_CONNECTION_FOLLOW_MISSES; a request that arrives in time clears it. Of the thread that owns it.
	 */
	unsigned int follow_misses;
	/** @internal @brief The requests that are not followed before the next try, 2^follow_misses - 1 after a miss. */
	unsigned int follow_skip;

};

/** @internal @brief A connection of an event loop, see struct HttpConnection. */
typedef struct HttpConnection HttpConnection;

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
 * @brief Starts the connection of a socket that a loop accepted: it prepares its receive and reads what the client
 * already sent, so a request that arrived with its connection never waits for the poller.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in] peer the connection of the cerver, with a non blocking socket; the connection owns it from now on
 * @return 0 on success, 1 on error (the peer was dropped)
 */
CERVER_HIDDEN unsigned int http_connection_accepted (struct EventLoop *loop, Connection *peer);

/**
 * @internal
 * @brief Handles a connection that its poller reported.
 *
 * @param[in,out] connection the connection
 * @param[in] ready EVENT_READY_* flags
 */
CERVER_HIDDEN void http_connection_ready (HttpConnection *connection, const unsigned int ready);

/**
 * @internal
 * @brief Handles the timer of a connection that expired: its deadline is checked, a later one arms it again.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_connection_timeout (HttpConnection *connection);

/**
 * @internal
 * @brief Handles a connection that a thread of the pool gave back to its loop.
 *
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void http_connection_completed (HttpConnection *connection);

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
 * @brief Closes at once a connection that its loop took out of its idle ones, to give its place to a new
 * connection when the cerver is at its limit of connections; nothing is done with a connection that is not idle.
 *
 * @param[in,out] connection the connection, owned by its loop
 */
CERVER_HIDDEN void http_connection_evict (HttpConnection *connection);

/**
 * @internal
 * @brief Closes a connection that its loop took out of its idle ones because its time ended: its writes are
 * shut down and it ends when its client closes it too, so a request that its client was sending is not
 * answered with a reset.
 *
 * @param[in,out] connection the connection, owned by its loop
 */
CERVER_HIDDEN void http_connection_idle_expired (HttpConnection *connection);

/**
 * @internal
 * @brief Returns whether the caller is in the loop of the connection and it owns it, so a send can not wait.
 *
 * @param[in] connection the connection
 * @return false while a thread of the pool runs its job
 */
CERVER_HIDDEN bool http_connection_is_loop_owned (const HttpConnection *connection);

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
 * @brief Sends the parts of a response from the loop without waiting: what the socket does not take is kept and
 * written when the socket can be written again.
 *
 * @param[in,out] connection the connection, owned by its loop
 * @param[in] parts the parts
 * @param[in] n_parts the number of parts
 * @param[out] sent the bytes that were sent or kept
 * @param[out] error the errno of a send that failed
 * @return 0 on success, 1 on error
 */
CERVER_HIDDEN u8 http_connection_send (
	HttpConnection *connection, const struct iovec *parts, const int n_parts,
	size_t *sent, int *error
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
