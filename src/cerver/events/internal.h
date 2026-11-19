/**
 * @file events/internal.h
 * @internal
 * @brief The event loops of a cerver: the timers, the loops and their group, never installed. Their I/O is in
 * events/backend.h.
 *
 * The methods that the tests use are exported (#CERVER_PRIVATE), the rest are hidden.
 */

#ifndef _CERVER_EVENTS_INTERNAL_H_
#define _CERVER_EVENTS_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include <pthread.h>

#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/events/backend.h"
#include "cerver/events/events.h"

#include "cerver/http/core/http.h"

#include "cerver/threads/thpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @defgroup events_wheel Timer wheel
 * @brief The deadlines of a loop: slots of #EVENT_WHEEL_TICK milliseconds, adding and removing a timer is O(1).
 * @{
 */

/** @internal @brief The milliseconds of a tick of the wheel: a timer never expires before its deadline and at most a tick after it. */
#define EVENT_WHEEL_TICK				(1u << 8)

/** @internal @brief The slots of the wheel; a deadline farther than a turn stays in its slot until its turn. */
#define EVENT_WHEEL_SLOTS				(1u << 8)

/** @internal @brief No timer is armed: nothing to wait for. */
#define EVENT_WHEEL_NEXT_NONE			(-1)

/**
 * @internal
 * @brief A timer, embedded in its owner; a zeroed timer is not armed.
 */
typedef struct EventTimer {

	/** @internal @brief The next timer of its slot. */
	struct EventTimer *next;
	/** @internal @brief The previous timer of its slot. */
	struct EventTimer *previous;
	/** @internal @brief When it expires, in the milliseconds of the clock of the wheel. */
	u64 deadline;
	/** @internal @brief The timer is in the wheel. */
	bool armed;
	/** @internal @brief The owner of the timer. */
	void *owner;
	/** @internal @brief The owner is a source of the loop (struct EventSource), not a connection. */
	bool source;

} EventTimer;

/** @internal @brief Called with each timer that expired, already removed from the wheel; it can arm it again. */
typedef void (*EventTimerExpired) (EventTimer *timer, void *args);

/** @internal @brief A wheel of timers. */
typedef struct EventWheel {

	/** @internal @brief The first timer of each slot. */
	EventTimer *slots[EVENT_WHEEL_SLOTS];
	/** @internal @brief The tick that was processed last. */
	u64 tick;
	/** @internal @brief The timers that are armed. */
	unsigned int armed;

} EventWheel;

/**
 * @internal
 * @brief Starts an empty wheel.
 *
 * @param[out] wheel the wheel
 * @param[in] now the current milliseconds of the clock
 */
CERVER_PRIVATE void event_wheel_init (EventWheel *wheel, const u64 now);

/**
 * @internal
 * @brief Arms a timer; one that is armed is moved to its new deadline.
 *
 * @param[in,out] wheel the wheel
 * @param[in,out] timer the timer
 * @param[in] deadline when it expires, in the milliseconds of the clock
 */
CERVER_PRIVATE void event_wheel_add (EventWheel *wheel, EventTimer *timer, const u64 deadline);

/**
 * @internal
 * @brief Disarms a timer, nothing is done when it is not armed.
 *
 * @param[in,out] wheel the wheel
 * @param[in,out] timer the timer
 */
CERVER_PRIVATE void event_wheel_remove (EventWheel *wheel, EventTimer *timer);

/**
 * @internal
 * @brief Moves the wheel to the current time and reports the timers whose deadline passed.
 *
 * @param[in,out] wheel the wheel
 * @param[in] now the current milliseconds of the clock
 * @param[in] expired called with each timer that expired
 * @param[in] args passed to expired
 * @return the timers that expired
 */
CERVER_PRIVATE unsigned int event_wheel_advance (
	EventWheel *wheel, const u64 now, const EventTimerExpired expired, void *args
);

/**
 * @internal
 * @brief The time until the wheel has to be moved again.
 *
 * @param[in] wheel the wheel
 * @param[in] now the current milliseconds of the clock
 * @return the milliseconds until its next tick, #EVENT_WHEEL_NEXT_NONE when no timer is armed
 */
CERVER_PRIVATE int event_wheel_next (const EventWheel *wheel, const u64 now);

/**
 * @internal
 * @brief The milliseconds of the monotonic clock, the default clock of a loop.
 *
 * @return the milliseconds
 */
CERVER_PRIVATE u64 event_clock_milliseconds (void);

/** @} */

/**
 * @internal
 * @defgroup events_queue Queue
 * @brief What the other threads give to a loop, without a lock: many threads add, only the loop takes.
 * @{
 */

/** @internal @brief What a node of the queue of a loop is. */
typedef enum EventNodeType {

	/** @internal @brief Not a node. */
	EVENT_NODE_TYPE_NONE				= 0,
	/** @internal @brief A connection that the thread that ran its job gives back. */
	EVENT_NODE_TYPE_CONNECTION			= 1,
	/** @internal @brief Bytes of a response that a thread of a pool wrote, for the loop to send them. */
	EVENT_NODE_TYPE_OUTPUT				= 2,
	/**
	 * @internal
	 * @brief A connection whose handler deferred its response in a thread of a pool: the loop bounds the wait of
	 * its request.
	 */
	EVENT_NODE_TYPE_DEFERRED			= 3,
	/** @internal @brief A source of the loop (struct EventSource) that another thread gives to it. */
	EVENT_NODE_TYPE_SOURCE				= 4

} EventNodeType;

/** @internal @brief A node of the queue of a loop, the first member of what is given. */
typedef struct EventNode {

	/** @internal @brief The node that was added before it; after it is taken, the one that was added after it. */
	struct EventNode *next;
	/** @internal @brief What the node is. */
	EventNodeType type;

} EventNode;

/**
 * @internal
 * @brief The queue of a loop.
 *
 * A thread adds a node at its head with one atomic exchange; the loop takes all of them with another one and
 * turns them around, so it handles them in the order that they were added.
 */
typedef struct EventQueue {

	/** @internal @brief The node that was added last; atomic. */
	EventNode *head;

} EventQueue;

/**
 * @internal
 * @brief Adds a node to a queue.
 *
 * @param[in,out] queue the queue
 * @param[in,out] node the node, that its thread does not use anymore
 * @threadsafety Safe from any thread.
 */
static inline void event_queue_push (EventQueue *queue, EventNode *node) {

	EventNode *head = __atomic_load_n (&queue->head, __ATOMIC_RELAXED);

	do {
		node->next = head;
	} while (!__atomic_compare_exchange_n (
		&queue->head, &head, node, true, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED
	));

}

/**
 * @internal
 * @brief Returns whether a queue has nodes.
 *
 * @param[in] queue the queue
 * @return true with nodes
 * @threadsafety Safe from any thread.
 */
static inline bool event_queue_has_nodes (const EventQueue *queue) {

	return __atomic_load_n (&queue->head, __ATOMIC_SEQ_CST) != NULL;

}

/**
 * @internal
 * @brief Takes every node of a queue.
 *
 * @param[in,out] queue the queue
 * @return the node that was added first, followed by the others in the order that they were added; NULL without
 * nodes
 * @threadsafety Only the thread of the loop of the queue.
 */
static inline EventNode *event_queue_take (EventQueue *queue) {

	EventNode *node = __atomic_exchange_n (&queue->head, NULL, __ATOMIC_SEQ_CST);

	EventNode *first = NULL;
	while (node) {
		EventNode *previous = node->next;

		node->next = first;
		first = node;

		node = previous;
	}

	return first;

}

/** @} */

/**
 * @internal
 * @defgroup events_loop Loop
 * @brief An I/O thread: it accepts connections, reads and parses their requests and sends their responses through
 * its backend, without waiting, and gives the work that can wait (a custom authentication, a handler) to a
 * dispatcher. Only the loop uses the socket of a connection.
 * @{
 */

/**
 * @internal
 * @brief The connections that a loop keeps without a place at the limit of its cerver, the ones that its backend
 * had already accepted; and the ones that it rejects at most each time that it has no fds.
 */
#define EVENT_LOOP_ACCEPT_BATCH				(1 << 6)

/**
 * @internal
 * @brief The milliseconds that a loop stops accepting when it can not accept: without fds or at its limit. It is
 * not a timer of the wheel: the loop waits in its backend until then at most, like THREADS waits after a failed accept.
 */
#define EVENT_LOOP_ACCEPT_BACKOFF			(1u << 4)

/** @internal @brief The connections without use that a loop keeps to be used again. */
#define EVENT_LOOP_RECYCLE_LIMIT			FREELIST_RECYCLE_LIMIT (1 << 10)

struct _Cerver;
struct _HttpReceive;
struct HttpConnection;
struct EventLoopGroup;

/** @internal @brief Gives the jobs of the connections to the threads that run them. */
typedef struct EventDispatcher EventDispatcher;

/**
 * @internal
 * @brief Gives the jobs of the connections to the threads that run them: the thpool of a cerver, or a method of
 * a test.
 *
 * A job that was taken ends with http_connection_work () or with http_connection_work_dropped (), once.
 */
struct EventDispatcher {

	/**
	 * @internal
	 * @brief Takes the job of a connection, without waiting.
	 *
	 * Returns 0 when it was taken (the caller does not use the connection anymore), 1 when there is no place.
	 */
	unsigned int (*submit) (EventDispatcher *dispatcher, struct HttpConnection *connection);

	/** @internal @brief Returns whether a submit would find no place, to not try it. */
	bool (*is_full) (const EventDispatcher *dispatcher);

	/** @internal @brief The data of the dispatcher. */
	void *data;

	/**
	 * @internal
	 * @brief Its jobs run inside submit, in the thread of the loop (event_dispatcher_inline ()): what a job writes
	 * never waits for its loop to send, the loop is the one that would wait.
	 */
	bool in_loop;

};

/** @internal @brief The lanes that a loop has at most: one for each pool of an HTTP cerver. */
#define EVENT_LOOP_LANES_SIZE				CERVER_HTTP_POOLS_SIZE

/** @internal @brief The lane of the default pool, the one that every loop has. */
#define EVENT_LOOP_LANE_DEFAULT				0

/**
 * @internal
 * @brief A lane of a loop: the dispatcher of a pool of its HTTP cerver (http/core/pool.h), and the connections of
 * the loop that wait for a place in it. A lane has the position of its pool in its cerver.
 *
 * Each lane has its own wait: a pool that is full keeps its requests in its lane, and the requests of the other
 * lanes are dispatched.
 */
typedef struct EventLane {

	/** @internal @brief Who runs the jobs of its connections, NULL for a lane that the loop does not have. */
	EventDispatcher *dispatcher;

	/** @internal @brief The first connection that waits for a place in its dispatcher. */
	struct HttpConnection *parked_head;
	/** @internal @brief The last one. */
	struct HttpConnection *parked_tail;
	/** @internal @brief The connections that wait, atomic: the threads of its pool read it to wake the loop up. */
	unsigned int n_parked;

} EventLane;

/**
 * @internal
 * @brief An event loop.
 *
 * Only its thread uses it, except its queue, its wake up, the wait of the threads that write faster than their
 * clients read (with its mutex), and the counters and flags that are atomic.
 */
typedef struct EventLoop {

	/** @internal @brief The cerver of the loop. */
	struct _Cerver *cerver;
	/** @internal @brief The group of the loop, NULL for a loop alone. */
	struct EventLoopGroup *group;
	/** @internal @brief The position of the loop in its group. */
	unsigned int id;

	/** @internal @brief The backend of its I/O: of the thread that runs its first step. */
	EventBackend *backend;

	/** @internal @brief The timers. */
	EventWheel wheel;
	/** @internal @brief The clock, in milliseconds; a test can set its own. */
	u64 (*clock) (void);
	/** @internal @brief The time of the current step. */
	u64 now;
	/**
	 * @internal
	 * @brief The time of the step was read after its wait: the first completion of a step reads it, because the
	 * loop can have slept longer than a deadline before it, and what a completion arms counts from now.
	 */
	bool now_read;

	/** @internal @brief Its lanes: the first one is the one of the default pool, the others are set with event_loop_lane_set (). */
	EventLane lanes[EVENT_LOOP_LANES_SIZE];
	/** @internal @brief The lanes in use: one more than the last lane with a dispatcher. */
	unsigned int n_lanes;

	/** @internal @brief The socket that accepts connections. */
	EventHandle listen;
	/** @internal @brief The fd of #listen, -1 without one. */
	int listen_fd;
	/** @internal @brief The loop closes #listen_fd when it stops accepting (the first loop uses the socket of its cerver). */
	bool listen_owned;
	/** @internal @brief The loop does not accept anymore: its listen fd is left when its accept ended in its backend. */
	bool listen_ended;
	/** @internal @brief When the loop accepts again after a pause, in the milliseconds of its clock; 0 while it accepts. */
	u64 accept_resume;

	/**
	 * @internal
	 * @brief The connections that its backend accepted when the cerver was at its limit: they wait here for a
	 * place, like the ones that wait in the socket that listens.
	 */
	int held[EVENT_LOOP_ACCEPT_BATCH];
	/** @internal @brief The connections in #held. */
	unsigned int n_held;

	/** @internal @brief What the threads of the pools give to the loop: the connections whose job ended and the bytes of their responses. */
	EventQueue queue;
	/**
	 * @internal
	 * @brief The loop is about to wait in its backend, or waits: a thread that adds to its queue, or that frees
	 * a place for its parked connections, wakes it up. Atomic.
	 */
	bool sleeping;

	/** @internal @brief The parked connections of all its lanes, atomic. */
	unsigned int n_parked;

	/** @internal @brief The first connection that the loop owns: the ones whose job is not in a pool. */
	struct HttpConnection *active;

	/** @internal @brief The connection that has waited the longest for its next request, the first one that is closed for a new connection. */
	struct HttpConnection *idle_head;
	/** @internal @brief The connection that became idle last. */
	struct HttpConnection *idle_tail;
	/** @internal @brief The idle connections; atomic. */
	unsigned int n_idle;
	/** @internal @brief Every connection that was closed while it was idle: its client left, its time ended, the cerver stopped or a new connection took its place; atomic. */
	u64 n_idle_closed;

	/** @internal @brief The connections without use. */
	struct HttpConnection *recycled;
	/** @internal @brief The connections in #recycled. */
	unsigned int n_recycled;
	/** @internal @brief The most connections that #recycled keeps, #EVENT_LOOP_RECYCLE_LIMIT. */
	unsigned int recycle_limit;

	/** @internal @brief Every connection that the loop has taken; atomic. */
	u64 n_accepted;
	/** @internal @brief Every connection of the loop that ended; atomic. */
	u64 n_ended;

	/** @internal @brief The connections of the loop, also the ones that the pool owns; atomic. */
	unsigned int n_connections;
	/**
	 * @internal
	 * @brief The connections that ended and whose backend still has operations: they are not counted anymore, and
	 * the loop does not end until their memory came back.
	 */
	unsigned int n_ending;
	/** @internal @brief The connections whose request has not been given to the pool to be handled; atomic. */
	unsigned int n_pending;
	/**
	 * @internal
	 * @brief The connections whose handler deferred its response and that have not been completed: no thread has
	 * them, so the drain of the cerver waits for them here; atomic.
	 */
	unsigned int n_deferred;

	/** @internal @brief The first source that the loop tells when it stops (event_loop_source_attach ()). */
	struct EventSource *sources;
	/**
	 * @internal
	 * @brief The sources that count for the loop (event_loop_source_add ()): it does not end with one; atomic.
	 */
	unsigned int n_sources;

	/** @internal @brief The loop does not accept more connections and ends the ones without a request; atomic. */
	bool stopping;
	/** @internal @brief Pending connections are answered with 503 instead of being completed; atomic. */
	bool rejecting;
	/** @internal @brief The loop ends when it has no connections; atomic. */
	bool finishing;
	/** @internal @brief The thread of the loop ended; atomic. */
	bool ended;

	/**
	 * @internal
	 * @brief Protects #output_condition: a thread of a pool that wrote more bytes than its connection can keep
	 * waits there until the loop sent some.
	 */
	pthread_mutex_t output_mutex;
	/** @internal @brief Told when a connection with threads that wait sent bytes or ended. */
	pthread_cond_t output_condition;
	/** @internal @brief The threads that wait in #output_condition; atomic: the loop only takes the mutex with some. */
	unsigned int output_waiters;

	/** @internal @brief The thread of the loop. */
	pthread_t thread;
	/** @internal @brief #thread was created. */
	bool started;

	/**
	 * @internal
	 * @brief Called with the receive of each request right before its data is released, by the thread of the
	 * loop; for the tests, NULL otherwise.
	 */
	void (*observer) (const struct _HttpReceive *http_receive, void *args);
	/** @internal @brief Passed to #observer. */
	void *observer_args;

} EventLoop;

/**
 * @internal
 * @brief Creates a loop for the connections of a web cerver, with the backend that its cerver asks for
 * (cerver_set_events_backend ()).
 *
 * @param[in] cerver the cerver, with its HttpCerver initialized
 * @param[in] dispatcher who runs the jobs of its connections, the one of its default lane; it must outlive the loop
 * @return the loop, NULL on error (also when the backend that was asked for is not available)
 */
CERVER_PRIVATE EventLoop *event_loop_create (struct _Cerver *cerver, EventDispatcher *dispatcher);

/**
 * @internal
 * @brief Creates a loop without a cerver: it has no socket that listens, no connections and no pools, only
 * sources (struct EventSource). Its thread runs it with event_loop_run () and it ends after event_loop_finish ().
 *
 * @param[in] backend the backend of its I/O, #CERVER_EVENTS_BACKEND_AUTO for io_uring where it is available
 * @return the loop, NULL on error (also when the backend that was asked for is not available)
 */
CERVER_PRIVATE EventLoop *event_loop_create_alone (const CerverEventsBackend backend);

/**
 * @internal
 * @brief Sets the dispatcher of a lane of a loop that has no connections: the requests of the endpoints that chose
 * the pool with that position are given to it.
 *
 * @param[in,out] loop the loop
 * @param[in] lane the lane, the position of its pool in its HTTP cerver
 * @param[in] dispatcher who runs the jobs of the lane, it must outlive the loop
 * @return 0 on success, 1 for a NULL argument or a lane that is not less than #EVENT_LOOP_LANES_SIZE
 */
CERVER_PRIVATE unsigned int event_loop_lane_set (
	EventLoop *loop, const unsigned int lane, EventDispatcher *dispatcher
);

/**
 * @internal
 * @brief Releases a loop that is not running: its connections without use and its backend. Its listen fd is closed
 * when the loop owns it.
 *
 * @param[in] loop the loop, it can be NULL
 */
CERVER_PRIVATE void event_loop_delete (EventLoop *loop);

/**
 * @internal
 * @brief Sets the clock of a loop that has no connections, for the tests of the deadlines.
 *
 * @param[in,out] loop the loop
 * @param[in] clock returns the current milliseconds
 */
CERVER_PRIVATE void event_loop_set_clock (EventLoop *loop, u64 (*clock) (void));

/**
 * @internal
 * @brief Makes the loop accept the connections of a listening socket.
 *
 * @param[in,out] loop the loop
 * @param[in] listen_fd a non blocking socket that listens
 * @param[in] owned the loop closes it when it stops accepting
 * @return 0 on success, 1 on error
 */
CERVER_PRIVATE unsigned int event_loop_listen (EventLoop *loop, const int listen_fd, const bool owned);

/**
 * @internal
 * @brief Takes a connected socket as a connection of the loop, what the loop does with each one that it accepts.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in] fd a non blocking socket; the loop owns it from now on
 * @param[in] address the address of its peer, NULL to ask its socket for it
 * @return 0 on success, 1 on error (the socket was closed)
 */
CERVER_PRIVATE unsigned int event_loop_adopt (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
);

/**
 * @internal
 * @brief Runs one step of the loop: it waits for the completions of its backend, its wake up or its next timer,
 * and handles them, what the other threads gave to it, the timers that expired and the parked connections.
 *
 * @param[in,out] loop the loop, the caller is its thread: the one that runs its first step
 * @param[in] timeout the most milliseconds to wait, 0 to not wait, #EVENT_BACKEND_WAIT_FOREVER
 * @return the completions, nodes of its queue and timers that were handled, -1 on error of its backend
 */
CERVER_PRIVATE int event_loop_step (EventLoop *loop, const int timeout);

/**
 * @internal
 * @brief Runs the steps of the loop until it is told to finish and it has no connections.
 *
 * @param[in,out] loop the loop, the caller is its thread
 */
CERVER_PRIVATE void event_loop_run (EventLoop *loop);

/**
 * @internal
 * @brief Gives a node to a loop, and wakes the loop up when it waits: a connection after its job, or the bytes of
 * a response.
 *
 * @param[in,out] loop the loop
 * @param[in] node the node; the caller does not use it anymore
 * @threadsafety Safe from any thread.
 */
CERVER_HIDDEN void event_loop_give (EventLoop *loop, EventNode *node);

/**
 * @internal
 * @brief Makes the step of the loop return.
 *
 * @param[in] loop the loop
 * @threadsafety Safe from any thread and from a signal handler.
 */
CERVER_PRIVATE void event_loop_wake (EventLoop *loop);

/**
 * @internal
 * @brief Tells the loops with connections parked in a lane that its dispatcher has a place: a job started to run.
 * Only a loop that waits in its backend is woken up, one that is running gives the place at the end of its step.
 *
 * @param[in,out] loop the loop of the job that started
 * @param[in] lane the lane of the job
 * @threadsafety Safe from any thread.
 */
CERVER_HIDDEN void event_loop_space (EventLoop *loop, const unsigned int lane);

/**
 * @internal
 * @brief Adds a connection at the end of the parked ones of its lane.
 *
 * @param[in,out] loop the loop
 * @param[in,out] connection the connection, with its lane
 */
CERVER_HIDDEN void event_loop_park (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Takes a connection out of the parked ones of its lane, nothing is done when it is not there.
 *
 * @param[in,out] loop the loop
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_unpark (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Returns whether a lane of the loop has connections that wait for a place in its dispatcher.
 *
 * @param[in] loop the loop, the caller is its thread
 * @param[in] lane the lane
 * @return true with parked connections
 */
CERVER_HIDDEN bool event_loop_lane_has_parked (const EventLoop *loop, const unsigned int lane);

/**
 * @internal
 * @brief Adds a connection at the end of the idle ones: it was kept and its client has not sent its next request.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_idle_add (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Takes a connection out of the idle ones, nothing is done when it is not one of them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_idle_remove (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Counts an idle connection that was closed.
 *
 * @param[in,out] loop the loop
 */
CERVER_HIDDEN void event_loop_idle_closed (EventLoop *loop);

/**
 * @internal
 * @brief Gives a connection without use: one that was kept or a new one. It is counted by the loop and it is one
 * of the connections that the loop owns.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @return the connection, NULL on error
 */
CERVER_HIDDEN struct HttpConnection *event_loop_connection_take (EventLoop *loop);

/**
 * @internal
 * @brief Adds a connection to the ones that the loop owns, nothing is done when it is already one of them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_connection_attach (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Takes a connection out of the ones that the loop owns: a thread of a pool runs its job, or it waits for
 * its backend to end. Nothing is done when it is not one of them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_connection_detach (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Counts a connection that ended: it is not one of the connections of the loop anymore, its place is
 * free for another one.
 *
 * @param[in,out] loop the loop, the caller is its thread
 */
CERVER_HIDDEN void event_loop_connection_gone (EventLoop *loop);

/**
 * @internal
 * @brief Counts a connection that ended (event_loop_group_ended ()): its socket was closed, or its WebSocket has it,
 * and its memory goes back to its loop. With io_uring that is some steps after event_loop_connection_gone (),
 * when its backend ended its operations.
 *
 * @param[in,out] loop the loop, the caller is its thread
 */
CERVER_HIDDEN void event_loop_connection_ended (EventLoop *loop);

/**
 * @internal
 * @brief Takes back the memory of a connection that ended and was counted (event_loop_connection_gone ()): it is
 * kept to be used again, or released.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in] connection the connection, without operations in the backend; the caller does not use it anymore
 */
CERVER_HIDDEN void event_loop_connection_recycle (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Counts a connection that is not pending anymore (its request went to the pool to be handled or it
 * ended), or that is pending again.
 *
 * @param[in,out] loop the loop
 * @param[in] pending whether it is pending now
 */
CERVER_HIDDEN void event_loop_connection_pending (EventLoop *loop, const bool pending);

/**
 * @internal
 * @brief Counts a connection whose handler deferred its response, or one that was completed.
 *
 * @param[in,out] loop the loop
 * @param[in] deferred whether its response is deferred now
 * @threadsafety Any thread: the handler defers in the thread that runs it.
 */
CERVER_HIDDEN void event_loop_connection_deferred (EventLoop *loop, const bool deferred);

/**
 * @internal
 * @brief Returns the connections of a loop whose response is deferred.
 *
 * @param[in] loop the loop
 * @return the count, 0 for a NULL loop
 */
CERVER_PRIVATE unsigned int event_loop_deferred (const EventLoop *loop);

/**
 * @internal
 * @brief Returns whether the calling thread is the one that runs a loop: what it writes for a connection of that
 * loop must never wait for the loop to send.
 *
 * For the internal methods of cerver.
 *
 * @param[in] loop the loop
 * @return true in the thread that is taking a step of the loop, or that took its last one, while the loop exists
 * @warning The thread that deletes a loop forgets it; another thread that took a step of it by hand, without
 * event_loop_run (), still has it as its loop.
 */
CERVER_PRIVATE bool event_loop_is_current (const EventLoop *loop);

/**
 * @internal
 * @brief Waits until the loop has sent bytes of a connection, or the time ends: a thread of a pool that wrote
 * more than its connection keeps does not write more until then.
 *
 * @param[in,out] loop the loop
 * @param[in] queued the bytes that the connection has to send, atomic; the wait ends when they are at most limit
 * @param[in] limit the bytes
 * @param[in] failed whether the connection failed, atomic; the wait ends when it did
 * @param[in] milliseconds the most time to wait
 * @return true when the connection has at most limit bytes to send and it did not fail
 * @threadsafety A thread of a pool that runs the job of the connection, never the loop.
 */
CERVER_HIDDEN bool event_loop_output_wait (
	EventLoop *loop, const size_t *queued, const size_t limit, const bool *failed, const u64 milliseconds
);

/**
 * @internal
 * @brief Tells the threads that wait in event_loop_output_wait () that a connection sent bytes or failed; nothing
 * is done without them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 */
CERVER_HIDDEN void event_loop_output_sent (EventLoop *loop);

/**
 * @internal
 * @brief Tells a loop to stop accepting connections; with reject, the requests that have not gone to the pool are
 * answered with 503 instead of being completed.
 *
 * @param[in,out] loop the loop
 * @param[in] reject the drain policy of the cerver rejects what is not running
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE void event_loop_stop (EventLoop *loop, const bool reject);

/**
 * @internal
 * @brief Tells a loop that stopped to end its thread when it has no connections.
 *
 * @param[in,out] loop the loop
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE void event_loop_finish (EventLoop *loop);

/**
 * @internal
 * @brief The connections whose request has not gone to the pool to be handled.
 *
 * @param[in] loop the loop
 * @return the count
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_pending (const EventLoop *loop);

/**
 * @internal
 * @brief The connections of the loop, also the ones that the pool owns.
 *
 * @param[in] loop the loop
 * @return the count
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_connections (const EventLoop *loop);

/**
 * @internal
 * @brief The connections of the loop that wait for a place in a pool, in all its lanes.
 *
 * @param[in] loop the loop
 * @return the count
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_parked (const EventLoop *loop);

/**
 * @internal
 * @brief The connections of the loop that wait for a place in the dispatcher of a lane.
 *
 * @param[in] loop the loop
 * @param[in] lane the lane
 * @return the count, 0 for a lane that is not less than #EVENT_LOOP_LANES_SIZE
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_lane_parked (const EventLoop *loop, const unsigned int lane);

/**
 * @internal
 * @brief The connections of the loop that were kept and wait for their next request.
 *
 * @param[in] loop the loop
 * @return the count
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_idle (const EventLoop *loop);

/**
 * @internal
 * @brief Every connection of the loop that was closed while it was idle.
 *
 * @param[in] loop the loop
 * @return the count
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE u64 event_loop_idle_closed_total (const EventLoop *loop);

/** @} */

/**
 * @internal
 * @defgroup events_dispatch Dispatch
 * @brief The dispatchers of the jobs of the connections.
 * @{
 */

/**
 * @internal
 * @brief Sets a dispatcher that gives the jobs to a thpool: a job that the thpool drops goes back to its loop.
 *
 * @param[out] dispatcher the dispatcher
 * @param[in] thpool the thpool, it must outlive the dispatcher
 */
CERVER_PRIVATE void event_dispatcher_thpool (EventDispatcher *dispatcher, Thpool *thpool);

/**
 * @internal
 * @brief Sets a dispatcher that runs each job when it is submitted, in the thread of the loop, for the tests of
 * one thread and the fuzzer: it is never full.
 *
 * @param[out] dispatcher the dispatcher
 */
CERVER_PRIVATE void event_dispatcher_inline (EventDispatcher *dispatcher);

/** @} */

/**
 * @internal
 * @defgroup events_source Source
 * @brief What a loop drives that is not a connection of its cerver: a socket, a timer and what the other
 * threads give to it, each one with the methods of its owner (a request or a connection of an HTTP client).
 * @{
 */

/** @internal @brief A source of a loop, see struct EventSource. */
typedef struct EventSource EventSource;

/**
 * @internal
 * @brief The methods of a source: the thread of its loop calls them. A method that the source never needs can
 * be NULL, except #stopped.
 */
typedef struct EventSourceMethods {

	/** @internal @brief An operation of the socket of the source completed. */
	void (*completed) (EventSource *source, const EventCompletion *completion);
	/** @internal @brief The timer of the source expired; it is not armed anymore. */
	void (*expired) (EventSource *source);
	/** @internal @brief Another thread gave the source to its loop (event_loop_source_give ()). */
	void (*given) (EventSource *source);
	/**
	 * @internal
	 * @brief The loop ends: the source ends what it does and removes itself (event_loop_source_remove ()), now or
	 * when the operations of its socket stopped. Called once for each source that is attached.
	 */
	void (*stopped) (EventSource *source);

} EventSourceMethods;

/**
 * @internal
 * @brief A source of a loop, the first member of its owner.
 *
 * Only the thread of its loop uses it, except to count it (event_loop_source_add ()) and to give it
 * (event_loop_source_give ()). Its memory stays valid until it is removed and its handle has no operations.
 */
struct EventSource {

	/** @internal @brief The node that gives the source to its loop. */
	EventNode node;
	/** @internal @brief Its socket, with fd -1 until it has one. */
	EventHandle handle;
	/** @internal @brief Its timer. */
	EventTimer timer;
	/** @internal @brief Its loop, from event_loop_source_add (). */
	EventLoop *loop;
	/** @internal @brief Its methods. */
	const EventSourceMethods *methods;
	/** @internal @brief The next source that is attached to the loop. */
	EventSource *next;
	/** @internal @brief The previous source that is attached to the loop. */
	EventSource *previous;
	/** @internal @brief The source is in the list of its loop, that tells it when it ends. */
	bool attached;
	/** @internal @brief The loop told the source that it ends. */
	bool stopped;

};

/**
 * @internal
 * @brief Sets a source without a loop, without a socket and with its timer not armed.
 *
 * @param[out] source the source
 * @param[in] methods its methods, they must outlive the source
 */
CERVER_PRIVATE void event_source_init (EventSource *source, const EventSourceMethods *methods);

/**
 * @internal
 * @brief Counts a source for a loop: the loop does not end until the source is removed.
 *
 * @param[in,out] loop the loop
 * @param[in,out] source the source, it takes the loop
 * @retval 0 the source counts
 * @retval 1 the loop is ending (event_loop_finish ()): the source does not count and must not be given
 * @threadsafety Any thread, while the memory of the loop is valid.
 */
CERVER_PRIVATE unsigned int event_loop_source_add (EventLoop *loop, EventSource *source);

/**
 * @internal
 * @brief Gives a source to its loop: its thread calls EventSourceMethods::given.
 *
 * @param[in,out] source a source that counts for its loop and that is not in its queue
 * @threadsafety Any thread.
 */
CERVER_PRIVATE void event_loop_source_give (EventSource *source);

/**
 * @internal
 * @brief Puts a source in the list of its loop, that calls EventSourceMethods::stopped when it ends. A loop
 * that is already ending calls it at its next step.
 *
 * @param[in,out] source a source that counts for its loop and is not attached
 * @threadsafety The thread of the loop.
 */
CERVER_PRIVATE void event_loop_source_attach (EventSource *source);

/**
 * @internal
 * @brief Removes a source from its loop: it is taken out of its list, its timer is cleared and it does not count
 * anymore.
 *
 * @param[in,out] source a source that counts for its loop
 * @threadsafety The thread of the loop.
 * @warning The handle of the source must have no operations before its memory is released.
 */
CERVER_PRIVATE void event_loop_source_remove (EventSource *source);

/**
 * @internal
 * @brief Arms the timer of a source, again when it was armed.
 *
 * @param[in,out] source a source that counts for its loop
 * @param[in] milliseconds the time from the current step of the loop until EventSourceMethods::expired
 * @threadsafety The thread of the loop.
 */
CERVER_PRIVATE void event_loop_source_timer_set (EventSource *source, const u64 milliseconds);

/**
 * @internal
 * @brief Clears the timer of a source.
 *
 * @param[in,out] source a source that counts for its loop
 * @threadsafety The thread of the loop.
 */
CERVER_PRIVATE void event_loop_source_timer_clear (EventSource *source);

/**
 * @internal
 * @brief Returns the sources that count for a loop.
 *
 * @param[in] loop the loop
 * @return the count, 0 for a NULL loop
 * @threadsafety Any thread.
 */
CERVER_PRIVATE unsigned int event_loop_sources (const EventLoop *loop);

/** @} */

/**
 * @internal
 * @defgroup events_group Group
 * @brief The loops of a cerver: they start, stop and end together.
 * @{
 */

/**
 * @internal
 * @brief The event loops of a cerver.
 */
typedef struct EventLoopGroup {

	/** @internal @brief The cerver. */
	struct _Cerver *cerver;

	/**
	 * @internal
	 * @brief Give the jobs of every loop to the pools of the cerver: the first one to its thpool, the others to
	 * the thpools of the named pools of its HTTP cerver, each one in the position of its pool.
	 */
	EventDispatcher dispatchers[EVENT_LOOP_LANES_SIZE];
	/** @internal @brief The dispatchers in use, the lanes of every loop. */
	unsigned int n_dispatchers;

	/** @internal @brief The loops. */
	EventLoop **loops;
	/** @internal @brief The number of loops. */
	unsigned int n_loops;

	/** @internal @brief The connections of all the loops, atomic: the limit of the cerver applies to them. */
	unsigned int n_connections;
	/** @internal @brief The parked connections of all the loops, atomic. */
	unsigned int n_parked;
	/** @internal @brief The parked connections of all the loops in each lane, atomic. */
	unsigned int lanes_parked[EVENT_LOOP_LANES_SIZE];

} EventLoopGroup;

/** @internal @brief The milliseconds between two checks of the loops by the thread that waits for them. */
#define EVENT_LOOP_GROUP_POLL				(1 << 2)

/** @internal @brief The milliseconds that the loops get to end the connections that a drain rejected at its deadline. */
#define EVENT_LOOP_GROUP_REJECT_GRACE		(1 << 11)

/**
 * @internal
 * @brief Runs a web cerver with event loops: it listens, starts the loops of the cerver (its I/O threads) and
 * waits until the cerver is stopped; then the loops stop accepting, and they keep the connections that they have
 * until the cerver is drained.
 *
 * The group of the loops stays in the cerver (Cerver::event_loops) until event_loop_group_delete ().
 *
 * @param[in,out] cerver the cerver: of type CERVER_TYPE_WEB, with its thpool and a non blocking socket that is bound
 * @return 0 when the cerver ran and was stopped, 1 on error
 */
CERVER_HIDDEN u8 event_loop_group_run (struct _Cerver *cerver);

/**
 * @internal
 * @brief The first part of the drain of the loops, before the thpool is drained: they stop accepting and it waits
 * until every request that they are reading went to the thpool or ended.
 *
 * With reject, and when the timeout ends, the requests that have not gone to the thpool are answered with 503.
 *
 * @param[in,out] group the group
 * @param[in] reject the drain policy rejects what is not running
 * @param[in] timeout the seconds to wait, 0 waits until they end
 * @return true when no loop has a request that is not in the thpool
 */
CERVER_HIDDEN bool event_loop_group_quiesce (EventLoopGroup *group, const bool reject, const time_t timeout);

/**
 * @internal
 * @brief A part of the drain of the loops, after the pools were drained: it waits until every request whose
 * handler deferred its response was completed. No thread of a pool has them, so the drain of the pools does not
 * wait for them.
 *
 * @param[in] group the group
 * @param[in] timeout the seconds to wait, 0 waits until they are completed
 * @return true when no loop has a deferred request
 */
CERVER_HIDDEN bool event_loop_group_settle (const EventLoopGroup *group, const time_t timeout);

/**
 * @internal
 * @brief The second part of the drain of the loops, after the thpool was drained: the loops end when they have no
 * connections and their threads are joined.
 *
 * @param[in,out] group the group
 * @param[in] timeout the seconds to wait, 0 waits until they end
 * @return true when every loop ended
 */
CERVER_HIDDEN bool event_loop_group_finish (EventLoopGroup *group, const time_t timeout);

/**
 * @internal
 * @brief Releases the loops of a cerver that ended.
 *
 * @param[in,out] cerver the cerver, nothing is done when it has no loops
 */
CERVER_HIDDEN void event_loop_group_delete (struct _Cerver *cerver);

/**
 * @internal
 * @brief The connections of all the loops of a cerver, also the ones that the thpool owns.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_group_connections (const struct _Cerver *cerver);

/**
 * @internal
 * @brief The connections of all the loops of a cerver that wait for a place in a pool.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_group_parked (const struct _Cerver *cerver);

/**
 * @internal
 * @brief The connections of all the loops of a cerver that were kept and wait for their next request.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_group_idle (const struct _Cerver *cerver);

/**
 * @internal
 * @brief The requests of all the loops of a cerver whose handler deferred its response and that nobody completed.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_group_deferred (const struct _Cerver *cerver);

/**
 * @internal
 * @brief The connections of all the loops of a cerver that wait for a place in the pool of a lane.
 *
 * @param[in] cerver the cerver
 * @param[in] lane the lane, the position of the pool in its HTTP cerver
 * @return the count, 0 without loops or for a lane that is not less than #EVENT_LOOP_LANES_SIZE
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE unsigned int event_loop_group_lane_parked (
	const struct _Cerver *cerver, const unsigned int lane
);

/**
 * @internal
 * @brief Every connection that the loops of a cerver have taken since it started.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE u64 event_loop_group_accepted (const struct _Cerver *cerver);

/**
 * @internal
 * @brief Every connection of the loops of a cerver that ended: its request was logged and its memory went back
 * to its loop.
 *
 * @param[in] cerver the cerver
 * @return the count, 0 without loops
 * @threadsafety Safe from any thread.
 */
CERVER_PRIVATE u64 event_loop_group_ended (const struct _Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
