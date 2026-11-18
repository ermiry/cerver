#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#include <sys/socket.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/core/internal.h"
#include "cerver/core/types.h"
#include "cerver/events/internal.h"
#include "cerver/handler/events.h"
#include "cerver/network/network.h"

#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/pool.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

// the poll () of the thread that started the cerver has no timeout: the stop pipe of the cerver or a signal end it
#define EVENT_LOOP_GROUP_WAIT_FOREVER			(-1)

#define EVENT_LOOP_GROUP_MILLISECONDS_PER_SECOND		1000
#define EVENT_LOOP_GROUP_NANOSECONDS_PER_MILLISECOND	1000000L

#pragma region loops

// the processor of a loop is the one with its position among the ones that the process can use,
// the loops after the last one start again from the first
static void event_loop_group_thread_affinity (const EventLoop *loop) {

	cpu_set_t allowed;
	CPU_ZERO (&allowed);

	if (!sched_getaffinity (0, sizeof (cpu_set_t), &allowed) && (CPU_COUNT (&allowed) > 0)) {
		unsigned int position = loop->id % (unsigned int) CPU_COUNT (&allowed);

		int processor = 0;
		while (!CPU_ISSET (processor, &allowed) || position) {
			if (CPU_ISSET (processor, &allowed)) position -= 1;

			processor += 1;
		}

		cpu_set_t one;
		CPU_ZERO (&one);
		CPU_SET (processor, &one);

		(void) pthread_setaffinity_np (pthread_self (), sizeof (cpu_set_t), &one);
	}

}

static void *event_loop_group_thread (void *loop_ptr) {

	EventLoop *loop = (EventLoop *) loop_ptr;

	(void) thread_set_name ("events-%u", loop->id);

	if (loop->cerver->io_threads_affinity) event_loop_group_thread_affinity (loop);

	event_loop_run (loop);

	return NULL;

}

// the first loop accepts from the socket of the cerver, that the cerver closes;
// each one of the others has its own socket in the same address
// returns 0 on success, 1 on error
static unsigned int event_loop_group_loop_listen (
	const EventLoopGroup *group, EventLoop *loop
) {

	unsigned int retval = 1;

	if (!loop->id) {
		retval = event_loop_listen (loop, group->cerver->sock, false);
	}

	else {
		const i32 sock = cerver_network_listen_socket_create (group->cerver);
		if (sock >= 0) {
			retval = event_loop_listen (loop, sock, true);
			if (retval) (void) close (sock);
		}
	}

	return retval;

}

// a lane for each named pool that has threads, in the position of its pool
static void event_loop_group_loop_lanes (EventLoopGroup *group, EventLoop *loop) {

	for (unsigned int idx = EVENT_LOOP_LANE_DEFAULT + 1; idx < group->n_dispatchers; idx++) {
		if (group->dispatchers[idx].submit) (void) event_loop_lane_set (loop, idx, &group->dispatchers[idx]);
	}

}

// returns 0 on success, 1 on error
static unsigned int event_loop_group_loops_create (EventLoopGroup *group) {

	unsigned int errors = 0;

	for (unsigned int idx = 0; (idx < group->n_loops) && !errors; idx++) {
		EventLoop *loop = event_loop_create (group->cerver, &group->dispatchers[EVENT_LOOP_LANE_DEFAULT]);
		if (loop) {
			loop->group = group;
			loop->id = idx;

			event_loop_group_loop_lanes (group, loop);

			group->loops[idx] = loop;

			errors = event_loop_group_loop_listen (group, loop);
		}

		else {
			errors = 1;
		}
	}

	return errors;

}

static void event_loop_group_free (EventLoopGroup *group) {

	if (group) {
		if (group->loops) {
			for (unsigned int idx = 0; idx < group->n_loops; idx++) {
				event_loop_delete (group->loops[idx]);
			}

			free (group->loops);
		}

		free (group);
	}

}

static void event_loop_group_dispatcher_set (EventLoopGroup *group, const HttpPool *pool) {

	if (pool->thpool) {
		event_dispatcher_thpool (&group->dispatchers[pool->index], pool->thpool);

		group->n_dispatchers = pool->index + 1;
	}

}

// the first dispatcher gives the jobs to the thpool of the cerver, the default pool;
// each named pool of its HTTP cerver that has threads has its own, in its position
static void event_loop_group_dispatchers (EventLoopGroup *group) {

	const HttpCerver *http_cerver = (const HttpCerver *) group->cerver->cerver_data;

	event_dispatcher_thpool (&group->dispatchers[EVENT_LOOP_LANE_DEFAULT], group->cerver->thpool);
	group->n_dispatchers = EVENT_LOOP_LANE_DEFAULT + 1;

	const unsigned int n_pools = http_cerver ? http_cerver->n_pools : 0;
	for (unsigned int idx = EVENT_LOOP_LANE_DEFAULT + 1; idx < n_pools; idx++) {
		event_loop_group_dispatcher_set (group, http_cerver->pools[idx]);
	}

}

// the processors that the process can use, the ones of its affinity: a container can have fewer than its machine
static unsigned int event_loop_group_processors (void) {

	cpu_set_t processors;
	CPU_ZERO (&processors);

	const int count = !sched_getaffinity (0, sizeof (cpu_set_t), &processors) ? CPU_COUNT (&processors) : 0;

	return (count > 0) ? (unsigned int) count : CERVER_DEFAULT_IO_THREADS;

}

// a loop for each I/O thread that the cerver asked for, or one for each processor
static unsigned int event_loop_group_loops_count (const Cerver *cerver) {

	unsigned int count = cerver->n_io_threads ? cerver->n_io_threads : CERVER_DEFAULT_IO_THREADS;

	if (count == CERVER_IO_THREADS_PER_CORE) count = event_loop_group_processors ();

	return count;

}

static EventLoopGroup *event_loop_group_create (Cerver *cerver) {

	EventLoopGroup *group = (EventLoopGroup *) calloc (1, sizeof (EventLoopGroup));
	if (group) {
		group->cerver = cerver;

		event_loop_group_dispatchers (group);

		group->n_loops = event_loop_group_loops_count (cerver);
		group->loops = (EventLoop **) calloc (group->n_loops, sizeof (EventLoop *));

		if (!group->loops || event_loop_group_loops_create (group)) {
			cerver_log_error (
				"Failed to create the event loops of cerver %s!", cerver->info->name
			);

			event_loop_group_free (group);
			group = NULL;
		}
	}

	return group;

}

// returns 0 on success, 1 on error
static unsigned int event_loop_group_threads_start (EventLoopGroup *group) {

	unsigned int errors = 0;

	for (unsigned int idx = 0; (idx < group->n_loops) && !errors; idx++) {
		EventLoop *loop = group->loops[idx];

		errors = thread_create_joinable (&loop->thread, event_loop_group_thread, loop);
		if (!errors) loop->started = true;
	}

	if (errors) {
		cerver_log_error (
			"Failed to start the event loops of cerver %s!", group->cerver->info->name
		);
	}

	return errors;

}

void event_loop_group_delete (struct _Cerver *cerver) {

	if (cerver && cerver->event_loops) {
		EventLoopGroup *group = cerver->event_loops;
		cerver->event_loops = NULL;

		event_loop_group_free (group);
	}

}

unsigned int event_loop_group_connections (const struct _Cerver *cerver) {

	return (cerver && cerver->event_loops) ?
		__atomic_load_n (&cerver->event_loops->n_connections, __ATOMIC_ACQUIRE) : 0;

}

unsigned int event_loop_group_parked (const struct _Cerver *cerver) {

	return (cerver && cerver->event_loops) ?
		__atomic_load_n (&cerver->event_loops->n_parked, __ATOMIC_ACQUIRE) : 0;

}

unsigned int event_loop_group_idle (const struct _Cerver *cerver) {

	unsigned int idle = 0;

	if (cerver && cerver->event_loops) {
		const EventLoopGroup *group = cerver->event_loops;
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			idle += event_loop_idle (group->loops[idx]);
		}
	}

	return idle;

}

unsigned int event_loop_group_lane_parked (
	const struct _Cerver *cerver, const unsigned int lane
) {

	return (cerver && cerver->event_loops && (lane < EVENT_LOOP_LANES_SIZE)) ?
		__atomic_load_n (&cerver->event_loops->lanes_parked[lane], __ATOMIC_ACQUIRE) : 0;

}

u64 event_loop_group_accepted (const struct _Cerver *cerver) {

	u64 accepted = 0;

	if (cerver && cerver->event_loops) {
		const EventLoopGroup *group = cerver->event_loops;
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			accepted += __atomic_load_n (&group->loops[idx]->n_accepted, __ATOMIC_ACQUIRE);
		}
	}

	return accepted;

}

u64 event_loop_group_ended (const struct _Cerver *cerver) {

	u64 ended = 0;

	if (cerver && cerver->event_loops) {
		const EventLoopGroup *group = cerver->event_loops;
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			ended += __atomic_load_n (&group->loops[idx]->n_ended, __ATOMIC_ACQUIRE);
		}
	}

	return ended;

}

#pragma endregion

#pragma region run

// the event loops are for the connections of a web cerver: they give the requests to its thpool
static bool event_loop_group_can_run (const Cerver *cerver) {

	bool can_run = false;

	if (cerver->type != CERVER_TYPE_WEB) {
		cerver_log_error (
			"Can't start cerver %s in CERVER_HANDLER_TYPE_EVENTS - it is not a web cerver!",
			cerver->info->name
		);
	}

	else if (!cerver->thpool) {
		cerver_log_error (
			"Can't start cerver %s in CERVER_HANDLER_TYPE_EVENTS - it has no thpool threads!",
			cerver->info->name
		);
	}

	else if (cerver->blocking) {
		cerver_log_error (
			"Can't start cerver %s in CERVER_HANDLER_TYPE_EVENTS - socket is NOT set to non blocking!",
			cerver->info->name
		);
	}

	else {
		can_run = true;
	}

	return can_run;

}

// the wait of the thread that started the cerver ends with the stop pipe that cerver_stop_request () writes,
// a cerver whose pipe could not be created would never end it
// returns 0 when the cerver has its pipe, 1 when it does not
static unsigned int event_loop_group_stop_pipe_check (const Cerver *cerver) {

	if (cerver->stop_pipe[CERVER_STOP_PIPE_READ] < 0) {
		cerver_log_error (
			"Cerver %s has no stop pipe to wait for its stop!", cerver->info->name
		);
	}

	return (cerver->stop_pipe[CERVER_STOP_PIPE_READ] >= 0) ? 0 : 1;

}

static void event_loop_group_log_ready (const EventLoopGroup *group) {

	const Cerver *cerver = group->cerver;

	char ip[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	(void) sock_ip_to_string_actual ((const struct sockaddr *) &cerver->address, ip);

	cerver_log (
		LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
		"Cerver %s is ready in %s port %d with %u event loops (%s)!",
		cerver->info->name, ip, cerver->port, group->n_loops, group->loops[0]->backend->name
	);

}

// a signal that interrupts the wait stops the cerver,
// so its handler can run when cerver_start () returns, like an interrupted accept ()
static void event_loop_group_wait_failed (Cerver *cerver, const int error) {

	if (error == EINTR) {
		cerver_log_warning (
			"Cerver %s was interrupted by a signal - stopping...",
			cerver->info->name
		);
	}

	else {
		cerver_log_error (
			"Cerver %s failed to wait for its stop - errno %d - stopping...",
			cerver->info->name, error
		);
	}

	cerver_stop_request (cerver);

}

// the thread that started the cerver does not run a loop: it waits until the cerver is stopped
// with its signals unblocked (the threads of the loops block them)
// cerver_stop_request () clears the running flag before it writes the stop pipe, that stays readable,
// so a stop that was requested before this wait is seen by its first check
static void event_loop_group_wait (Cerver *cerver) {

	struct pollfd descriptor;
	(void) memset (&descriptor, 0, sizeof (struct pollfd));
	descriptor.fd = cerver->stop_pipe[CERVER_STOP_PIPE_READ];
	descriptor.events = POLLIN;

	while (cerver_is_running (cerver)) {
		if (poll (&descriptor, 1, EVENT_LOOP_GROUP_WAIT_FOREVER) < 0) {
			event_loop_group_wait_failed (cerver, errno);
		}
	}

}

static void event_loop_group_stop (EventLoopGroup *group, const bool reject) {

	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		event_loop_stop (group->loops[idx], reject);
	}

}

// the loops are in the cerver before the wait: a teardown ends the ones that started
static u8 event_loop_group_start (Cerver *cerver, EventLoopGroup *group) {

	u8 retval = 1;

	cerver->event_loops = group;

	(void) time (&cerver->info->time_started);

	cerver_event_trigger (CERVER_EVENT_STARTED, cerver, NULL, NULL);

	if (!event_loop_group_threads_start (group)) {
		event_loop_group_log_ready (group);

		event_loop_group_wait (cerver);

		retval = 0;
	}

	// the loops do not accept anymore, they keep their connections until the cerver is drained
	event_loop_group_stop (group, false);

	return retval;

}

u8 event_loop_group_run (struct _Cerver *cerver) {

	u8 retval = 1;

	if (cerver && event_loop_group_can_run (cerver)) {
		if (!listen (cerver->sock, cerver->connection_queue)) {
			EventLoopGroup *group = event_loop_group_create (cerver);

			if (group && !event_loop_group_stop_pipe_check (cerver)) {
				retval = event_loop_group_start (cerver, group);
			}

			else {
				event_loop_group_free (group);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to listen in cerver %s socket!",
				cerver->info->name
			);

			cerver_close_socket (cerver);
		}
	}

	return retval;

}

#pragma endregion

#pragma region drain

// no loop has a request that has not gone to the thpool to be handled
static bool event_loop_group_is_quiet (const EventLoopGroup *group) {

	bool quiet = true;

	for (unsigned int idx = 0; (idx < group->n_loops) && quiet; idx++) {
		const EventLoop *loop = group->loops[idx];

		quiet = !loop->started || !event_loop_pending (loop);
	}

	return quiet;

}

// the thread of every loop that started ended
static bool event_loop_group_has_ended (const EventLoopGroup *group) {

	bool ended = true;

	for (unsigned int idx = 0; (idx < group->n_loops) && ended; idx++) {
		const EventLoop *loop = group->loops[idx];

		ended = !loop->started || __atomic_load_n (&loop->ended, __ATOMIC_ACQUIRE);
	}

	return ended;

}

static void event_loop_group_sleep (void) {

	struct timespec interval = { 0, EVENT_LOOP_GROUP_POLL * EVENT_LOOP_GROUP_NANOSECONDS_PER_MILLISECOND };

	(void) nanosleep (&interval, NULL);

}

// checks the loops every EVENT_LOOP_GROUP_POLL milliseconds until the condition is true
// returns false when the milliseconds ended first, 0 waits without a limit
static bool event_loop_group_wait_until (
	const EventLoopGroup *group,
	bool (*condition) (const EventLoopGroup *group), const u64 milliseconds
) {

	const u64 start = event_clock_milliseconds ();

	bool done = condition (group);
	while (!done && (!milliseconds || ((event_clock_milliseconds () - start) < milliseconds))) {
		event_loop_group_sleep ();

		done = condition (group);
	}

	return done;

}

bool event_loop_group_quiesce (EventLoopGroup *group, const bool reject, const time_t timeout) {

	bool quiet = true;

	if (group) {
		event_loop_group_stop (group, reject);

		quiet = event_loop_group_wait_until (
			group, event_loop_group_is_quiet,
			(u64) timeout * EVENT_LOOP_GROUP_MILLISECONDS_PER_SECOND
		);

		// the time ended: what the loops still read is answered with 503 or closed
		if (!quiet) {
			event_loop_group_stop (group, true);

			quiet = event_loop_group_wait_until (
				group, event_loop_group_is_quiet, EVENT_LOOP_GROUP_REJECT_GRACE
			);
		}
	}

	return quiet;

}

// a loop that ended is joined once, then it counts as one that never started
static void event_loop_group_join (EventLoopGroup *group) {

	for (unsigned int idx = 0; idx < group->n_loops; idx++) {
		EventLoop *loop = group->loops[idx];

		if (loop->started && __atomic_load_n (&loop->ended, __ATOMIC_ACQUIRE)) {
			(void) pthread_join (loop->thread, NULL);

			loop->started = false;
		}
	}

}

bool event_loop_group_finish (EventLoopGroup *group, const time_t timeout) {

	bool ended = true;

	if (group) {
		for (unsigned int idx = 0; idx < group->n_loops; idx++) {
			event_loop_finish (group->loops[idx]);
		}

		ended = event_loop_group_wait_until (
			group, event_loop_group_has_ended,
			(u64) timeout * EVENT_LOOP_GROUP_MILLISECONDS_PER_SECOND
		);

		event_loop_group_join (group);
	}

	return ended;

}

#pragma endregion
