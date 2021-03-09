#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <cerver/threads/jobs.h>

#include "../test.h"

static void work_method (void *args) {

	(void) printf ("Some work that needs to be done!");

}

static Job *test_job_new (void) {

	Job *job = job_new ();

	test_check_ptr (job);
	test_check_null_ptr (job->work);
	test_check_null_ptr (job->args);

	return job;

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

	JobHandler *handler = job_handler_new ();
	
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

	JobHandler *handler = job_handler_create ();

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

void threads_tests_jobs (void) {

	(void) printf ("Testing THREADS jobs...\n");

	// jobs
	test_job_create ();
	test_job_reset ();

	// handlers
	test_job_handler_create ();
	test_job_handler_reset ();
	test_job_handler_done ();

	(void) printf ("Done!\n");

}