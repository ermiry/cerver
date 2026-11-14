#ifndef _CERVER_THREADS_MANAGER_H_
#define _CERVER_THREADS_MANAGER_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/worker.h"

#define WORKER_MANAGER_NAME_SIZE					64

// the most workers that a manager can have
#define WORKER_MANAGER_MAX_WORKERS					256

#define WORKER_MANAGER_DEFAULT_MIN_WORKERS			1
#define WORKER_MANAGER_DEFAULT_MAX_WORKERS			8

// a job that waits this long (ms) without a free worker makes the manager add one
#define WORKER_MANAGER_DEFAULT_SCALE_UP_WAIT		64

// a worker without a job for this long (ms) is removed, down to the minimum
#define WORKER_MANAGER_DEFAULT_IDLE_TIMEOUT			4096

// how often (ms) the manager checks its workers when nothing wakes it up
#define WORKER_MANAGER_DEFAULT_CHECK_INTERVAL		128

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WorkerManagerStats {

	unsigned int workers;			// the workers that the manager has now
	unsigned int available;			// workers without a job
	unsigned int working;			// workers running a job
	unsigned int stopped;			// workers that were stopped

	u64 created_workers;			// workers that were added, the first ones too
	u64 removed_workers;			// idle workers that were removed
	unsigned int max_workers;		// the most workers that it had at the same time

	JobQueueStats queue;			// the stats of the queue of the workers

} WorkerManagerStats;

// a group of workers that take their jobs from the same queue,
// the manager adds workers while the jobs wait and removes the idle ones,
// always between its minimum and its maximum
typedef struct WorkerManager {

	char name[WORKER_MANAGER_NAME_SIZE];

	JobQueue *job_queue;

	// the workers and the values below are protected by the mutex
	pthread_mutex_t mutex;
	pthread_cond_t wake;

	Worker *workers[WORKER_MANAGER_MAX_WORKERS];
	unsigned int n_workers;
	unsigned int next_id;

	unsigned int min_workers;
	unsigned int max_workers;

	u32 scale_up_wait;
	u32 idle_timeout;
	u32 check_interval;

	// the work of the jobs that are pushed without one
	void (*work) (void *args);

	bool running;
	bool stopped;
	pthread_t control_thread_id;

	u64 created_workers;
	u64 removed_workers;
	unsigned int peak_workers;

} WorkerManager;

// creates a manager with a queue of JOB_QUEUE_DEFAULT_PRIORITIES priorities
CERVER_PUBLIC WorkerManager *worker_manager_create (const char *name);

// ends the manager if it is running and deletes it with its workers and its queue
CERVER_PUBLIC void worker_manager_delete (WorkerManager *manager);

// sets the least and the most workers (1 to WORKER_MANAGER_MAX_WORKERS), before the start
// returns 0 on success, 1 if the limits are not valid
CERVER_PUBLIC unsigned int worker_manager_set_limits (
	WorkerManager *manager,
	const unsigned int min_workers, const unsigned int max_workers
);

// sets when the manager adds a worker (a job that waits scale_up_wait ms)
// and when it removes one (a worker without a job for idle_timeout ms)
CERVER_PUBLIC void worker_manager_set_scaling (
	WorkerManager *manager,
	const u32 scale_up_wait, const u32 idle_timeout
);

// sets the work of the jobs that are pushed without one
CERVER_PUBLIC void worker_manager_set_work (
	WorkerManager *manager, void (*work) (void *args)
);

// starts the minimum workers and the thread that scales them
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int worker_manager_start (WorkerManager *manager);

// stops every worker after its job, the jobs stay in the queue
// returns 0 on success, 1 if it was not running
CERVER_PUBLIC unsigned int worker_manager_stop (WorkerManager *manager);

// the stopped workers take jobs again
// returns 0 on success, 1 if it was not stopped
CERVER_PUBLIC unsigned int worker_manager_resume (WorkerManager *manager);

// ends the scaling thread and every worker after its job
// returns 0 on success, 1 if it was not running
CERVER_PUBLIC unsigned int worker_manager_end (WorkerManager *manager);

// pushes a job with the default priority
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int worker_manager_push_job (
	WorkerManager *manager,
	void (*work) (void *args), void *args
);

// pushes a job with a priority, 0 is the highest one
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int worker_manager_push_job_priority (
	WorkerManager *manager,
	void (*work) (void *args), void *args,
	const unsigned int priority
);

// returns how many workers the manager has now
CERVER_PUBLIC unsigned int worker_manager_get_n_workers (WorkerManager *manager);

// copies the state of each worker, up to max_states
// returns how many states were copied
CERVER_PUBLIC unsigned int worker_manager_get_states (
	WorkerManager *manager,
	WorkerState *states, const unsigned int max_states
);

CERVER_PUBLIC void worker_manager_get_stats (
	WorkerManager *manager, WorkerManagerStats *stats
);

#ifdef __cplusplus
}
#endif

#endif
