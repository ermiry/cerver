#include <stdlib.h>

#include "cerver/collections/dlist.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/bsem.h"

void job_queue_clear (JobQueue *job_queue);

Job *job_new (void) {

	Job *job = (Job *) malloc (sizeof (Job));
	if (job) {
		// job->prev = NULL;
		job->method = NULL;
		job->args = NULL;
	}

	return job;

}

void job_delete (void *job_ptr) {

	if (job_ptr) free (job_ptr);

}

Job *job_create (void (*method) (void *args), void *args) {

	Job *job = job_new ();
	if (job) {
		job->method = method;
		job->args = args;
	}

	return job;

}

JobQueue *job_queue_new (void) {

	JobQueue *job_queue = (JobQueue *) malloc (sizeof (JobQueue));
	if (job_queue) {
		// job_queue->front = NULL;
		// job_queue->rear = NULL;

		// job_queue->size = 0;

		job_queue->queue = NULL;

		job_queue->rwmutex = NULL;
		job_queue->has_jobs = NULL;
	}

	return job_queue;

}

void job_queue_delete (void *job_queue_ptr) {

	if (job_queue_ptr) {
		JobQueue *job_queue = (JobQueue *) job_queue_ptr;

		pthread_mutex_lock (job_queue->rwmutex);

		// job_queue_clear (job_queue);
		dlist_delete (job_queue->queue);

		pthread_mutex_unlock (job_queue->rwmutex);
		pthread_mutex_destroy (job_queue->rwmutex);
		free (job_queue->rwmutex);

		bsem_delete (job_queue->has_jobs);

		free (job_queue);
	}

}

JobQueue *job_queue_create (void) {

	JobQueue *job_queue = job_queue_new ();
	if (job_queue) {
		job_queue->queue = dlist_init (job_delete, NULL);

		job_queue->rwmutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (job_queue->rwmutex, NULL);

		job_queue->has_jobs = bsem_new ();
		bsem_init (job_queue->has_jobs, 0);
	}

	return job_queue;

}

// add a new job to the queue
// returns 0 on success, 1 on error
int job_queue_push (JobQueue *job_queue, Job *job) {

	int retval = 1;

	if (job_queue && job) {
		pthread_mutex_lock (job_queue->rwmutex);

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
			job
		);

		bsem_post (job_queue->has_jobs);

		pthread_mutex_unlock (job_queue->rwmutex);
	}

	return retval;

}

// get the job at the start of the queue
Job *job_queue_pull (JobQueue *job_queue) {

	Job *retval = NULL;

	if (job_queue) {
		pthread_mutex_lock (job_queue->rwmutex);

		switch (job_queue->queue->size) {
			case 0: break;

			case 1:
				// remove at the start of the list
				retval = (Job *) dlist_remove_element (job_queue->queue, NULL);
				break;

			default:
				// remove at the start of the list
				retval = (Job *) dlist_remove_element (job_queue->queue, NULL);
				bsem_post (job_queue->has_jobs);
				break;
		}

		pthread_mutex_unlock (job_queue->rwmutex);
	}

	return retval;

}

// clears the job queue -> destroys all jobs
void job_queue_clear (JobQueue *job_queue) {

	if (job_queue) {
		dlist_reset (job_queue->queue);

		bsem_reset (job_queue->has_jobs);
	}

}