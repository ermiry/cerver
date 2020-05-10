#ifndef _CERVER_THREADS_JOBS_H_
#define _CERVER_THREADS_JOBS_H_

#include <pthread.h>

#include "cerver/threads/common.h"

typedef struct Job {

	struct Job *prev;
	void (*method) (void *args);
	void *args;

} Job;

extern Job *job_new (void);

extern void job_delete (void *job_ptr);

typedef struct JobQueue {

	Job *front;
	Job *rear;

	size_t size;

	pthread_mutex_t *rwmutex;             // used for queue r/w access
	bsem *has_jobs;
	
} JobQueue;

extern JobQueue *job_queue_new (void);

extern void job_queue_delete (void *job_queue_ptr);

#endif