#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/threads/jobs.h>
#include <cerver/threads/worker.h>

#include "../test.h"

static void work_method (void *data_ptr) {

	(void) data_ptr;

	(void) printf ("Some work that needs to be done!");

}

static void worker_remove_reference (const void *worker_ptr) {

	(void) worker_ptr;

	(void) printf ("Removing reference...");

}

static Worker *test_worker_create (void) {

	Worker *worker = worker_create ();

	test_check_ptr (worker);

	test_check_unsigned_eq (worker->id, 0, NULL);

	test_check_unsigned_eq (worker->name_len, 0, NULL);
	test_check_str_empty (worker->name);

	test_check_unsigned_eq (worker->state, WORKER_STATE_NONE, NULL);

	test_check_unsigned_eq (worker->thread_id, 0, NULL);

	test_check_false (worker->stop);
	test_check_false (worker->end);

	test_check_ptr (worker->job_queue);

	test_check_null_ptr (worker->work);
	test_check_null_ptr (worker->delete_data);

	test_check_null_ptr (worker->reference);
	test_check_null_ptr (worker->remove_reference);

	return worker;

}

static void test_worker_set_name (void) {

	Worker *worker = test_worker_create ();

	static const char *name = "test";

	worker_set_name (worker, name);

	test_check_unsigned_eq (worker->name_len, strlen (name), NULL);
	test_check_str_eq (worker->name, name, NULL);

	worker_delete (worker);

}

static void test_worker_get_state (void) {

	Worker *worker = test_worker_create ();

	test_check_unsigned_eq (
		worker->state,
		WORKER_STATE_NONE,
		worker_get_state (worker)
	);

	worker_delete (worker);

}

static void test_worker_set_state (void) {

	Worker *worker = test_worker_create ();

	worker_set_state (worker, WORKER_STATE_AVAILABLE);

	test_check_unsigned_eq (
		worker->state,
		WORKER_STATE_AVAILABLE,
		worker_get_state (worker)
	);

	worker_delete (worker);

}

static void test_worker_get_stop (void) {

	Worker *worker = test_worker_create ();

	test_check_false (worker_get_stop (worker));

	worker_delete (worker);

}

static void test_worker_set_stop (void) {

	Worker *worker = test_worker_create ();

	worker_set_stop (worker, true);

	test_check_true (worker_get_stop (worker));

	worker_delete (worker);

}

static void test_worker_get_end (void) {

	Worker *worker = test_worker_create ();

	test_check_false (worker_get_end (worker));

	worker_delete (worker);

}

static void test_worker_set_end (void) {

	Worker *worker = test_worker_create ();

	worker_set_end (worker, true);

	test_check_true (worker_get_end (worker));

	worker_delete (worker);

}

static void test_worker_set_work (void) {

	Worker *worker = test_worker_create ();

	worker_set_work (worker, work_method);

	test_check_ptr_eq (worker->work, work_method);

	worker_delete (worker);

}

static void test_worker_set_delete_data (void) {

	Worker *worker = test_worker_create ();

	worker_set_delete_data (worker, free);

	test_check_ptr_eq (worker->delete_data, free);

	worker_delete (worker);

}

static void test_worker_set_reference (void) {

	Worker *worker = test_worker_create ();

	worker_set_reference (worker, worker);

	test_check_ptr (worker->reference);

	worker_delete (worker);

}

static void test_worker_set_remove_reference (void) {

	Worker *worker = test_worker_create ();

	worker_set_remove_reference (worker, worker_remove_reference);

	test_check_ptr_eq (worker->remove_reference, worker_remove_reference);

	worker_delete (worker);

}

static void test_worker_start (void) {

	Worker *worker = test_worker_create ();

	test_check_unsigned_eq (worker_start (worker), 0, NULL);

	test_check_unsigned_eq (worker_end (worker), 0, NULL);

	worker_delete (worker);

}

static void test_worker_stop (void) {

	Worker *worker = test_worker_create ();

	test_check_unsigned_eq (worker_start (worker), 0, NULL);

	test_check_unsigned_eq (worker_stop (worker), 0, NULL);

	test_check_unsigned_eq (worker_end (worker), 0, NULL);

	worker_delete (worker);

}

static void test_worker_resume (void) {

	Worker *worker = test_worker_create ();

	test_check_unsigned_eq (worker_start (worker), 0, NULL);

	test_check_unsigned_eq (worker_stop (worker), 0, NULL);

	test_check_unsigned_eq (worker_resume (worker), 0, NULL);

	test_check_unsigned_eq (worker_end (worker), 0, NULL);

	worker_delete (worker);

}

#define WORKER_TEST_JOBS					16
#define WORKER_TEST_CONTROL_MAX_SECONDS		0.05
#define WORKER_TEST_SLOW_NANOSECONDS		64000000L
#define WORKER_TEST_IDLE_NANOSECONDS		16000000L
#define WORKER_TEST_IDLE_MILLISECONDS		16
#define WORKER_TEST_SHARED_WORKERS			2

static unsigned int worker_jobs = 0;

static void worker_test_count (void *args) {

	(void) args;

	test_counter_add (&worker_jobs);

}

static void worker_test_slow (void *args) {

	(void) args;

	const struct timespec slow = { 0, WORKER_TEST_SLOW_NANOSECONDS };
	(void) nanosleep (&slow, NULL);

	test_counter_add (&worker_jobs);

}

// #352: the worker waits for its state in a condition,
// before stop and end checked it every 100 ms
static void test_worker_control_wait (void) {

	Worker *worker = test_worker_create ();
	test_check_unsigned_eq (worker_start (worker), 0, NULL);

	double start = test_wait_now ();
	test_check_unsigned_eq (worker_stop (worker), 0, NULL);
	test_check ((test_wait_now () - start) < WORKER_TEST_CONTROL_MAX_SECONDS, "the stop polled the worker");
	test_check_int_eq (worker_get_state (worker), WORKER_STATE_STOPPED, NULL);

	// a stopped worker can not be stopped again, and a running one can not be resumed
	test_check_unsigned_ne (worker_stop (worker), 0);
	test_check_unsigned_eq (worker_resume (worker), 0, NULL);
	test_check_unsigned_ne (worker_resume (worker), 0);

	start = test_wait_now ();
	test_check_unsigned_eq (worker_end (worker), 0, NULL);
	test_check ((test_wait_now () - start) < WORKER_TEST_CONTROL_MAX_SECONDS, "the end polled the worker");
	test_check_int_eq (worker_get_state (worker), WORKER_STATE_ENDED, NULL);
	test_check_unsigned_ne (worker_end (worker), 0);

	worker_delete (worker);

}

// a stop while a job runs waits for it, the jobs pushed while it is stopped wait for the resume
static void test_worker_jobs (void) {

	worker_jobs = 0;

	Worker *worker = test_worker_create ();
	test_check_unsigned_eq (worker_start (worker), 0, NULL);

	test_check_unsigned_eq (worker_push_job_with_work (worker, worker_test_slow, NULL), 0, NULL);

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;
	while ((worker_get_state (worker) != WORKER_STATE_WORKING) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	test_check_int_eq (worker_get_state (worker), WORKER_STATE_WORKING, NULL);

	test_check_unsigned_eq (worker_stop (worker), 0, NULL);
	test_check_unsigned_eq (worker_jobs, 1, "the stop did not wait for the job");

	worker_set_work (worker, worker_test_count);
	for (unsigned int i = 0; i < WORKER_TEST_JOBS; i++) {
		test_check_unsigned_eq (worker_push_job (worker, NULL), 0, NULL);
	}

	test_check_unsigned_eq ((unsigned int) job_queue_size (worker->job_queue), WORKER_TEST_JOBS, NULL);

	test_check_unsigned_eq (worker_resume (worker), 0, NULL);
	test_check_unsigned_eq (
		test_wait_counter (&worker_jobs, WORKER_TEST_JOBS + 1, TEST_WAIT_SECONDS),
		WORKER_TEST_JOBS + 1, NULL
	);

	// an available worker counts how long it has been without a job
	const struct timespec idle = { 0, WORKER_TEST_IDLE_NANOSECONDS };
	(void) nanosleep (&idle, NULL);
	test_check (worker_get_idle_milliseconds (worker) >= WORKER_TEST_IDLE_MILLISECONDS, NULL);

	// the delete ends a worker that is running
	worker_delete (worker);

}

// workers that share a queue take each job once, and they do not delete the queue
static void test_worker_shared_queue (void) {

	worker_jobs = 0;

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
	test_check_null_ptr (worker_create_with_queue (0, NULL));

	Worker *workers[WORKER_TEST_SHARED_WORKERS] = { NULL };
	for (unsigned int i = 0; i < WORKER_TEST_SHARED_WORKERS; i++) {
		workers[i] = worker_create_with_queue (i, job_queue);
		test_check_ptr (workers[i]);
		test_check_false (workers[i]->owns_queue);
		test_check_unsigned_eq (worker_start (workers[i]), 0, NULL);
	}

	for (unsigned int i = 0; i < WORKER_TEST_JOBS; i++) {
		test_check_unsigned_eq (job_queue_push_job (job_queue, worker_test_count, NULL), 0, NULL);
	}

	test_check_unsigned_eq (test_wait_counter (&worker_jobs, WORKER_TEST_JOBS, TEST_WAIT_SECONDS), WORKER_TEST_JOBS, NULL);

	for (unsigned int i = 0; i < WORKER_TEST_SHARED_WORKERS; i++) {
		worker_delete (workers[i]);
	}

	JobQueueStats stats;
	job_queue_get_stats (job_queue, &stats);
	test_check_unsigned_eq ((unsigned int) stats.completed_jobs, WORKER_TEST_JOBS, NULL);

	job_queue_delete (job_queue);

}

void threads_tests_worker (void) {

	(void) printf ("Testing THREADS worker...\n");

	test_worker_set_name ();
	test_worker_get_state ();
	test_worker_set_state ();
	test_worker_get_stop ();
	test_worker_set_stop ();
	test_worker_get_end ();
	test_worker_set_end ();
	test_worker_set_work ();
	test_worker_set_delete_data ();
	test_worker_set_reference ();
	test_worker_set_remove_reference ();

	test_worker_start ();
	test_worker_stop ();
	test_worker_resume ();

	test_worker_control_wait ();
	test_worker_jobs ();
	test_worker_shared_queue ();

	(void) printf ("Done!\n");

}
