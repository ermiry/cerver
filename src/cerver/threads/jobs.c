#include <stdlib.h>

#include "cerver/threads/jobs.h"
#include "cerver/threads/common.h"

Job *job_new (void) {

	Job *job = (Job *) malloc (sizeof (Job));
	if (job) {
		job->prev = NULL;
		job->method = NULL;
		job->args = NULL;
	}

	return job;

}

void job_delete (void *job_ptr) {

	if (job_ptr) free (job_ptr);

}

JobQueue *job_queue_new (void) {

	JobQueue *job_queue = (JobQueue *) malloc (sizeof (JobQueue));
	if (job_queue) {
		job_queue->front = NULL;
		job_queue->rear = NULL;

		job_queue->size = 0;

		job_queue->rwmutex = NULL;
		job_queue->has_jobs = NULL;
	}

	return job_queue;

}

void job_queue_delete (void *job_queue_ptr) {

	if (job_queue_ptr) {
		JobQueue *job_queue = (JobQueue *) job_queue_ptr;

		// FIXME: delete queue

		// FIXME: delet bsem
	}

}