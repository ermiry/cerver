#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include <cerver/threads/thpool.h>

#include "../test.h"

#define THPOOL_N_THREADS		4
#define THPOOL_NAME				"test-thpool"

static void work_method (void *args) {

	(void) printf ("Some work that needs to be done!");

	(void) sleep (3);

}

static Thpool *test_thpool_create (void) {

	Thpool *thpool = thpool_create (THPOOL_N_THREADS);

	test_check_ptr (thpool);

	test_check_unsigned_eq (thpool->n_threads, THPOOL_N_THREADS, NULL);
	test_check_ptr (thpool->threads);

	test_check_bool_eq (thpool->keep_alive, false, NULL);
	test_check_unsigned_eq (thpool->num_threads_alive, 0, NULL);
	test_check_unsigned_eq (thpool->num_threads_working, 0, NULL);

	test_check_ptr (thpool->mutex);
	test_check_ptr (thpool->threads_all_idle);
	
	test_check_ptr (thpool->job_queue);

	return thpool;

}

static void test_thpool_set_name (void) {

	Thpool *thpool = test_thpool_create ();

	thpool_set_name (thpool, THPOOL_NAME);

	test_check_str_eq (thpool->name, THPOOL_NAME, NULL);
	test_check_str_len (thpool->name, strlen (THPOOL_NAME), NULL);

	thpool_destroy (thpool);

}

static void test_thpool_is_empty (void) {

	Thpool *thpool = test_thpool_create ();

	bool empty = thpool_is_empty (thpool);

	test_check_bool_eq (empty, true, NULL);

	thpool_destroy (thpool);

}

static void test_thpool_init (void) {

	Thpool *thpool = test_thpool_create ();

	bool empty = thpool_is_empty (thpool);
	test_check_bool_eq (empty, true, NULL);

	unsigned int result = thpool_init (thpool);
	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (thpool->n_threads, THPOOL_N_THREADS, NULL);
	test_check_ptr (thpool->threads);
	test_check_bool_eq (thpool->keep_alive, true, NULL);
	test_check_unsigned_eq (thpool->num_threads_alive, THPOOL_N_THREADS, NULL);
	test_check_unsigned_eq (thpool->num_threads_working, 0, NULL);
	test_check_ptr (thpool->mutex);
	test_check_ptr (thpool->threads_all_idle);\

	unsigned int alive = thpool_get_num_threads_alive (thpool);
	test_check_unsigned_eq (alive, THPOOL_N_THREADS, NULL);

	unsigned int working = thpool_get_num_threads_working (thpool);
	test_check_unsigned_eq (working, 0, NULL);

	empty = thpool_is_empty (thpool);
	test_check_bool_eq (empty, true, NULL);

	bool full = thpool_is_full (thpool);
	test_check_bool_eq (full, false, NULL);

	thpool_destroy (thpool);

}

static void test_thpool_add_work (void) {

	Thpool *thpool = test_thpool_create ();

	bool empty = thpool_is_empty (thpool);
	test_check_bool_eq (empty, true, NULL);

	unsigned int result = thpool_init (thpool);
	test_check_unsigned_eq (result, 0, NULL);
	test_check_unsigned_eq (thpool->n_threads, THPOOL_N_THREADS, NULL);
	test_check_ptr (thpool->threads);
	test_check_bool_eq (thpool->keep_alive, true, NULL);
	test_check_unsigned_eq (thpool->num_threads_alive, THPOOL_N_THREADS, NULL);
	test_check_unsigned_eq (thpool->num_threads_working, 0, NULL);
	test_check_ptr (thpool->mutex);
	test_check_ptr (thpool->threads_all_idle);

	unsigned int alive = thpool_get_num_threads_alive (thpool);
	test_check_unsigned_eq (alive, THPOOL_N_THREADS, NULL);

	unsigned int working = thpool_get_num_threads_working (thpool);
	test_check_unsigned_eq (working, 0, NULL);

	bool full = thpool_is_full (thpool);
	test_check_bool_eq (full, false, NULL);

	// add work
	int added = thpool_add_work (thpool, work_method, NULL);
	test_check_int_eq (added, 0, NULL);

	(void) sleep (1);

	alive = thpool_get_num_threads_alive (thpool);
	test_check_unsigned_eq (alive, THPOOL_N_THREADS, NULL);

	working = thpool_get_num_threads_working (thpool);
	test_check_unsigned_eq (working, 1, NULL);

	empty = thpool_is_empty (thpool);
	test_check_bool_eq (empty, false, NULL);

	full = thpool_is_full (thpool);
	test_check_bool_eq (empty, false, NULL);

	thpool_destroy (thpool);

}

void threads_tests_thpool (void) {

	(void) printf ("Testing THREADS thpool...\n");

	test_thpool_set_name ();
	test_thpool_is_empty ();
	test_thpool_init ();
	test_thpool_add_work ();

	(void) printf ("Done!\n");

}