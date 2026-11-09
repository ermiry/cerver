#ifndef _CERVER_THREADS_JOBS_H_
#define _CERVER_THREADS_JOBS_H_

#include <stdbool.h>

#include <pthread.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/threads/thpool.h"

#define JOB_QUEUE_POOL_INIT					16

// the priorities of a queue, 0 is the highest one,
// job_queue_push () uses the one in the middle (job_queue_default_priority ())
#define JOB_QUEUE_DEFAULT_PRIORITIES		3
#define JOB_QUEUE_MAX_PRIORITIES			16

#define JOB_QUEUE_PRIORITY_HIGH				0
#define JOB_QUEUE_PRIORITY_NORMAL			1
#define JOB_QUEUE_PRIORITY_LOW				2

// the jobs that each priority has room for, it grows by doubling
#define JOB_QUEUE_DEFAULT_RING_CAPACITY		16

// after these jobs were taken ahead of a waiting job of a lower priority,
// the next one is taken from the lower priority, so it is never starved
#define JOB_QUEUE_DEFAULT_STARVATION_LIMIT	8
#define JOB_QUEUE_STARVATION_DISABLED		0

// a wait without a deadline
#define JOB_QUEUE_WAIT_FOREVER				0xFFFFFFFFu

#define JOB_QUEUE_MILLISECONDS_PER_SECOND		1000
#define JOB_QUEUE_NANOSECONDS_PER_MILLISECOND	1000000L
#define JOB_QUEUE_NANOSECONDS_PER_SECOND		1000000000L

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Connection;

struct _JobQueue;

typedef struct Job {

	u64 id;
	void (*work) (void *args);
	void *args;

} Job;

CERVER_PUBLIC void *job_new (void);

CERVER_PUBLIC void job_delete (void *job_ptr);

CERVER_PUBLIC int job_comparator (
	const void *a, const void *b
);

// allocates a new job, job_get () takes one from the pool of a queue instead
CERVER_PUBLIC Job *job_create (
	void (*work) (void *args), void *args
);

// takes a job from the pool of the queue, it allocates one only if the pool is empty
CERVER_PUBLIC Job *job_get (struct _JobQueue *job_queue);

CERVER_PUBLIC void job_reset (Job *job);

// returns a job to the pool of the queue, to be used again by job_get ()
CERVER_PUBLIC void job_return (
	struct _JobQueue *job_queue, Job *job
);

typedef struct JobHandler {

	struct _Cerver *cerver;
	struct _Connection *connection;

	pthread_cond_t *cond;
	pthread_mutex_t *mutex;

	bool done;

	void *data;
	void (*data_delete) (void *data_ptr);

} JobHandler;

CERVER_PUBLIC void *job_handler_new (void);

CERVER_PUBLIC void job_handler_delete (void *job_handler_ptr);

CERVER_PUBLIC void *job_handler_create (void);

CERVER_PUBLIC JobHandler *job_handler_get (struct _JobQueue *job_queue);

CERVER_PUBLIC void job_handler_reset (JobHandler *job_handler);

// wake up waiting thread
CERVER_PUBLIC void job_handler_signal (JobHandler *handler);

CERVER_PUBLIC void job_handler_return (
	struct _JobQueue *job_queue, JobHandler *job_handler
);

CERVER_PUBLIC void job_handler_wait (
	struct _JobQueue *job_queue,
	void *data, void (*data_delete) (void *data_ptr)
);

#define JOB_QUEUE_TYPE_MAP(XX)				\
	XX(0,	NONE, 		None)				\
	XX(1,	JOBS, 		Jobs)				\
	XX(2,	HANDLERS, 	Handlers)

typedef enum JobQueueType {

	#define XX(num, name, string) JOB_QUEUE_TYPE_##name = num,
	JOB_QUEUE_TYPE_MAP (XX)
	#undef XX

} JobQueueType;

// the jobs of one priority in the order they were pushed
typedef struct JobQueueEntry {

	void *job;
	struct timespec queued;

} JobQueueEntry;

typedef struct JobQueueRing {

	JobQueueEntry *entries;
	size_t head;
	size_t count;
	size_t capacity;

} JobQueueRing;

// the stats of a queue, read them with job_queue_get_stats ()
typedef struct JobQueueStats {

	u64 total_jobs;					// jobs that entered the queue
	u64 pulled_jobs;				// jobs that were taken from the queue
	u64 completed_jobs;				// jobs whose work finished, see job_queue_record_run ()
	u64 requested_jobs;				// jobs given to job_queue_request () by their id
	u64 timed_out_waits;			// waits that ended at their deadline without a job
	u64 promoted_jobs;				// jobs taken ahead of a higher priority, so they were not starved
	u64 max_queued;					// the most jobs that were in the queue at the same time

	u64 priority_jobs[JOB_QUEUE_MAX_PRIORITIES];	// jobs that entered each priority

	u64 wait_total_microseconds;
	u64 run_total_microseconds;

	// the jobs that waited or ran up to 2^i ms, as in ThpoolStats
	u64 wait_histogram[THPOOL_HISTOGRAM_BUCKETS];
	u64 run_histogram[THPOOL_HISTOGRAM_BUCKETS];

} JobQueueStats;

struct _JobQueue {

	JobQueueType type;

	Pool *pool;

	// every value below is protected by the mutex
	pthread_mutex_t mutex;
	pthread_cond_t has_jobs;
	pthread_cond_t has_request;

	unsigned int n_priorities;
	JobQueueRing rings[JOB_QUEUE_MAX_PRIORITIES];
	size_t count;

	unsigned int starvation_limit;
	unsigned int starvation_streak;

	// job_queue_signal () wakes up every thread that waits,
	// the generation tells a waiting thread that it was signaled
	u64 signals;
	bool signaled;

	bool waiting;
	u64 requested_id;
	void *requested_job;

	JobQueueStats stats;

	bool running;
	pthread_t handler_thread_id;
	void (*handler) (void *data);

	// destroys the jobs that are still in the queue when it is cleared or deleted
	void (*destroy) (void *job);

};

typedef struct _JobQueue JobQueue;

CERVER_PUBLIC JobQueue *job_queue_new (void);

CERVER_PUBLIC void job_queue_delete (void *job_queue_ptr);

// creates a queue with JOB_QUEUE_DEFAULT_PRIORITIES priorities
CERVER_PUBLIC JobQueue *job_queue_create (
	const JobQueueType type
);

// creates a queue with n_priorities priorities (1 to JOB_QUEUE_MAX_PRIORITIES)
CERVER_PUBLIC JobQueue *job_queue_create_with_priorities (
	const JobQueueType type, const unsigned int n_priorities
);

CERVER_PUBLIC void job_queue_set_handler (
	JobQueue *queue, void (*handler) (void *data)
);

// sets how many jobs can be taken ahead of a waiting job of a lower priority
// before it is taken (JOB_QUEUE_DEFAULT_STARVATION_LIMIT),
// JOB_QUEUE_STARVATION_DISABLED always takes the highest priority first
CERVER_PUBLIC void job_queue_set_starvation_limit (
	JobQueue *job_queue, const unsigned int limit
);

// the priority that job_queue_push () uses, the one in the middle
CERVER_PUBLIC unsigned int job_queue_default_priority (
	const JobQueue *job_queue
);

// returns how many jobs are in the queue
CERVER_PUBLIC size_t job_queue_size (JobQueue *job_queue);

// returns the milliseconds that the oldest job in the queue has waited, 0 if it is empty
CERVER_PUBLIC u64 job_queue_oldest_wait (JobQueue *job_queue);

// adds a new job to the queue with the default priority
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push (
	JobQueue *job_queue, void *job_ptr
);

// adds a new job to the queue with a priority, 0 is the highest one
// a priority that does not exist is the lowest one
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push_priority (
	JobQueue *job_queue, void *job_ptr, const unsigned int priority
);

// creates & adds a new job to the queue
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push_job (
	JobQueue *job_queue,
	void (*work) (void *args), void *args
);

// creates & adds a new job to the queue with a priority
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push_job_priority (
	JobQueue *job_queue,
	void (*work) (void *args), void *args,
	const unsigned int priority
);

// adds a job with an id, a thread that waits for it in job_queue_request () gets it directly
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push_job_with_id (
	JobQueue *job_queue,
	const u64 job_id,
	void (*work) (void *args), void *args
);

// creates & adds a new handler to the queue
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int job_queue_push_handler (
	JobQueue *job_queue,
	struct _Cerver *cerver,
	struct _Connection *connection,
	void *data, void (*data_delete) (void *data_ptr)
);

// gets the next job without waiting, NULL if the queue is empty
CERVER_PUBLIC void *job_queue_pull (JobQueue *job_queue);

// waits up to timeout ms (or JOB_QUEUE_WAIT_FOREVER) for the next job
// returns NULL if the deadline passed or job_queue_signal () was called
CERVER_PUBLIC void *job_queue_pull_timeout (
	JobQueue *job_queue, const u32 timeout
);

// waits for the next job until interrupted (args) is true,
// the thread that changes what interrupted () reads must call job_queue_signal () after it,
// interrupted () is called with the queue's lock, so the wake up is never lost
// returns NULL when it was interrupted or signaled
CERVER_PUBLIC void *job_queue_pull_interruptible (
	JobQueue *job_queue,
	bool (*interrupted) (const void *args), const void *args
);

// requests to get an specific job from the queue by matching id
// blocks and waits until the requested job is available
CERVER_PUBLIC void *job_queue_request (
	JobQueue *job_queue, const u64 job_id
);

// requests a job by its id and waits up to timeout ms for it
// returns NULL if it did not arrive before the deadline
CERVER_PUBLIC void *job_queue_request_timeout (
	JobQueue *job_queue, const u64 job_id, const u32 timeout
);

// records that a job that was pulled finished its work, it started at start (CLOCK_MONOTONIC)
CERVER_PUBLIC void job_queue_record_run (
	JobQueue *job_queue, const struct timespec *start
);

// copies the stats of the queue
CERVER_PUBLIC void job_queue_get_stats (
	JobQueue *job_queue, JobQueueStats *stats
);

CERVER_PUBLIC void job_queue_stats_print (JobQueue *job_queue);

CERVER_PUBLIC unsigned int job_queue_start (JobQueue *job_queue);

// waits until there is a job in the queue or the queue is signaled,
// a signal that arrives before the wait is kept
CERVER_PUBLIC void job_queue_wait (JobQueue *job_queue);

// wakes up every thread that waits in the queue
CERVER_PUBLIC void job_queue_signal (JobQueue *job_queue);

CERVER_PUBLIC unsigned int job_queue_stop (JobQueue *job_queue);

// clears the job queue -> destroys all jobs
CERVER_PUBLIC void job_queue_clear (JobQueue *job_queue);

#ifdef __cplusplus
}
#endif

#endif
