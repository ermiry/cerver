/**
 * @file cerver/threads/thpool.h
 * @brief A pool of threads with a bounded queue of jobs, its drain and its stats.
 */

#ifndef _CERVER_THREADS_THPOOL_H_
#define _CERVER_THREADS_THPOOL_H_

#include <stdbool.h>
#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup threads_thpool Thpool
 * @ingroup threads
 * @brief A fixed number of threads that take the jobs of a bounded queue.
 * @{
 */

/** @brief The size of the name of a thpool, with its terminator. */
#define THPOOL_NAME_SIZE					64

/** @brief The default size of the queue is the number of threads times this factor, rounded up to a power of 2. */
#define THPOOL_DEFAULT_QUEUE_SIZE_FACTOR	2

/** @brief A drain timeout that waits until every job has finished. */
#define THPOOL_DRAIN_TIMEOUT_NONE			0

/**
 * @brief The time that a job waits and the time that it runs are counted in buckets of up to 2^0, 2^1 ... 2^18 ms
 * (262 secs), and a last bucket for the longer ones.
 */
#define THPOOL_HISTOGRAM_MAX_EXPONENT		18

/** @brief The buckets of the histograms of a thpool. */
#define THPOOL_HISTOGRAM_BUCKETS			(THPOOL_HISTOGRAM_MAX_EXPONENT + 2)

/** @brief The microseconds of a millisecond. */
#define THPOOL_MICROSECONDS_PER_MILLISECOND		1000

/** @brief The microseconds of a second. */
#define THPOOL_MICROSECONDS_PER_SECOND			1000000

/** @brief The nanoseconds of a microsecond. */
#define THPOOL_NANOSECONDS_PER_MICROSECOND		1000

#ifdef __cplusplus
extern "C" {
#endif

struct _PoolThread;

/**
 * @brief A job inside the queue of a thpool.
 *
 * reject is called instead of work when the job is dropped (the thpool is drained with reject or it is destroyed),
 * so the args can always be released.
 */
typedef struct ThpoolJob {

	/** @brief The work. */
	void (*work) (void *args);
	/** @brief Called with the args instead of work when the job is dropped, it can be NULL. */
	void (*reject) (void *args);
	/** @brief The argument of the work. */
	void *args;

	/** @brief When the job entered the queue (CLOCK_MONOTONIC). */
	struct timespec queued;

} ThpoolJob;

/**
 * @brief How the wait of a thread on the waiter of its thpool ended.
 *
 * @since 2.0b-77
 */
typedef enum ThpoolWaited {

	/** @brief It was interrupted, or what it got was not for it: the thread waits again. */
	THPOOL_WAITED_NOTHING		= 0,
	/** @brief It has a work for the thread. */
	THPOOL_WAITED_WORK			= 1,
	/** @brief One of the wake ups of the thpool made it return (ThpoolWaiter::wake). */
	THPOOL_WAITED_WOKEN			= 2

} ThpoolWaited;

/**
 * @brief What a thread of a thpool waits on when its queue has no job, instead of the condition of the queue: a
 * thpool whose threads also wait for other work, like the sockets of the connections that wait for their next
 * request.
 *
 * The queue always goes first: a thread only waits on it when the queue is empty, and a job that is pushed wakes
 * one of the threads that wait on it.
 *
 * @threadsafety wait runs in the threads of the thpool at the same time, without the mutex of the thpool; wake
 * runs with that mutex.
 * @since 2.0b-77
 */
typedef struct ThpoolWaiter {

	/**
	 * @brief Waits until it has a work for the thread, or until it is woken up.
	 *
	 * Returns #THPOOL_WAITED_WORK with the work in job (its work and its args): the thread runs it like a job of the
	 * queue, it is counted as one that entered and completed (not in the histogram of the wait, it never waited in
	 * the queue). Returns #THPOOL_WAITED_WOKEN when one wake up made it return, and #THPOOL_WAITED_NOTHING when it was
	 * interrupted: the thread checks its queue and waits again.
	 */
	ThpoolWaited (*wait) (void *args, ThpoolJob *job);

	/**
	 * @brief Wakes up count threads that wait on it: each wake up makes one wait return #THPOOL_WAITED_WOKEN, once,
	 * so the thpool only asks for the ones that it needs.
	 */
	void (*wake) (void *args, const unsigned int count);

	/** @brief The argument of wait and of wake. */
	void *args;

} ThpoolWaiter;

/**
 * @brief The values that a thpool counts since it was created.
 */
typedef struct ThpoolStats {

	/** @brief The jobs that entered the queue. */
	u64 total_jobs;
	/** @brief The jobs that finished their work. */
	u64 completed_jobs;
	/** @brief The jobs that could not enter the queue (full or not accepting). */
	u64 rejected_jobs;
	/** @brief The queued jobs that were rejected by a drain or the destroy. */
	u64 dropped_jobs;
	/** @brief The times that a caller had to wait for space in the queue. */
	u64 waited_jobs;

	/** @brief The most jobs that were waiting in the queue at once. */
	unsigned int max_queued;

	/** @brief The microseconds that the jobs waited in the queue. */
	u64 wait_total_microseconds;
	/** @brief The microseconds that the jobs ran. */
	u64 run_total_microseconds;

	/** @brief The jobs that waited up to 2^i ms, the last bucket for more than 2^THPOOL_HISTOGRAM_MAX_EXPONENT ms. */
	u64 wait_histogram[THPOOL_HISTOGRAM_BUCKETS];
	/** @brief The jobs that ran up to 2^i ms, like wait_histogram. */
	u64 run_histogram[THPOOL_HISTOGRAM_BUCKETS];

} ThpoolStats;

/**
 * @brief A thpool.
 *
 * @threadsafety Every function is safe from many threads; the fields after threads are protected by its mutex.
 */
typedef struct Thpool {

	/** @brief The length of the name. */
	size_t namelen;
	/** @brief The name, that names its threads (`thpool-<name>-<id>`). */
	char name[THPOOL_NAME_SIZE];

	/** @brief The number of threads. */
	unsigned int n_threads;
	/** @brief The threads. */
	struct _PoolThread **threads;

	/** @brief The threads keep taking jobs. */
	bool keep_alive;
	/** @brief New jobs can enter the queue. */
	bool accepting;
	/** @brief The threads that are running. */
	unsigned int num_threads_alive;
	/** @brief The threads that are running a job. */
	unsigned int num_threads_working;

	/** @brief The bounded ring of the jobs, allocated once. */
	ThpoolJob *jobs;
	/** @brief The size of the ring, a power of 2. */
	unsigned int queue_size;
	/** @brief The position of the oldest job. */
	unsigned int queue_start;
	/** @brief The jobs in the queue. */
	unsigned int queue_count;

	/** @brief Protects the fields of the thpool. */
	pthread_mutex_t *mutex;
	/** @brief Signaled when a job is pushed. */
	pthread_cond_t *has_jobs;
	/** @brief Signaled when a job leaves the queue. */
	pthread_cond_t *has_space;
	/** @brief Signaled when the threads are idle or their number changes. */
	pthread_cond_t *threads_all_idle;

	/**
	 * @brief An eventfd that is written every time a full queue gets space, so a thread can wait for it with poll ()
	 * and still be interrupted by a signal.
	 */
	int space_fd;
	/** @brief A thread waits in thpool_wait_space (). */
	bool space_waiting;

	/** @brief The stats of the thpool. */
	ThpoolStats stats;

	/**
	 * @brief The jobs in the queue, written with the mutex and with atomic operations each time that they change:
	 * thpool_has_queued () reads it without the mutex.
	 *
	 * @since 2.0b-76
	 */
	unsigned int queue_awaited;

	/**
	 * @brief What the threads wait on when the queue is empty, without wait they wait for the jobs of the queue
	 * (thpool_set_waiter ()).
	 *
	 * @since 2.0b-77
	 */
	ThpoolWaiter waiter;
	/**
	 * @brief The threads that wait on the waiter, written with the mutex and with atomic operations:
	 * thpool_get_num_threads_waiting () reads it without the mutex.
	 *
	 * @since 2.0b-77
	 */
	unsigned int num_threads_waiting;
	/**
	 * @brief The wake ups of the waiter that no thread consumed yet, with the mutex: a job only asks for one when more
	 * threads wait than wake ups there are, so they never pile up behind the jobs that the busy threads take.
	 *
	 * @since 2.0b-77
	 */
	unsigned int waiter_wakes;

} Thpool;

/**
 * @brief Creates a thpool, without starting its threads.
 *
 * @param[in] n_threads the threads; the queue size is n_threads * #THPOOL_DEFAULT_QUEUE_SIZE_FACTOR, rounded up to a
 * power of 2
 * @return the new thpool, released with thpool_destroy (); NULL if it could not be allocated
 */
CERVER_EXPORT Thpool *thpool_create (unsigned int n_threads);

/**
 * @brief Sets how many jobs can wait in the queue.
 *
 * @param[in,out] thpool the thpool
 * @param[in] queue_size the size, rounded up to a power of 2
 * @retval 0 the size was set
 * @retval 1 a NULL thpool, a size of 0, a thpool already initialized, or the queue could not be allocated
 * @note Call it before thpool_init ().
 */
CERVER_EXPORT unsigned int thpool_set_queue_size (
	Thpool *thpool, unsigned int queue_size
);

/**
 * @brief Starts the threads of the thpool and waits until every one is running.
 *
 * @param[in,out] thpool the thpool, from thpool_create ()
 * @retval 0 the threads are running
 * @retval 1 a NULL thpool, one without threads or already initialized, or a thread could not be created (then no
 * thread is left running)
 */
CERVER_EXPORT unsigned int thpool_init (Thpool *thpool);

/**
 * @brief Sets the name of the thpool, that names its threads.
 *
 * @param[in,out] thpool the thpool
 * @param[in] name the name, truncated to THPOOL_NAME_SIZE - 1 characters; NULL keeps the current one
 * @note Call it before thpool_init ().
 */
CERVER_EXPORT void thpool_set_name (
	Thpool *thpool, const char *name
);

/**
 * @brief Returns the threads that are alive (running).
 *
 * @param[in] thpool the thpool
 * @return the threads, 0 for a NULL thpool
 */
CERVER_EXPORT unsigned int thpool_get_num_threads_alive (
	Thpool *thpool
);

/**
 * @brief Returns the threads that are running a job.
 *
 * @param[in] thpool the thpool
 * @return the threads, 0 for a NULL thpool
 */
CERVER_EXPORT unsigned int thpool_get_num_threads_working (
	Thpool *thpool
);

/**
 * @brief Returns how many jobs are waiting in the queue.
 *
 * @param[in] thpool the thpool
 * @return the jobs, 0 for a NULL thpool
 */
CERVER_EXPORT unsigned int thpool_get_queued (Thpool *thpool);

/**
 * @brief Returns whether a job waits in the queue, without taking the mutex of the thpool: for a thread that
 * asks it often, like one that decides whether it keeps working on what it has.
 *
 * @param[in] thpool the thpool
 * @return true when the queue had a job the last time that it changed; false for a NULL thpool
 * @threadsafety Safe from any thread, it reads an atomic value.
 * @since 2.0b-76
 */
CERVER_EXPORT bool thpool_has_queued (const Thpool *thpool);

/**
 * @brief Sets what the threads of the thpool wait on when its queue is empty, or clears it.
 *
 * The threads that wait on the condition of the queue are woken up to wait on the new waiter; with NULL, the ones
 * that wait on the waiter that is cleared are woken up with its wake, and the call returns once none of them uses
 * it anymore, so its args can be released after it.
 *
 * @param[in,out] thpool the thpool
 * @param[in] waiter what the threads wait on, it is copied; NULL (or one without wait) clears it, the threads
 * wait for the jobs of the queue again
 * @retval 0 it was set
 * @retval 1 a NULL thpool, or a waiter without wake
 * @threadsafety Safe from any thread but the ones of the thpool, that it waits for when it clears the waiter.
 * @note For the internal methods of cerver.
 * @since 2.0b-77
 */
CERVER_PRIVATE unsigned int thpool_set_waiter (Thpool *thpool, const ThpoolWaiter *waiter);

/**
 * @brief Returns how many threads wait on the waiter of the thpool, without taking its mutex.
 *
 * @param[in] thpool the thpool
 * @return the threads, 0 for a NULL thpool or one without a waiter
 * @threadsafety Safe from any thread, it reads an atomic value.
 * @note For the internal methods of cerver.
 * @since 2.0b-77
 */
CERVER_PRIVATE unsigned int thpool_get_num_threads_waiting (const Thpool *thpool);

/**
 * @brief Returns how many jobs can wait in the queue.
 *
 * @param[in] thpool the thpool
 * @return the size, 0 for a NULL thpool
 */
CERVER_EXPORT unsigned int thpool_get_queue_size (Thpool *thpool);

/**
 * @brief Copies the current stats of the thpool.
 *
 * @param[in] thpool the thpool
 * @param[out] stats the copy
 */
CERVER_EXPORT void thpool_get_stats (Thpool *thpool, ThpoolStats *stats);

/**
 * @brief Returns the bucket of the histograms of an elapsed time: the smallest i with microseconds <= 2^i ms, or the
 * last bucket.
 *
 * @param[in] microseconds the elapsed time
 * @return the bucket, from 0 to THPOOL_HISTOGRAM_BUCKETS - 1
 */
CERVER_EXPORT unsigned int thpool_histogram_bucket (const u64 microseconds);

/**
 * @brief Prints the stats of a thpool with the cerver log.
 *
 * @param[in] stats the stats, it can be NULL
 */
CERVER_EXPORT void thpool_stats_print (const ThpoolStats *stats);

/**
 * @brief Returns whether no thread of the thpool is running a job.
 *
 * @param[in] thpool the thpool
 * @return true without working threads, false for a NULL thpool
 */
CERVER_EXPORT bool thpool_is_empty (Thpool *thpool);

/**
 * @brief Returns whether a new job can NOT be queued right now: the queue is full or the thpool is not accepting
 * jobs.
 *
 * @param[in] thpool the thpool
 * @return true when it is full, and for a NULL thpool
 */
CERVER_EXPORT bool thpool_is_full (Thpool *thpool);

/**
 * @brief Adds a work to the queue without blocking; it runs once it is the next in line and a thread is free.
 *
 * @param[in,out] thpool the thpool
 * @param[in] work the work
 * @param[in] args the argument of the work
 * @retval 0 the job was queued
 * @retval 1 a NULL thpool or work, the queue is full or the thpool is not accepting jobs
 */
CERVER_EXPORT int thpool_add_work (
	Thpool *thpool, void (*work) (void *), void *args
);

/**
 * @brief Adds a work like thpool_add_work (), with a reject that gets the args if the job is dropped before it runs.
 *
 * @param[in,out] thpool the thpool
 * @param[in] work the work
 * @param[in] reject called with the args if the job is dropped, it can be NULL
 * @param[in] args the argument of the work
 * @retval 0 the job was queued
 * @retval 1 a NULL thpool or work, the queue is full or the thpool is not accepting jobs (reject is not called)
 */
CERVER_EXPORT int thpool_add_work_with_reject (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
);

/**
 * @brief Adds a work like thpool_add_work_with_reject (), blocking while the queue is full until there is space.
 *
 * @param[in,out] thpool the thpool
 * @param[in] work the work
 * @param[in] reject called with the args if the job is dropped, it can be NULL
 * @param[in] args the argument of the work
 * @retval 0 the job was queued
 * @retval 1 a NULL thpool or work, or the thpool stopped accepting jobs
 */
CERVER_EXPORT int thpool_add_work_wait (
	Thpool *thpool,
	void (*work) (void *), void (*reject) (void *),
	void *args
);

/**
 * @brief Waits until a new job can be queued, the thpool stops accepting jobs, the timeout expires or a signal
 * interrupts the wait.
 *
 * It waits with poll () on the eventfd of the thpool, so a signal interrupts it.
 *
 * @param[in,out] thpool the thpool
 * @param[in] timeout the milliseconds, as in poll () (-1 waits forever)
 * @retval 0 there is space
 * @retval 1 on timeout, when the thpool is not accepting jobs, or for a NULL thpool
 * @retval -1 a signal interrupted the wait, with errno set to EINTR
 */
CERVER_EXPORT int thpool_wait_space (Thpool *thpool, int timeout);

/**
 * @brief Waits until the queue is empty and no thread is running a job.
 *
 * @param[in] thpool the thpool, it can be NULL
 */
CERVER_EXPORT void thpool_wait (Thpool *thpool);

/**
 * @brief Stops accepting new jobs and waits for the current ones.
 *
 * When run_queued is false the queued jobs are rejected right away, otherwise they still run until the timeout;
 * when the timeout is reached, the jobs that are still queued are rejected.
 *
 * @param[in,out] thpool the thpool
 * @param[in] run_queued true to run the queued jobs
 * @param[in] timeout the seconds, #THPOOL_DRAIN_TIMEOUT_NONE waits until every job has finished
 * @retval 0 every job finished, or the thpool is NULL
 * @retval 1 some threads are still working after the timeout
 */
CERVER_EXPORT unsigned int thpool_drain (
	Thpool *thpool, bool run_queued, time_t timeout
);

/**
 * @brief Stops the threads after their current job, rejects every job that is still queued and deletes the thpool.
 *
 * @param[in] thpool the thpool, invalid after the call; it can be NULL
 */
CERVER_EXPORT void thpool_destroy (Thpool *thpool);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
