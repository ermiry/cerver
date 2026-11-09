#ifndef _CERVER_THREADS_THPOOL_H_
#define _CERVER_THREADS_THPOOL_H_

#include <stdbool.h>
#include <pthread.h>

#include <time.h>

#include "cerver/config.h"

#include "cerver/types/types.h"

#include "cerver/threads/jobs.h"

#define THPOOL_NAME_SIZE					64

// the default queue size is the number of threads times this factor,
// rounded up to a power of 2
#define THPOOL_DEFAULT_QUEUE_SIZE_FACTOR	2

// a drain timeout of 0 waits until every job has finished
#define THPOOL_DRAIN_TIMEOUT_NONE			0

// the time that a job waits in the queue and the time that it runs
// are counted in buckets of up to 2^0, 2^1 ... 2^18 ms (262 secs),
// and a last bucket for the longer ones
#define THPOOL_HISTOGRAM_MAX_EXPONENT		18
#define THPOOL_HISTOGRAM_BUCKETS			(THPOOL_HISTOGRAM_MAX_EXPONENT + 2)

#define THPOOL_MICROSECONDS_PER_MILLISECOND		1000
#define THPOOL_MICROSECONDS_PER_SECOND			1000000
#define THPOOL_NANOSECONDS_PER_MICROSECOND		1000

#ifdef __cplusplus
extern "C" {
#endif

struct _PoolThread;

// a job inside the thpool queue
// reject is called instead of work when the job is dropped
// (the thpool is drained with reject or it is destroyed),
// so the args can always be released
typedef struct ThpoolJob {

	void (*work) (void *args);
	void (*reject) (void *args);
	void *args;

	// when the job entered the queue (CLOCK_MONOTONIC)
	struct timespec queued;

} ThpoolJob;

// the values that a thpool counts since it was created
typedef struct ThpoolStats {

	u64 total_jobs;				// jobs that entered the queue
	u64 completed_jobs;			// jobs that finished their work
	u64 rejected_jobs;			// jobs that could not enter the queue (full or not accepting)
	u64 dropped_jobs;			// queued jobs that were rejected by a drain or the destroy
	u64 waited_jobs;			// times that a caller had to wait for space in the queue

	unsigned int max_queued;	// the most jobs that were waiting in the queue at once

	u64 wait_total_microseconds;
	u64 run_total_microseconds;

	// wait_histogram[i] are the jobs that waited up to 2^i ms,
	// the last bucket is for the ones that waited more than 2^THPOOL_HISTOGRAM_MAX_EXPONENT ms
	u64 wait_histogram[THPOOL_HISTOGRAM_BUCKETS];
	u64 run_histogram[THPOOL_HISTOGRAM_BUCKETS];

} ThpoolStats;

typedef struct Thpool {

	size_t namelen;
	char name[THPOOL_NAME_SIZE];

	unsigned int n_threads;
	struct _PoolThread **threads;

	// every field below is protected by the mutex
	bool keep_alive;
	bool accepting;
	unsigned int num_threads_alive;
	unsigned int num_threads_working;

	// bounded ring buffer of jobs, allocated once
	ThpoolJob *jobs;
	unsigned int queue_size;
	unsigned int queue_start;
	unsigned int queue_count;

	pthread_mutex_t *mutex;
	pthread_cond_t *has_jobs;
	pthread_cond_t *has_space;
	pthread_cond_t *threads_all_idle;

	// an eventfd that is written every time a full queue gets space,
	// so a thread can wait for it with poll () and still be interrupted by a signal
	int space_fd;
	bool space_waiting;

	// protected by the mutex
	ThpoolStats stats;

} Thpool;

// creates a new thpool with n threads
// the queue size defaults to n threads * THPOOL_DEFAULT_QUEUE_SIZE_FACTOR
CERVER_EXPORT Thpool *thpool_create (unsigned int n_threads);

// sets how many jobs can wait in the queue
// it is always rounded up to a power of 2
// must be called before thpool_init ()
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int thpool_set_queue_size (
	Thpool *thpool, unsigned int queue_size
);

// initializes the thpool
// must be called after thpool_create ()
// waits until every thread is running
// returns 0 on success, 1 on error (no thread is left running)
CERVER_EXPORT unsigned int thpool_init (Thpool *thpool);

// sets the name for the thpool
CERVER_EXPORT void thpool_set_name (
	Thpool *thpool, const char *name
);

// gets the current number of threads
// that are alive (running) in the thpool
CERVER_EXPORT unsigned int thpool_get_num_threads_alive (
	Thpool *thpool
);

// gets the current number of threads
// that are busy working in a job
CERVER_EXPORT unsigned int thpool_get_num_threads_working (
	Thpool *thpool
);

// gets how many jobs are waiting in the queue
CERVER_EXPORT unsigned int thpool_get_queued (Thpool *thpool);

// gets how many jobs can wait in the queue
CERVER_EXPORT unsigned int thpool_get_queue_size (Thpool *thpool);

// copies the current stats of the thpool
CERVER_EXPORT void thpool_get_stats (Thpool *thpool, ThpoolStats *stats);

// returns the bucket of the histograms for an elapsed time
CERVER_EXPORT unsigned int thpool_histogram_bucket (const u64 microseconds);

// prints the stats with the cerver log
CERVER_EXPORT void thpool_stats_print (const ThpoolStats *stats);

// returns true if the thpool does NOT have any working thread
CERVER_EXPORT bool thpool_is_empty (Thpool *thpool);

// returns true if a new job can NOT be queued right now
// (the queue is full or the thpool is not accepting jobs)
CERVER_EXPORT bool thpool_is_full (Thpool *thpool);

// adds a work to the thpool's job queue without blocking
// it will be executed once it is the next in line and a thread is free
// returns 0 on success, 1 if the queue is full or the thpool is not accepting jobs
CERVER_EXPORT int thpool_add_work (
	Thpool *thpool, void (*work) (void *), void *args
);

// works like thpool_add_work () but reject is called
// with the args if the job is dropped before it runs
CERVER_EXPORT int thpool_add_work_with_reject (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
);

// works like thpool_add_work_with_reject () but blocks
// while the queue is full until there is space
// returns 0 on success, 1 if the thpool stopped accepting jobs
CERVER_EXPORT int thpool_add_work_wait (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
);

// waits until a new job can be queued, the thpool stops accepting jobs,
// the timeout (in ms) expires or a signal interrupts the wait
// returns 0 when there is space, 1 on timeout or when the thpool is not accepting jobs
// and -1 with errno set to EINTR when a signal interrupted the wait
CERVER_EXPORT int thpool_wait_space (Thpool *thpool, int timeout);

// wait until all jobs have finished
CERVER_EXPORT void thpool_wait (Thpool *thpool);

// stops accepting new jobs and waits for the current ones
// if run_queued is false, the queued jobs are rejected right away,
// otherwise they still run until the timeout (in seconds) is reached
// when the timeout is reached, the jobs that are still queued are rejected
// a timeout of THPOOL_DRAIN_TIMEOUT_NONE waits until every job has finished
// returns 0 if every job finished, 1 if some threads are still working
CERVER_EXPORT unsigned int thpool_drain (
	Thpool *thpool, bool run_queued, time_t timeout
);

// stops the threads, waits for them to finish their current job
// rejects every job that is still queued and deletes the thpool
CERVER_EXPORT void thpool_destroy (Thpool *thpool);

#ifdef __cplusplus
}
#endif

#endif
