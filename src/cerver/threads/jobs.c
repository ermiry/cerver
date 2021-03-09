#include <stdlib.h>

#include "cerver/collections/dlist.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#ifdef THREADS_DEBUG
#include "cerver/utils/log.h"
#endif

void job_queue_clear (JobQueue *job_queue);

void *job_new (void) {

	Job *job = (Job *) malloc (sizeof (Job));
	if (job) {
		job->work = NULL;
		job->args = NULL;
	}

	return job;

}

void job_delete (void *job_ptr) {

	if (job_ptr) free (job_ptr);

}

Job *job_create (void (*work) (void *args), void *args) {

	Job *job = job_new ();
	if (job) {
		job->work = work;
		job->args = args;
	}

	return job;

}

void job_reset (Job *job) {

	job->work = NULL;
	job->args = NULL;

}

void job_return (
	JobQueue *job_queue, Job *job
) {

	if (job_queue && job) {
		job_reset (job);
		(void) pool_push (job_queue->pool, job);
	}

}

void *job_handler_new (void) {

	JobHandler *job_handler = (JobHandler *) malloc (sizeof (JobHandler));
	if (job_handler) {
		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		job_handler->mutex = NULL;
		job_handler->cond = NULL;

		job_handler->done = false;

		job_handler->data = NULL;
		job_handler->data_delete = NULL;
	}

	return job_handler;

}

void job_handler_delete (void *job_handler_ptr) {

	if (job_handler_ptr) {
		JobHandler *job_handler = (JobHandler *) job_handler_ptr;

		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		pthread_mutex_delete (job_handler->mutex);
		pthread_cond_delete (job_handler->cond);

		if (job_handler->data_delete)
			job_handler->data_delete (job_handler->data);

		job_handler->data = NULL;
		job_handler->data_delete = NULL;

		free (job_handler);
	}

}

void *job_handler_create (void) {

	JobHandler *job_handler = (JobHandler *) job_handler_new ();
	if (job_handler) {
		job_handler->cond = pthread_cond_new ();
		job_handler->mutex = pthread_mutex_new ();
	}

	return job_handler;

}

JobHandler *job_handler_get (JobQueue *job_queue) {

	return (JobHandler *) pool_pop (job_queue->pool);

}

void job_handler_reset (JobHandler *job_handler) {

	if (job_handler) {
		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		job_handler->done = false;

		if (job_handler->data_delete)
			job_handler->data_delete (job_handler->data);

		job_handler->data = NULL;
	}

}

// wake up waiting thread
void job_handler_signal (JobHandler *handler) {

	if (handler) {
		(void) pthread_mutex_lock (handler->mutex);

		handler->done = true;

		(void) pthread_cond_signal (handler->cond);
		(void) pthread_mutex_unlock (handler->mutex);
	}

}

void job_handler_return (
	JobQueue *job_queue, JobHandler *job_handler
) {

	if (job_queue && job_handler) {
		job_handler_reset (job_handler);
		(void) pool_push (job_queue->pool, job_handler);
	}

}

void job_handler_wait (
	JobQueue *job_queue,
	void *data, void (*data_delete) (void *data_ptr),
	void (*work) (void *data_ptr)
) {

	JobHandler *handler = (JobHandler *) pool_pop (job_queue->pool);
	if (handler) {
		handler->data = data;
		handler->data_delete = data_delete;

		// push handler to queue and wait for results
		if (!job_queue_push (job_queue, handler)) {
			// wait for response
			(void) pthread_mutex_lock (handler->mutex);

			while (!handler->done) {
				#ifdef THREADS_DEBUG
				cerver_log_debug ("job_handler_wait () waiting...");
				#endif
				(void) pthread_cond_wait (handler->cond, handler->mutex);
			}

			(void) pthread_mutex_unlock (handler->mutex);

			// do work
			work (data);
		}

		job_handler_return (job_queue, handler);
	}

}

JobQueue *job_queue_new (void) {

	JobQueue *job_queue = (JobQueue *) malloc (sizeof (JobQueue));
	if (job_queue) {
		job_queue->type = JOB_QUEUE_TYPE_NONE;

		job_queue->pool = NULL;

		job_queue->queue = NULL;

		job_queue->rwmutex = NULL;
		job_queue->has_jobs = NULL;

		job_queue->running = false;
		job_queue->handler = NULL;
	}

	return job_queue;

}

void job_queue_delete (void *job_queue_ptr) {

	if (job_queue_ptr) {
		JobQueue *job_queue = (JobQueue *) job_queue_ptr;

		(void) pthread_mutex_lock (job_queue->rwmutex);

		// job_queue_clear (job_queue);
		dlist_delete (job_queue->queue);

		(void) pthread_mutex_unlock (job_queue->rwmutex);
		(void) pthread_mutex_destroy (job_queue->rwmutex);
		free (job_queue->rwmutex);

		bsem_delete (job_queue->has_jobs);

		free (job_queue);
	}

}

static void job_queue_create_jobs (JobQueue *job_queue) {

	job_queue->pool = pool_create (job_delete);
	if (job_queue->pool) {
		pool_set_create (job_queue->pool, job_new);
		pool_set_produce_if_empty (job_queue->pool, true);

		(void) pool_init (
			job_queue->pool,
			job_new,
			JOB_QUEUE_POOL_INIT
		);
	}

	job_queue->queue = dlist_init (job_delete, NULL);

}

static void job_queue_create_handlers (JobQueue *job_queue) {

	job_queue->pool = pool_create (job_handler_delete);
	if (job_queue->pool) {
		pool_set_create (job_queue->pool, job_handler_new);
		pool_set_produce_if_empty (job_queue->pool, true);

		(void) pool_init (
			job_queue->pool,
			job_handler_new,
			JOB_QUEUE_POOL_INIT
		);
	}

	job_queue->queue = dlist_init (job_handler_delete, NULL);

}

JobQueue *job_queue_create (const JobQueueType type) {

	JobQueue *job_queue = job_queue_new ();
	if (job_queue) {
		job_queue->type = type;

		switch (job_queue->type) {
			case JOB_QUEUE_TYPE_JOBS:
				job_queue_create_jobs (job_queue);
				break;

			case JOB_QUEUE_TYPE_HANDLERS:
				job_queue_create_handlers (job_queue);
				break;

			default: break;
		}

		job_queue->rwmutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		(void) pthread_mutex_init (job_queue->rwmutex, NULL);

		job_queue->has_jobs = bsem_new ();
		bsem_init (job_queue->has_jobs, 0);
	}

	return job_queue;

}

void job_queue_set_handler (
	JobQueue *queue, void (*handler) (JobHandler *job_handler)
) {

	if (queue) queue->handler = handler;

}

// add a new job to the queue
// returns 0 on success, 1 on error
int job_queue_push (JobQueue *job_queue, void *job_ptr) {

	int retval = 1;

	if (job_queue && job_ptr) {
		(void) pthread_mutex_lock (job_queue->rwmutex);

		// job->prev = NULL;
		// switch (job_queue->size) {
		// 	case 0:
		// 		job_queue->front = job;
		// 		job_queue->rear = job;
		// 		break;

		// 	default:
		// 		job->prev = job_queue->rear;
		// 		job_queue->rear = job;
		// 		break;
		// }

		retval = dlist_insert_after (
			job_queue->queue,
			dlist_end (job_queue->queue),
			job_ptr
		);

		bsem_post (job_queue->has_jobs);

		(void) pthread_mutex_unlock (job_queue->rwmutex);
	}

	return retval;

}

// get the job at the start of the queue
void *job_queue_pull (JobQueue *job_queue) {

	void *retval = NULL;

	if (job_queue) {
		(void) pthread_mutex_lock (job_queue->rwmutex);

		switch (job_queue->queue->size) {
			case 0: break;

			case 1:
				// remove at the start of the list
				retval = dlist_remove_element (job_queue->queue, NULL);
				break;

			default:
				// remove at the start of the list
				retval = dlist_remove_element (job_queue->queue, NULL);
				bsem_post (job_queue->has_jobs);
				break;
		}

		(void) pthread_mutex_unlock (job_queue->rwmutex);
	}

	return retval;

}

static void *job_queue_jobs (void *job_queue_ptr) {

	JobQueue *job_queue = (JobQueue *) job_queue_ptr;

	Job *job = NULL;
	while (job_queue->running) {
		bsem_wait (job_queue->has_jobs);

		job = (Job *) job_queue_pull (job_queue);
		if (job) {
			#ifdef THREADS_DEBUG
			cerver_log_debug ("job_queue_jobs () new job!");
			#endif

			// do work
			if (job->work)
				job->work (job->args);

			job_return (job_queue, job);
		}
	}

	return NULL;

}

static void *job_queue_handlers (void *job_queue_ptr) {

	JobQueue *job_queue = (JobQueue *) job_queue_ptr;

	JobHandler *job_handler = NULL;
	while (job_queue->running) {
		bsem_wait (job_queue->has_jobs);

		job_handler = (JobHandler *) job_queue_pull (job_queue);
		if (job_handler) {
			#ifdef THREADS_DEBUG
			cerver_log_debug ("job_queue_handlers () new job!");
			#endif

			// do work
			job_queue->handler (job_handler);

			// signal
			#ifdef THREADS_DEBUG
			cerver_log_debug ("BEFORE job_handler_signal ()");
			#endif

			job_handler_signal (job_handler);

			#ifdef THREADS_DEBUG
			cerver_log_debug ("AFTER job_handler_signal ()");
			#endif
		}
	}

	return NULL;

}

static unsigned int job_queue_start_internal (JobQueue *job_queue) {
	
	unsigned int retval = 1;

	job_queue->running = true;
	switch (job_queue->type) {
		case JOB_QUEUE_TYPE_JOBS:
			retval = thread_create_detachable (
				&job_queue->handler_thread_id,
				job_queue_jobs,
				job_queue
			);
			break;

		case JOB_QUEUE_TYPE_HANDLERS:
			retval = thread_create_detachable (
				&job_queue->handler_thread_id,
				job_queue_handlers,
				job_queue
			);
			break;

		default: break;
	}

	return retval;

}

unsigned int job_queue_start (JobQueue *job_queue) {

	unsigned int errors = 0;

	if (job_queue) {
		if (job_queue_start_internal (job_queue)) {
			job_queue->running = false;
			errors = 1;
		}
	}

	return errors;

}

// clears the job queue -> destroys all jobs
void job_queue_clear (JobQueue *job_queue) {

	if (job_queue) {
		dlist_reset (job_queue->queue);

		bsem_reset (job_queue->has_jobs);
	}

}