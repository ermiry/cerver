#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/threads/worker.h>

#include "../test.h"

static void work_method (void *data_ptr) {

	(void) printf ("Some work that needs to be done!");

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

	test_worker_start ();
	test_worker_stop ();
	test_worker_resume ();

	(void) printf ("Done!\n");

}
