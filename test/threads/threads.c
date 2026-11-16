#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerver/threads/thread.h>

#include "../test.h"
#include "threads.h"

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

static void test_thread_create_detached (void) {

	// good test
	int data = 128;
	pthread_t thread_id = thread_create_detached (
		test_thread, &data
	);

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

// returns 1 if SIGINT and SIGTERM are blocked in the thread
static void *test_thread_signals (void *result_ptr) {

	sigset_t current;
	(void) pthread_sigmask (SIG_BLOCK, NULL, &current);

	*(int *) result_ptr = sigismember (&current, SIGINT) && sigismember (&current, SIGTERM);

	return NULL;

}

static void test_threads_joinable (void) {

	pthread_t thread_id = 0;

	test_check_unsigned_eq (thread_create_joinable (NULL, test_thread, NULL), 1, NULL);
	test_check_unsigned_eq (thread_create_joinable (&thread_id, NULL, NULL), 1, NULL);

	// every thread created by cerver blocks the process signals
	int blocked = 0;
	test_check_unsigned_eq (thread_create_joinable (&thread_id, test_thread_signals, &blocked), 0, NULL);
	test_check_int_eq (pthread_join (thread_id, NULL), 0, NULL);
	test_check_int_eq (blocked, 1, NULL);

	// the creator keeps its signals
	sigset_t current;
	(void) pthread_sigmask (SIG_BLOCK, NULL, &current);
	test_check_false (sigismember (&current, SIGINT));
	test_check_false (sigismember (&current, SIGTERM));

}

static void threads_tests_main (void) {

	(void) printf ("Testing THREADS main...\n");

	test_threads_detachable ();

	test_thread_create_detached ();

	test_threads_joinable ();

	test_threads_mutex ();
	test_threads_cond ();

	(void) printf ("Done!\n");

}

int main (void) {

	test_output_init ();

	(void) printf ("Testing THREADS...\n");

	threads_tests_main ();

	threads_tests_bsem ();

	threads_tests_jobs ();

	threads_tests_thpool ();

	threads_tests_worker ();

	threads_tests_manager ();

	(void) printf ("\nDone with THREADS tests!\n\n");

	return 0;

}
