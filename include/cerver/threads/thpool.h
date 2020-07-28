#ifndef _THREADS_THPOOL_H_
#define _THREADS_THPOOL_H_

#include <stdbool.h>
#include <pthread.h>

#include "cerver/threads/jobs.h"

struct _PoolThread;

typedef struct Thpool {

    const char *name;

    unsigned int n_threads;
    struct _PoolThread **threads;

    volatile bool keep_alive;
    volatile unsigned int num_threads_alive;
    volatile unsigned int num_threads_working;

    pthread_mutex_t *mutex;
    pthread_cond_t *threads_all_idle;

    JobQueue *job_queue;

} Thpool;

// creates a new thpool with n threads
extern Thpool *thpool_create (unsigned int n_threads);

// initializes the thpool
// must be called after thpool_create ()
// returns 0 on success, 1 on error
extern unsigned int thpool_init (Thpool *thpool);

// sets the name for the thpool
extern void thpool_set_name (Thpool *thpool, const char *name);

// gets the current number of threads that are alive (running) in the thpool
extern unsigned int thpool_get_num_threads_alive (Thpool *thpool);

// gets the current number of threads that are busy working in a job
extern unsigned int thpool_get_num_threads_working (Thpool *thpool);

// returns true if the thpool does NOT have any working thread
extern bool thpool_is_empty (Thpool *thpool);

// returns true if the thpool has ALL its threads working
extern bool thpool_is_full (Thpool *thpool);

// adds a work to the thpool's job queue
// it will be executed once it is the next in line and a thread is free
extern int thpool_add_work (Thpool *thpool, void (*work) (void *), void *args);

// wait until all jobs have finished
extern void thpool_wait (Thpool *thpool);

// destroys the thpool and deletes all of its data
extern void thpool_destroy (Thpool *thpool);

#endif