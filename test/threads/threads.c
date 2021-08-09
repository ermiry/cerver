#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <cerver/threads/thread.h>

#include "threads.h"

#include "../test.h"

static void *test_thread (void *data_ptr) {

	test_check_ptr (data_ptr);

	return NULL;

}

static void test_threads_detachable (void) {

	u8 result = 0;
	pthread_t thread_id = 0;

	// test with NULL thread id
	result = thread_create_detachable (
		NULL, test_thread, NULL
	);

	test_check_unsigned_eq (result, 1, NULL);
	test_check_unsigned_eq (thread_id, 0, NULL);

	// test with NULL work
	thread_id = 0;
	result = thread_create_detachable (
		&thread_id, NULL, NULL
	);

	test_check_unsigned_eq (result, 1, NULL);
	test_check_unsigned_eq (thread_id, 0, NULL);

	// good test
	int data = 128;
	thread_id = 0;
	result = thread_create_detachable (
		&thread_id, test_thread, &data
	);

	test_check_int_eq (result, 0, NULL);
	test_check_unsigned_ne (thread_id, 0);

}

static void test_threads_mutex (void) {

	pthread_mutex_t *mutex = thread_mutex_new ();
	test_check_ptr (mutex);

	int result = pthread_mutex_lock (mutex);
	test_check_int_eq (result, 0, NULL);

	result = pthread_mutex_unlock (mutex);
	test_check_int_eq (result, 0, NULL);

	thread_mutex_lock (mutex);

	thread_mutex_unlock (mutex);

	thread_mutex_delete (mutex);

}

static void test_threads_cond (void) {

	pthread_cond_t *cond = thread_cond_new ();
	test_check_ptr (cond);

	int result = pthread_cond_signal (cond);
	test_check_int_eq (result, 0, NULL);

	thread_cond_delete (cond);

}

static void threads_tests_main (void) {

	(void) printf ("Testing THREADS main...\n");

	test_threads_detachable ();
	test_threads_mutex ();
	test_threads_cond ();

	(void) printf ("Done!\n");

}

int main (int argc, char **argv) {

	(void) printf ("Testing THREADS...\n");

	threads_tests_main ();

	threads_tests_bsem ();

	threads_tests_jobs ();

	threads_tests_thpool ();

	threads_tests_worker ();

	(void) printf ("\nDone with THREADS tests!\n\n");

	return 0;

}