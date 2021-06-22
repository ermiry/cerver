#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <cerver/threads/jobs.h>

#include "../test.h"

static void work_method (void *args) {

	(void) printf ("Some work that needs to be done!");

}

static void handler_method (void *job_handler) {

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
	test_check_null_ptr (job_queue->queue);
	test_check_null_ptr (job_queue->rwmutex);
	test_check_null_ptr (job_queue->has_jobs);
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
	test_check_ptr (job_queue->queue);
	test_check_ptr (job_queue->rwmutex);
	test_check_ptr (job_queue->has_jobs);
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
	test_check_ptr (job_queue->queue);
	test_check_ptr (job_queue->rwmutex);
	test_check_ptr (job_queue->has_jobs);
	test_check_bool_eq (job_queue->running, false, NULL);
	test_check_unsigned_eq (job_queue->handler_thread_id, 0, NULL);
	test_check_null_ptr (job_queue->handler);

	job_queue_delete (job_queue);

}

static void test_job_queue_set_handler (void) {

	JobQueue *job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);

	test_check_ptr (job_queue);
	test_check_int_eq (job_queue->type, JOB_QUEUE_TYPE_HANDLERS, NULL);
	test_check_ptr (job_queue->pool);
	test_check_ptr (job_queue->queue);
	test_check_ptr (job_queue->rwmutex);
	test_check_ptr (job_queue->has_jobs);
	test_check_bool_eq (job_queue->running, false, NULL);
	test_check_unsigned_eq (job_queue->handler_thread_id, 0, NULL);
	test_check_null_ptr (job_queue->handler);

	job_queue_set_handler (job_queue, handler_method);
	test_check_ptr (job_queue->handler);

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

	(void) printf ("Done!\n");

}