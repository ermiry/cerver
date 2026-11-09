#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/eventfd.h>

#include <pthread.h>

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

static void *thread_do (void *thread_ptr);

#pragma region thread

struct _PoolThread {

	int id;
	pthread_t thread_id;
	bool started;
	Thpool *thpool;

};

typedef struct _PoolThread PoolThread;

static PoolThread *pool_thread_new (void) {

	PoolThread *thread = (PoolThread *) malloc (sizeof (PoolThread));
	if (thread) {
		thread->id = -1;
		thread->thread_id = 0;
		thread->started = false;
		thread->thpool = NULL;
	}

	return thread;

}

static void pool_thread_delete (void *thread_ptr) {

	if (thread_ptr) free (thread_ptr);

}

static PoolThread *pool_thread_create (int id, Thpool *thpool) {

	PoolThread *thread = pool_thread_new ();
	if (thread) {
		thread->id = id;
		thread->thpool = thpool;
	}

	return thread;

}

static unsigned int pool_thread_init (PoolThread *thread) {

	unsigned int retval = 1;

	if (!thread_create_joinable (&thread->thread_id, thread_do, thread)) {
		thread->started = true;
		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region internal

// returns the smallest power of 2 that is >= value
static unsigned int thpool_round_power_of_two (unsigned int value) {

	unsigned int retval = 1;
	while (retval < value) retval <<= 1;

	return retval;

}

static pthread_cond_t *thpool_cond_create (void) {

	pthread_cond_t *cond = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	if (cond) {
		// the timed waits use the monotonic clock
		pthread_condattr_t attr;
		(void) pthread_condattr_init (&attr);
		(void) pthread_condattr_setclock (&attr, CLOCK_MONOTONIC);
		(void) pthread_cond_init (cond, &attr);
		(void) pthread_condattr_destroy (&attr);
	}

	return cond;

}

static void thpool_cond_delete (pthread_cond_t *cond) {

	if (cond) {
		(void) pthread_cond_destroy (cond);
		free (cond);
	}

}

static Thpool *thpool_new (void) {

	Thpool *thpool = (Thpool *) malloc (sizeof (Thpool));
	if (thpool) {
		thpool->namelen = 0;
		(void) memset (thpool->name, 0, THPOOL_NAME_SIZE);

		thpool->n_threads = 0;
		thpool->threads = NULL;

		thpool->keep_alive = false;
		thpool->accepting = false;
		thpool->num_threads_alive = 0;
		thpool->num_threads_working = 0;

		thpool->jobs = NULL;
		thpool->queue_size = 0;
		thpool->queue_start = 0;
		thpool->queue_count = 0;

		thpool->mutex = NULL;
		thpool->has_jobs = NULL;
		thpool->has_space = NULL;
		thpool->threads_all_idle = NULL;

		thpool->space_fd = -1;
		thpool->space_waiting = false;

		(void) memset (&thpool->stats, 0, sizeof (ThpoolStats));
	}

	return thpool;

}

static void thpool_delete (Thpool *thpool) {

	if (thpool->threads) {
		for (unsigned int i = 0; i < thpool->n_threads; i++) {
			pool_thread_delete (thpool->threads[i]);
		}

		free (thpool->threads);
	}

	if (thpool->jobs) free (thpool->jobs);

	if (thpool->mutex) {
		(void) pthread_mutex_destroy (thpool->mutex);
		free (thpool->mutex);
	}

	thpool_cond_delete (thpool->has_jobs);
	thpool_cond_delete (thpool->has_space);
	thpool_cond_delete (thpool->threads_all_idle);

	if (thpool->space_fd > -1) (void) close (thpool->space_fd);

	free (thpool);

}

static bool thpool_is_valid (const Thpool *thpool) {

	return (
		thpool->threads && thpool->jobs && thpool->mutex
		&& thpool->has_jobs && thpool->has_space && thpool->threads_all_idle
		&& (thpool->space_fd > -1)
	);

}

// wakes up the thread that waits in thpool_wait_space ()
// must be called with the mutex locked
static void thpool_space_notify (Thpool *thpool) {

	if (thpool->space_waiting) {
		const uint64_t value = 1;

		// it can only fail if the counter is full, then the waiter already wakes up
		if (write (thpool->space_fd, &value, sizeof (uint64_t)) < 0) {
			thpool->space_waiting = false;
		}
	}

}

static void thpool_space_clear (Thpool *thpool) {

	uint64_t value = 0;

	// a non blocking eventfd without notifications fails with EAGAIN
	if (read (thpool->space_fd, &value, sizeof (uint64_t)) < 0) {
		value = 0;
	}

}

#pragma region stats

static u64 thpool_elapsed_microseconds (
	const struct timespec *start, const struct timespec *end
) {

	const long long elapsed =
		((long long) (end->tv_sec - start->tv_sec) * THPOOL_MICROSECONDS_PER_SECOND)
		+ ((end->tv_nsec - start->tv_nsec) / THPOOL_NANOSECONDS_PER_MICROSECOND);

	return (elapsed > 0) ? (u64) elapsed : 0;

}

static u64 thpool_elapsed_since (const struct timespec *start) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return thpool_elapsed_microseconds (start, &now);

}

// the stats methods must be called with the mutex locked
static void thpool_stats_queued (Thpool *thpool) {

	thpool->stats.total_jobs += 1;

	if (thpool->queue_count > thpool->stats.max_queued) {
		thpool->stats.max_queued = thpool->queue_count;
	}

}

static void thpool_stats_waited (Thpool *thpool, const ThpoolJob *job) {

	const u64 elapsed = thpool_elapsed_since (&job->queued);

	thpool->stats.wait_total_microseconds += elapsed;
	thpool->stats.wait_histogram[thpool_histogram_bucket (elapsed)] += 1;

}

static void thpool_stats_completed (Thpool *thpool, const struct timespec *start) {

	const u64 elapsed = thpool_elapsed_since (start);

	thpool->stats.completed_jobs += 1;
	thpool->stats.run_total_microseconds += elapsed;
	thpool->stats.run_histogram[thpool_histogram_bucket (elapsed)] += 1;

}

#pragma endregion

// the queue methods must be called with the mutex locked
static void thpool_queue_push (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
) {

	unsigned int idx = (thpool->queue_start + thpool->queue_count) & (thpool->queue_size - 1);

	thpool->jobs[idx].work = work;
	thpool->jobs[idx].reject = reject;
	thpool->jobs[idx].args = args;
	(void) clock_gettime (CLOCK_MONOTONIC, &thpool->jobs[idx].queued);

	thpool->queue_count += 1;

	thpool_stats_queued (thpool);

	(void) pthread_cond_signal (thpool->has_jobs);

}

static ThpoolJob thpool_queue_pop (Thpool *thpool) {

	ThpoolJob job = thpool->jobs[thpool->queue_start];

	thpool->jobs[thpool->queue_start].work = NULL;
	thpool->jobs[thpool->queue_start].reject = NULL;
	thpool->jobs[thpool->queue_start].args = NULL;

	thpool->queue_start = (thpool->queue_start + 1) & (thpool->queue_size - 1);
	thpool->queue_count -= 1;

	(void) pthread_cond_signal (thpool->has_space);
	thpool_space_notify (thpool);

	return job;

}

// removes every queued job and rejects them without the lock
static void thpool_queue_reject_all (Thpool *thpool) {

	(void) pthread_mutex_lock (thpool->mutex);

	while (thpool->queue_count) {
		ThpoolJob job = thpool_queue_pop (thpool);
		thpool->stats.dropped_jobs += 1;

		(void) pthread_mutex_unlock (thpool->mutex);

		if (job.reject) job.reject (job.args);

		(void) pthread_mutex_lock (thpool->mutex);
	}

	if (!thpool->num_threads_working) {
		(void) pthread_cond_broadcast (thpool->threads_all_idle);
	}

	(void) pthread_mutex_unlock (thpool->mutex);

}

static bool thpool_queue_is_full (const Thpool *thpool) {

	return (thpool->queue_count == thpool->queue_size);

}

// waits for a job, returns false when the thread must exit
// must be called with the mutex locked
static bool thread_do_wait (Thpool *thpool) {

	while (!thpool->queue_count && thpool->keep_alive) {
		(void) pthread_cond_wait (thpool->has_jobs, thpool->mutex);
	}

	return (thpool->queue_count > 0);

}

static void thread_do_finish_job (Thpool *thpool) {

	thpool->num_threads_working -= 1;

	if (!thpool->num_threads_working && !thpool->queue_count) {
		(void) pthread_cond_broadcast (thpool->threads_all_idle);
	}

}

static void *thread_do (void *thread_ptr) {

	PoolThread *thread = (PoolThread *) thread_ptr;
	Thpool *thpool = thread->thpool;

	if (thpool->namelen) {
		(void) thread_set_name (
			"thpool-%s-%d",
			thpool->name, thread->id
		);
	}

	(void) pthread_mutex_lock (thpool->mutex);

	// mark thread as alive
	thpool->num_threads_alive += 1;
	(void) pthread_cond_broadcast (thpool->threads_all_idle);

	// the queued jobs are still executed after keep_alive is false,
	// except when thpool_destroy () rejected them first
	while (thread_do_wait (thpool)) {
		ThpoolJob job = thpool_queue_pop (thpool);
		thpool->num_threads_working += 1;

		thpool_stats_waited (thpool, &job);

		(void) pthread_mutex_unlock (thpool->mutex);

		struct timespec start = { 0, 0 };
		(void) clock_gettime (CLOCK_MONOTONIC, &start);

		if (job.work) job.work (job.args);

		(void) pthread_mutex_lock (thpool->mutex);

		thpool_stats_completed (thpool, &start);

		thread_do_finish_job (thpool);
	}

	thpool->num_threads_alive -= 1;
	(void) pthread_cond_broadcast (thpool->threads_all_idle);

	(void) pthread_mutex_unlock (thpool->mutex);

	return NULL;

}

// stops every thread and joins the ones that were started
static void thpool_threads_stop (Thpool *thpool) {

	(void) pthread_mutex_lock (thpool->mutex);

	thpool->keep_alive = false;
	thpool->accepting = false;

	(void) pthread_cond_broadcast (thpool->has_jobs);
	(void) pthread_cond_broadcast (thpool->has_space);
	thpool_space_notify (thpool);

	(void) pthread_mutex_unlock (thpool->mutex);

	for (unsigned int i = 0; i < thpool->n_threads; i++) {
		PoolThread *thread = thpool->threads[i];
		if (thread && thread->started) {
			(void) pthread_join (thread->thread_id, NULL);
			thread->started = false;
		}
	}

}

static unsigned int thpool_threads_start (Thpool *thpool) {

	unsigned int errors = 0;

	for (unsigned int i = 0; i < thpool->n_threads; i++) {
		thpool->threads[i] = pool_thread_create ((int) i, thpool);
		if (!thpool->threads[i] || pool_thread_init (thpool->threads[i])) {
			errors = 1;
			break;
		}
	}

	return errors;

}

static void thpool_threads_wait_alive (Thpool *thpool) {

	(void) pthread_mutex_lock (thpool->mutex);

	while (thpool->num_threads_alive != thpool->n_threads) {
		(void) pthread_cond_wait (thpool->threads_all_idle, thpool->mutex);
	}

	(void) pthread_mutex_unlock (thpool->mutex);

}

static void thpool_deadline_create (
	struct timespec *deadline, time_t timeout
) {

	(void) clock_gettime (CLOCK_MONOTONIC, deadline);
	deadline->tv_sec += timeout;

}

// waits until there is nothing queued nor running
// returns 0 when idle, 1 if the deadline was reached
static unsigned int thpool_wait_idle (
	Thpool *thpool, const struct timespec *deadline
) {

	unsigned int retval = 0;

	(void) pthread_mutex_lock (thpool->mutex);

	while (thpool->queue_count || thpool->num_threads_working) {
		int result = deadline ?
			pthread_cond_timedwait (thpool->threads_all_idle, thpool->mutex, deadline) :
			pthread_cond_wait (thpool->threads_all_idle, thpool->mutex);

		if (result == ETIMEDOUT) {
			retval = 1;
			break;
		}
	}

	(void) pthread_mutex_unlock (thpool->mutex);

	return retval;

}

static int thpool_add_work_internal (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args, bool wait
) {

	int retval = 1;

	(void) pthread_mutex_lock (thpool->mutex);

	if (wait && thpool->accepting && thpool_queue_is_full (thpool)) {
		thpool->stats.waited_jobs += 1;
	}

	while (wait && thpool->accepting && thpool_queue_is_full (thpool)) {
		(void) pthread_cond_wait (thpool->has_space, thpool->mutex);
	}

	if (thpool->accepting && !thpool_queue_is_full (thpool)) {
		thpool_queue_push (thpool, work, reject, args);
		retval = 0;
	}

	else {
		thpool->stats.rejected_jobs += 1;
	}

	(void) pthread_mutex_unlock (thpool->mutex);

	return retval;

}

#pragma endregion

#pragma region public

// creates a new thpool with n threads
Thpool *thpool_create (unsigned int n_threads) {

	Thpool *thpool = thpool_new ();
	if (thpool) {
		thpool->n_threads = n_threads;
		thpool->threads = (PoolThread **) calloc (thpool->n_threads ? thpool->n_threads : 1, sizeof (PoolThread *));

		thpool->queue_size = thpool_round_power_of_two (
			n_threads * THPOOL_DEFAULT_QUEUE_SIZE_FACTOR
		);

		thpool->jobs = (ThpoolJob *) calloc (thpool->queue_size, sizeof (ThpoolJob));

		thpool->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		if (thpool->mutex) (void) pthread_mutex_init (thpool->mutex, NULL);

		thpool->has_jobs = thpool_cond_create ();
		thpool->has_space = thpool_cond_create ();
		thpool->threads_all_idle = thpool_cond_create ();

		thpool->space_fd = eventfd (0, EFD_NONBLOCK | EFD_CLOEXEC);

		if (!thpool_is_valid (thpool)) {
			thpool_delete (thpool);
			thpool = NULL;
		}
	}

	return thpool;

}

// sets how many jobs can wait in the queue
// it is always rounded up to a power of 2
// must be called before thpool_init ()
// returns 0 on success, 1 on error
unsigned int thpool_set_queue_size (
	Thpool *thpool, unsigned int queue_size
) {

	unsigned int retval = 1;

	if (thpool && queue_size && !thpool->keep_alive) {
		unsigned int size = thpool_round_power_of_two (queue_size);

		ThpoolJob *jobs = (ThpoolJob *) calloc (size, sizeof (ThpoolJob));
		if (jobs) {
			free (thpool->jobs);

			thpool->jobs = jobs;
			thpool->queue_size = size;
			thpool->queue_start = 0;
			thpool->queue_count = 0;

			retval = 0;
		}
	}

	return retval;

}

// initializes the thpool
// must be called after thpool_create ()
// returns 0 on success, 1 on error
unsigned int thpool_init (Thpool *thpool) {

	unsigned int retval = 1;

	if (thpool && thpool->n_threads && !thpool->keep_alive) {
		(void) pthread_mutex_lock (thpool->mutex);
		thpool->keep_alive = true;
		thpool->accepting = true;
		(void) pthread_mutex_unlock (thpool->mutex);

		if (!thpool_threads_start (thpool)) {
			thpool_threads_wait_alive (thpool);
			retval = 0;
		}

		else {
			cerver_log_error (
				"thpool_init () - failed to start thpool %s threads",
				thpool->name
			);

			// the threads that did start are stopped
			thpool_threads_stop (thpool);
		}
	}

	return retval;

}

// sets the name for the thpool
void thpool_set_name (Thpool *thpool, const char *name) {

	if (thpool && name) {
		(void) strncpy (thpool->name, name, THPOOL_NAME_SIZE - 1);
		thpool->namelen = strlen (thpool->name);
	}

}

// gets the current number of threads that are alive (running) in the thpool
unsigned int thpool_get_num_threads_alive (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);
		retval = thpool->num_threads_alive;
		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// gets the current number of threads that are busy working in a job
unsigned int thpool_get_num_threads_working (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);
		retval = thpool->num_threads_working;
		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// gets how many jobs are waiting in the queue
unsigned int thpool_get_queued (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);
		retval = thpool->queue_count;
		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// gets how many jobs can wait in the queue
unsigned int thpool_get_queue_size (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);
		retval = thpool->queue_size;
		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// copies the current stats of the thpool
void thpool_get_stats (Thpool *thpool, ThpoolStats *stats) {

	if (thpool && stats) {
		(void) pthread_mutex_lock (thpool->mutex);
		(void) memcpy (stats, &thpool->stats, sizeof (ThpoolStats));
		(void) pthread_mutex_unlock (thpool->mutex);
	}

}

// returns the bucket of the histograms for an elapsed time
unsigned int thpool_histogram_bucket (const u64 microseconds) {

	unsigned int bucket = 0;
	while (
		(bucket <= THPOOL_HISTOGRAM_MAX_EXPONENT)
		&& (microseconds > (((u64) 1 << bucket) * THPOOL_MICROSECONDS_PER_MILLISECOND))
	) {
		bucket += 1;
	}

	return bucket;

}

static void thpool_histogram_print (
	const char *name, const u64 histogram[THPOOL_HISTOGRAM_BUCKETS]
) {

	cerver_log_msg ("%s:", name);

	for (unsigned int i = 0; i <= THPOOL_HISTOGRAM_MAX_EXPONENT; i++) {
		if (histogram[i]) {
			cerver_log_msg ("\t<= %lu ms: %lu", ((u64) 1 << i), histogram[i]);
		}
	}

	if (histogram[THPOOL_HISTOGRAM_BUCKETS - 1]) {
		cerver_log_msg (
			"\t> %lu ms: %lu",
			((u64) 1 << THPOOL_HISTOGRAM_MAX_EXPONENT),
			histogram[THPOOL_HISTOGRAM_BUCKETS - 1]
		);
	}

}

// prints the stats with the cerver log
void thpool_stats_print (const ThpoolStats *stats) {

	if (stats) {
		cerver_log_msg ("Thpool total jobs:             %lu", stats->total_jobs);
		cerver_log_msg ("Thpool completed jobs:         %lu", stats->completed_jobs);
		cerver_log_msg ("Thpool rejected jobs:          %lu", stats->rejected_jobs);
		cerver_log_msg ("Thpool dropped jobs:           %lu", stats->dropped_jobs);
		cerver_log_msg ("Thpool waited jobs:            %lu", stats->waited_jobs);
		cerver_log_msg ("Thpool max queued:             %u", stats->max_queued);
		cerver_log_msg ("Thpool wait total (us):        %lu", stats->wait_total_microseconds);
		cerver_log_msg ("Thpool run total (us):         %lu", stats->run_total_microseconds);

		thpool_histogram_print ("Thpool wait", stats->wait_histogram);
		thpool_histogram_print ("Thpool run", stats->run_histogram);
	}

}

// returns true if the thpool does NOT have any working thread
bool thpool_is_empty (Thpool *thpool) {

	bool retval = false;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);

		retval = (thpool->num_threads_working == 0);

		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// returns true if a new job can NOT be queued right now
bool thpool_is_full (Thpool *thpool) {

	bool retval = true;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);

		retval = (!thpool->accepting || thpool_queue_is_full (thpool));

		(void) pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// adds a work to the thpool's job queue without blocking
int thpool_add_work (
	Thpool *thpool, void (*work) (void *), void *args
) {

	return (thpool && work) ? thpool_add_work_internal (
		thpool, work, NULL, args, false
	) : 1;

}

// works like thpool_add_work () but reject is called
// with the args if the job is dropped before it runs
int thpool_add_work_with_reject (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
) {

	return (thpool && work) ? thpool_add_work_internal (
		thpool, work, reject, args, false
	) : 1;

}

// blocks while the queue is full until there is space
int thpool_add_work_wait (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
) {

	return (thpool && work) ? thpool_add_work_internal (
		thpool, work, reject, args, true
	) : 1;

}

// returns 0 if a job can be queued, 1 if the queue is full
// or the thpool is not accepting jobs, -1 if the caller must wait
static int thpool_wait_space_check (Thpool *thpool, const bool wait) {

	int retval = 1;

	(void) pthread_mutex_lock (thpool->mutex);

	if (thpool->accepting) {
		if (!thpool_queue_is_full (thpool)) retval = 0;
		else if (wait) retval = -1;
	}

	// a notification is only written while there is someone waiting
	thpool->space_waiting = (retval == -1);

	if (retval == -1) thpool->stats.waited_jobs += 1;

	(void) pthread_mutex_unlock (thpool->mutex);

	return retval;

}

// waits until a new job can be queued, the thpool stops accepting jobs,
// the timeout (in ms) expires or a signal interrupts the wait
int thpool_wait_space (Thpool *thpool, int timeout) {

	int retval = 1;

	if (thpool) {
		retval = thpool_wait_space_check (thpool, true);
		if (retval == -1) {
			struct pollfd poll_fd = { thpool->space_fd, POLLIN, 0 };
			const int result = poll (&poll_fd, 1, timeout);
			const int error = errno;

			if (result > 0) thpool_space_clear (thpool);

			retval = thpool_wait_space_check (thpool, false);

			// the signal is reported only if the wait did not end with space
			if ((result < 0) && (error == EINTR) && retval) {
				errno = EINTR;
				retval = -1;
			}
		}
	}

	return retval;

}

// wait until all jobs have finished
void thpool_wait (Thpool *thpool) {

	if (thpool) {
		(void) thpool_wait_idle (thpool, NULL);
	}

}

// stops accepting new jobs and waits for the current ones
unsigned int thpool_drain (
	Thpool *thpool, bool run_queued, time_t timeout
) {

	unsigned int retval = 0;

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);

		thpool->accepting = false;
		(void) pthread_cond_broadcast (thpool->has_space);
		thpool_space_notify (thpool);

		(void) pthread_mutex_unlock (thpool->mutex);

		if (!run_queued) thpool_queue_reject_all (thpool);

		struct timespec deadline;
		thpool_deadline_create (&deadline, timeout);

		retval = thpool_wait_idle (
			thpool, (timeout == THPOOL_DRAIN_TIMEOUT_NONE) ? NULL : &deadline
		);

		// the jobs that could not run in time are rejected
		if (retval) thpool_queue_reject_all (thpool);
	}

	return retval;

}

// stops the threads, waits for them to finish their current job
// rejects every job that is still queued and deletes the thpool
void thpool_destroy (Thpool *thpool) {

	if (thpool) {
		(void) pthread_mutex_lock (thpool->mutex);
		thpool->accepting = false;
		(void) pthread_mutex_unlock (thpool->mutex);

		thpool_queue_reject_all (thpool);

		thpool_threads_stop (thpool);

		// a job that was added while the threads stopped
		thpool_queue_reject_all (thpool);

		thpool_delete (thpool);
	}

}

#pragma endregion
