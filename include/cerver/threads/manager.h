/**
 * @file cerver/threads/manager.h
 * @brief A worker manager: workers that take the jobs of the same queue, added while the jobs wait and removed when
 * they are idle.
 */

#ifndef _CERVER_THREADS_MANAGER_H_
#define _CERVER_THREADS_MANAGER_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/worker.h"

/**
 * @defgroup threads_manager Worker manager
 * @ingroup threads
 * @brief A group of workers that scales between a minimum and a maximum.
 * @{
 */

/** @brief The size of the name of a manager, with its terminator. */
#define WORKER_MANAGER_NAME_SIZE					64

/** @brief The most workers that a manager can have. */
#define WORKER_MANAGER_MAX_WORKERS					256

/** @brief The default least workers of a manager. */
#define WORKER_MANAGER_DEFAULT_MIN_WORKERS			1

/** @brief The default most workers of a manager. */
#define WORKER_MANAGER_DEFAULT_MAX_WORKERS			8

/** @brief A job that waits this long (ms) without a free worker makes the manager add one. */
#define WORKER_MANAGER_DEFAULT_SCALE_UP_WAIT		64

/** @brief A worker without a job for this long (ms) is removed, down to the minimum. */
#define WORKER_MANAGER_DEFAULT_IDLE_TIMEOUT			4096

/** @brief How often (ms) the manager checks its workers when nothing wakes it up. */
#define WORKER_MANAGER_DEFAULT_CHECK_INTERVAL		128

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The stats of a manager, read them with worker_manager_get_stats ().
 */
typedef struct WorkerManagerStats {

	/** @brief The workers that the manager has now. */
	unsigned int workers;
	/** @brief The workers without a job. */
	unsigned int available;
	/** @brief The workers running a job. */
	unsigned int working;
	/** @brief The workers that were stopped. */
	unsigned int stopped;

	/** @brief The workers that were added, the first ones too. */
	u64 created_workers;
	/** @brief The idle workers that were removed. */
	u64 removed_workers;
	/** @brief The most workers that it had at the same time. */
	unsigned int max_workers;

	/** @brief The stats of the queue of the workers. */
	JobQueueStats queue;

} WorkerManagerStats;

/**
 * @brief A group of workers that take their jobs from the same queue.
 *
 * The manager adds workers while the jobs wait and removes the idle ones, always between its minimum and its
 * maximum.
 */
typedef struct WorkerManager {

	/** @brief The name of the manager. */
	char name[WORKER_MANAGER_NAME_SIZE];

	/** @brief The queue that the workers share. */
	JobQueue *job_queue;

	/** @brief Protects the workers and the values below. */
	pthread_mutex_t mutex;
	/** @brief Wakes up the thread that scales the workers. */
	pthread_cond_t wake;

	/** @brief The workers. */
	Worker *workers[WORKER_MANAGER_MAX_WORKERS];
	/** @brief The number of workers. */
	unsigned int n_workers;
	/** @brief The id of the next worker. */
	unsigned int next_id;

	/** @brief The least workers. */
	unsigned int min_workers;
	/** @brief The most workers. */
	unsigned int max_workers;

	/** @brief The milliseconds that a job waits before a worker is added. */
	u32 scale_up_wait;
	/** @brief The milliseconds without a job before a worker is removed. */
	u32 idle_timeout;
	/** @brief The milliseconds between two checks of the workers. */
	u32 check_interval;

	/** @brief The work of the jobs that are pushed without one. */
	void (*work) (void *args);

	/** @brief The manager was started. */
	bool running;
	/** @brief The workers were stopped. */
	bool stopped;
	/** @brief The thread that scales the workers. */
	pthread_t control_thread_id;

	/** @brief The workers that were added. */
	u64 created_workers;
	/** @brief The workers that were removed. */
	u64 removed_workers;
	/** @brief The most workers that it had at the same time. */
	unsigned int peak_workers;

} WorkerManager;

/**
 * @brief Creates a manager with a queue of #JOB_QUEUE_DEFAULT_PRIORITIES priorities.
 *
 * @param[in] name the name, truncated to WORKER_MANAGER_NAME_SIZE - 1 characters; it can be NULL
 * @return the new manager, released with worker_manager_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC WorkerManager *worker_manager_create (const char *name);

/**
 * @brief Ends the manager if it is running and deletes it with its workers and its queue.
 *
 * @param[in] manager the manager, it can be NULL
 */
CERVER_PUBLIC void worker_manager_delete (WorkerManager *manager);

/**
 * @brief Sets the least and the most workers.
 *
 * @param[in,out] manager the manager
 * @param[in] min_workers the least workers, at least 1
 * @param[in] max_workers the most workers, from min_workers to #WORKER_MANAGER_MAX_WORKERS
 * @retval 0 the limits were set
 * @retval 1 a NULL manager or limits that are not valid
 * @note Call it before worker_manager_start ().
 */
CERVER_PUBLIC unsigned int worker_manager_set_limits (
	WorkerManager *manager,
	const unsigned int min_workers, const unsigned int max_workers
);

/**
 * @brief Sets when the manager adds a worker and when it removes one.
 *
 * @param[in,out] manager the manager
 * @param[in] scale_up_wait a job that waits these milliseconds adds a worker, the default is
 * #WORKER_MANAGER_DEFAULT_SCALE_UP_WAIT
 * @param[in] idle_timeout a worker without a job for these milliseconds is removed, the default is
 * #WORKER_MANAGER_DEFAULT_IDLE_TIMEOUT
 */
CERVER_PUBLIC void worker_manager_set_scaling (
	WorkerManager *manager,
	const u32 scale_up_wait, const u32 idle_timeout
);

/**
 * @brief Sets the work of the jobs that are pushed without one.
 *
 * @param[in,out] manager the manager
 * @param[in] work the work, given to the workers that are added after the call
 * @note Call it before worker_manager_start ().
 */
CERVER_PUBLIC void worker_manager_set_work (
	WorkerManager *manager, void (*work) (void *args)
);

/**
 * @brief Starts the minimum workers and the thread that scales them.
 *
 * @param[in,out] manager the manager
 * @retval 0 the manager was started
 * @retval 1 a NULL manager, one that is already running, or a worker or the thread could not be started (then it is
 * ended)
 */
CERVER_PUBLIC unsigned int worker_manager_start (WorkerManager *manager);

/**
 * @brief Stops every worker after its job; the jobs stay in the queue.
 *
 * @param[in,out] manager the manager
 * @retval 0 the workers were stopped
 * @retval 1 a NULL manager, or it was not running or already stopped
 */
CERVER_PUBLIC unsigned int worker_manager_stop (WorkerManager *manager);

/**
 * @brief The stopped workers take jobs again.
 *
 * @param[in,out] manager the manager
 * @retval 0 the workers were resumed
 * @retval 1 a NULL manager, or it was not stopped
 */
CERVER_PUBLIC unsigned int worker_manager_resume (WorkerManager *manager);

/**
 * @brief Ends the thread that scales the workers and deletes every worker after its job.
 *
 * @param[in,out] manager the manager
 * @retval 0 the manager was ended
 * @retval 1 a NULL manager or it was not running
 */
CERVER_PUBLIC unsigned int worker_manager_end (WorkerManager *manager);

/**
 * @brief Pushes a job with the default priority and wakes up the thread that scales the workers.
 *
 * @param[in,out] manager the manager
 * @param[in] work the work, NULL for the work of the manager
 * @param[in] args the argument of the work
 * @retval 0 the job was pushed
 * @retval 1 a NULL manager or it could not be pushed
 */
CERVER_PUBLIC unsigned int worker_manager_push_job (
	WorkerManager *manager,
	void (*work) (void *args), void *args
);

/**
 * @brief Pushes a job with a priority and wakes up the thread that scales the workers.
 *
 * @param[in,out] manager the manager
 * @param[in] work the work, NULL for the work of the manager
 * @param[in] args the argument of the work
 * @param[in] priority the priority, 0 is the highest one
 * @retval 0 the job was pushed
 * @retval 1 a NULL manager or it could not be pushed
 */
CERVER_PUBLIC unsigned int worker_manager_push_job_priority (
	WorkerManager *manager,
	void (*work) (void *args), void *args,
	const unsigned int priority
);

/**
 * @brief Returns how many workers the manager has now.
 *
 * @param[in] manager the manager
 * @return the workers, 0 for a NULL manager
 */
CERVER_PUBLIC unsigned int worker_manager_get_n_workers (WorkerManager *manager);

/**
 * @brief Copies the state of each worker.
 *
 * @param[in] manager the manager
 * @param[out] states the states
 * @param[in] max_states the size of states
 * @return how many states were copied, 0 for a NULL argument
 */
CERVER_PUBLIC unsigned int worker_manager_get_states (
	WorkerManager *manager,
	WorkerState *states, const unsigned int max_states
);

/**
 * @brief Copies the stats of a manager and of its queue.
 *
 * @param[in] manager the manager
 * @param[out] stats the copy
 */
CERVER_PUBLIC void worker_manager_get_stats (
	WorkerManager *manager, WorkerManagerStats *stats
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
