#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>
#include <time.h>

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"
#include "cerver/threads/worker.h"

#include "cerver/utils/log.h"

#define WORKER_MILLISECONDS_PER_SECOND			1000
#define WORKER_NANOSECONDS_PER_MILLISECOND		1000000L

static void *worker_thread (void *worker_ptr);

const char *worker_state_to_string (
	const WorkerState state
) {

	switch (state) {
		#define XX(num, name, string) case WORKER_STATE_##name: return #string;
		WORKER_STATE_MAP(XX)
		#undef XX
	}

	return worker_state_to_string (WORKER_STATE_NONE);

}

static Worker *worker_new (void) {

	Worker *worker = (Worker *) malloc (sizeof (Worker));
	if (worker) {
		worker->id = 0;

		worker->name_len = 0;
		(void) memset (worker->name, 0, WORKER_NAME_SIZE);

		worker->state = WORKER_STATE_NONE;

		worker->thread_id = 0;
		worker->joinable = false;

		worker->stop = false;
		worker->end = false;

		worker->idle_since.tv_sec = 0;
		worker->idle_since.tv_nsec = 0;

		worker->job_queue = NULL;
		worker->owns_queue = false;

		worker->work = NULL;
		worker->delete_data = NULL;

		worker->reference = NULL;
		worker->remove_reference = NULL;

		(void) pthread_mutex_init (&worker->mutex, NULL);
		(void) pthread_cond_init (&worker->state_changed, NULL);
	}

	return worker;

}

// ends the worker if it is running and deletes it
void worker_delete (void *worker_ptr) {

	if (worker_ptr) {
		Worker *worker = (Worker *) worker_ptr;

		(void) worker_end (worker);

		if (worker->owns_queue) job_queue_delete (worker->job_queue);

		(void) pthread_cond_destroy (&worker->state_changed);
		(void) pthread_mutex_destroy (&worker->mutex);

		free (worker_ptr);
	}

}

Worker *worker_create (void) {

	Worker *worker = worker_new ();
	if (worker) {
		worker->job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
		worker->owns_queue = true;
	}

	return worker;

}

Worker *worker_create_with_id (const unsigned int id) {

	Worker *worker = worker_create ();
	if (worker) {
		worker->id = id;
	}

	return worker;

}

// a worker that takes its jobs from a queue that it shares with other workers
Worker *worker_create_with_queue (
	const unsigned int id, JobQueue *job_queue
) {

	Worker *worker = NULL;

	if (job_queue) {
		worker = worker_new ();
		if (worker) {
			worker->id = id;
			worker->job_queue = job_queue;
			worker->owns_queue = false;
		}
	}

	return worker;

}

void worker_set_name (Worker *worker, const char *name) {

	if (worker && name) {
		(void) snprintf (worker->name, WORKER_NAME_SIZE, "%s", name);
		worker->name_len = (unsigned int) strlen (worker->name);
	}

}

#pragma region state

WorkerState worker_get_state (Worker *worker) {

	WorkerState state = WORKER_STATE_NONE;

	(void) pthread_mutex_lock (&worker->mutex);

	state = worker->state;

	(void) pthread_mutex_unlock (&worker->mutex);

	return state;

}

// called with the worker's mutex
static void worker_set_state_locked (
	Worker *worker, const WorkerState state
) {

	worker->state = state;

	if (state == WORKER_STATE_AVAILABLE) {
		(void) clock_gettime (CLOCK_MONOTONIC, &worker->idle_since);
	}

	(void) pthread_cond_broadcast (&worker->state_changed);

}

void worker_set_state (
	Worker *worker, const WorkerState state
) {

	(void) pthread_mutex_lock (&worker->mutex);

	worker_set_state_locked (worker, state);

	(void) pthread_mutex_unlock (&worker->mutex);

}

bool worker_get_stop (Worker *worker) {

	return __atomic_load_n (&worker->stop, __ATOMIC_SEQ_CST);

}

void worker_set_stop (
	Worker *worker, const bool stop
) {

	__atomic_store_n (&worker->stop, stop, __ATOMIC_SEQ_CST);

}

bool worker_get_end (Worker *worker) {

	return __atomic_load_n (&worker->end, __ATOMIC_SEQ_CST);

}

void worker_set_end (
	Worker *worker, const bool end
) {

	__atomic_store_n (&worker->end, end, __ATOMIC_SEQ_CST);

}

// returns the milliseconds that an available worker has been without a job, 0 if it is not available
u64 worker_get_idle_milliseconds (Worker *worker) {

	u64 idle = 0;

	if (worker) {
		struct timespec now = { 0, 0 };
		(void) clock_gettime (CLOCK_MONOTONIC, &now);

		(void) pthread_mutex_lock (&worker->mutex);

		if (worker->state == WORKER_STATE_AVAILABLE) {
			const long long elapsed =
				((long long) (now.tv_sec - worker->idle_since.tv_sec) * WORKER_MILLISECONDS_PER_SECOND)
				+ ((long long) (now.tv_nsec - worker->idle_since.tv_nsec) / WORKER_NANOSECONDS_PER_MILLISECOND);

			idle = (elapsed > 0) ? (u64) elapsed : 0;
		}

		(void) pthread_mutex_unlock (&worker->mutex);
	}

	return idle;

}

// waits in the state condition until the worker reaches one of the states
static void worker_wait_state (
	Worker *worker, const WorkerState first, const WorkerState second
) {

	(void) pthread_mutex_lock (&worker->mutex);

	while ((worker->state != first) && (worker->state != second)) {
		(void) pthread_cond_wait (&worker->state_changed, &worker->mutex);
	}

	(void) pthread_mutex_unlock (&worker->mutex);

}

#pragma endregion

void worker_set_work (
	Worker *worker, void (*work) (void *args)
) {

	worker->work = work;

}

void worker_set_delete_data (
	Worker *worker, void (*delete_data) (void *args)
) {

	worker->delete_data = delete_data;

}

void worker_set_reference (
	Worker *worker, const void *reference
) {

	worker->reference = reference;

}

void worker_set_remove_reference (
	Worker *worker, void (*remove_reference) (const void *args)
) {

	worker->remove_reference = remove_reference;

}

#pragma region control

static unsigned int worker_start_internal (
	Worker *worker, const WorkerState worker_state
) {

	unsigned int retval = 1;

	worker_set_state (worker, worker_state);

	if (!worker->name_len) {
		(void) snprintf (
			worker->name, WORKER_NAME_SIZE,
			"worker-%u", worker->id
		);

		worker->name_len = (unsigned int) strlen (worker->name);
	}

	// joined by worker_end ()
	if (!thread_create_joinable (
		&worker->thread_id,
		worker_thread,
		worker
	)) {
		worker->joinable = true;
		retval = 0;
	}

	else {
		worker_set_state (worker, WORKER_STATE_NONE);

		cerver_log_error (
			"Failed to create worker <%s> thread!",
			worker->name
		);
	}

	return retval;

}

// starts the worker's thread in AVAILABLE or STOPPED
// returns 0 on success, 1 on error
unsigned int worker_start_with_state (
	Worker *worker, const WorkerState worker_state
) {

	unsigned int retval = 1;

	if (
		worker && (worker_get_state (worker) == WORKER_STATE_NONE)
		&& ((worker_state == WORKER_STATE_AVAILABLE) || (worker_state == WORKER_STATE_STOPPED))
	) {
		retval = worker_start_internal (worker, worker_state);
	}

	return retval;

}

unsigned int worker_start (Worker *worker) {

	return worker_start_with_state (worker, WORKER_STATE_AVAILABLE);

}

// a stopped worker takes jobs again
// returns 0 on success, 1 if it was not stopped
unsigned int worker_resume (Worker *worker) {

	unsigned int retval = 1;

	if (worker) {
		(void) pthread_mutex_lock (&worker->mutex);

		if (worker->state == WORKER_STATE_STOPPED) {
			worker_set_state_locked (worker, WORKER_STATE_AVAILABLE);
			retval = 0;
		}

		(void) pthread_mutex_unlock (&worker->mutex);
	}

	return retval;

}

// waits until the worker ends the job that it is running and stops
// returns 0 on success, 1 if it was not running
unsigned int worker_stop (Worker *worker) {

	unsigned int retval = 1;

	if (worker) {
		const WorkerState state = worker_get_state (worker);
		if ((state == WORKER_STATE_AVAILABLE) || (state == WORKER_STATE_WORKING)) {
			worker_set_stop (worker, true);

			// the thread reads the request with the lock of its queue
			job_queue_signal (worker->job_queue);

			worker_wait_state (worker, WORKER_STATE_STOPPED, WORKER_STATE_ENDED);

			retval = 0;
		}
	}

	return retval;

}

// a worker whose job ends its own worker can not wait for itself,
// its thread is detached and it ends after the job returns
static void worker_join (Worker *worker) {

	if (worker->joinable) {
		if (pthread_equal (worker->thread_id, pthread_self ())) {
			(void) pthread_detach (worker->thread_id);
		}

		else {
			(void) pthread_join (worker->thread_id, NULL);
		}

		worker->joinable = false;
	}

}

// waits until the worker ends the job that it is running and joins its thread
// returns 0 on success, 1 if it was not running
unsigned int worker_end (Worker *worker) {

	unsigned int retval = 1;

	if (worker) {
		const WorkerState state = worker_get_state (worker);
		if ((state != WORKER_STATE_NONE) && (state != WORKER_STATE_ENDED)) {
			worker_set_end (worker, true);

			// wakes the thread in its stopped state and in its queue
			(void) pthread_mutex_lock (&worker->mutex);
			(void) pthread_cond_broadcast (&worker->state_changed);
			(void) pthread_mutex_unlock (&worker->mutex);

			job_queue_signal (worker->job_queue);

			retval = 0;
		}

		worker_join (worker);
	}

	return retval;

}

unsigned int worker_push_job (
	Worker *worker, void *args
) {

	return job_queue_push_job (
		worker->job_queue, NULL, args
	);

}

unsigned int worker_push_job_with_work (
	Worker *worker,
	void (*work) (void *args), void *args
) {

	return job_queue_push_job (
		worker->job_queue, work, args
	);

}

#pragma endregion

#pragma region thread

// the worker was asked to stop or to end, read with the lock of its queue
static bool worker_interrupted (const void *worker_ptr) {

	const Worker *worker = (const Worker *) worker_ptr;

	return __atomic_load_n (&worker->stop, __ATOMIC_SEQ_CST)
		|| __atomic_load_n (&worker->end, __ATOMIC_SEQ_CST);

}

// a stop request moves the worker to STOPPED, where it waits until it is resumed or ended
// returns true if the worker must end
static bool worker_thread_check (Worker *worker) {

	(void) pthread_mutex_lock (&worker->mutex);

	if (worker_get_stop (worker) && !worker_get_end (worker)) {
		worker_set_stop (worker, false);
		worker_set_state_locked (worker, WORKER_STATE_STOPPED);
	}

	while ((worker->state == WORKER_STATE_STOPPED) && !worker_get_end (worker)) {
		(void) pthread_cond_wait (&worker->state_changed, &worker->mutex);
	}

	const bool end = worker_get_end (worker);

	(void) pthread_mutex_unlock (&worker->mutex);

	return end;

}

static void worker_thread_run (Worker *worker, Job *job) {

	worker_set_state (worker, WORKER_STATE_WORKING);

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	if (job->work) job->work (job->args);
	else if (worker->work) worker->work (job->args);

	job_queue_record_run (worker->job_queue, &start);

	if (worker->delete_data) worker->delete_data (job->args);

	if (worker->remove_reference) worker->remove_reference (worker->reference);

	job->args = NULL;
	job_return (worker->job_queue, job);

	worker_set_state (worker, WORKER_STATE_AVAILABLE);

}

static void *worker_thread (void *worker_ptr) {

	Worker *worker = (Worker *) worker_ptr;

	(void) thread_set_name (worker->name);

	#ifdef THREADS_DEBUG
	cerver_log_debug ("Worker <%s> thread has started!", worker->name);
	#endif

	bool end = false;
	while (!end) {
		end = worker_thread_check (worker);

		if (!end) {
			Job *job = (Job *) job_queue_pull_interruptible (
				worker->job_queue, worker_interrupted, worker
			);

			if (job) worker_thread_run (worker, job);
		}
	}

	#ifdef THREADS_DEBUG
	cerver_log_debug ("Worker <%s> thread has exited!", worker->name);
	#endif

	// the last access to the worker, worker_end () joins the thread after it
	worker_set_state (worker, WORKER_STATE_ENDED);

	return NULL;

}

#pragma endregion
