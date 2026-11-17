/**
 * @file events/internal.h
 * @internal
 * @brief The event loops of a cerver: the poller, the timers, the loops and their group, never installed.
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

#include "cerver/http/core/http.h"

#include "cerver/threads/thpool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @defgroup events_poller Poller
 * @brief The readiness of the file descriptors of a loop, the only part that knows the system call that gives it.
 * @{
 */

/** @internal @brief No interest: the source stays registered but only its errors are reported. */
#define EVENT_INTEREST_NONE				0u
/** @internal @brief Report when the source can be read. */
#define EVENT_INTEREST_READ				(1u << 0)
/** @internal @brief Report when the source can be written. */
#define EVENT_INTEREST_WRITE			(1u << 1)
/**
 * @internal
 * @brief Report the source only once: after it, the source stays registered but silent (not even its errors
 * are reported) until its interest is set again with event_poller_modify (). A source that is silent can be
 * given to another thread without a system call, and that thread arms it again when it gives it back.
 */
#define EVENT_INTEREST_ONCE				(1u << 2)

/** @internal @brief The source can be read (or its peer closed, a read tells). */
#define EVENT_READY_READ				(1u << 0)
/** @internal @brief The source can be written. */
#define EVENT_READY_WRITE				(1u << 1)
/** @internal @brief The source has an error or was hung up, reported with any interest. */
#define EVENT_READY_ERROR				(1u << 2)

/** @internal @brief The sources that a wait reports at most by default. */
#define EVENT_POLLER_DEFAULT_EVENTS		(1 << 8)

/** @internal @brief A wait without a timeout. */
#define EVENT_POLLER_WAIT_FOREVER		(-1)

/** @internal @brief What a source of a poller is, its owner reads it to know what was reported. */
typedef enum EventSourceType {

	/** @internal @brief Not a source. */
	EVENT_SOURCE_TYPE_NONE				= 0,
	/** @internal @brief A socket that accepts connections. */
	EVENT_SOURCE_TYPE_LISTEN			= 1,
	/** @internal @brief A connection. */
	EVENT_SOURCE_TYPE_CONNECTION		= 2

} EventSourceType;

/**
 * @internal
 * @brief A file descriptor that a poller watches, embedded in its owner.
 *
 * Its memory must stay valid while it is registered: the poller reports the pointer.
 */
typedef struct EventSource {

	/** @internal @brief What the source is. */
	EventSourceType type;
	/** @internal @brief The file descriptor, -1 when the source is not registered. */
	int fd;
	/** @internal @brief The interest that was registered, EVENT_INTEREST_* flags. */
	unsigned int interest;
	/** @internal @brief The source is registered in a poller. */
	bool registered;
	/**
	 * @internal
	 * @brief The source was registered with #EVENT_INTEREST_ONCE and it was reported: it is silent until its
	 * interest is set again. Its owner sets it when it handles the report.
	 */
	bool silent;
	/** @internal @brief The owner of the source. */
	void *owner;

	/**
	 * @internal
	 * @brief The poller where the source is registered, NULL when it is not: its interest is changed and it is
	 * removed there, whatever poller the caller names.
	 *
	 * @since 3.0b-10
	 */
	struct EventPoller *poller;

} EventSource;

/** @internal @brief A source that is ready and what it is ready for. */
typedef struct EventReady {

	/** @internal @brief The source. */
	EventSource *source;
	/** @internal @brief EVENT_READY_* flags. */
	unsigned int ready;

} EventReady;

/** @internal @brief A poller, defined by its backend. */
typedef struct EventPoller EventPoller;

/**
 * @internal
 * @brief Sets a source that is not registered.
 *
 * @param[out] source the source
 * @param[in] type what the source is
 * @param[in] owner the owner of the source
 */
CERVER_PRIVATE void event_source_init (
	EventSource *source, const EventSourceType type, void *owner
);

/**
 * @internal
 * @brief Creates a poller with its wake up.
 *
 * @param[in] max_events the sources that a wait reports at most, 0 for #EVENT_POLLER_DEFAULT_EVENTS
 * @return the poller, NULL on error
 */
CERVER_PRIVATE EventPoller *event_poller_create (const unsigned int max_events);

/**
 * @internal
 * @brief Creates a poller whose wake ups are counted: each one makes one wait return, so many threads can wait on
 * it and a wake up for one of them is not lost (the threads of a pool that wait for the idle connections of its
 * loops, events/idle.c).
 *
 * @param[in] max_events the sources that a wait reports at most, 0 for #EVENT_POLLER_DEFAULT_EVENTS
 * @return the poller, NULL on error
 * @since 3.0b-10
 */
CERVER_HIDDEN EventPoller *event_poller_create_counted (const unsigned int max_events);

/**
 * @internal
 * @brief Closes the poller and releases it; its sources are not closed.
 *
 * @param[in] poller the poller, it can be NULL
 */
CERVER_PRIVATE void event_poller_delete (EventPoller *poller);

/**
 * @internal
 * @brief Registers a file descriptor with an interest.
 *
 * @param[in,out] poller the poller
 * @param[in,out] source the source of the file descriptor, not registered
 * @param[in] fd the file descriptor
 * @param[in] interest EVENT_INTEREST_* flags
 * @return 0 on success, 1 on error (errno tells)
 */
CERVER_PRIVATE unsigned int event_poller_add (
	EventPoller *poller, EventSource *source, const int fd, const unsigned int interest
);

/**
 * @internal
 * @brief Changes the interest of a registered source, nothing is done when it is the same; an interest with
 * #EVENT_INTEREST_ONCE is always set again, it arms a source that is silent.
 *
 * @param[in,out] poller the poller
 * @param[in,out] source the source
 * @param[in] interest EVENT_INTEREST_* flags
 * @return 0 on success, 1 on error
 * @threadsafety The thread that owns the source calls it, also one that is not the one that waits in the
 * poller: the system call is safe from any thread.
 */
CERVER_PRIVATE unsigned int event_poller_modify (
	EventPoller *poller, EventSource *source, const unsigned int interest
);

/**
 * @internal
 * @brief Stops watching a source, nothing is done when it is not registered.
 *
 * It must be called before the file descriptor is given to another thread or closed, unless the source is
 * silent (#EVENT_INTEREST_ONCE): a silent source can be given to another thread as it is, and the close of
 * its file descriptor takes it out of the poller (event_source_forget ()).
 *
 * @param[in,out] poller the poller
 * @param[in,out] source the source
 * @return 0 on success, 1 on error
 */
CERVER_PRIVATE unsigned int event_poller_remove (EventPoller *poller, EventSource *source);

/**
 * @internal
 * @brief Watches a source in a poller with an interest: a source that is registered in another poller leaves it
 * first, one that is registered in this one gets the interest (event_poller_modify ()).
 *
 * @param[in,out] poller the poller
 * @param[in,out] source the source
 * @param[in] fd the file descriptor, used when the source is added
 * @param[in] interest EVENT_INTEREST_* flags
 * @return 0 on success, 1 on error (the source is not registered anymore)
 * @since 3.0b-10
 */
CERVER_HIDDEN unsigned int event_poller_watch (
	EventPoller *poller, EventSource *source, const int fd, const unsigned int interest
);

/**
 * @internal
 * @brief Forgets the registration of a source whose file descriptor was closed: the close took it out of its
 * poller, no system call is needed.
 *
 * @param[in,out] source the source
 */
CERVER_PRIVATE void event_source_forget (EventSource *source);

/**
 * @internal
 * @brief Waits until a source is ready, the poller is woken up or the timeout ends.
 *
 * A wake up is consumed here and it is not reported as a source.
 *
 * @param[in,out] poller the poller
 * @param[out] ready the sources that are ready
 * @param[in] max the size of ready
 * @param[in] timeout the milliseconds to wait, 0 to return at once, #EVENT_POLLER_WAIT_FOREVER
 * @return the sources that are ready (0 after a wake up, a timeout or a signal), -1 on error
 */
CERVER_PRIVATE int event_poller_wait (
	EventPoller *poller, EventReady *ready, const int max, const int timeout
);

/**
 * @internal
 * @brief Makes the wait of the poller return.
 *
 * @param[in] poller the poller
 * @threadsafety Safe from any thread and from a signal handler: it is one write ().
 */
CERVER_PRIVATE void event_poller_wake (const EventPoller *poller);

/**
 * @internal
 * @brief Makes count waits of a counted poller return (event_poller_create_counted ()); with any other poller it
 * is a wake up.
 *
 * @param[in] poller the poller
 * @param[in] count the waits
 * @threadsafety Safe from any thread: it is one write ().
 * @since 3.0b-10
 */
CERVER_HIDDEN void event_poller_wake_many (const EventPoller *poller, const unsigned int count);

/** @internal @brief The sources that event_poller_take () takes at most in a call. */
#define EVENT_POLLER_TAKE_EVENTS		(1 << 4)

/** @internal @brief event_poller_wait_one () ended without a source nor a wake up: a timeout, a signal or an error. */
#define EVENT_POLLER_WAITED_NOTHING		0
/** @internal @brief event_poller_wait_one () got a source. */
#define EVENT_POLLER_WAITED_SOURCE		1
/** @internal @brief event_poller_wait_one () consumed a wake up. */
#define EVENT_POLLER_WAITED_WAKE		2

/**
 * @internal
 * @brief Waits until one source is ready, the poller is woken up or the timeout ends, with the memory of the caller:
 * many threads can wait on the same poller at the same time, and each one gets one source.
 *
 * A wake up is consumed here (one of a counted poller) and it is not reported as a source.
 *
 * @param[in] poller the poller
 * @param[out] ready the source that is ready
 * @param[in] timeout the milliseconds to wait, 0 to return at once, #EVENT_POLLER_WAIT_FOREVER
 * @return #EVENT_POLLER_WAITED_SOURCE with a source, #EVENT_POLLER_WAITED_WAKE after a wake up,
 * #EVENT_POLLER_WAITED_NOTHING after a timeout, a signal or an error
 * @threadsafety Safe from many threads at the same time.
 * @since 3.0b-10
 */
CERVER_HIDDEN int event_poller_wait_one (const EventPoller *poller, EventReady *ready, const int timeout);

/**
 * @internal
 * @brief Takes the sources that are ready without waiting and without consuming its wake ups: they are left for
 * the threads that wait on the poller.
 *
 * @param[in] poller the poller
 * @param[out] ready the sources that are ready
 * @param[in] max the size of ready, at most #EVENT_POLLER_TAKE_EVENTS are taken
 * @return the sources that are ready, -1 on error
 * @threadsafety Safe while other threads wait on the poller.
 * @since 3.0b-10
 */
CERVER_HIDDEN int event_poller_take (const EventPoller *poller, EventReady *ready, const int max);

/** @} */

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
 * @defgroup events_loop Loop
 * @brief An I/O thread: it accepts connections, reads and parses their requests without waiting, and gives the
 * work that can wait (a custom authentication, a handler) to a dispatcher.
 * @{
 */

/** @internal @brief The bytes of the buffer that a loop reads into, one for all its connections. */
#define EVENT_LOOP_BUFFER_SIZE				(1 << 16)

/** @internal @brief The connections that a loop accepts at most before it handles the rest of its events. */
#define EVENT_LOOP_ACCEPT_BATCH				(1 << 6)

/**
 * @internal
 * @brief The milliseconds that a loop stops accepting when it can not accept: without fds or at its limit. It is
 * not a timer of the wheel: the loop waits in its poller until then at most, like THREADS waits after a failed accept.
 */
#define EVENT_LOOP_ACCEPT_BACKOFF			(1u << 4)

/** @internal @brief The connections without use that a loop keeps to be used again. */
#define EVENT_LOOP_RECYCLE_LIMIT			FREELIST_RECYCLE_LIMIT (1 << 10)

/**
 * @internal
 * @brief The milliseconds between the checks of the idle connections of a loop: a loop that only has idle
 * connections wakes up that often, and one whose time ended is closed at most that long (and a tick of the
 * timers) after it.
 */
#define EVENT_LOOP_IDLE_SWEEP				EVENT_WHEEL_TICK

/** @internal @brief How the socket of an idle connection is watched: its next request, reported once. */
#define EVENT_LOOP_IDLE_INTEREST			(EVENT_INTEREST_READ | EVENT_INTEREST_ONCE)

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

	/**
	 * @internal
	 * @brief Returns whether a job that it took waits for a thread: a thread does not follow its connection then.
	 * NULL for a dispatcher whose jobs never wait. Called from the threads that run the jobs.
	 */
	bool (*is_awaited) (const EventDispatcher *dispatcher);

	/** @internal @brief The data of the dispatcher. */
	void *data;

};

/** @internal @brief The lanes that a loop has at most: one for each pool of an HTTP cerver. */
#define EVENT_LOOP_LANES_SIZE				HTTP_CERVER_POOLS_SIZE

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

	/**
	 * @internal
	 * @brief Where the threads of its pool wait for the idle connections whose last request was of the lane, NULL
	 * when the loop watches them: those connections are armed there.
	 *
	 * @since 3.0b-10
	 */
	struct EventIdle *idle;

} EventLane;

/**
 * @internal
 * @brief An event loop.
 *
 * Only its thread uses it, except the queue of completed connections (with its mutex), the wake up and the
 * counters and flags that are atomic.
 */
typedef struct EventLoop {

	/** @internal @brief The cerver of the loop. */
	struct _Cerver *cerver;
	/** @internal @brief The group of the loop, NULL for a loop alone. */
	struct EventLoopGroup *group;
	/** @internal @brief The position of the loop in its group. */
	unsigned int id;

	/** @internal @brief The poller. */
	EventPoller *poller;
	/** @internal @brief The sources that a wait reported. */
	EventReady *ready;
	/** @internal @brief The size of #ready. */
	int max_ready;

	/** @internal @brief The timers. */
	EventWheel wheel;
	/** @internal @brief The clock, in milliseconds; a test can set its own. */
	u64 (*clock) (void);
	/** @internal @brief The time of the current step. */
	u64 now;

	/** @internal @brief Its lanes: the first one is the one of the default pool, the others are set with event_loop_lane_set (). */
	EventLane lanes[EVENT_LOOP_LANES_SIZE];
	/** @internal @brief The lanes in use: one more than the last lane with a dispatcher. */
	unsigned int n_lanes;

	/** @internal @brief The socket that accepts connections. */
	EventSource listen;
	/** @internal @brief The fd of #listen, -1 without one. */
	int listen_fd;
	/** @internal @brief The loop closes #listen_fd when it stops accepting (the first loop uses the socket of its cerver). */
	bool listen_owned;
	/**
	 * @internal
	 * @brief When the loop arms #listen again after a backoff, in the milliseconds of its clock; 0 while it accepts.
	 *
	 * @since 3.0b-10
	 */
	u64 accept_resume;

	/** @internal @brief The buffer of the reads. */
	char *buffer;
	/** @internal @brief The size of #buffer. */
	size_t buffer_size;

	/** @internal @brief Protects the completed connections and #waiting. */
	pthread_mutex_t completed_mutex;
	/** @internal @brief The first connection that a thread of the pool gave back. */
	struct HttpConnection *completed_head;
	/** @internal @brief The last one. */
	struct HttpConnection *completed_tail;
	/** @internal @brief The loop is about to wait in its poller: a push, or a place for its parked connections, wakes it up. */
	bool waiting;

	/** @internal @brief The parked connections of all its lanes, atomic. */
	unsigned int n_parked;

	/** @internal @brief The first connection that the loop can still end: the ones whose request is not being handled. */
	struct HttpConnection *active;

	/**
	 * @internal
	 * @brief Protects the idle connections: the thread of the pool that answered a request adds its connection
	 * and arms its socket, and the loop takes a connection out when its client sends its next request, when
	 * its time ends, for a new connection at the limit, or when the loop stops. Whoever takes a connection out
	 * owns it.
	 */
	pthread_mutex_t idle_mutex;
	/** @internal @brief The connection that has waited the longest for its next request, the first one that is closed for a new connection. */
	struct HttpConnection *idle_head;
	/** @internal @brief The connection that became idle last. */
	struct HttpConnection *idle_tail;
	/** @internal @brief The idle connections; atomic. */
	unsigned int n_idle;
	/** @internal @brief Ends the idle connections whose time ended, every #EVENT_LOOP_IDLE_SWEEP milliseconds while the loop has idle connections. */
	EventTimer idle_timer;
	/** @internal @brief The loop checks its idle connections; atomic: the thread that adds the first one sets it and wakes the loop up. */
	bool idle_watching;
	/** @internal @brief Every connection that was closed while it was idle: its client left, its time ended, the cerver stopped or a new connection took its place; atomic. */
	u64 n_idle_closed;

	/** @internal @brief Protects #recycled and #n_recycled: the threads of the pool give back the connections that they end. */
	pthread_mutex_t recycled_mutex;
	/** @internal @brief The connections without use. */
	struct HttpConnection *recycled;
	/** @internal @brief The connections in #recycled. */
	unsigned int n_recycled;
	/** @internal @brief The most connections that #recycled keeps, #EVENT_LOOP_RECYCLE_LIMIT. */
	unsigned int recycle_limit;
	/**
	 * @internal
	 * @brief The connections without use that #recycled did not keep, without their arena and their buffers, with
	 * #recycled_mutex. Their memory is only released with the loop: a thread of a pool that waits for the idle
	 * connections can still have one that ended (events/idle.c), and it reads its loop and its idle flag.
	 *
	 * @since 3.0b-10
	 */
	struct HttpConnection *stripped;

	/** @internal @brief Every connection that the loop has taken; atomic. */
	u64 n_accepted;
	/** @internal @brief Every connection of the loop that ended; atomic. */
	u64 n_ended;

	/** @internal @brief The connections of the loop, also the ones that the pool owns; atomic. */
	unsigned int n_connections;
	/** @internal @brief The connections whose request has not been given to the pool to be handled; atomic. */
	unsigned int n_pending;

	/** @internal @brief The loop does not accept more connections and ends the ones without a request; atomic. */
	bool stopping;
	/** @internal @brief Pending connections are answered with 503 instead of being completed; atomic. */
	bool rejecting;
	/** @internal @brief The loop ends when it has no connections; atomic. */
	bool finishing;
	/** @internal @brief The thread of the loop ended; atomic. */
	bool ended;

	/** @internal @brief The thread of the loop. */
	pthread_t thread;
	/** @internal @brief #thread was created. */
	bool started;

	/**
	 * @internal
	 * @brief Called with the receive of each request right before its data is released, by the thread that ended
	 * it (the loop or a thread of the pool); for the tests, NULL otherwise.
	 */
	void (*observer) (const struct _HttpReceive *http_receive, void *args);
	/** @internal @brief Passed to #observer. */
	void *observer_args;

} EventLoop;

/**
 * @internal
 * @brief Creates a loop for the connections of a web cerver.
 *
 * @param[in] cerver the cerver, with its HttpCerver initialized
 * @param[in] dispatcher who runs the jobs of its connections, the one of its default lane; it must outlive the loop
 * @return the loop, NULL on error
 */
CERVER_PRIVATE EventLoop *event_loop_create (struct _Cerver *cerver, EventDispatcher *dispatcher);

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
 * @brief Releases a loop that is not running: its connections without use and its poller. Its listen fd is closed
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
 * @param[in] address the address of its peer
 * @return 0 on success, 1 on error (the socket was closed)
 */
CERVER_PRIVATE unsigned int event_loop_adopt (
	EventLoop *loop, const int fd, const struct sockaddr_storage *address
);

/**
 * @internal
 * @brief Runs one step of the loop: it waits for its sources, its wake up or its next timer, and handles what is
 * ready, the completed connections, the timers that expired and the parked connections.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in] timeout the most milliseconds to wait, 0 to not wait, #EVENT_POLLER_WAIT_FOREVER
 * @return the sources, completed connections and timers that were handled, -1 on error of the poller
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
 * @brief Gives a connection back to its loop after its job, and wakes the loop up when it waits.
 *
 * @param[in,out] loop the loop
 * @param[in] connection the connection, with its outcome; the caller does not use it anymore
 * @threadsafety Safe from any thread.
 */
CERVER_HIDDEN void event_loop_complete (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Makes the step of the loop return.
 *
 * @param[in] loop the loop
 * @threadsafety Safe from any thread and from a signal handler.
 */
CERVER_PRIVATE void event_loop_wake (const EventLoop *loop);

/**
 * @internal
 * @brief Tells the loops with connections parked in a lane that its dispatcher has a place: a job started to run.
 * Only a loop that waits in its poller is woken up, one that is running gives the place at the end of its step.
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
 * @brief Adds a connection at the end of the idle ones and arms its socket to be reported once: it was kept
 * and its client has not sent its next request. The loop owns it from this call.
 *
 * @param[in,out] loop the loop
 * @param[in,out] connection the connection, that the caller owns; it does not use it after a success
 * @param[in] timeout the milliseconds that the connection can wait
 * @return 0 on success, 1 when its socket could not be armed (the caller still owns it)
 * @threadsafety The thread that owns the connection calls it: its loop, or the thread of the pool that
 * answered its request, that gives it back without waking its loop up.
 */
CERVER_HIDDEN unsigned int event_loop_idle_enter (
	EventLoop *loop, struct HttpConnection *connection, const u64 timeout
);

/**
 * @internal
 * @brief Takes a connection out of the idle ones, when it is one of them: its loop owns it again.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection that was reported, or that ends
 * @return true when it was idle
 */
CERVER_HIDDEN bool event_loop_idle_claim (EventLoop *loop, struct HttpConnection *connection);

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
 * of the connections that the loop can end.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @return the connection, NULL on error
 */
CERVER_HIDDEN struct HttpConnection *event_loop_connection_take (EventLoop *loop);

/**
 * @internal
 * @brief Adds a connection to the ones that the loop can end, nothing is done when it is already one of them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_connection_attach (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Takes a connection out of the ones that the loop can end: a thread of the pool handles its request and
 * ends it, the loop does not see it again. Nothing is done when it is not one of them.
 *
 * @param[in,out] loop the loop, the caller is its thread
 * @param[in,out] connection the connection
 */
CERVER_HIDDEN void event_loop_connection_detach (EventLoop *loop, struct HttpConnection *connection);

/**
 * @internal
 * @brief Takes back the memory of a connection that ended and that the loop can not end anymore: it is kept to
 * be used again, or released. A loop that is stopping is woken up, it ends when it has no connections.
 *
 * @param[in,out] loop the loop
 * @param[in] connection the connection, the caller does not use it anymore
 * @threadsafety Safe from any thread: the thread of the pool that ended the connection calls it.
 */
CERVER_HIDDEN void event_loop_connection_release (EventLoop *loop, struct HttpConnection *connection);

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

	/**
	 * @internal
	 * @brief Where the threads of each pool wait for the idle connections of every loop, in the position of its
	 * pool; NULL for a lane whose loops watch them (events/idle.c).
	 *
	 * @since 3.0b-10
	 */
	struct EventIdle *idles[EVENT_LOOP_LANES_SIZE];

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
 * @brief Releases the loops of a cerver that ended, and the eventfd of its stop.
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

/**
 * @internal
 * @defgroup events_idle Idle
 * @brief The threads of a pool wait for the idle connections of its loops (2.1b-19): the next request of a
 * connection that was kept goes from its socket to a thread of the pool of the request before it, without its loop.
 *
 * An idle connection whose last request was of the pool is armed once (#EVENT_LOOP_IDLE_INTEREST) in the poller of
 * the pool, shared by the loops of the cerver, instead of the one of its loop; the threads of the pool wait on it
 * through the waiter of their thpool (thpool_set_waiter ()), so a job of the queue still goes first. The thread that
 * gets a socket takes its connection out of the idle ones of its loop (event_loop_idle_claim ()), and reads, parses
 * and handles its request; it gives the connection back to its loop when its loop has to decide (requests of that
 * pool wait in the loop, the loop rejects them or stops). While no thread of the pool waits on it, each loop takes
 * the connections that are ready in its idle check (event_idle_rescue ()) and reads them itself, so a request waits
 * for a place and is refused like any other.
 * @{
 */

/** @internal @brief The poller of the idle connections of a pool, and the waiter of its threads. */
typedef struct EventIdle {

	/** @internal @brief The sockets of the idle connections and the wake ups of the threads, counted. */
	EventPoller *poller;

	/** @internal @brief The thpool whose threads wait on it. */
	Thpool *thpool;
	/** @internal @brief The lane of the pool, its position in its cerver. */
	unsigned int lane;

	/** @internal @brief The waiter is set in #thpool. */
	bool started;

} EventIdle;

/**
 * @internal
 * @brief Creates the poller of the idle connections of a pool; its threads do not wait on it until
 * event_idle_start ().
 *
 * @param[in] thpool the thpool of the pool
 * @param[in] lane the lane of the pool
 * @return the poller, NULL on error
 */
CERVER_HIDDEN EventIdle *event_idle_create (Thpool *thpool, const unsigned int lane);

/**
 * @internal
 * @brief Releases a poller of idle connections whose threads do not wait on it anymore (event_idle_end ()).
 *
 * @param[in] idle the poller, it can be NULL
 */
CERVER_HIDDEN void event_idle_delete (EventIdle *idle);

/**
 * @internal
 * @brief The threads of the pool wait on the poller when its queue is empty.
 *
 * @param[in,out] idle the poller
 * @return 0 on success, 1 on error
 */
CERVER_HIDDEN unsigned int event_idle_start (EventIdle *idle);

/**
 * @internal
 * @brief The threads of the pool wait for the jobs of its queue again: it returns once none of them uses the
 * poller; nothing is done when they do not wait on it.
 *
 * @param[in,out] idle the poller, it can be NULL
 */
CERVER_HIDDEN void event_idle_end (EventIdle *idle);

/**
 * @internal
 * @brief The idle check of a loop takes the connections that are ready in the poller while no thread of its pool
 * waits on it: the connections of the loop are read by it, the ones of another loop go to their loop.
 *
 * @param[in] idle the poller
 * @param[in,out] loop the loop, the caller is its thread
 */
CERVER_HIDDEN void event_idle_rescue (EventIdle *idle, EventLoop *loop);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
