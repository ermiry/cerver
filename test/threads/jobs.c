#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <pthread.h>
#include <time.h>

#include <cerver/collections/pool.h>

#include <cerver/threads/jobs.h>
#include <cerver/threads/thpool.h>

#include "../test.h"

#define JOB_QUEUE_STEADY_ROUNDS		1024

static void work_method (void *args) {

	(void) args;

	(void) printf ("Some work that needs to be done!");

}

static void handler_method (void *job_handler) {

	(void) job_handler;

	(void) printf ("Custom job queue handler method!");

}

static void test_job_new (void) {

	Job *job = (Job *) job_new ();

	test_check_ptr (job);
	test_check_unsigned_eq (job->id, 0, NULL);
	test_check_null_ptr (job->work);
	test_check_null_ptr (job->args);

	job_delete (job);

}

static void test_job_create (void) {

	unsigned int value = 10;
	void *data = &value;

	Job *job = job_create (work_method, data);

	test_check_ptr (job);
	test_check_ptr (job->work);
	test_check_ptr (job->args);

	job_delete (job);

}

static void test_job_reset (void) {

	unsigned int value = 10;
	void *data = &value;

	Job *job = job_create (work_method, data);

	test_check_ptr (job);
	test_check_ptr (job->work);
	test_check_ptr (job->args);

	job_reset (job);

	test_check_ptr (job);
	test_check_null_ptr (job->work);
	test_check_null_ptr (job->args);

	job_delete (job);

}

static void test_job_handler_new (void) {

	JobHandler *handler = (JobHandler *) job_handler_new ();
	
	test_check_ptr (handler);
	test_check_null_ptr (handler->cerver);
	test_check_null_ptr (handler->connection);
	test_check_null_ptr (handler->cond);
	test_check_null_ptr (handler->mutex);
	test_check_bool_eq (handler->done, false, NULL);
	test_check_null_ptr (handler->data);
	test_check_null_ptr (handler->data_delete);

	job_handler_delete (handler);

}

static JobHandler *test_job_handler_create (void) {

	JobHandler *handler = (JobHandler *) job_handler_create ();

	test_check_ptr (handler);
	test_check_null_ptr (handler->cerver);
	test_check_null_ptr (handler->connection);
	test_check_ptr (handler->cond);
	test_check_ptr (handler->mutex);
	test_check_bool_eq (handler->done, false, NULL);
	test_check_null_ptr (handler->data);
	test_check_null_ptr (handler->data_delete);

	return handler;

}

static void test_job_handler_reset (void) {
	
	JobHandler *handler = test_job_handler_create ();

	job_handler_reset (handler);
	test_check_ptr (handler);
	test_check_null_ptr (handler->cerver);
	test_check_null_ptr (handler->connection);
	test_check_ptr (handler->cond);
	test_check_ptr (handler->mutex);
	test_check_bool_eq (handler->done, false, NULL);
	test_check_null_ptr (handler->data);
	test_check_null_ptr (handler->data_delete);

	job_handler_delete (handler);

}

static void test_job_handler_done (void) {
	
	JobHandler *handler = test_job_handler_create ();

	job_handler_signal (handler);
	test_check_ptr (handler);
	test_check_null_ptr (handler->cerver);
	test_check_null_ptr (handler->connection);
	test_check_ptr (handler->cond);
	test_check_ptr (handler->mutex);
	test_check_bool_eq (handler->done, true, NULL);
	test_check_null_ptr (handler->data);
	test_check_null_ptr (handler->data_delete);

	job_handler_delete (handler);

}

static void test_job_queue_new (void) {

	JobQueue *job_queue = job_queue_new ();

	test_check_ptr (job_queue);
	test_check_int_eq (job_queue->type, JOB_QUEUE_TYPE_NONE, NULL);
	test_check_null_ptr (job_queue->pool);
	test_check_unsigned_eq (job_queue->n_priorities, JOB_QUEUE_DEFAULT_PRIORITIES, NULL);
	test_check_unsigned_eq ((unsigned int) job_queue->count, 0, NULL);
	test_check_unsigned_eq (job_queue->starvation_limit, JOB_QUEUE_DEFAULT_STARVATION_LIMIT, NULL);
	test_check_bool_eq (job_queue->waiting, false, NULL);
	test_check_unsigned_eq (job_queue->requested_id, 0, NULL);
	test_check_null_ptr (job_queue->requested_job);
	test_check_bool_eq (job_queue->running, false, NULL);
	test_check_unsigned_eq (job_queue->handler_thread_id, 0, NULL);
	test_check_null_ptr (job_queue->handler);

	job_queue_delete (job_queue);

}

static void test_job_queue_create_jobs (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

	test_check_ptr (job_queue);
	test_check_int_eq (job_queue->type, JOB_QUEUE_TYPE_JOBS, NULL);
	test_check_ptr (job_queue->pool);
	test_check_unsigned_eq ((unsigned int) pool_size (job_queue->pool), JOB_QUEUE_POOL_INIT, NULL);
	test_check (job_queue->destroy == job_delete, NULL);
	test_check_bool_eq (job_queue->running, false, NULL);
	test_check_unsigned_eq (job_queue->handler_thread_id, 0, NULL);
	test_check_null_ptr (job_queue->handler);

	job_queue_delete (job_queue);

}

static void test_job_queue_create_handlers (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);

	test_check_ptr (job_queue);
	test_check_int_eq (job_queue->type, JOB_QUEUE_TYPE_HANDLERS, NULL);
	test_check_ptr (job_queue->pool);
	test_check (job_queue->destroy == job_handler_delete, NULL);
	test_check_bool_eq (job_queue->running, false, NULL);
	test_check_unsigned_eq (job_queue->handler_thread_id, 0, NULL);
	test_check_null_ptr (job_queue->handler);

	job_queue_delete (job_queue);

}

static void test_job_queue_set_handler (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);

	test_check_ptr (job_queue);
	test_check_null_ptr (job_queue->handler);

	job_queue_set_handler (job_queue, handler_method);
	test_check_ptr (job_queue->handler);

	job_queue_delete (job_queue);

}

#define JOBS_TEST_PRIORITIES				5
#define JOBS_TEST_COUNT						8
#define JOBS_TEST_STARVATION_LIMIT			2
#define JOBS_TEST_PUSHES_PER_ROUND			3
#define JOBS_TEST_TIMEOUT					64
#define JOBS_TEST_TIMEOUT_MIN_SECONDS		0.05
#define JOBS_TEST_LATE_PUSH_NANOSECONDS		32000000L
#define JOBS_TEST_REQUEST_ID				7
#define JOBS_TEST_THREADS					4
#define JOBS_TEST_THREAD_JOBS				4096
#define JOBS_TEST_RUN_NANOSECONDS			2000000L

// the priorities are clamped to the limits
static void test_job_queue_priorities (void) {

	JobQueue *job_queue = job_queue_create_with_priorities (JOB_QUEUE_TYPE_JOBS, JOBS_TEST_PRIORITIES);
	test_check_unsigned_eq (job_queue->n_priorities, JOBS_TEST_PRIORITIES, NULL);
	test_check_unsigned_eq (job_queue_default_priority (job_queue), JOBS_TEST_PRIORITIES / 2, NULL);
	job_queue_delete (job_queue);

	job_queue = job_queue_create_with_priorities (JOB_QUEUE_TYPE_JOBS, 0);
	test_check_unsigned_eq (job_queue->n_priorities, 1, NULL);
	job_queue_delete (job_queue);

	job_queue = job_queue_create_with_priorities (JOB_QUEUE_TYPE_JOBS, JOB_QUEUE_MAX_PRIORITIES + 1);
	test_check_unsigned_eq (job_queue->n_priorities, JOB_QUEUE_MAX_PRIORITIES, NULL);
	job_queue_delete (job_queue);

	job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
	test_check_unsigned_eq (job_queue_default_priority (job_queue), JOB_QUEUE_PRIORITY_NORMAL, NULL);
	job_queue_delete (job_queue);

}

// the jobs of a priority keep their order, and a higher priority goes first
static void test_job_queue_order (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_NONE);
	job_queue_set_starvation_limit (job_queue, JOB_QUEUE_STARVATION_DISABLED);

	unsigned int values[JOBS_TEST_COUNT] = { 0 };
	for (unsigned int i = 0; i < JOBS_TEST_COUNT; i++) {
		values[i] = i;
		test_check_unsigned_eq (job_queue_push_priority (job_queue, &values[i], JOB_QUEUE_PRIORITY_LOW), 0, NULL);
	}

	unsigned int urgent = JOBS_TEST_COUNT;
	test_check_unsigned_eq (job_queue_push_priority (job_queue, &urgent, JOB_QUEUE_PRIORITY_HIGH), 0, NULL);

	unsigned int normal = JOBS_TEST_COUNT + 1;
	test_check_unsigned_eq (job_queue_push (job_queue, &normal), 0, NULL);

	// a priority that does not exist is the lowest one
	unsigned int lowest = JOBS_TEST_COUNT + 2;
	test_check_unsigned_eq (job_queue_push_priority (job_queue, &lowest, JOB_QUEUE_MAX_PRIORITIES), 0, NULL);

	test_check_unsigned_eq ((unsigned int) job_queue_size (job_queue), JOBS_TEST_COUNT + 3, NULL);

	test_check (job_queue_pull (job_queue) == &urgent, "the high priority job was not first");
	test_check (job_queue_pull (job_queue) == &normal, "the normal priority job was not second");
	for (unsigned int i = 0; i < JOBS_TEST_COUNT; i++) {
		test_check (job_queue_pull (job_queue) == &values[i], "the low priority jobs lost their order");
	}

	test_check (job_queue_pull (job_queue) == &lowest, NULL);
	test_check_null_ptr (job_queue_pull (job_queue));

	// more jobs than the first capacity of a priority, with its head moved by the pulls,
	// so it grows while the jobs wrap around: each round pushes 3 jobs and pulls the oldest one
	for (unsigned int round = 0; round < JOB_QUEUE_DEFAULT_RING_CAPACITY; round++) {
		for (unsigned int push = 0; push < JOBS_TEST_PUSHES_PER_ROUND; push++) {
			test_check_unsigned_eq (job_queue_push (job_queue, &values[round % JOBS_TEST_COUNT]), 0, NULL);
		}

		test_check (
			job_queue_pull (job_queue) == &values[(round / JOBS_TEST_PUSHES_PER_ROUND) % JOBS_TEST_COUNT],
			"the order changed when it grew"
		);
	}

	const unsigned int remaining = JOB_QUEUE_DEFAULT_RING_CAPACITY * (JOBS_TEST_PUSHES_PER_ROUND - 1);
	for (unsigned int pulled = 0; pulled < remaining; pulled++) {
		const unsigned int pushed = JOB_QUEUE_DEFAULT_RING_CAPACITY + pulled;
		test_check (
			job_queue_pull (job_queue) == &values[(pushed / JOBS_TEST_PUSHES_PER_ROUND) % JOBS_TEST_COUNT],
			"the order changed when it grew"
		);
	}

	test_check_unsigned_eq ((unsigned int) job_queue_size (job_queue), 0, NULL);

	job_queue_delete (job_queue);

}

// a waiting low priority job is taken after the starvation limit of high priority jobs
static void test_job_queue_starvation (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_NONE);
	job_queue_set_starvation_limit (job_queue, JOBS_TEST_STARVATION_LIMIT);

	unsigned int high[JOBS_TEST_COUNT] = { 0 };
	for (unsigned int i = 0; i < JOBS_TEST_COUNT; i++) {
		test_check_unsigned_eq (job_queue_push_priority (job_queue, &high[i], JOB_QUEUE_PRIORITY_HIGH), 0, NULL);
	}

	unsigned int low = 0;
	test_check_unsigned_eq (job_queue_push_priority (job_queue, &low, JOB_QUEUE_PRIORITY_LOW), 0, NULL);

	for (unsigned int i = 0; i < JOBS_TEST_STARVATION_LIMIT; i++) {
		test_check (job_queue_pull (job_queue) == &high[i], NULL);
	}

	test_check (job_queue_pull (job_queue) == &low, "the low priority job was starved");

	JobQueueStats stats;
	job_queue_get_stats (job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.promoted_jobs, 1, NULL);
	test_check_unsigned_eq ((unsigned int) stats.priority_jobs[JOB_QUEUE_PRIORITY_HIGH], JOBS_TEST_COUNT, NULL);
	test_check_unsigned_eq ((unsigned int) stats.priority_jobs[JOB_QUEUE_PRIORITY_LOW], 1, NULL);
	test_check_unsigned_eq ((unsigned int) stats.max_queued, JOBS_TEST_COUNT + 1, NULL);

	job_queue_delete (job_queue);

}

static void *jobs_test_late_push (void *job_queue_ptr) {

	const struct timespec late = { 0, JOBS_TEST_LATE_PUSH_NANOSECONDS };
	(void) nanosleep (&late, NULL);

	static unsigned int value = 0;
	(void) job_queue_push ((JobQueue *) job_queue_ptr, &value);

	return NULL;

}

static void *jobs_test_late_signal (void *job_queue_ptr) {

	const struct timespec late = { 0, JOBS_TEST_LATE_PUSH_NANOSECONDS };
	(void) nanosleep (&late, NULL);

	job_queue_signal ((JobQueue *) job_queue_ptr);

	return NULL;

}

// #320: a wait with a deadline, that a push or a signal ends before it
static void test_job_queue_timeouts (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

	const double start = test_wait_now ();
	test_check_null_ptr (job_queue_pull_timeout (job_queue, JOBS_TEST_TIMEOUT));
	test_check ((test_wait_now () - start) >= JOBS_TEST_TIMEOUT_MIN_SECONDS, "the pull did not wait its timeout");

	pthread_t thread = 0;
	test_check_int_eq (pthread_create (&thread, NULL, jobs_test_late_push, job_queue), 0, NULL);
	test_check_ptr (job_queue_pull_timeout (job_queue, JOB_QUEUE_WAIT_FOREVER));
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	test_check_int_eq (pthread_create (&thread, NULL, jobs_test_late_signal, job_queue), 0, NULL);
	test_check_null_ptr (job_queue_pull_timeout (job_queue, JOB_QUEUE_WAIT_FOREVER));
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	// a signal sent before job_queue_wait () is kept
	job_queue_signal (job_queue);
	job_queue_wait (job_queue);

	JobQueueStats stats;
	job_queue_get_stats (job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.timed_out_waits, 1, NULL);

	job_queue_delete (job_queue);

}

static void *jobs_test_late_job_with_id (void *job_queue_ptr) {

	const struct timespec late = { 0, JOBS_TEST_LATE_PUSH_NANOSECONDS };
	(void) nanosleep (&late, NULL);

	(void) job_queue_push_job_with_id ((JobQueue *) job_queue_ptr, JOBS_TEST_REQUEST_ID, work_method, NULL);

	return NULL;

}

// a job by its id: already in the queue, pushed while it waits, and never pushed
static void test_job_queue_request (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

	test_check_unsigned_eq (job_queue_push_job (job_queue, work_method, NULL), 0, NULL);
	test_check_unsigned_eq (job_queue_push_job_with_id (job_queue, JOBS_TEST_REQUEST_ID, work_method, NULL), 0, NULL);
	test_check_unsigned_eq (job_queue_push_job (job_queue, work_method, NULL), 0, NULL);

	Job *job = (Job *) job_queue_request (job_queue, JOBS_TEST_REQUEST_ID);
	test_check_ptr (job);
	test_check_unsigned_eq ((unsigned int) job->id, JOBS_TEST_REQUEST_ID, NULL);
	test_check_unsigned_eq ((unsigned int) job_queue_size (job_queue), 2, NULL);
	job_return (job_queue, job);

	pthread_t thread = 0;
	test_check_int_eq (pthread_create (&thread, NULL, jobs_test_late_job_with_id, job_queue), 0, NULL);
	job = (Job *) job_queue_request_timeout (job_queue, JOBS_TEST_REQUEST_ID, JOB_QUEUE_WAIT_FOREVER);
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);
	test_check_ptr (job);
	test_check_unsigned_eq ((unsigned int) job->id, JOBS_TEST_REQUEST_ID, NULL);
	job_return (job_queue, job);

	test_check_null_ptr (job_queue_request_timeout (job_queue, JOBS_TEST_REQUEST_ID, JOBS_TEST_TIMEOUT));

	JobQueueStats stats;
	job_queue_get_stats (job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.requested_jobs, 2, NULL);

	job_queue_delete (job_queue);

}

// the jobs come from the pool of the queue and go back to it
static void test_job_queue_pool (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

	Job *job = job_get (job_queue);
	test_check_ptr (job);
	test_check_unsigned_eq ((unsigned int) pool_size (job_queue->pool), JOB_QUEUE_POOL_INIT - 1, NULL);

	job->args = job;
	job_return (job_queue, job);
	test_check_unsigned_eq ((unsigned int) pool_size (job_queue->pool), JOB_QUEUE_POOL_INIT, NULL);

	test_check (job_get (job_queue) == job, "the pool did not give back the same job");
	test_check_null_ptr (job->args);
	job_return (job_queue, job);

	// a push that creates the job takes it from the pool
	test_check_unsigned_eq (job_queue_push_job (job_queue, work_method, NULL), 0, NULL);
	test_check_unsigned_eq ((unsigned int) pool_size (job_queue->pool), JOB_QUEUE_POOL_INIT - 1, NULL);

	// the jobs in the queue are destroyed with it
	job_queue_delete (job_queue);

}

static bool job_queue_pool_owns (Job **jobs, const Job *job) {

	bool owned = false;
	for (unsigned int index = 0; !owned && (index < JOB_QUEUE_POOL_INIT); index++) {
		owned = (jobs[index] == job);
	}

	return owned;

}

// in a steady state every push takes a job that the pool already had,
// so pushing and pulling jobs never allocates (before 1.8b-5 each push allocated its job)
static void test_job_queue_pool_steady (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

	Job *jobs[JOB_QUEUE_POOL_INIT];
	for (unsigned int index = 0; index < JOB_QUEUE_POOL_INIT; index++) {
		jobs[index] = job_get (job_queue);
		test_check_ptr (jobs[index]);
	}

	for (unsigned int index = 0; index < JOB_QUEUE_POOL_INIT; index++) {
		job_return (job_queue, jobs[index]);
	}

	for (unsigned int round = 0; round < JOB_QUEUE_STEADY_ROUNDS; round++) {
		test_check_unsigned_eq (job_queue_push_job (job_queue, work_method, NULL), 0, NULL);

		Job *job = (Job *) job_queue_pull (job_queue);
		test_check_ptr (job);
		test_check (job_queue_pool_owns (jobs, job), "a push allocated a new job");

		job_return (job_queue, job);
	}

	test_check_unsigned_eq ((unsigned int) pool_size (job_queue->pool), JOB_QUEUE_POOL_INIT, NULL);

	job_queue_delete (job_queue);

}

typedef struct JobsTestShared {

	JobQueue *job_queue;
	bool done;
	unsigned int consumed;

} JobsTestShared;

static bool jobs_test_done (const void *shared_ptr) {

	return __atomic_load_n (&((const JobsTestShared *) shared_ptr)->done, __ATOMIC_SEQ_CST);

}

static void *jobs_test_producer (void *shared_ptr) {

	JobsTestShared *shared = (JobsTestShared *) shared_ptr;

	for (unsigned int i = 0; i < JOBS_TEST_THREAD_JOBS; i++) {
		test_check_unsigned_eq (
			job_queue_push_job_priority (shared->job_queue, work_method, NULL, i % JOB_QUEUE_DEFAULT_PRIORITIES),
			0, NULL
		);
	}

	return NULL;

}

static void *jobs_test_consumer (void *shared_ptr) {

	JobsTestShared *shared = (JobsTestShared *) shared_ptr;

	while (!jobs_test_done (shared)) {
		Job *job = (Job *) job_queue_pull_interruptible (shared->job_queue, jobs_test_done, shared);
		if (job) {
			test_counter_add (&shared->consumed);
			job_return (shared->job_queue, job);
		}
	}

	return NULL;

}

// many producers and consumers that wait in the queue: every job is taken once,
// the binary semaphore merged the posts of two pushes into one wake up
static void test_job_queue_threads (void) {

	JobsTestShared shared = { job_queue_create (JOB_QUEUE_TYPE_JOBS), false, 0 };

	pthread_t consumers[JOBS_TEST_THREADS];
	pthread_t producers[JOBS_TEST_THREADS];
	for (unsigned int i = 0; i < JOBS_TEST_THREADS; i++) {
		test_check_int_eq (pthread_create (&consumers[i], NULL, jobs_test_consumer, &shared), 0, NULL);
	}

	for (unsigned int i = 0; i < JOBS_TEST_THREADS; i++) {
		test_check_int_eq (pthread_create (&producers[i], NULL, jobs_test_producer, &shared), 0, NULL);
	}

	for (unsigned int i = 0; i < JOBS_TEST_THREADS; i++) {
		test_check_int_eq (pthread_join (producers[i], NULL), 0, NULL);
	}

	const unsigned int expected = JOBS_TEST_THREADS * JOBS_TEST_THREAD_JOBS;
	test_check_unsigned_eq (test_wait_counter (&shared.consumed, expected, TEST_WAIT_SECONDS), expected, "jobs were lost");

	// the consumers check the flag with the queue's lock, so the signal is never lost
	__atomic_store_n (&shared.done, true, __ATOMIC_SEQ_CST);
	job_queue_signal (shared.job_queue);

	for (unsigned int i = 0; i < JOBS_TEST_THREADS; i++) {
		test_check_int_eq (pthread_join (consumers[i], NULL), 0, NULL);
	}

	JobQueueStats stats;
	job_queue_get_stats (shared.job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.total_jobs, expected, NULL);
	test_check_unsigned_eq ((unsigned int) stats.pulled_jobs, expected, NULL);

	u64 waited = 0;
	for (unsigned int bucket = 0; bucket < THPOOL_HISTOGRAM_BUCKETS; bucket++) waited += stats.wait_histogram[bucket];
	test_check_unsigned_eq ((unsigned int) waited, expected, NULL);

	job_queue_delete (shared.job_queue);

}

static unsigned int ran_jobs = 0;

static void jobs_test_slow_work (void *args) {

	(void) args;

	const struct timespec run = { 0, JOBS_TEST_RUN_NANOSECONDS };
	(void) nanosleep (&run, NULL);

	test_counter_add (&ran_jobs);

}

// the queue's own thread runs the jobs and records how long they ran
static void test_job_queue_thread (void) {

	ran_jobs = 0;

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
	test_check_unsigned_eq (job_queue_start (job_queue), 0, NULL);

	for (unsigned int i = 0; i < JOBS_TEST_COUNT; i++) {
		test_check_unsigned_eq (job_queue_push_job (job_queue, jobs_test_slow_work, NULL), 0, NULL);
	}

	test_check_unsigned_eq (test_wait_counter (&ran_jobs, JOBS_TEST_COUNT, TEST_WAIT_SECONDS), JOBS_TEST_COUNT, NULL);
	test_check_unsigned_eq (job_queue_stop (job_queue), 0, NULL);
	test_check_unsigned_ne (job_queue_stop (job_queue), 0);

	JobQueueStats stats;
	job_queue_get_stats (job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.completed_jobs, JOBS_TEST_COUNT, NULL);
	test_check (stats.run_total_microseconds > 0, "the run time was not recorded");

	job_queue_stats_print (job_queue);

	job_queue_delete (job_queue);

}

// the oldest job tells how long the queue has waited
static void test_job_queue_oldest_wait (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_NONE);
	test_check_unsigned_eq ((unsigned int) job_queue_oldest_wait (job_queue), 0, NULL);

	unsigned int value = 0;
	test_check_unsigned_eq (job_queue_push (job_queue, &value), 0, NULL);

	const struct timespec late = { 0, JOBS_TEST_LATE_PUSH_NANOSECONDS };
	(void) nanosleep (&late, NULL);

	test_check (
		job_queue_oldest_wait (job_queue) >= (JOBS_TEST_LATE_PUSH_NANOSECONDS / JOB_QUEUE_NANOSECONDS_PER_MILLISECOND),
		"the oldest job did not wait"
	);

	// a job queue without a type does not own its jobs
	job_queue_delete (job_queue);

}

void threads_tests_jobs (void) {

	(void) printf ("Testing THREADS jobs...\n");

	// jobs
	test_job_new ();
	test_job_create ();
	test_job_reset ();

	// handlers
	test_job_handler_new ();
	test_job_handler_reset ();
	test_job_handler_done ();

	// queue
	test_job_queue_new ();
	test_job_queue_create_jobs ();
	test_job_queue_create_handlers ();
	test_job_queue_set_handler ();
	test_job_queue_priorities ();
	test_job_queue_order ();
	test_job_queue_starvation ();
	test_job_queue_timeouts ();
	test_job_queue_request ();
	test_job_queue_pool ();
	test_job_queue_pool_steady ();
	test_job_queue_threads ();
	test_job_queue_thread ();
	test_job_queue_oldest_wait ();

	(void) printf ("Done!\n");

}
