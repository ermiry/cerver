/**
 * @file cerver/threads/worker.h
 * @brief A worker: a thread that takes the jobs of a queue, and that can be stopped, resumed and ended.
 */

#ifndef _CERVER_THREADS_WORKER_H_
#define _CERVER_THREADS_WORKER_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#include "cerver/config.h"

#include "cerver/threads/jobs.h"

/**
 * @defgroup threads_worker Worker
 * @ingroup threads
 * @brief A thread that runs the jobs of its own queue or of a queue that it shares.
 * @{
 */

/** @brief The size of the name of a worker, with its terminator. */
#define WORKER_NAME_SIZE		64

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the states of a worker: the number, the name and the string.
 *
 * How a worker moves between them:
 * - NONE -> AVAILABLE (worker_start ()) or STOPPED (worker_start_with_state ());
 * - AVAILABLE -> WORKING when it takes a job, WORKING -> AVAILABLE when the job ends;
 * - AVAILABLE / WORKING -> STOPPED (worker_stop ()), after the job that it is running;
 * - STOPPED -> AVAILABLE (worker_resume ());
 * - AVAILABLE / WORKING / STOPPED -> ENDED (worker_end ()), after the job that it is running.
 *
 * Every change is made with the mutex of the worker and signaled in its state condition.
 */
#define WORKER_STATE_MAP(XX)		\
	XX(0,	NONE, 		None)		\
	XX(1,	AVAILABLE, 	Available)	\
	XX(2,	WORKING, 	Working)	\
	XX(3,	STOPPED, 	Stopped)	\
	XX(4,	ENDED, 		Ended)

/**
 * @brief The state of a worker, one `WORKER_STATE_<name>` for each row of #WORKER_STATE_MAP.
 */
typedef enum WorkerState {

	#define XX(num, name, string) WORKER_STATE_##name = num,
	WORKER_STATE_MAP (XX)
	#undef XX

} WorkerState;

/**
 * @brief Returns the string of a state.
 *
 * @param[in] state the state
 * @return the string of its row in #WORKER_STATE_MAP, or the one of WORKER_STATE_NONE for a value out of the table;
 * a static string that must not be freed
 */
CERVER_EXPORT const char *worker_state_to_string (
	const WorkerState state
);

/**
 * @brief A worker.
 */
struct _Worker {

	/** @brief The id of the worker. */
	unsigned int id;

	/** @brief The length of the name. */
	unsigned int name_len;
	/** @brief The name, that names its thread; `worker-<id>` when it is not set. */
	char name[WORKER_NAME_SIZE];

	/** @brief The state, protected by the mutex. */
	WorkerState state;

	/** @brief The thread of the worker. */
	pthread_t thread_id;
	/** @brief The thread was created and has not been joined. */
	bool joinable;

	/** @brief A stop was requested; read with __atomic, the thread checks it with the lock of its queue. */
	bool stop;
	/** @brief An end was requested; read with __atomic, the thread checks it with the lock of its queue. */
	bool end;

	/** @brief Since when the worker is available without a job (CLOCK_MONOTONIC). */
	struct timespec idle_since;

	/** @brief The queue of the jobs. */
	JobQueue *job_queue;

	/** @brief The worker deletes its queue; a worker of a manager shares the queue of the manager. */
	bool owns_queue;

	/** @brief The work of the jobs that do not have one. */
	void (*work) (void *args);
	/** @brief Releases the argument of each job after its work, NULL to keep it. */
	void (*delete_data) (void *args);

	/** @brief A reference that remove_reference gets after each job. */
	const void *reference;
	/** @brief Called with the reference after each job, NULL for none. */
	void (*remove_reference) (const void *args);

	/** @brief Protects the state. */
	pthread_mutex_t mutex;
	/** @brief Signaled on every change of the state. */
	pthread_cond_t state_changed;

};

/** @brief A worker, see struct _Worker. */
typedef struct _Worker Worker;

/**
 * @brief Ends a worker if it is running, joins its thread and deletes it, with its queue when it owns it.
 *
 * @param[in] worker_ptr the worker, it can be NULL
 */
CERVER_PUBLIC void worker_delete (void *worker_ptr);

/**
 * @brief Creates a worker with its own queue of JOB_QUEUE_TYPE_JOBS.
 *
 * @return the new worker, released with worker_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Worker *worker_create (void);

/**
 * @brief Creates a worker with its own queue and an id.
 *
 * @param[in] id the id
 * @return the new worker, released with worker_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Worker *worker_create_with_id (
	const unsigned int id
);

/**
 * @brief Creates a worker that takes its jobs from a queue that it shares with other workers, and does not delete.
 *
 * @param[in] id the id
 * @param[in] job_queue the queue, of JOB_QUEUE_TYPE_JOBS
 * @return the new worker, released with worker_delete (); NULL for a NULL queue or if it could not be allocated
 */
CERVER_PUBLIC Worker *worker_create_with_queue (
	const unsigned int id, JobQueue *job_queue
);

/**
 * @brief Sets the name of a worker, that names its thread.
 *
 * @param[in,out] worker the worker
 * @param[in] name the name, truncated to WORKER_NAME_SIZE - 1 characters; NULL keeps the current one
 * @note Call it before the worker starts.
 */
CERVER_PUBLIC void worker_set_name (
	Worker *worker, const char *name
);

/**
 * @brief Returns the state of a worker.
 *
 * @param[in] worker the worker
 * @return the state, WORKER_STATE_NONE for a NULL worker
 */
CERVER_PUBLIC WorkerState worker_get_state (
	Worker *worker
);

/**
 * @brief Sets the state of a worker and signals the change.
 *
 * @param[in,out] worker the worker
 * @param[in] state the state
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void worker_set_state (
	Worker *worker, const WorkerState state
);

/**
 * @brief Returns whether a stop was requested.
 *
 * @param[in] worker the worker
 * @return the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool worker_get_stop (
	Worker *worker
);

/**
 * @brief Sets the request of a stop.
 *
 * @param[in,out] worker the worker
 * @param[in] stop the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void worker_set_stop (
	Worker *worker, const bool stop
);

/**
 * @brief Returns whether an end was requested.
 *
 * @param[in] worker the worker
 * @return the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool worker_get_end (
	Worker *worker
);

/**
 * @brief Sets the request of an end.
 *
 * @param[in,out] worker the worker
 * @param[in] end the request
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void worker_set_end (
	Worker *worker, const bool end
);

/**
 * @brief Returns the milliseconds that an available worker has been without a job.
 *
 * @param[in] worker the worker
 * @return the milliseconds, 0 if it is not available or NULL
 */
CERVER_PUBLIC u64 worker_get_idle_milliseconds (Worker *worker);

/**
 * @brief Sets the work of the jobs that are pushed without one (worker_push_job ()).
 *
 * @param[in,out] worker the worker, NULL is ignored
 * @param[in] work the work
 */
CERVER_PUBLIC void worker_set_work (
	Worker *worker, void (*work) (void *args)
);

/**
 * @brief Sets how the argument of each job is released after its work.
 *
 * @param[in,out] worker the worker, NULL is ignored
 * @param[in] delete_data releases the argument, NULL to keep it
 */
CERVER_PUBLIC void worker_set_delete_data (
	Worker *worker, void (*delete_data) (void *args)
);

/**
 * @brief Sets the reference that remove_reference gets after each job.
 *
 * @param[in,out] worker the worker, NULL is ignored
 * @param[in] reference the reference
 */
CERVER_PUBLIC void worker_set_reference (
	Worker *worker, const void *reference
);

/**
 * @brief Sets the method that gets the reference after each job.
 *
 * @param[in,out] worker the worker, NULL is ignored
 * @param[in] remove_reference the method, NULL for none
 */
CERVER_PUBLIC void worker_set_remove_reference (
	Worker *worker, void (*remove_reference) (const void *args)
);

/**
 * @brief Starts the thread of a worker in AVAILABLE or STOPPED.
 *
 * @param[in,out] worker the worker
 * @param[in] worker_state WORKER_STATE_AVAILABLE or WORKER_STATE_STOPPED
 * @retval 0 the thread was started
 * @retval 1 a NULL worker, one that is not in NONE, another state, or the thread could not be created
 */
CERVER_PUBLIC unsigned int worker_start_with_state (
	Worker *worker, const WorkerState worker_state
);

/**
 * @brief Starts the thread of a worker in AVAILABLE.
 *
 * @param[in,out] worker the worker
 * @retval 0 the thread was started
 * @retval 1 a NULL worker, one that is not in NONE, or the thread could not be created
 */
CERVER_PUBLIC unsigned int worker_start (Worker *worker);

/**
 * @brief A stopped worker takes jobs again.
 *
 * @param[in,out] worker the worker
 * @retval 0 the worker is available
 * @retval 1 a NULL worker or it was not stopped
 */
CERVER_PUBLIC unsigned int worker_resume (Worker *worker);

/**
 * @brief Waits until the worker ends the job that it is running and stops.
 *
 * @param[in,out] worker the worker
 * @retval 0 the worker is stopped (or ended)
 * @retval 1 a NULL worker or it was not available or working
 */
CERVER_PUBLIC unsigned int worker_stop (Worker *worker);

/**
 * @brief Waits until the worker ends the job that it is running, and joins its thread.
 *
 * @param[in,out] worker the worker
 * @retval 0 the worker was ended
 * @retval 1 a NULL worker or it was not running
 */
CERVER_PUBLIC unsigned int worker_end (Worker *worker);

/**
 * @brief Pushes a job without a work to the queue of a worker: its work is the one of worker_set_work ().
 *
 * @param[in,out] worker the worker
 * @param[in] args the argument of the work
 * @retval 0 the job was pushed
 * @retval 1 it could not be pushed, or the worker is NULL
 */
CERVER_PUBLIC unsigned int worker_push_job (
	Worker *worker, void *args
);

/**
 * @brief Pushes a job with its work to the queue of a worker.
 *
 * @param[in,out] worker the worker
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @retval 0 the job was pushed
 * @retval 1 it could not be pushed, or the worker is NULL
 */
CERVER_PUBLIC unsigned int worker_push_job_with_work (
	Worker *worker,
	void (*work) (void *args), void *args
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
