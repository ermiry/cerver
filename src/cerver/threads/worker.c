#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "cerver/threads/jobs.h"
#include "cerver/threads/worker.h"

const char *worker_state_to_string (
	const WorkerState state
) {

	switch (state) {
		#define XX(num, name, string) case WORKER_STATE_##name: return #string;
		WORKER_STATE_MAP(XX)
		#undef XX
	}

	return worker_state_to_string (WORKER_STATE_NONE);

}

static Worker *worker_new (void) {

	Worker *worker = (Worker *) malloc (sizeof (Worker));
	if (worker) {
		worker->id = 0;

		worker->state = WORKER_STATE_NONE;

		worker->thread_id = 0;

		worker->stop = false;
		worker->end = false;

		worker->job_queue = NULL;

		(void) memset (&worker->mutex, 0, sizeof (pthread_mutex_t));
	}

	return worker;

}

void worker_delete (void *worker_ptr) {

	if (worker_ptr) {
		Worker *worker = (Worker *) worker_ptr;

		pthread_mutex_destroy (&worker->mutex);

		free (worker_ptr);
	}

}

Worker *worker_create (const unsigned int id) {

	Worker *worker = worker_new ();
	if (worker) {
		worker->id = id;

		worker->job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);

		(void) pthread_mutex_init (&worker->mutex, NULL);
	}

	return worker;

}
