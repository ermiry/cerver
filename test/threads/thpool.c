#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/wait.h>

#include <pthread.h>

#include <cerver/threads/thpool.h>

#include "../test.h"

#include "threads.h"

// bugs that this suite pinned:
// - thpool_init busy waited for the threads, and hanged forever if one could not be created
// - thpool_is_full compared the working threads against the alive ones and ignored the queue
// - every job was a malloc and a free, and the binary semaphore merged the posts
// - thpool_destroy freed the queued jobs without running or rejecting them (leaks and open fds)
// - a thread unlocked the mutex after thpool_destroy could have destroyed it
// - the threads did not block the process signals
// coverage holes:
// - the thpool is not shared between processes

#define THPOOL_N_THREADS				4
#define THPOOL_NAME						"test-thpool"

#define THPOOL_SMALL_THREADS			2
#define THPOOL_SMALL_QUEUE				2

#define THPOOL_BURST_JOBS				16384

#define THPOOL_ROUNDED_QUEUE			100
#define THPOOL_ROUNDED_QUEUE_EXPECTED	128

#define THPOOL_DRAIN_JOBS				8
#define THPOOL_DRAIN_TIMEOUT			8
#define THPOOL_DRAIN_SHORT_TIMEOUT		1

// 2^24 ns
#define THPOOL_TEST_SHORT_SLEEP			16777216L

// the child process that can not create its threads
#define THPOOL_FAILURE_THREADS			256
#define THPOOL_FAILURE_MEMORY_LIMIT		(64UL << 20)
#define THPOOL_FAILURE_ALARM			16

#if defined (__SANITIZE_ADDRESS__) || defined (__SANITIZE_THREAD__)
#define THPOOL_TEST_SANITIZER
#endif

#define THPOOL_TEST_PRELOAD_VARIABLE	"LD_PRELOAD"
#define THPOOL_TEST_VALGRIND_PRELOAD	"vgpreload"

#pragma region helpers

static void thpool_test_sleep (const long nanoseconds) {

	struct timespec interval = { 0, nanoseconds };
	(void) nanosleep (&interval, NULL);

}

static unsigned int thpool_test_counter_get (unsigned int *counter) {

	return __atomic_load_n (counter, __ATOMIC_SEQ_CST);

}

static void thpool_test_counter_add (void *counter_ptr) {

	(void) __atomic_add_fetch ((unsigned int *) counter_ptr, 1, __ATOMIC_SEQ_CST);

}

// a gate that keeps the jobs working until it is opened
typedef struct ThpoolTestGate {

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	bool open;

	unsigned int ran;
	unsigned int rejected;

} ThpoolTestGate;

static void thpool_test_gate_init (ThpoolTestGate *gate) {

	(void) pthread_mutex_init (&gate->mutex, NULL);
	(void) pthread_cond_init (&gate->cond, NULL);
	gate->open = false;

	gate->ran = 0;
	gate->rejected = 0;

}

static void thpool_test_gate_open (ThpoolTestGate *gate) {

	(void) pthread_mutex_lock (&gate->mutex);
	gate->open = true;
	(void) pthread_cond_broadcast (&gate->cond);
	(void) pthread_mutex_unlock (&gate->mutex);

}

static void thpool_test_gate_end (ThpoolTestGate *gate) {

	(void) pthread_mutex_destroy (&gate->mutex);
	(void) pthread_cond_destroy (&gate->cond);

}

static void thpool_test_gate_work (void *gate_ptr) {

	ThpoolTestGate *gate = (ThpoolTestGate *) gate_ptr;

	(void) pthread_mutex_lock (&gate->mutex);
	while (!gate->open) (void) pthread_cond_wait (&gate->cond, &gate->mutex);
	(void) pthread_mutex_unlock (&gate->mutex);

	thpool_test_counter_add (&gate->ran);

}

static void thpool_test_gate_reject (void *gate_ptr) {

	thpool_test_counter_add (&((ThpoolTestGate *) gate_ptr)->rejected);

}

// waits until the threads are working in the blocked jobs
static void thpool_test_wait_working (Thpool *thpool, const unsigned int working) {

	while (thpool_get_num_threads_working (thpool) != working) {
		thpool_test_sleep (THPOOL_TEST_SHORT_SLEEP);
	}

}

static Thpool *thpool_test_create_small (void) {

	Thpool *thpool = thpool_create (THPOOL_SMALL_THREADS);
	test_check_ptr (thpool);
	test_check_unsigned_eq (thpool_set_queue_size (thpool, THPOOL_SMALL_QUEUE), 0, NULL);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	return thpool;

}

// blocks every thread of a small thpool and fills its queue
static void thpool_test_fill (Thpool *thpool, ThpoolTestGate *gate) {

	for (unsigned int i = 0; i < THPOOL_SMALL_THREADS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, gate), 0, NULL);
	}

	thpool_test_wait_working (thpool, THPOOL_SMALL_THREADS);

	for (unsigned int i = 0; i < THPOOL_SMALL_QUEUE; i++) {
		test_check_int_eq (
			thpool_add_work_with_reject (thpool, thpool_test_gate_work, thpool_test_gate_reject, gate),
			0, NULL
		);
	}

	test_check_unsigned_eq (thpool_get_queued (thpool), THPOOL_SMALL_QUEUE, NULL);

}

#pragma endregion

#pragma region main

static void test_thpool_create (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);

	test_check_ptr (thpool);

	test_check_unsigned_eq (thpool->n_threads, THPOOL_N_THREADS, NULL);
	test_check_ptr (thpool->threads);

	test_check_bool_eq (thpool->keep_alive, false, NULL);
	test_check_bool_eq (thpool->accepting, false, NULL);
	test_check_unsigned_eq (thpool->num_threads_alive, 0, NULL);
	test_check_unsigned_eq (thpool->num_threads_working, 0, NULL);

	// the default queue is the number of threads * the factor, rounded to a power of 2
	test_check_unsigned_eq (
		thpool_get_queue_size (thpool),
		THPOOL_N_THREADS * THPOOL_DEFAULT_QUEUE_SIZE_FACTOR, NULL
	);

	test_check_ptr (thpool->jobs);
	test_check_ptr (thpool->mutex);
	test_check_ptr (thpool->has_jobs);
	test_check_ptr (thpool->has_space);
	test_check_ptr (thpool->threads_all_idle);

	// a thpool that has not been initialized does not accept jobs
	unsigned int counter = 0;
	test_check_int_eq (thpool_add_work (thpool, thpool_test_counter_add, &counter), 1, NULL);
	test_check_true (thpool_is_full (thpool));

	thpool_destroy (thpool);

}

static void test_thpool_set_name (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);

	thpool_set_name (thpool, THPOOL_NAME);

	test_check_str_eq (thpool->name, THPOOL_NAME, NULL);
	test_check_str_len (thpool->name, strlen (THPOOL_NAME), NULL);

	thpool_destroy (thpool);

}

static void test_thpool_queue_size (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);

	test_check_unsigned_eq (thpool_set_queue_size (thpool, THPOOL_ROUNDED_QUEUE), 0, NULL);
	test_check_unsigned_eq (thpool_get_queue_size (thpool), THPOOL_ROUNDED_QUEUE_EXPECTED, NULL);

	// 0 is not a valid size
	test_check_unsigned_eq (thpool_set_queue_size (thpool, 0), 1, NULL);

	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	// the queue can not change while the threads are running
	test_check_unsigned_eq (thpool_set_queue_size (thpool, THPOOL_N_THREADS), 1, NULL);
	test_check_unsigned_eq (thpool_get_queue_size (thpool), THPOOL_ROUNDED_QUEUE_EXPECTED, NULL);

	thpool_destroy (thpool);

}

static void test_thpool_init (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);

	test_check_true (thpool_is_empty (thpool));

	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);
	test_check_bool_eq (thpool->keep_alive, true, NULL);
	test_check_bool_eq (thpool->accepting, true, NULL);

	// every thread is running when thpool_init () returns
	test_check_unsigned_eq (thpool_get_num_threads_alive (thpool), THPOOL_N_THREADS, NULL);
	test_check_unsigned_eq (thpool_get_num_threads_working (thpool), 0, NULL);

	test_check_true (thpool_is_empty (thpool));
	test_check_false (thpool_is_full (thpool));

	// a second init fails
	test_check_unsigned_eq (thpool_init (thpool), 1, NULL);

	thpool_destroy (thpool);

	// a thpool without threads can not be initialized
	Thpool *empty = thpool_create (0);
	test_check_ptr (empty);
	test_check_unsigned_eq (thpool_init (empty), 1, NULL);
	thpool_destroy (empty);

	// NULL values
	test_check_unsigned_eq (thpool_init (NULL), 1, NULL);
	test_check_int_eq (thpool_add_work (NULL, thpool_test_counter_add, NULL), 1, NULL);
	test_check_true (thpool_is_full (NULL));
	thpool_destroy (NULL);

}

#pragma endregion

#pragma region work

static void test_thpool_add_work (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	// a job without work is not added
	test_check_int_eq (thpool_add_work (thpool, NULL, NULL), 1, NULL);

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, &gate), 0, NULL);

	thpool_test_wait_working (thpool, 1);
	test_check_false (thpool_is_empty (thpool));
	test_check_unsigned_eq (thpool_get_queued (thpool), 0, NULL);

	thpool_test_gate_open (&gate);
	thpool_wait (thpool);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.ran), 1, NULL);
	test_check_true (thpool_is_empty (thpool));

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

// many more jobs than threads and queue slots, every job runs exactly once
static void test_thpool_burst (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	unsigned int counter = 0;
	for (unsigned int i = 0; i < THPOOL_BURST_JOBS; i++) {
		test_check_int_eq (
			thpool_add_work_wait (thpool, thpool_test_counter_add, NULL, &counter),
			0, NULL
		);
	}

	thpool_wait (thpool);

	test_check_unsigned_eq (thpool_test_counter_get (&counter), THPOOL_BURST_JOBS, NULL);
	test_check_unsigned_eq (thpool_get_queued (thpool), 0, NULL);
	test_check_unsigned_eq (thpool_get_num_threads_working (thpool), 0, NULL);

	thpool_destroy (thpool);

}

// the queue is bounded, a new job fails instead of growing it
static void test_thpool_full (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	test_check_true (thpool_is_full (thpool));
	test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, &gate), 1, NULL);

	thpool_test_gate_open (&gate);
	thpool_wait (thpool);

	test_check_unsigned_eq (
		thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS + THPOOL_SMALL_QUEUE, NULL
	);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.rejected), 0, NULL);
	test_check_false (thpool_is_full (thpool));

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

typedef struct ThpoolTestWaiter {

	Thpool *thpool;
	ThpoolTestGate *gate;

	int result;
	unsigned int returned;

} ThpoolTestWaiter;

static void *thpool_test_waiter (void *waiter_ptr) {

	ThpoolTestWaiter *waiter = (ThpoolTestWaiter *) waiter_ptr;

	waiter->result = thpool_add_work_wait (
		waiter->thpool, thpool_test_gate_work, thpool_test_gate_reject, waiter->gate
	);

	thpool_test_counter_add (&waiter->returned);

	return NULL;

}

// thpool_add_work_wait () blocks until a thread takes a queued job
static void test_thpool_wait_space (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	ThpoolTestWaiter waiter = { thpool, &gate, -1, 0 };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, thpool_test_waiter, &waiter), 0, NULL);

	thpool_test_sleep (THPOOL_TEST_SHORT_SLEEP);
	test_check_unsigned_eq (thpool_test_counter_get (&waiter.returned), 0, NULL);

	thpool_test_gate_open (&gate);
	(void) pthread_join (thread_id, NULL);

	test_check_int_eq (waiter.result, 0, NULL);

	thpool_wait (thpool);
	test_check_unsigned_eq (
		thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS + THPOOL_SMALL_QUEUE + 1, NULL
	);

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

// a drain wakes up the threads that wait for space, and they fail
static void test_thpool_wait_space_drain (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	ThpoolTestWaiter waiter = { thpool, &gate, -1, 0 };
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, thpool_test_waiter, &waiter), 0, NULL);

	thpool_test_sleep (THPOOL_TEST_SHORT_SLEEP);

	// the queued jobs are rejected and the working ones do not finish in time
	test_check_unsigned_eq (thpool_drain (thpool, false, THPOOL_DRAIN_SHORT_TIMEOUT), 1, NULL);

	(void) pthread_join (thread_id, NULL);
	test_check_int_eq (waiter.result, 1, NULL);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.rejected), THPOOL_SMALL_QUEUE, NULL);

	thpool_test_gate_open (&gate);
	thpool_destroy (thpool);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS, NULL);

	thpool_test_gate_end (&gate);

}

#pragma endregion

#pragma region drain

static void thpool_test_sleep_work (void *counter_ptr) {

	thpool_test_sleep (THPOOL_TEST_SHORT_SLEEP);

	thpool_test_counter_add (counter_ptr);

}

#pragma endregion

#pragma region stats

#define THPOOL_STATS_SLEEP_JOBS					2
#define THPOOL_STATS_WAIT_SPACE_TIMEOUT			16
#define THPOOL_STATS_SHORT_SLEEP_MICROSECONDS	(THPOOL_TEST_SHORT_SLEEP / THPOOL_NANOSECONDS_PER_MICROSECOND)

// the first bucket whose upper bound is more than the short sleep
#define THPOOL_STATS_SHORT_SLEEP_BUCKET			5

static u64 thpool_test_histogram_total (const u64 histogram[THPOOL_HISTOGRAM_BUCKETS]) {

	u64 total = 0;
	for (unsigned int i = 0; i < THPOOL_HISTOGRAM_BUCKETS; i++) total += histogram[i];

	return total;

}

static u64 thpool_test_histogram_below (
	const u64 histogram[THPOOL_HISTOGRAM_BUCKETS], const unsigned int bucket
) {

	u64 total = 0;
	for (unsigned int i = 0; i < bucket; i++) total += histogram[i];

	return total;

}

static void test_thpool_stats_bucket (void) {

	const u64 millisecond = THPOOL_MICROSECONDS_PER_MILLISECOND;
	const u64 last = ((u64) 1 << THPOOL_HISTOGRAM_MAX_EXPONENT) * millisecond;

	test_check_unsigned_eq (thpool_histogram_bucket (0), 0, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket (millisecond), 0, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket (millisecond + 1), 1, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket (2 * millisecond), 1, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket ((2 * millisecond) + 1), 2, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket (last), THPOOL_HISTOGRAM_MAX_EXPONENT, NULL);
	test_check_unsigned_eq (thpool_histogram_bucket (last + 1), THPOOL_HISTOGRAM_BUCKETS - 1, NULL);
	test_check_unsigned_eq (
		thpool_histogram_bucket (THPOOL_STATS_SHORT_SLEEP_MICROSECONDS),
		THPOOL_STATS_SHORT_SLEEP_BUCKET, NULL
	);

}

// the jobs that ran, the one that did not fit and the most that were queued
static void test_thpool_stats_counts (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolStats stats;
	thpool_get_stats (thpool, &stats);
	test_check_unsigned_eq (stats.total_jobs, 0, NULL);
	test_check_unsigned_eq (thpool_test_histogram_total (stats.wait_histogram), 0, NULL);

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);
	test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, &gate), 1, NULL);

	// a caller that waits for space and does not get it
	test_check_int_eq (thpool_wait_space (thpool, THPOOL_STATS_WAIT_SPACE_TIMEOUT), 1, NULL);

	thpool_test_gate_open (&gate);
	thpool_wait (thpool);

	const unsigned int expected = THPOOL_SMALL_THREADS + THPOOL_SMALL_QUEUE;

	thpool_get_stats (thpool, &stats);
	test_check_unsigned_eq (stats.total_jobs, expected, NULL);
	test_check_unsigned_eq (stats.completed_jobs, expected, NULL);
	test_check_unsigned_eq (stats.rejected_jobs, 1, NULL);
	test_check_unsigned_eq (stats.dropped_jobs, 0, NULL);
	test_check_unsigned_eq (stats.waited_jobs, 1, NULL);
	test_check_unsigned_eq (stats.max_queued, THPOOL_SMALL_QUEUE, NULL);
	test_check_unsigned_eq (thpool_test_histogram_total (stats.wait_histogram), expected, NULL);
	test_check_unsigned_eq (thpool_test_histogram_total (stats.run_histogram), expected, NULL);

	thpool_stats_print (&stats);

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

// a job that sleeps runs for at least the sleep,
// and the one that is queued behind it in a single thread waits for at least the same
static void test_thpool_stats_times (void) {

	Thpool *thpool = thpool_create (1);
	test_check_ptr (thpool);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	unsigned int counter = 0;
	for (unsigned int i = 0; i < THPOOL_STATS_SLEEP_JOBS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_sleep_work, &counter), 0, NULL);
	}

	thpool_wait (thpool);
	test_check_unsigned_eq (thpool_test_counter_get (&counter), THPOOL_STATS_SLEEP_JOBS, NULL);

	ThpoolStats stats;
	thpool_get_stats (thpool, &stats);

	test_check (
		stats.run_total_microseconds >= (THPOOL_STATS_SLEEP_JOBS * THPOOL_STATS_SHORT_SLEEP_MICROSECONDS),
		NULL
	);
	test_check (stats.wait_total_microseconds >= THPOOL_STATS_SHORT_SLEEP_MICROSECONDS, NULL);
	test_check_unsigned_eq (
		thpool_test_histogram_below (stats.run_histogram, THPOOL_STATS_SHORT_SLEEP_BUCKET), 0, NULL
	);

	thpool_destroy (thpool);

}

// the queued jobs that a drain rejects
static void test_thpool_stats_dropped (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	test_check_unsigned_eq (thpool_drain (thpool, false, THPOOL_DRAIN_SHORT_TIMEOUT), 1, NULL);

	thpool_test_gate_open (&gate);
	thpool_wait (thpool);

	ThpoolStats stats;
	thpool_get_stats (thpool, &stats);
	test_check_unsigned_eq (stats.dropped_jobs, THPOOL_SMALL_QUEUE, NULL);
	test_check_unsigned_eq (stats.completed_jobs, THPOOL_SMALL_THREADS, NULL);

	// a job after the drain is rejected
	test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, &gate), 1, NULL);
	thpool_get_stats (thpool, &stats);
	test_check_unsigned_eq (stats.rejected_jobs, 1, NULL);

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

// the queued jobs still run, and new jobs are not accepted
static void test_thpool_drain_run (void) {

	Thpool *thpool = thpool_create (THPOOL_SMALL_THREADS);
	test_check_unsigned_eq (thpool_set_queue_size (thpool, THPOOL_DRAIN_JOBS), 0, NULL);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	unsigned int counter = 0;
	for (unsigned int i = 0; i < THPOOL_DRAIN_JOBS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_sleep_work, &counter), 0, NULL);
	}

	test_check_unsigned_eq (thpool_drain (thpool, true, THPOOL_DRAIN_TIMEOUT), 0, NULL);
	test_check_unsigned_eq (thpool_test_counter_get (&counter), THPOOL_DRAIN_JOBS, NULL);

	test_check_int_eq (thpool_add_work (thpool, thpool_test_sleep_work, &counter), 1, NULL);
	test_check_true (thpool_is_full (thpool));

	thpool_destroy (thpool);

	// a drain without a timeout waits for every job
	thpool = thpool_create (THPOOL_SMALL_THREADS);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	counter = 0;
	for (unsigned int i = 0; i < THPOOL_SMALL_THREADS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_sleep_work, &counter), 0, NULL);
	}

	test_check_unsigned_eq (thpool_drain (thpool, true, THPOOL_DRAIN_TIMEOUT_NONE), 0, NULL);
	test_check_unsigned_eq (thpool_test_counter_get (&counter), THPOOL_SMALL_THREADS, NULL);

	thpool_destroy (thpool);

}

// the queued jobs are rejected right away, the working ones finish
static void test_thpool_drain_reject (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	test_check_unsigned_eq (thpool_drain (thpool, false, THPOOL_DRAIN_SHORT_TIMEOUT), 1, NULL);
	test_check_unsigned_eq (thpool_test_counter_get (&gate.rejected), THPOOL_SMALL_QUEUE, NULL);
	test_check_unsigned_eq (thpool_get_queued (thpool), 0, NULL);

	thpool_test_gate_open (&gate);
	thpool_wait (thpool);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS, NULL);

	thpool_destroy (thpool);
	thpool_test_gate_end (&gate);

}

// with the run policy, the jobs that are still queued when the timeout expires are rejected
static void test_thpool_drain_timeout (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	thpool_test_fill (thpool, &gate);

	test_check_unsigned_eq (thpool_drain (thpool, true, THPOOL_DRAIN_SHORT_TIMEOUT), 1, NULL);
	test_check_unsigned_eq (thpool_test_counter_get (&gate.rejected), THPOOL_SMALL_QUEUE, NULL);
	test_check_unsigned_eq (thpool_get_num_threads_working (thpool), THPOOL_SMALL_THREADS, NULL);

	thpool_test_gate_open (&gate);
	thpool_destroy (thpool);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS, NULL);

	thpool_test_gate_end (&gate);

}

static void thpool_test_free_args (void *args) {

	free (args);

}

static void thpool_test_never_work (void *args) {

	(void) args;

	fail ("a rejected job was executed");

}

static void *thpool_test_destroy_thread (void *thpool_ptr) {

	thpool_destroy ((Thpool *) thpool_ptr);

	return NULL;

}

// every queued job is rejected, so its args can be released (checked with valgrind / asan)
static void test_thpool_destroy_full (void) {

	Thpool *thpool = thpool_test_create_small ();

	ThpoolTestGate gate;
	thpool_test_gate_init (&gate);

	for (unsigned int i = 0; i < THPOOL_SMALL_THREADS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_gate_work, &gate), 0, NULL);
	}

	thpool_test_wait_working (thpool, THPOOL_SMALL_THREADS);

	for (unsigned int i = 0; i < THPOOL_SMALL_QUEUE; i++) {
		test_check_int_eq (
			thpool_add_work_with_reject (
				thpool, thpool_test_never_work, thpool_test_free_args,
				malloc (sizeof (unsigned int))
			),
			0, NULL
		);
	}

	// the destroy rejects the queued jobs while the working ones are still blocked,
	// and then it waits for them
	pthread_t thread_id;
	test_check_int_eq (pthread_create (&thread_id, NULL, thpool_test_destroy_thread, thpool), 0, NULL);

	while (thpool_get_queued (thpool)) thpool_test_sleep (THPOOL_TEST_SHORT_SLEEP);

	thpool_test_gate_open (&gate);
	(void) pthread_join (thread_id, NULL);

	test_check_unsigned_eq (thpool_test_counter_get (&gate.ran), THPOOL_SMALL_THREADS, NULL);

	thpool_test_gate_end (&gate);

}

#pragma endregion

#pragma region threads

typedef struct ThpoolTestSignals {

	unsigned int checked;
	unsigned int blocked;

} ThpoolTestSignals;

static void thpool_test_signals_work (void *signals_ptr) {

	ThpoolTestSignals *signals = (ThpoolTestSignals *) signals_ptr;

	sigset_t current;
	(void) pthread_sigmask (SIG_BLOCK, NULL, &current);

	if (sigismember (&current, SIGINT) && sigismember (&current, SIGTERM)) {
		thpool_test_counter_add (&signals->blocked);
	}

	thpool_test_counter_add (&signals->checked);

}

// the process signals are only delivered to the main thread
static void test_thpool_signals (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);
	test_check_unsigned_eq (thpool_init (thpool), 0, NULL);

	ThpoolTestSignals signals = { 0, 0 };
	for (unsigned int i = 0; i < THPOOL_N_THREADS; i++) {
		test_check_int_eq (thpool_add_work (thpool, thpool_test_signals_work, &signals), 0, NULL);
	}

	thpool_wait (thpool);

	test_check_unsigned_eq (signals.checked, THPOOL_N_THREADS, NULL);
	test_check_unsigned_eq (signals.blocked, THPOOL_N_THREADS, NULL);

	// the creator keeps its own mask
	sigset_t current;
	(void) pthread_sigmask (SIG_BLOCK, NULL, &current);
	test_check_false (sigismember (&current, SIGTERM));

	thpool_destroy (thpool);

}

// the child can not allocate the stacks of every thread,
// thpool_init () must fail and stop the threads that did start, instead of hanging
static void thpool_test_failure_child (void) {

	(void) alarm (THPOOL_FAILURE_ALARM);

	struct rlimit limit = { THPOOL_FAILURE_MEMORY_LIMIT, THPOOL_FAILURE_MEMORY_LIMIT };
	if (setrlimit (RLIMIT_AS, &limit)) _exit (EXIT_FAILURE);

	Thpool *thpool = thpool_create (THPOOL_FAILURE_THREADS);
	if (!thpool) _exit (EXIT_FAILURE);

	const unsigned int result = thpool_init (thpool);
	const unsigned int alive = thpool_get_num_threads_alive (thpool);

	thpool_destroy (thpool);

	_exit ((result == 1) && (alive == 0) ? EXIT_SUCCESS : EXIT_FAILURE);

}

// valgrind and the sanitizers need more address space than the limit
static bool thpool_test_memory_limit_is_valid (void) {

	const char *preload = getenv (THPOOL_TEST_PRELOAD_VARIABLE);

	return !(preload && strstr (preload, THPOOL_TEST_VALGRIND_PRELOAD));

}

static void test_thpool_init_failure (void) {

	#ifndef THPOOL_TEST_SANITIZER
	if (!thpool_test_memory_limit_is_valid ()) return;

	(void) fflush (stdout);

	pid_t pid = fork ();
	test_check (pid >= 0, "fork () failed");

	if (pid == 0) thpool_test_failure_child ();

	int status = 0;
	(void) waitpid (pid, &status, 0);

	test_check (WIFEXITED (status), "the thpool init failure child did not exit (it hanged or crashed)");
	test_check_int_eq (WEXITSTATUS (status), EXIT_SUCCESS, NULL);
	#endif

}

#pragma endregion

void threads_tests_thpool (void) {

	(void) printf ("Testing THREADS thpool...\n");

	test_thpool_create ();
	test_thpool_set_name ();
	test_thpool_queue_size ();
	test_thpool_init ();
	test_thpool_add_work ();
	test_thpool_burst ();
	test_thpool_full ();
	test_thpool_wait_space ();
	test_thpool_wait_space_drain ();
	test_thpool_drain_run ();
	test_thpool_drain_reject ();
	test_thpool_drain_timeout ();
	test_thpool_stats_bucket ();
	test_thpool_stats_counts ();
	test_thpool_stats_times ();
	test_thpool_stats_dropped ();
	test_thpool_destroy_full ();
	test_thpool_signals ();
	test_thpool_init_failure ();

	(void) printf ("Done!\n");

}
