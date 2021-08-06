#ifndef _CERVER_THREADS_WORKER_H_
#define _CERVER_THREADS_WORKER_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/config.h"

#include "cerver/threads/jobs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WORKER_STATE_MAP(XX)		\
	XX(0,	NONE, 		None)		\
	XX(1,	AVAILABLE, 	Available)	\
	XX(2,	WORKING, 	Working)	\
	XX(3,	STOPPED, 	Stopped)	\
	XX(4,	ENDED, 		Ended)

typedef enum WorkerState {

	#define XX(num, name, string) WORKER_STATE_##name = num,
	WORKER_STATE_MAP (XX)
	#undef XX

} WorkerState;

CERVER_EXPORT const char *worker_state_to_string (
	const WorkerState state
);

typedef struct Worker {

	unsigned int id;

	WorkerState state;

	pthread_t thread_id;

	bool stop;
	bool end;

	JobQueue *job_queue;

	pthread_mutex_t mutex;

} Worker;

CERVER_PRIVATE void worker_delete (void *worker_ptr);

CERVER_PUBLIC Worker *worker_create (const unsigned int id);

CERVER_PRIVATE WorkerState recon_worker_get_state (
	Worker *worker
);

CERVER_PRIVATE void recon_worker_set_state (
	Worker *worker, const WorkerState state
);

CERVER_PRIVATE bool recon_worker_get_stop (
	Worker *worker
);

CERVER_PRIVATE void recon_worker_set_stop (
	Worker *worker, const bool stop
);

CERVER_PRIVATE bool recon_worker_get_end (
	Worker *worker
);

CERVER_PRIVATE void recon_worker_set_end (
	Worker *worker, const bool end
);

#ifdef __cplusplus
}
#endif

#endif