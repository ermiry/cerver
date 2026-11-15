#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include "cerver/threads/jobs.h"
#include "cerver/threads/manager.h"
#include "cerver/threads/thread.h"
#include "cerver/threads/worker.h"

// the characters of the manager's name that go in the name of each worker
#define WORKER_MANAGER_WORKER_NAME_PREFIX		48

#define WORKER_MANAGER_MILLISECONDS_PER_SECOND		1000
#define WORKER_MANAGER_NANOSECONDS_PER_MILLISECOND	1000000L
#define WORKER_MANAGER_NANOSECONDS_PER_SECOND		1000000000L

static WorkerManager *worker_manager_new (void) {

	WorkerManager *manager = (WorkerManager *) malloc (sizeof (WorkerManager));
	if (manager) {
		(void) memset (manager, 0, sizeof (WorkerManager));

		manager->job_queue = NULL;

		(void) pthread_mutex_init (&manager->mutex, NULL);

		// the scaling thread waits with CLOCK_MONOTONIC deadlines
		pthread_condattr_t attributes;
		(void) pthread_condattr_init (&attributes);
		(void) pthread_condattr_setclock (&attributes, CLOCK_MONOTONIC);
		(void) pthread_cond_init (&manager->wake, &attributes);
		(void) pthread_condattr_destroy (&attributes);

		manager->n_workers = 0;
		manager->next_id = 0;

		manager->min_workers = WORKER_MANAGER_DEFAULT_MIN_WORKERS;
		manager->max_workers = WORKER_MANAGER_DEFAULT_MAX_WORKERS;

		manager->scale_up_wait = WORKER_MANAGER_DEFAULT_SCALE_UP_WAIT;
		manager->idle_timeout = WORKER_MANAGER_DEFAULT_IDLE_TIMEOUT;
		manager->check_interval = WORKER_MANAGER_DEFAULT_CHECK_INTERVAL;

		manager->work = NULL;

		manager->running = false;
		manager->stopped = false;
		manager->control_thread_id = 0;
	}

	return manager;

}

// creates a manager with a queue of JOB_QUEUE_DEFAULT_PRIORITIES priorities
WorkerManager *worker_manager_create (const char *name) {

	WorkerManager *manager = worker_manager_new ();
	if (manager) {
		if (name) (void) snprintf (manager->name, WORKER_MANAGER_NAME_SIZE, "%s", name);

		manager->job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
		if (!manager->job_queue) {
			worker_manager_delete (manager);
			manager = NULL;
		}
	}

	return manager;

}

// ends the manager if it is running and deletes it with its workers and its queue
void worker_manager_delete (WorkerManager *manager) {

	if (manager) {
		(void) worker_manager_end (manager);

		job_queue_delete (manager->job_queue);

		(void) pthread_cond_destroy (&manager->wake);
		(void) pthread_mutex_destroy (&manager->mutex);

		free (manager);
	}

}

// sets the least and the most workers (1 to WORKER_MANAGER_MAX_WORKERS), before the start
// returns 0 on success, 1 if the limits are not valid
unsigned int worker_manager_set_limits (
	WorkerManager *manager,
	const unsigned int min_workers, const unsigned int max_workers
) {

	unsigned int retval = 1;

	if (
		manager && min_workers && (min_workers <= max_workers)
		&& (max_workers <= WORKER_MANAGER_MAX_WORKERS)
	) {
		(void) pthread_mutex_lock (&manager->mutex);

		manager->min_workers = min_workers;
		manager->max_workers = max_workers;

		(void) pthread_mutex_unlock (&manager->mutex);

		retval = 0;
	}

	return retval;

}

// sets when the manager adds a worker (a job that waits scale_up_wait ms)
// and when it removes one (a worker without a job for idle_timeout ms)
void worker_manager_set_scaling (
	WorkerManager *manager,
	const u32 scale_up_wait, const u32 idle_timeout
) {

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		manager->scale_up_wait = scale_up_wait;
		manager->idle_timeout = idle_timeout;

		(void) pthread_mutex_unlock (&manager->mutex);
	}

}

// sets the work of the jobs that are pushed without one
void worker_manager_set_work (
	WorkerManager *manager, void (*work) (void *args)
) {

	if (manager) manager->work = work;

}

#pragma region workers

// called with the manager's mutex
// returns 0 on success, 1 on error
static unsigned int worker_manager_add_worker (WorkerManager *manager) {

	unsigned int retval = 1;

	Worker *worker = worker_create_with_queue (manager->next_id, manager->job_queue);
	if (worker) {
		char name[WORKER_NAME_SIZE] = { 0 };
		(void) snprintf (
			name, WORKER_NAME_SIZE, "%.*s-%u",
			WORKER_MANAGER_WORKER_NAME_PREFIX, manager->name, manager->next_id
		);

		worker_set_name (worker, name);

		if (manager->work) worker_set_work (worker, manager->work);

		if (!worker_start (worker)) {
			manager->workers[manager->n_workers] = worker;
			manager->n_workers += 1;
			manager->next_id += 1;

			manager->created_workers += 1;
			if (manager->n_workers > manager->peak_workers) {
				manager->peak_workers = manager->n_workers;
			}

			retval = 0;
		}

		else {
			worker_delete (worker);
		}
	}

	return retval;

}

// ends the worker after its job and keeps the other ones in order,
// called with the manager's mutex
static void worker_manager_remove_worker (WorkerManager *manager, const unsigned int index) {

	worker_delete (manager->workers[index]);

	for (unsigned int current = index; (current + 1) < manager->n_workers; current++) {
		manager->workers[current] = manager->workers[current + 1];
	}

	manager->n_workers -= 1;
	manager->workers[manager->n_workers] = NULL;

	manager->removed_workers += 1;

}

static unsigned int worker_manager_count_state (
	WorkerManager *manager, const WorkerState state
) {

	unsigned int count = 0;

	for (unsigned int index = 0; index < manager->n_workers; index++) {
		if (worker_get_state (manager->workers[index]) == state) count += 1;
	}

	return count;

}

// when every worker is busy while jobs wait, or the oldest job has waited longer than scale_up_wait,
// adds a worker for each waiting job that does not have a free worker, up to the maximum
static void worker_manager_scale_up (WorkerManager *manager) {

	const size_t queued = job_queue_size (manager->job_queue);
	if (queued && (manager->n_workers < manager->max_workers)) {
		const unsigned int available = worker_manager_count_state (manager, WORKER_STATE_AVAILABLE);

		if (!available || (job_queue_oldest_wait (manager->job_queue) >= manager->scale_up_wait)) {
			size_t missing = (queued > available) ? (queued - available) : 1;

			unsigned int errors = 0;
			while (missing && !errors && (manager->n_workers < manager->max_workers)) {
				errors = worker_manager_add_worker (manager);
				missing -= 1;
			}
		}
	}

}

// removes one worker that has been without a job longer than idle_timeout
static void worker_manager_remove_idle_worker (WorkerManager *manager) {

	bool removed = false;
	for (
		unsigned int index = 0;
		(index < manager->n_workers) && (manager->n_workers > manager->min_workers) && !removed;
		index++
	) {
		if (worker_get_idle_milliseconds (manager->workers[index]) >= manager->idle_timeout) {
			worker_manager_remove_worker (manager, index);
			removed = true;
		}
	}

}

// never removes a worker while jobs wait in the queue,
// because another worker would be added again to take them
static void worker_manager_scale_down (WorkerManager *manager) {

	if (!job_queue_size (manager->job_queue)) {
		worker_manager_remove_idle_worker (manager);
	}

}

#pragma endregion

#pragma region control

// the absolute CLOCK_MONOTONIC time after milliseconds
static void worker_manager_deadline (struct timespec *deadline, const u32 milliseconds) {

	(void) clock_gettime (CLOCK_MONOTONIC, deadline);

	deadline->tv_sec += (time_t) (milliseconds / WORKER_MANAGER_MILLISECONDS_PER_SECOND);
	deadline->tv_nsec += (long) (milliseconds % WORKER_MANAGER_MILLISECONDS_PER_SECOND)
		* WORKER_MANAGER_NANOSECONDS_PER_MILLISECOND;

	if (deadline->tv_nsec >= WORKER_MANAGER_NANOSECONDS_PER_SECOND) {
		deadline->tv_sec += 1;
		deadline->tv_nsec -= WORKER_MANAGER_NANOSECONDS_PER_SECOND;
	}

}

// checks the workers every check interval, or when a pushed job wakes it up
static void *worker_manager_control (void *manager_ptr) {

	WorkerManager *manager = (WorkerManager *) manager_ptr;

	(void) pthread_mutex_lock (&manager->mutex);

	while (manager->running) {
		struct timespec deadline = { 0, 0 };
		worker_manager_deadline (&deadline, manager->check_interval);

		(void) pthread_cond_timedwait (&manager->wake, &manager->mutex, &deadline);

		if (manager->running && !manager->stopped) {
			worker_manager_scale_up (manager);
			worker_manager_scale_down (manager);
		}
	}

	(void) pthread_mutex_unlock (&manager->mutex);

	return NULL;

}

// starts the minimum workers and the thread that scales them
// returns 0 on success, 1 on error
unsigned int worker_manager_start (WorkerManager *manager) {

	unsigned int retval = 1;

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		if (!manager->running) {
			unsigned int errors = 0;
			while (!errors && (manager->n_workers < manager->min_workers)) {
				errors = worker_manager_add_worker (manager);
			}

			manager->running = true;
			manager->stopped = false;

			if (!errors && !thread_create_joinable (
				&manager->control_thread_id, worker_manager_control, manager
			)) {
				retval = 0;
			}

			else {
				manager->running = false;
				manager->control_thread_id = 0;
			}
		}

		(void) pthread_mutex_unlock (&manager->mutex);

		// the workers that did start are ended with the manager
		if (retval) (void) worker_manager_end (manager);
	}

	return retval;

}

// stops every worker after its job, the jobs stay in the queue
// returns 0 on success, 1 if it was not running
unsigned int worker_manager_stop (WorkerManager *manager) {

	unsigned int retval = 1;

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		if (manager->running && !manager->stopped) {
			manager->stopped = true;

			for (unsigned int index = 0; index < manager->n_workers; index++) {
				(void) worker_stop (manager->workers[index]);
			}

			retval = 0;
		}

		(void) pthread_mutex_unlock (&manager->mutex);
	}

	return retval;

}

// the stopped workers take jobs again
// returns 0 on success, 1 if it was not stopped
unsigned int worker_manager_resume (WorkerManager *manager) {

	unsigned int retval = 1;

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		if (manager->running && manager->stopped) {
			manager->stopped = false;

			for (unsigned int index = 0; index < manager->n_workers; index++) {
				(void) worker_resume (manager->workers[index]);
			}

			retval = 0;
		}

		(void) pthread_mutex_unlock (&manager->mutex);
	}

	return retval;

}

// ends the scaling thread and every worker after its job
// returns 0 on success, 1 if it was not running
unsigned int worker_manager_end (WorkerManager *manager) {

	unsigned int retval = 1;

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		const bool running = manager->running;
		manager->running = false;
		(void) pthread_cond_broadcast (&manager->wake);

		(void) pthread_mutex_unlock (&manager->mutex);

		if (manager->control_thread_id) {
			(void) pthread_join (manager->control_thread_id, NULL);
			manager->control_thread_id = 0;
		}

		(void) pthread_mutex_lock (&manager->mutex);

		while (manager->n_workers) {
			manager->n_workers -= 1;
			worker_delete (manager->workers[manager->n_workers]);
			manager->workers[manager->n_workers] = NULL;
		}

		(void) pthread_mutex_unlock (&manager->mutex);

		if (running) retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region jobs

// pushes a job with a priority, 0 is the highest one
// returns 0 on success, 1 on error
unsigned int worker_manager_push_job_priority (
	WorkerManager *manager,
	void (*work) (void *args), void *args,
	const unsigned int priority
) {

	unsigned int retval = 1;

	if (manager) {
		retval = job_queue_push_job_priority (manager->job_queue, work, args, priority);

		// the scaling thread checks right away if a worker is free for it,
		// without the manager's lock: a job can push other jobs while the manager
		// waits with its lock for that job to end, and a lost wake waits one check interval
		if (!retval) (void) pthread_cond_signal (&manager->wake);
	}

	return retval;

}

// pushes a job with the default priority
// returns 0 on success, 1 on error
unsigned int worker_manager_push_job (
	WorkerManager *manager,
	void (*work) (void *args), void *args
) {

	return manager ? worker_manager_push_job_priority (
		manager, work, args, job_queue_default_priority (manager->job_queue)
	) : 1;

}

#pragma endregion

#pragma region stats

// returns how many workers the manager has now
unsigned int worker_manager_get_n_workers (WorkerManager *manager) {

	unsigned int workers = 0;

	if (manager) {
		(void) pthread_mutex_lock (&manager->mutex);

		workers = manager->n_workers;

		(void) pthread_mutex_unlock (&manager->mutex);
	}

	return workers;

}

// copies the state of each worker, up to max_states
// returns how many states were copied
unsigned int worker_manager_get_states (
	WorkerManager *manager,
	WorkerState *states, const unsigned int max_states
) {

	unsigned int copied = 0;

	if (manager && states) {
		(void) pthread_mutex_lock (&manager->mutex);

		for (unsigned int index = 0; (index < manager->n_workers) && (copied < max_states); index++) {
			states[copied] = worker_get_state (manager->workers[index]);
			copied += 1;
		}

		(void) pthread_mutex_unlock (&manager->mutex);
	}

	return copied;

}

void worker_manager_get_stats (
	WorkerManager *manager, WorkerManagerStats *stats
) {

	if (manager && stats) {
		(void) memset (stats, 0, sizeof (WorkerManagerStats));

		(void) pthread_mutex_lock (&manager->mutex);

		stats->workers = manager->n_workers;
		stats->available = worker_manager_count_state (manager, WORKER_STATE_AVAILABLE);
		stats->working = worker_manager_count_state (manager, WORKER_STATE_WORKING);
		stats->stopped = worker_manager_count_state (manager, WORKER_STATE_STOPPED);

		stats->created_workers = manager->created_workers;
		stats->removed_workers = manager->removed_workers;
		stats->max_workers = manager->peak_workers;

		(void) pthread_mutex_unlock (&manager->mutex);

		job_queue_get_stats (manager->job_queue, &stats->queue);
	}

}

#pragma endregion
