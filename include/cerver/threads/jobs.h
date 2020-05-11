#ifndef _CERVER_THREADS_JOBS_H_
#define _CERVER_THREADS_JOBS_H_

#include <pthread.h>

#include "cerver/collections/dllist.h"

#include "cerver/threads/common.h"

typedef struct Job {

	// struct Job *prev;
	void (*method) (void *args);
	void *args;

} Job;

extern Job *job_new (void);

extern void job_delete (void *job_ptr);

extern Job *job_create (void (*method) (void *args), void *args);

typedef struct JobQueue {

	// Job *front;
	// Job *rear;

	// size_t size;

	DoubleList *queue;

	pthread_mutex_t *rwmutex;             // used for queue r/w access
	bsem *has_jobs;
	
} JobQueue;

extern JobQueue *job_queue_new (void);

extern void job_queue_delete (void *job_queue_ptr);

extern JobQueue *job_queue_create (void);

// add a new job to the queue
// returns 0 on success, 1 on error
extern int job_queue_push (JobQueue *job_queue, Job *job);

// get the job at the start of the queue
extern Job *job_queue_pull (JobQueue *job_queue);

// clears the job queue -> destroys all jobs
extern void job_queue_clear (JobQueue *job_queue);

#endif