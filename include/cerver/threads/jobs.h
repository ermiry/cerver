/**
 * @file cerver/threads/jobs.h
 * @brief The jobs, the job handlers and the job queues with priorities.
 */

#ifndef _CERVER_THREADS_JOBS_H_
#define _CERVER_THREADS_JOBS_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/threads/thpool.h"

/**
 * @defgroup threads_jobs Jobs
 * @ingroup threads
 * @brief A queue of jobs (or of job handlers) with priorities, that a thread of its own or many workers take.
 * @{
 */

/** @brief The jobs or handlers that the pool of a queue has at the start, it grows when it is empty. */
#define JOB_QUEUE_POOL_INIT					16

/** @brief The default priorities of a queue, 0 is the highest one. */
#define JOB_QUEUE_DEFAULT_PRIORITIES		3

/** @brief The most priorities that a queue can have. */
#define JOB_QUEUE_MAX_PRIORITIES			16

/** @brief The highest priority of a queue with the default priorities. */
#define JOB_QUEUE_PRIORITY_HIGH				0

/** @brief The priority in the middle of a queue with the default priorities, the one of job_queue_push (). */
#define JOB_QUEUE_PRIORITY_NORMAL			1

/** @brief The lowest priority of a queue with the default priorities. */
#define JOB_QUEUE_PRIORITY_LOW				2

/** @brief The jobs that each priority has room for at first, it grows by doubling. */
#define JOB_QUEUE_DEFAULT_RING_CAPACITY		16

/**
 * @brief After these jobs were taken ahead of a waiting job of a lower priority, the next one is taken from the
 * lower priority, so it is never starved.
 */
#define JOB_QUEUE_DEFAULT_STARVATION_LIMIT	8

/** @brief A starvation limit that always takes the highest priority first. */
#define JOB_QUEUE_STARVATION_DISABLED		0

/** @brief A wait without a deadline. */
#define JOB_QUEUE_WAIT_FOREVER				0xFFFFFFFFu

/** @brief The milliseconds of a second. */
#define JOB_QUEUE_MILLISECONDS_PER_SECOND		1000

/** @brief The nanoseconds of a millisecond. */
#define JOB_QUEUE_NANOSECONDS_PER_MILLISECOND	1000000L

/** @brief The nanoseconds of a second. */
#define JOB_QUEUE_NANOSECONDS_PER_SECOND		1000000000L

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Connection;

struct _JobQueue;

/**
 * @brief A job: a work and its argument, with an id for job_queue_request ().
 */
typedef struct Job {

	/** @brief The id of the job, 0 when it does not have one. */
	u64 id;
	/** @brief The work. */
	void (*work) (void *args);
	/** @brief The argument of the work. */
	void *args;

} Job;

/**
 * @brief Allocates an empty job.
 *
 * @return the new job, NULL if it could not be allocated
 */
CERVER_PUBLIC void *job_new (void);

/**
 * @brief Releases a job, not its argument.
 *
 * @param[in] job_ptr the job, it can be NULL
 */
CERVER_PUBLIC void job_delete (void *job_ptr);

/**
 * @brief Compares two jobs by their ids.
 *
 * @param[in] a the first job
 * @param[in] b the second job
 * @return -1, 0 or 1 as the id of a is smaller, equal or bigger; a NULL job goes after the other one
 */
CERVER_PUBLIC int job_comparator (
	const void *a, const void *b
);

/**
 * @brief Allocates a new job; job_get () takes one from the pool of a queue instead.
 *
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @return the new job, NULL if it could not be allocated
 */
CERVER_PUBLIC Job *job_create (
	void (*work) (void *args), void *args
);

/**
 * @brief Takes a job from the pool of a queue, it allocates one only if the pool is empty.
 *
 * @param[in] job_queue a queue of JOB_QUEUE_TYPE_JOBS
 * @return the job with its id in 0, NULL for a NULL queue or if it could not be allocated
 */
CERVER_PUBLIC Job *job_get (struct _JobQueue *job_queue);

/**
 * @brief Sets the id, the work and the argument of a job to 0 and NULL.
 *
 * @param[out] job the job, it can be NULL
 */
CERVER_PUBLIC void job_reset (Job *job);

/**
 * @brief Resets a job and returns it to the pool of its queue, to be used again by job_get ().
 *
 * @param[in] job_queue the queue
 * @param[in] job the job, it can be NULL
 */
CERVER_PUBLIC void job_return (
	struct _JobQueue *job_queue, Job *job
);

/**
 * @brief A job handler: the data of a request that a thread waits for, in a queue of JOB_QUEUE_TYPE_HANDLERS.
 */
typedef struct JobHandler {

	/** @brief The cerver of the request. */
	struct _Cerver *cerver;
	/** @brief The connection of the request. */
	struct _Connection *connection;

	/** @brief Signaled when the handler of the queue is done with it. */
	pthread_cond_t *cond;
	/** @brief Protects done. */
	pthread_mutex_t *mutex;

	/** @brief The handler of the queue is done with it. */
	bool done;

	/** @brief The data that the handler of the queue gets. */
	void *data;
	/** @brief Releases the data when the job handler is reset or deleted, NULL to keep it. */
	void (*data_delete) (void *data_ptr);

} JobHandler;

/**
 * @brief Allocates a job handler without its mutex and its condition.
 *
 * @return the new job handler, NULL if it could not be allocated
 */
CERVER_PUBLIC void *job_handler_new (void);

/**
 * @brief Releases a job handler, its mutex, its condition and its data (with its data_delete).
 *
 * @param[in] job_handler_ptr the job handler, it can be NULL
 */
CERVER_PUBLIC void job_handler_delete (void *job_handler_ptr);

/**
 * @brief Allocates a job handler with its mutex and its condition.
 *
 * @return the new job handler, NULL if it could not be allocated
 */
CERVER_PUBLIC void *job_handler_create (void);

/**
 * @brief Takes a job handler from the pool of a queue, it allocates one only if the pool is empty.
 *
 * @param[in] job_queue a queue of JOB_QUEUE_TYPE_HANDLERS
 * @return the job handler, NULL for a NULL queue or if it could not be allocated
 */
CERVER_PUBLIC JobHandler *job_handler_get (struct _JobQueue *job_queue);

/**
 * @brief Clears a job handler, releasing its data with its data_delete.
 *
 * @param[in,out] job_handler the job handler, it can be NULL
 */
CERVER_PUBLIC void job_handler_reset (JobHandler *job_handler);

/**
 * @brief Marks a job handler as done and wakes up the thread that waits for it.
 *
 * @param[in,out] handler the job handler, with its mutex and its condition; it can be NULL
 */
CERVER_PUBLIC void job_handler_signal (JobHandler *handler);

/**
 * @brief Resets a job handler and returns it to the pool of its queue.
 *
 * @param[in] job_queue the queue
 * @param[in] job_handler the job handler, it can be NULL
 */
CERVER_PUBLIC void job_handler_return (
	struct _JobQueue *job_queue, JobHandler *job_handler
);

/**
 * @brief Pushes the data to a queue of handlers and waits until its handler is done with it.
 *
 * The job handler releases the data with data_delete when it returns to the pool.
 *
 * @param[in] job_queue a queue of JOB_QUEUE_TYPE_HANDLERS, started with its handler
 * @param[in] data the data
 * @param[in] data_delete releases the data, NULL to keep it
 * @warning Without a job handler (a NULL queue or no memory) it returns right away, without releasing the data.
 */
CERVER_PUBLIC void job_handler_wait (
	struct _JobQueue *job_queue,
	void *data, void (*data_delete) (void *data_ptr)
);

/**
 * @brief The X-macro table of the types of a queue: the number, the name and the string.
 */
#define JOB_QUEUE_TYPE_MAP(XX)				\
	XX(0,	NONE, 		None)				\
	XX(1,	JOBS, 		Jobs)				\
	XX(2,	HANDLERS, 	Handlers)

/**
 * @brief The type of a queue, one `JOB_QUEUE_TYPE_<name>` for each row of #JOB_QUEUE_TYPE_MAP: a queue of Job or a
 * queue of JobHandler.
 */
typedef enum JobQueueType {

	#define XX(num, name, string) JOB_QUEUE_TYPE_##name = num,
	JOB_QUEUE_TYPE_MAP (XX)
	#undef XX

} JobQueueType;

/**
 * @brief A job in its priority, with when it was pushed.
 */
typedef struct JobQueueEntry {

	/** @brief The job. */
	void *job;
	/** @brief When it was pushed (CLOCK_MONOTONIC). */
	struct timespec queued;

} JobQueueEntry;

/**
 * @brief The jobs of one priority in the order they were pushed, a ring that grows by doubling.
 */
typedef struct JobQueueRing {

	/** @brief The entries of the ring. */
	JobQueueEntry *entries;
	/** @brief The position of the oldest entry. */
	size_t head;
	/** @brief The entries in the ring. */
	size_t count;
	/** @brief The size of the ring. */
	size_t capacity;

} JobQueueRing;

/**
 * @brief The stats of a queue, read them with job_queue_get_stats ().
 */
typedef struct JobQueueStats {

	/** @brief The jobs that entered the queue. */
	u64 total_jobs;
	/** @brief The jobs that were taken from the queue. */
	u64 pulled_jobs;
	/** @brief The jobs whose work finished, see job_queue_record_run (). */
	u64 completed_jobs;
	/** @brief The jobs given to job_queue_request () by their id. */
	u64 requested_jobs;
	/** @brief The waits that ended at their deadline without a job. */
	u64 timed_out_waits;
	/** @brief The jobs taken ahead of a higher priority, so they were not starved. */
	u64 promoted_jobs;
	/** @brief The most jobs that were in the queue at the same time. */
	u64 max_queued;

	/** @brief The jobs that entered each priority. */
	u64 priority_jobs[JOB_QUEUE_MAX_PRIORITIES];

	/** @brief The microseconds that the pulled jobs waited in the queue. */
	u64 wait_total_microseconds;
	/** @brief The microseconds that the completed jobs ran. */
	u64 run_total_microseconds;

	/** @brief The jobs that waited up to 2^i ms, as in ThpoolStats. */
	u64 wait_histogram[THPOOL_HISTOGRAM_BUCKETS];
	/** @brief The jobs that ran up to 2^i ms, as in ThpoolStats. */
	u64 run_histogram[THPOOL_HISTOGRAM_BUCKETS];

} JobQueueStats;

/**
 * @brief A thread that waits in job_queue_request () for the job with an id, in the list of its queue.
 * @since 1.9b-2
 */
struct _JobQueueRequest {

	/** @brief The id that it waits for. */
	u64 id;
	/** @brief The job that was given to it, NULL until it arrives. */
	void *job;
	/** @brief The next request of the queue. */
	struct _JobQueueRequest *next;

};

/** @brief A thread that waits for a job, see struct _JobQueueRequest. */
typedef struct _JobQueueRequest JobQueueRequest;

/**
 * @brief A queue of jobs or of job handlers with priorities.
 *
 * @threadsafety Every function of a queue is safe from many threads; the fields are protected by its mutex.
 */
struct _JobQueue {

	/** @brief What the queue has: jobs or job handlers. */
	JobQueueType type;

	/** @brief The jobs or job handlers to reuse. */
	Pool *pool;

	/** @brief Protects every value below. */
	pthread_mutex_t mutex;
	/** @brief Signaled when a job is pushed or the queue is signaled. */
	pthread_cond_t has_jobs;
	/** @brief Signaled when the requested job arrives or the queue is signaled. */
	pthread_cond_t has_request;

	/** @brief The priorities of the queue. */
	unsigned int n_priorities;
	/** @brief The jobs of each priority. */
	JobQueueRing rings[JOB_QUEUE_MAX_PRIORITIES];
	/** @brief The jobs in the queue. */
	size_t count;

	/** @brief The jobs that can be taken ahead of a waiting job of a lower priority. */
	unsigned int starvation_limit;
	/** @brief The jobs that were taken ahead of a lower priority in a row. */
	unsigned int starvation_streak;

	/** @brief The signals of job_queue_signal (), so a waiting thread knows that it was signaled. */
	u64 signals;
	/** @brief A signal that job_queue_wait () has not seen yet. */
	bool signaled;

	/**
	 * @brief The threads that wait in job_queue_request (), one request each (it lives in the stack of its thread).
	 * @since 1.9b-2
	 */
	struct _JobQueueRequest *requests;

	/** @brief The stats of the queue. */
	JobQueueStats stats;

	/** @brief The thread of the queue is running, read with __atomic. */
	bool running;
	/** @brief The thread of job_queue_start (). */
	pthread_t handler_thread_id;
	/** @brief The handler of a queue of JOB_QUEUE_TYPE_HANDLERS, it gets the data of each job handler. */
	void (*handler) (void *data);

	/** @brief Destroys the jobs that are still in the queue when it is cleared or deleted. */
	void (*destroy) (void *job);

};

/** @brief A queue of jobs, see struct _JobQueue. */
typedef struct _JobQueue JobQueue;

/**
 * @brief Allocates a queue of JOB_QUEUE_TYPE_NONE, without its pool.
 *
 * @return the new queue, NULL if it could not be allocated
 */
CERVER_PUBLIC JobQueue *job_queue_new (void);

/**
 * @brief Stops the thread of a queue, destroys its jobs and releases it.
 *
 * @param[in] job_queue_ptr the queue, it can be NULL
 */
CERVER_PUBLIC void job_queue_delete (void *job_queue_ptr);

/**
 * @brief Creates a queue with #JOB_QUEUE_DEFAULT_PRIORITIES priorities and the pool of its type.
 *
 * @param[in] type JOB_QUEUE_TYPE_JOBS or JOB_QUEUE_TYPE_HANDLERS
 * @return the new queue, released with job_queue_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC JobQueue *job_queue_create (
	const JobQueueType type
);

/**
 * @brief Creates a queue with some priorities and the pool of its type.
 *
 * @param[in] type JOB_QUEUE_TYPE_JOBS or JOB_QUEUE_TYPE_HANDLERS
 * @param[in] n_priorities the priorities, 0 is taken as 1 and more than #JOB_QUEUE_MAX_PRIORITIES as the max
 * @return the new queue, released with job_queue_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC JobQueue *job_queue_create_with_priorities (
	const JobQueueType type, const unsigned int n_priorities
);

/**
 * @brief Sets the handler of a queue of JOB_QUEUE_TYPE_HANDLERS.
 *
 * @param[in,out] queue the queue
 * @param[in] handler gets the data of each job handler, in the thread of the queue
 * @warning Set it before job_queue_start (): the thread of the queue calls it without checking it.
 */
CERVER_PUBLIC void job_queue_set_handler (
	JobQueue *queue, void (*handler) (void *data)
);

/**
 * @brief Sets how many jobs can be taken ahead of a waiting job of a lower priority before it is taken.
 *
 * @param[in,out] job_queue the queue
 * @param[in] limit the jobs, the default is #JOB_QUEUE_DEFAULT_STARVATION_LIMIT; #JOB_QUEUE_STARVATION_DISABLED
 * always takes the highest priority first
 */
CERVER_PUBLIC void job_queue_set_starvation_limit (
	JobQueue *job_queue, const unsigned int limit
);

/**
 * @brief Returns the priority that job_queue_push () uses, the one in the middle.
 *
 * @param[in] job_queue the queue
 * @return n_priorities / 2, 0 for a NULL queue
 */
CERVER_PUBLIC unsigned int job_queue_default_priority (
	const JobQueue *job_queue
);

/**
 * @brief Returns how many jobs are in the queue.
 *
 * @param[in] job_queue the queue
 * @return the jobs, 0 for a NULL queue
 */
CERVER_PUBLIC size_t job_queue_size (JobQueue *job_queue);

/**
 * @brief Returns the milliseconds that the oldest job in the queue has waited.
 *
 * @param[in] job_queue the queue
 * @return the milliseconds, 0 if it is empty or NULL
 */
CERVER_PUBLIC u64 job_queue_oldest_wait (JobQueue *job_queue);

/**
 * @brief Pushes a job (or a job handler) with the default priority.
 *
 * @param[in,out] job_queue the queue
 * @param[in] job_ptr the job, the queue owns it until it is taken
 * @retval 0 the job was pushed
 * @retval 1 a NULL argument or its priority could not grow
 */
CERVER_PUBLIC unsigned int job_queue_push (
	JobQueue *job_queue, void *job_ptr
);

/**
 * @brief Pushes a job (or a job handler) with a priority.
 *
 * @param[in,out] job_queue the queue
 * @param[in] job_ptr the job, the queue owns it until it is taken
 * @param[in] priority the priority, 0 is the highest one; one that does not exist is the lowest one
 * @retval 0 the job was pushed
 * @retval 1 a NULL argument or its priority could not grow
 */
CERVER_PUBLIC unsigned int job_queue_push_priority (
	JobQueue *job_queue, void *job_ptr, const unsigned int priority
);

/**
 * @brief Takes a job from the pool of a queue of JOB_QUEUE_TYPE_JOBS and pushes it with the default priority.
 *
 * @param[in,out] job_queue the queue
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @retval 0 the job was pushed
 * @retval 1 a NULL queue, without a job, or it could not be pushed
 */
CERVER_PUBLIC unsigned int job_queue_push_job (
	JobQueue *job_queue,
	void (*work) (void *args), void *args
);

/**
 * @brief Takes a job from the pool of a queue of JOB_QUEUE_TYPE_JOBS and pushes it with a priority.
 *
 * @param[in,out] job_queue the queue
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @param[in] priority the priority, 0 is the highest one
 * @retval 0 the job was pushed
 * @retval 1 a NULL queue, without a job, or it could not be pushed
 */
CERVER_PUBLIC unsigned int job_queue_push_job_priority (
	JobQueue *job_queue,
	void (*work) (void *args), void *args,
	const unsigned int priority
);

/**
 * @brief Pushes a job with an id; a thread that waits for it in job_queue_request () gets it directly.
 *
 * @param[in,out] job_queue a queue of JOB_QUEUE_TYPE_JOBS
 * @param[in] job_id the id
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @retval 0 the job was pushed or given
 * @retval 1 a NULL queue, without a job, or it could not be pushed
 */
CERVER_PUBLIC unsigned int job_queue_push_job_with_id (
	JobQueue *job_queue,
	const u64 job_id,
	void (*work) (void *args), void *args
);

/**
 * @brief Takes a job handler from the pool of a queue of JOB_QUEUE_TYPE_HANDLERS and pushes it.
 *
 * @param[in,out] job_queue the queue
 * @param[in] cerver the cerver of the request
 * @param[in] connection the connection of the request
 * @param[in] data the data that the handler gets
 * @param[in] data_delete releases the data, NULL to keep it
 * @retval 0 the job handler was pushed
 * @retval 1 a NULL queue, without a job handler, or it could not be pushed
 */
CERVER_PUBLIC unsigned int job_queue_push_handler (
	JobQueue *job_queue,
	struct _Cerver *cerver,
	struct _Connection *connection,
	void *data, void (*data_delete) (void *data_ptr)
);

/**
 * @brief Takes the next job without waiting.
 *
 * @param[in,out] job_queue the queue
 * @return the job, NULL if the queue is empty or NULL
 */
CERVER_PUBLIC void *job_queue_pull (JobQueue *job_queue);

/**
 * @brief Waits for the next job.
 *
 * @param[in,out] job_queue the queue
 * @param[in] timeout the milliseconds, or #JOB_QUEUE_WAIT_FOREVER
 * @return the job, NULL if the deadline passed, job_queue_signal () was called or the queue is NULL
 */
CERVER_PUBLIC void *job_queue_pull_timeout (
	JobQueue *job_queue, const u32 timeout
);

/**
 * @brief Waits for the next job until interrupted (args) is true.
 *
 * @param[in,out] job_queue the queue
 * @param[in] interrupted called with the lock of the queue, so the wake up is never lost
 * @param[in] args the argument of interrupted
 * @return the job, NULL when it was interrupted or signaled
 * @note The thread that changes what interrupted () reads must call job_queue_signal () after it.
 */
CERVER_PUBLIC void *job_queue_pull_interruptible (
	JobQueue *job_queue,
	bool (*interrupted) (const void *args), const void *args
);

/**
 * @brief Takes the job with an id from the queue, waiting until it is pushed.
 *
 * @param[in,out] job_queue a queue of JOB_QUEUE_TYPE_JOBS
 * @param[in] job_id the id
 * @return the job, NULL for a NULL queue
 * @note Several threads can wait at the same time, each one with its own request (JobQueue::requests).
 */
CERVER_PUBLIC void *job_queue_request (
	JobQueue *job_queue, const u64 job_id
);

/**
 * @brief Takes the job with an id from the queue, waiting up to a timeout until it is pushed.
 *
 * @param[in,out] job_queue a queue of JOB_QUEUE_TYPE_JOBS
 * @param[in] job_id the id
 * @param[in] timeout the milliseconds, or #JOB_QUEUE_WAIT_FOREVER
 * @return the job, NULL if it did not arrive before the deadline or for a NULL queue
 * @note Several threads can wait at the same time, like in job_queue_request ().
 */
CERVER_PUBLIC void *job_queue_request_timeout (
	JobQueue *job_queue, const u64 job_id, const u32 timeout
);

/**
 * @brief Records that a job that was pulled finished its work.
 *
 * @param[in,out] job_queue the queue
 * @param[in] start when the work started (CLOCK_MONOTONIC)
 */
CERVER_PUBLIC void job_queue_record_run (
	JobQueue *job_queue, const struct timespec *start
);

/**
 * @brief Copies the stats of a queue.
 *
 * @param[in] job_queue the queue
 * @param[out] stats the copy
 */
CERVER_PUBLIC void job_queue_get_stats (
	JobQueue *job_queue, JobQueueStats *stats
);

/**
 * @brief Prints the stats of a queue with the cerver log.
 *
 * @param[in] job_queue the queue, it can be NULL
 */
CERVER_PUBLIC void job_queue_stats_print (JobQueue *job_queue);

/**
 * @brief Starts the thread of a queue, that runs its jobs (JOB_QUEUE_TYPE_JOBS) or gives the data of its job
 * handlers to its handler (JOB_QUEUE_TYPE_HANDLERS) until job_queue_stop ().
 *
 * @param[in,out] job_queue the queue, it can be NULL
 * @retval 0 the thread was started, or the queue is NULL
 * @retval 1 a queue of JOB_QUEUE_TYPE_NONE or the thread could not be created
 * @warning A second start creates another thread and loses the first one.
 */
CERVER_PUBLIC unsigned int job_queue_start (JobQueue *job_queue);

/**
 * @brief Waits until there is a job in the queue or the queue is signaled; a signal that arrives before the wait
 * is kept.
 *
 * @param[in,out] job_queue the queue, it can be NULL
 */
CERVER_PUBLIC void job_queue_wait (JobQueue *job_queue);

/**
 * @brief Wakes up every thread that waits in the queue, for a job or for a request.
 *
 * @param[in,out] job_queue the queue, it can be NULL
 * @note A thread in job_queue_request () or job_queue_request_timeout () waits again until its job arrives or its
 * timeout ends: the signal does not make it return NULL.
 */
CERVER_PUBLIC void job_queue_signal (JobQueue *job_queue);

/**
 * @brief Stops the thread of a queue after its current job and joins it.
 *
 * @param[in,out] job_queue the queue
 * @retval 0 the thread was stopped
 * @retval 1 a NULL queue or it was not running
 */
CERVER_PUBLIC unsigned int job_queue_stop (JobQueue *job_queue);

/**
 * @brief Destroys every job that is still in the queue, with its destroy method.
 *
 * @param[in,out] job_queue the queue, it can be NULL
 */
CERVER_PUBLIC void job_queue_clear (JobQueue *job_queue);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
