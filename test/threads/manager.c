#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/threads/jobs.h>
#include <cerver/threads/manager.h>
#include <cerver/threads/worker.h>

#include "threads.h"

#include "../test.h"

#define MANAGER_TEST_NAME					"test-manager"
#define MANAGER_TEST_MIN_WORKERS			1
#define MANAGER_TEST_MAX_WORKERS			4
#define MANAGER_TEST_SCALE_UP_WAIT			8
#define MANAGER_TEST_IDLE_TIMEOUT			64
#define MANAGER_TEST_JOBS					32
#define MANAGER_TEST_SLOW_NANOSECONDS		16000000L
#define MANAGER_TEST_BLOCKED_NANOSECONDS	128000000L
#define MANAGER_TEST_END_MAX_SECONDS		1

static unsigned int manager_jobs = 0;

static void manager_test_slow (void *args) {

	(void) args;

	const struct timespec slow = { 0, MANAGER_TEST_SLOW_NANOSECONDS };
	(void) nanosleep (&slow, NULL);

	test_counter_add (&manager_jobs);

}

static void manager_test_blocked (void *args) {

	(void) args;

	const struct timespec blocked = { 0, MANAGER_TEST_BLOCKED_NANOSECONDS };
	(void) nanosleep (&blocked, NULL);

	test_counter_add (&manager_jobs);

}

// waits until the manager has the expected workers
static unsigned int manager_test_wait_workers (
	WorkerManager *manager, const unsigned int expected
) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	unsigned int workers = worker_manager_get_n_workers (manager);
	while ((workers != expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		workers = worker_manager_get_n_workers (manager);
	}

	return workers;

}

static WorkerManager *manager_test_create (void) {

	WorkerManager *manager = worker_manager_create (MANAGER_TEST_NAME);
	test_check_ptr (manager);
	test_check_str_eq (manager->name, MANAGER_TEST_NAME, NULL);
	test_check_ptr (manager->job_queue);
	test_check_unsigned_eq (manager->min_workers, WORKER_MANAGER_DEFAULT_MIN_WORKERS, NULL);
	test_check_unsigned_eq (manager->max_workers, WORKER_MANAGER_DEFAULT_MAX_WORKERS, NULL);

	test_check_unsigned_ne (worker_manager_set_limits (manager, 0, MANAGER_TEST_MAX_WORKERS), 0);
	test_check_unsigned_ne (worker_manager_set_limits (manager, MANAGER_TEST_MAX_WORKERS, MANAGER_TEST_MIN_WORKERS), 0);
	test_check_unsigned_ne (worker_manager_set_limits (manager, MANAGER_TEST_MIN_WORKERS, WORKER_MANAGER_MAX_WORKERS + 1), 0);
	test_check_unsigned_eq (worker_manager_set_limits (manager, MANAGER_TEST_MIN_WORKERS, MANAGER_TEST_MAX_WORKERS), 0, NULL);

	worker_manager_set_scaling (manager, MANAGER_TEST_SCALE_UP_WAIT, MANAGER_TEST_IDLE_TIMEOUT);
	test_check_unsigned_eq (manager->scale_up_wait, MANAGER_TEST_SCALE_UP_WAIT, NULL);
	test_check_unsigned_eq (manager->idle_timeout, MANAGER_TEST_IDLE_TIMEOUT, NULL);

	return manager;

}

// #353: the manager adds workers while the jobs wait, up to its maximum,
// and removes the idle ones down to its minimum
static void test_manager_scaling (void) {

	manager_jobs = 0;

	WorkerManager *manager = manager_test_create ();
	test_check_unsigned_eq (worker_manager_start (manager), 0, NULL);
	test_check_unsigned_eq (worker_manager_get_n_workers (manager), MANAGER_TEST_MIN_WORKERS, NULL);

	for (unsigned int i = 0; i < MANAGER_TEST_JOBS; i++) {
		test_check_unsigned_eq (worker_manager_push_job (manager, manager_test_slow, NULL), 0, NULL);
	}

	test_check_unsigned_eq (test_wait_counter (&manager_jobs, MANAGER_TEST_JOBS, TEST_WAIT_SECONDS), MANAGER_TEST_JOBS, NULL);

	WorkerManagerStats stats;
	worker_manager_get_stats (manager, &stats);
	test_check_unsigned_eq (stats.max_workers, MANAGER_TEST_MAX_WORKERS, "the manager did not add workers");

	test_check_unsigned_eq (
		manager_test_wait_workers (manager, MANAGER_TEST_MIN_WORKERS),
		MANAGER_TEST_MIN_WORKERS, "the manager did not remove the idle workers"
	);

	worker_manager_get_stats (manager, &stats);
	test_check_unsigned_eq (stats.max_workers, MANAGER_TEST_MAX_WORKERS, NULL);
	test_check_unsigned_eq ((unsigned int) stats.created_workers, MANAGER_TEST_MAX_WORKERS, NULL);
	test_check_unsigned_eq ((unsigned int) stats.removed_workers, MANAGER_TEST_MAX_WORKERS - MANAGER_TEST_MIN_WORKERS, NULL);
	test_check_unsigned_eq ((unsigned int) stats.queue.completed_jobs, MANAGER_TEST_JOBS, NULL);

	WorkerState states[WORKER_MANAGER_MAX_WORKERS];
	test_check_unsigned_eq (
		worker_manager_get_states (manager, states, WORKER_MANAGER_MAX_WORKERS),
		MANAGER_TEST_MIN_WORKERS, NULL
	);

	test_check_unsigned_eq (worker_manager_end (manager), 0, NULL);
	test_check_unsigned_ne (worker_manager_end (manager), 0);

	worker_manager_delete (manager);

}

// a stopped manager keeps its jobs until it is resumed
static void test_manager_stop_resume (void) {

	manager_jobs = 0;

	WorkerManager *manager = manager_test_create ();
	test_check_unsigned_eq (worker_manager_start (manager), 0, NULL);

	test_check_unsigned_eq (worker_manager_stop (manager), 0, NULL);
	test_check_unsigned_ne (worker_manager_stop (manager), 0);

	for (unsigned int i = 0; i < MANAGER_TEST_JOBS; i++) {
		test_check_unsigned_eq (worker_manager_push_job_priority (manager, manager_test_slow, NULL, JOB_QUEUE_PRIORITY_HIGH), 0, NULL);
	}

	WorkerManagerStats stats;
	worker_manager_get_stats (manager, &stats);
	test_check_unsigned_eq (stats.stopped, MANAGER_TEST_MIN_WORKERS, NULL);
	test_check_unsigned_eq (manager_jobs, 0, "a stopped manager ran a job");

	test_check_unsigned_eq (worker_manager_resume (manager), 0, NULL);
	test_check_unsigned_ne (worker_manager_resume (manager), 0);

	test_check_unsigned_eq (test_wait_counter (&manager_jobs, MANAGER_TEST_JOBS, TEST_WAIT_SECONDS), MANAGER_TEST_JOBS, NULL);

	worker_manager_delete (manager);

}

// the end waits for the jobs that are running, the queued ones are released with the manager
static void test_manager_end_running (void) {

	manager_jobs = 0;

	WorkerManager *manager = manager_test_create ();
	worker_manager_set_work (manager, manager_test_blocked);
	test_check_unsigned_eq (worker_manager_start (manager), 0, NULL);

	for (unsigned int i = 0; i < MANAGER_TEST_MAX_WORKERS; i++) {
		test_check_unsigned_eq (worker_manager_push_job (manager, NULL, NULL), 0, NULL);
	}

	const double start = test_wait_now ();
	test_check_unsigned_eq (worker_manager_end (manager), 0, NULL);
	test_check ((test_wait_now () - start) < MANAGER_TEST_END_MAX_SECONDS, "the end took too long");
	test_check_unsigned_eq (worker_manager_get_n_workers (manager), 0, NULL);

	worker_manager_delete (manager);

	// a manager without a name, deleted without being started
	manager = worker_manager_create (NULL);
	test_check_ptr (manager);
	test_check_str_empty (manager->name);
	test_check_unsigned_ne (worker_manager_end (manager), 0);
	worker_manager_delete (manager);

}

void threads_tests_manager (void) {

	(void) printf ("Testing THREADS manager...\n");

	test_manager_scaling ();
	test_manager_stop_resume ();
	test_manager_end_running ();

	(void) printf ("Done!\n");

}
