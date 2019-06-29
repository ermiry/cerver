/**********************************
 * @author      Johan Hanssen Seferidis
 * License:     MIT
 **********************************/

#ifndef _CERVER_THPOOL_H_
#define _CERVER_THPOOL_H_

#include <pthread.h>

#include "cerver/types/string.h"

struct threadpool;

// binary semaphore
typedef struct bsem {

	pthread_mutex_t mutex;
	pthread_cond_t   cond;
	int v;

} bsem;

typedef struct job {
	
	struct job *prev;                   /* pointer to previous job   */
	void (*function)(void* arg);        /* function pointer          */
	void *arg;                          /* function's argument       */

} job;

typedef struct jobqueue {

	pthread_mutex_t rwmutex;             /* used for queue r/w access */
	job  *front;                         /* pointer to front of queue */
	job  *rear;                          /* pointer to rear  of queue */
	bsem *has_jobs;                      /* flag as binary semaphore  */
	int   len;                           /* number of jobs in queue   */

} jobqueue;

typedef struct thread {

	int id;                        		 /* friendly id               */
	pthread_t pthread;                   /* pointer to actual thread  */
	struct threadpool *thpool_p;         /* access to thpool          */

} thread;

typedef struct threadpool {

	String *name;

	thread **threads;                    /* pointer to threads        */
	volatile int num_threads_alive;      /* threads currently alive   */
	volatile int num_threads_working;    /* threads currently working */
	pthread_mutex_t  thcount_lock;       /* used for thread count etc */
	pthread_cond_t  threads_all_idle;    /* signal to thpool_wait     */
	jobqueue  *job_queue;                 /* job queue                 */

} threadpool;

//  * Initializes a threadpool. This function will not return untill all
//  * threads have initialized successfully.
extern threadpool *thpool_create (const char *name, unsigned int num_threads);

//  Takes an action and its argument and adds it to the threadpool's job queue.
//  * If you want to add to work a function with more than one arguments then
//  * a way to implement this is by passing a pointer to a structure.
extern int thpool_add_work (threadpool *thpool_p, void (*function_p)(void*), void* arg_p);

//  * Will wait for all jobs - both queued and currently running to finish.
//  * Once the queue is empty and all work has completed, the calling thread
//  * (probably the main program) will continue.
//  *
//  * Smart polling is used in wait. The polling is initially 0 - meaning that
//  * there is virtually no polling at all. If after 1 seconds the threads
//  * haven't finished, the polling interval starts growing exponentially
//  * untill it reaches max_secs seconds. Then it jumps down to a maximum polling
//  * interval assuming that heavy processing is being used in the threadpool.
extern void thpool_wait (threadpool *thpool_p);

//  * The threads will be paused no matter if they are idle or working.
//  * The threads return to their previous states once thpool_resume
//  * is called.
//  *
//  * While the thread is being paused, new work can be added.
extern void thpool_pause (threadpool *thpool_p);

// Unpauses all threads if they are paused
extern void thpool_resume (threadpool *thpool_p);

// gets how many threads are active
extern int thpool_num_threads_working (threadpool *thpool_p);

//  * This will wait for the currently active threads to finish and then 'kill'
//  * the whole threadpool to free up memory.
extern void thpool_destroy (threadpool *thpool_p);

#endif