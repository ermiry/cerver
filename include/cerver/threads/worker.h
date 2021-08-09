#ifndef _CERVER_THREADS_WORKER_H_
#define _CERVER_THREADS_WORKER_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/config.h"

#include "cerver/threads/jobs.h"

#define WORKER_NAME_SIZE		64

#define WORKER_SLEEP_TIME		100000

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

struct _Worker {

	unsigned int id;

	unsigned int name_len;
	char name[WORKER_NAME_SIZE];

	WorkerState state;

	pthread_t thread_id;

	bool stop;
	bool end;

	JobQueue *job_queue;

	void (*work) (void *args);
	void (*delete_data) (void *args);

	pthread_mutex_t mutex;

};

typedef struct _Worker Worker;

CERVER_PRIVATE void worker_delete (void *worker_ptr);

CERVER_PUBLIC Worker *worker_create (void);

CERVER_PUBLIC Worker *worker_create_with_id (
	const unsigned int id
);

CERVER_PUBLIC void worker_set_name (
	Worker *worker, const char *name
);

CERVER_PRIVATE WorkerState worker_get_state (
	Worker *worker
);

CERVER_PRIVATE void worker_set_state (
	Worker *worker, const WorkerState state
);

CERVER_PRIVATE bool worker_get_stop (
	Worker *worker
);

CERVER_PRIVATE void worker_set_stop (
	Worker *worker, const bool stop
);

CERVER_PRIVATE bool worker_get_end (
	Worker *worker
);

CERVER_PRIVATE void worker_set_end (
	Worker *worker, const bool end
);

CERVER_PUBLIC void worker_set_work (
	Worker *worker, void (*work) (void *args)
);

CERVER_PUBLIC void worker_set_delete_data (
	Worker *worker, void (*delete_data) (void *args)
);

CERVER_PUBLIC unsigned int worker_start_with_state (
	Worker *worker, const WorkerState worker_state
);

CERVER_PUBLIC unsigned int worker_start (Worker *worker);

CERVER_PUBLIC unsigned int worker_resume (Worker *worker);

CERVER_PUBLIC unsigned int worker_stop (Worker *worker);

CERVER_PUBLIC unsigned int worker_end (Worker *worker);

CERVER_PUBLIC unsigned int worker_push_job (
	Worker *worker, void *args
);

CERVER_PUBLIC unsigned int worker_push_job_with_work (
	Worker *worker,
	void (*work) (void *args), void *args
);

#ifdef __cplusplus
}
#endif

#endif