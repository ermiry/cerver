#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <unistd.h>

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"
#include "cerver/threads/worker.h"

#include "cerver/utils/log.h"

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

		worker->stop = false;
		worker->end = false;

		worker->job_queue = NULL;

		worker->work = NULL;
		worker->delete_data = NULL;

		(void) memset (&worker->mutex, 0, sizeof (pthread_mutex_t));
	}

	return worker;

}

void worker_delete (void *worker_ptr) {

	if (worker_ptr) {
		Worker *worker = (Worker *) worker_ptr;

		job_queue_delete (worker->job_queue);

		pthread_mutex_destroy (&worker->mutex);

		free (worker_ptr);
	}

}

Worker *worker_create (void) {

	Worker *worker = worker_new ();
	if (worker) {
		worker->job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

		(void) pthread_mutex_init (&worker->mutex, NULL);
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

void worker_set_name (Worker *worker, const char *name) {

	if (worker && name) {
		strncpy (worker->name, name, WORKER_NAME_SIZE - 1);
		worker->name_len = (unsigned int) strlen (worker->name);
	}

}

WorkerState worker_get_state (Worker *worker) {

	WorkerState state = WORKER_STATE_NONE;

	(void) pthread_mutex_lock (&worker->mutex);

	state = worker->state;

	(void) pthread_mutex_unlock (&worker->mutex);

	return state;

}

void worker_set_state (
	Worker *worker, const WorkerState state
) {

	(void) pthread_mutex_lock (&worker->mutex);

	worker->state = state;

	(void) pthread_mutex_unlock (&worker->mutex);

}

bool worker_get_stop (Worker *worker) {

	bool stop = false;

	(void) pthread_mutex_lock (&worker->mutex);

	stop = worker->stop;

	(void) pthread_mutex_unlock (&worker->mutex);

	return stop;

}

void worker_set_stop (
	Worker *worker, const bool stop
) {

	(void) pthread_mutex_lock (&worker->mutex);

	worker->stop = stop;

	(void) pthread_mutex_unlock (&worker->mutex);

}

bool worker_get_end (Worker *worker) {

	bool end = false;

	(void) pthread_mutex_lock (&worker->mutex);

	end = worker->end;

	(void) pthread_mutex_unlock (&worker->mutex);

	return end;

}

void worker_set_end (
	Worker *worker, const bool end
) {

	(void) pthread_mutex_lock (&worker->mutex);

	worker->end = end;

	(void) pthread_mutex_unlock (&worker->mutex);

}

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

unsigned int worker_start_with_state (
	Worker *worker, const WorkerState worker_state
) {

	unsigned int retval = 1;

	worker_set_state (worker, worker_state);

	if (!worker->name_len) {
		(void) snprintf (
			worker->name, WORKER_NAME_SIZE - 1,
			"worker-%u", worker->id
		);

		worker->name_len = (unsigned int) strlen (worker->name);
	}

	if (!thread_create_detachable (
		&worker->thread_id,
		worker_thread,
		worker
	)) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create worker <%s> thread!",
			worker->name
		);
	}

	return retval;

}

unsigned int worker_start (Worker *worker) {

	return worker_start_with_state (worker, WORKER_STATE_AVAILABLE);

}

unsigned int worker_stop (Worker *worker) {

	unsigned int retval = 1;

	#ifdef THREADS_DEBUG
	cerver_log_msg ("Stopping worker <%s>...", worker->name);
	#endif

	worker_set_stop (worker, true);

	WorkerState state = WORKER_STATE_NONE;
	while (state != WORKER_STATE_STOPPED) {
		state = worker_get_state (worker);

		switch (state) {
			case WORKER_STATE_AVAILABLE:
			case WORKER_STATE_WORKING: {
				bsem_post (worker->job_queue->has_jobs);

				retval = 0;
			} break;

			default: break;
		}

		(void) usleep (WORKER_SLEEP_TIME);
	}

	#ifdef THREADS_DEBUG
	cerver_log_msg ("Stopped worker <%s>", worker->name);
	#endif

	return retval;

}

unsigned int worker_end (Worker *worker) {

	unsigned int retval = 1;

	#ifdef THREADS_DEBUG
	cerver_log_msg ("Ending worker <%s>...", worker->name);
	#endif

	worker_set_end (worker, true);

	WorkerState state = WORKER_STATE_NONE;
	while (state != WORKER_STATE_ENDED) {
		state = worker_get_state (worker);

		switch (state) {
			case WORKER_STATE_AVAILABLE:
			case WORKER_STATE_WORKING:
			case WORKER_STATE_STOPPED: {
				bsem_post (worker->job_queue->has_jobs);

				retval = 0;
			} break;

			default: break;
		}

		(void) usleep (WORKER_SLEEP_TIME);
	}

	#ifdef THREADS_DEBUG
	cerver_log_msg ("Ended worker <%s>", worker->name);
	#endif

	return retval;

}

unsigned int worker_push_job (
	Worker *worker,
	void (*work) (void *args), void *args
) {

	return job_queue_push_job (
		worker->job_queue,
		work ? work : worker->work,
		args
	);

}

static void *worker_thread (void *worker_ptr) {

	Worker *worker = (Worker *) worker_ptr;

	cerver_log_success (
		"Worker <%s> thread has started!",
		worker->name
	);

	(void) thread_set_name (worker->name);

	#ifdef THREADS_DEBUG
	cerver_log_debug (
		"Worker <%s> state: %s",
		worker->name,
		worker_state_to_string (worker_get_state (worker))
	);
	#endif

	Job *job = NULL;
	WorkerState state = WORKER_STATE_NONE;
	while (state != WORKER_STATE_ENDED) {
		// wait for work or signal
		bsem_wait (worker->job_queue->has_jobs);

		// check if we are still required to do work
		state = worker_get_state (worker);

		// worker was requested to stop while it was waiting
		if (worker_get_stop (worker)) {
			#ifdef THREADS_DEBUG
			cerver_log_msg (
				"Worker <%s> stop while waiting!\n",
				worker->name
			);
			#endif

			worker_set_state (
				worker, WORKER_STATE_STOPPED
			);

			state = WORKER_STATE_STOPPED;

			worker->stop = false;
		}

		// worker was requested to end while stopped or waiting
		else if (worker_get_end (worker)) {
			#ifdef THREADS_DEBUG
			switch (state) {
				case WORKER_STATE_AVAILABLE: {
					cerver_log_msg (
						"Worker <%s> end while waiting!\n",
						worker->name
					);
				} break;

				case WORKER_STATE_STOPPED: {
					cerver_log_msg (
						"Worker <%s> end while stopped!\n",
						worker->name
					);
				} break;

				default: break;
			}
			#endif

			worker_set_state (
				worker, WORKER_STATE_ENDED
			);

			state = WORKER_STATE_ENDED;
		}

		if (state == WORKER_STATE_AVAILABLE) {
			job = job_queue_pull (worker->job_queue);
			if (job) {
				#ifdef THREADS_DEBUG
				cerver_log_success (
					"Worker <%s> new job!",
					worker->name
				);
				#endif

				worker_set_state (
					worker, WORKER_STATE_WORKING
				);

				// do actual work
				if (job->work) {
					job->work (job->args);
				}

				else if (worker->work) {
					worker->work (job->args);
				}

				// worker was requested to stop while working
				if (worker_get_stop (worker)) {
					#ifdef THREADS_DEBUG
					cerver_log_msg (
						"Worker <%s> stop while working!\n",
						worker->name
					);
					#endif

					worker_set_state (
						worker, WORKER_STATE_STOPPED
					);

					state = WORKER_STATE_STOPPED;

					worker->stop = false;
				}

				// worker was requested to end while working
				else if (worker_get_end (worker)) {
					#ifdef THREADS_DEBUG
					cerver_log_msg (
						"Worker <%s> end while working!\n",
						worker->name
					);
					#endif

					worker_set_state (
						worker, WORKER_STATE_ENDED
					);

					state = WORKER_STATE_ENDED;
				}

				else {
					// worker is still available to handle work
					worker_set_state (
						worker, WORKER_STATE_AVAILABLE
					);
				}

				if (worker->delete_data) {
					worker->delete_data (job->args);
				}

				job->args = NULL;

				job_return (worker->job_queue, job);
			}
		}
	}

	cerver_log_success (
		"Worker <%s> thread has exited!",
		worker->name
	);

	return NULL;

}
