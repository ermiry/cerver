#ifndef _CERVER_THREADS_WORKER_H_
#define _CERVER_THREADS_WORKER_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#include "cerver/config.h"

#include "cerver/threads/jobs.h"

#define WORKER_NAME_SIZE		64

#ifdef __cplusplus
extern "C" {
#endif

// the states of a worker and how it moves between them:
// NONE -> AVAILABLE (worker_start ()) or STOPPED (worker_start_with_state ())
// AVAILABLE -> WORKING when it takes a job, WORKING -> AVAILABLE when the job ends
// AVAILABLE / WORKING -> STOPPED (worker_stop ()), after the job that it is running
// STOPPED -> AVAILABLE (worker_resume ())
// AVAILABLE / WORKING / STOPPED -> ENDED (worker_end ()), after the job that it is running
// every change is made with the worker's mutex and signaled in its state condition
#define WORKER_STATE_MAP(XX)		\
	XX(0,	NONE, 		None)		\
	XX(1,	AVAILABLE, 	Available)	\
	XX(2,	WORKING, 	Working)	\
	XX(3,	STOPPED, 	Stopped)	\
	XX(4,	ENDED, 		Ended)

typedef enum WorkerState {

	#define XX(num, name, string) WORKER_STATE_##name = num,
	WORKER_STATE_MAP (XX)
	#undef XX

} WorkerState;

CERVER_EXPORT const char *worker_state_to_string (
	const WorkerState state
);

struct _Worker {

	unsigned int id;

	unsigned int name_len;
	char name[WORKER_NAME_SIZE];

	WorkerState state;

	pthread_t thread_id;
	bool joinable;

	// requests read with __atomic, the thread checks them with the lock of its queue
	bool stop;
	bool end;

	// since when the worker is available without a job (CLOCK_MONOTONIC)
	struct timespec idle_since;

	JobQueue *job_queue;

	// a worker of a manager shares the queue of the manager, it does not delete it
	bool owns_queue;

	void (*work) (void *args);
	void (*delete_data) (void *args);

	const void *reference;
	void (*remove_reference) (const void *args);

	pthread_mutex_t mutex;
	pthread_cond_t state_changed;

};

typedef struct _Worker Worker;

// ends the worker if it is running and deletes it
CERVER_PUBLIC void worker_delete (void *worker_ptr);

CERVER_PUBLIC Worker *worker_create (void);

CERVER_PUBLIC Worker *worker_create_with_id (
	const unsigned int id
);

// a worker that takes its jobs from a queue that it shares with other workers
CERVER_PUBLIC Worker *worker_create_with_queue (
	const unsigned int id, JobQueue *job_queue
);

CERVER_PUBLIC void worker_set_name (
	Worker *worker, const char *name
);

CERVER_PUBLIC WorkerState worker_get_state (
	Worker *worker
);

CERVER_PRIVATE void worker_set_state (
	Worker *worker, const WorkerState state
);

CERVER_PRIVATE bool worker_get_stop (
	Worker *worker
);

CERVER_PRIVATE void worker_set_stop (
	Worker *worker, const bool stop
);

CERVER_PRIVATE bool worker_get_end (
	Worker *worker
);

CERVER_PRIVATE void worker_set_end (
	Worker *worker, const bool end
);

// returns the milliseconds that an available worker has been without a job, 0 if it is not available
CERVER_PUBLIC u64 worker_get_idle_milliseconds (Worker *worker);

CERVER_PUBLIC void worker_set_work (
	Worker *worker, void (*work) (void *args)
);

CERVER_PUBLIC void worker_set_delete_data (
	Worker *worker, void (*delete_data) (void *args)
);

CERVER_PUBLIC void worker_set_reference (
	Worker *worker, const void *reference
);

CERVER_PUBLIC void worker_set_remove_reference (
	Worker *worker, void (*remove_reference) (const void *args)
);

// starts the worker's thread in AVAILABLE or STOPPED
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int worker_start_with_state (
	Worker *worker, const WorkerState worker_state
);

CERVER_PUBLIC unsigned int worker_start (Worker *worker);

// a stopped worker takes jobs again
// returns 0 on success, 1 if it was not stopped
CERVER_PUBLIC unsigned int worker_resume (Worker *worker);

// waits until the worker ends the job that it is running and stops
// returns 0 on success, 1 if it was not running
CERVER_PUBLIC unsigned int worker_stop (Worker *worker);

// waits until the worker ends the job that it is running and joins its thread
// returns 0 on success, 1 if it was not running
CERVER_PUBLIC unsigned int worker_end (Worker *worker);

CERVER_PUBLIC unsigned int worker_push_job (
	Worker *worker, void *args
);

CERVER_PUBLIC unsigned int worker_push_job_with_work (
	Worker *worker,
	void (*work) (void *args), void *args
);

#ifdef __cplusplus
}
#endif

#endif
