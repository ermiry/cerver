#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/prctl.h>

#include "cerver/types/types.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

#pragma region threads

// the new thread inherits the signal mask of its creator,
// so the signals are blocked only while the thread is created
static void thread_signals_block (sigset_t *previous) {

	sigset_t blocked;
	(void) sigemptyset (&blocked);
	(void) sigaddset (&blocked, SIGINT);
	(void) sigaddset (&blocked, SIGTERM);

	(void) pthread_sigmask (SIG_BLOCK, &blocked, previous);

}

static void thread_signals_restore (const sigset_t *previous) {

	(void) pthread_sigmask (SIG_SETMASK, previous, NULL);

}

// returns the pthread_create () result
static int thread_create_internal (
	pthread_t *thread, int detach_state,
	void *(*work) (void *), void *args
) {

	int retval = 1;

	pthread_attr_t attr;
	if (!pthread_attr_init (&attr)) {
		if (!pthread_attr_setdetachstate (&attr, detach_state)) {
			sigset_t previous;
			thread_signals_block (&previous);

			retval = pthread_create (thread, &attr, work, args);

			thread_signals_restore (&previous);
		}

		(void) pthread_attr_destroy (&attr);
	}

	return retval;

}

// creates a joinable thread that must be joined with pthread_join ()
// returns 0 on success, 1 on error
unsigned int thread_create_joinable (
	pthread_t *thread, void *(*work) (void *), void *args
) {

	unsigned int retval = 1;

	if (thread && work) {
		int result = thread_create_internal (
			thread, PTHREAD_CREATE_JOINABLE, work, args
		);

		if (result == THREAD_OK) {
			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to create joinable thread - %s", strerror (result)
			);
		}
	}

	return retval;

}

// creates a custom detachable thread (will go away on its own upon completion)
// returns 0 on success, 1 on error
unsigned int thread_create_detachable (
	pthread_t *thread, void *(*work) (void *), void *args
) {

	unsigned int retval = 1;

	if (thread && work) {
		int result = thread_create_internal (
			thread, PTHREAD_CREATE_DETACHED, work, args
		);

		if (result == THREAD_OK) {
			retval = 0;     // success
		}

		else {
			cerver_log_error (
				"Failed to create detachable thread - %s", strerror (result)
			);
		}
	}

	return retval;

}

pthread_t thread_create_detached (void *(*work) (void *), void *args) {

	pthread_t thread_id = 0;

	if (work) {
		int result = thread_create_internal (
			&thread_id, PTHREAD_CREATE_DETACHED, work, args
		);

		if (result != THREAD_OK) {
			thread_id = 0;

			cerver_log_error (
				"Failed to create detached thread - %s", strerror (result)
			);
		}
	}

	return thread_id;

}

// sets thread name from inisde it
unsigned int thread_set_name (const char *name, ...) {

	unsigned int retval = 1;

	if (name) {
		va_list args;
		va_start (args, name);

		char thread_name[THREAD_NAME_BUFFER_SIZE] = { 0 };
		(void) vsnprintf (
			thread_name, THREAD_NAME_BUFFER_SIZE - 1, name, args
		);

		if (!prctl (PR_SET_NAME, thread_name)) {
			retval = 0;
		}

		va_end (args);
	}

	return retval;

}

#pragma endregion

#pragma region group

// the work of a thread of a group, its data is released by the thread
typedef struct ThreadGroupWork {

	ThreadGroup *group;

	void *(*work) (void *);
	void *args;

} ThreadGroupWork;

void thread_group_init (ThreadGroup *group) {

	if (group) {
		group->alive = 0;

		(void) pthread_mutex_init (&group->mutex, NULL);
		(void) pthread_cond_init (&group->ended, NULL);
	}

}

void thread_group_end (ThreadGroup *group) {

	if (group) {
		(void) pthread_mutex_destroy (&group->mutex);
		(void) pthread_cond_destroy (&group->ended);
	}

}

static void thread_group_count (ThreadGroup *group, const bool started) {

	(void) pthread_mutex_lock (&group->mutex);

	if (started) group->alive += 1;
	else group->alive -= 1;

	(void) pthread_cond_broadcast (&group->ended);
	(void) pthread_mutex_unlock (&group->mutex);

}

static void *thread_group_run (void *group_work_ptr) {

	ThreadGroupWork *group_work = (ThreadGroupWork *) group_work_ptr;
	ThreadGroup *group = group_work->group;

	(void) group_work->work (group_work->args);

	free (group_work);

	// the last access to the group, its owner can be released after it
	thread_group_count (group, false);

	return NULL;

}

// creates a detached thread that is counted in the group until its work returns
// returns 0 on success, 1 on error
unsigned int thread_group_create (
	ThreadGroup *group, void *(*work) (void *), void *args
) {

	unsigned int retval = 1;

	if (group && work) {
		ThreadGroupWork *group_work = (ThreadGroupWork *) malloc (sizeof (ThreadGroupWork));
		if (group_work) {
			group_work->group = group;
			group_work->work = work;
			group_work->args = args;

			// counted before the thread exists, so a wait never misses it
			thread_group_count (group, true);

			pthread_t thread_id = 0;
			retval = thread_create_detachable (&thread_id, thread_group_run, group_work);
			if (retval) {
				free (group_work);

				thread_group_count (group, false);
			}
		}
	}

	return retval;

}

// returns the number of threads of the group that are alive
unsigned int thread_group_alive (ThreadGroup *group) {

	unsigned int alive = 0;

	if (group) {
		(void) pthread_mutex_lock (&group->mutex);
		alive = group->alive;
		(void) pthread_mutex_unlock (&group->mutex);
	}

	return alive;

}

// waits until every thread of the group has ended,
// or until the timeout in seconds passes (THREAD_GROUP_WAIT_FOREVER waits for them)
// returns the number of threads that are still alive
unsigned int thread_group_wait (ThreadGroup *group, const time_t timeout) {

	unsigned int alive = 0;

	if (group) {
		struct timespec deadline = { 0, 0 };
		(void) clock_gettime (CLOCK_REALTIME, &deadline);
		deadline.tv_sec += timeout;

		(void) pthread_mutex_lock (&group->mutex);

		int result = 0;
		while (group->alive && (result != ETIMEDOUT)) {
			if (timeout == THREAD_GROUP_WAIT_FOREVER) {
				result = pthread_cond_wait (&group->ended, &group->mutex);
			}

			else {
				result = pthread_cond_timedwait (&group->ended, &group->mutex, &deadline);
			}
		}

		alive = group->alive;

		(void) pthread_mutex_unlock (&group->mutex);
	}

	return alive;

}

#pragma endregion

#pragma region mutex

// allocates & initializes a new mutex that should be deleted after use
pthread_mutex_t *thread_mutex_new (void) {

	pthread_mutex_t *mutex = (pthread_mutex_t *) malloc (
		sizeof (pthread_mutex_t)
	);

	if (mutex) {
		(void) pthread_mutex_init (mutex, NULL);
	}

	return mutex;

}

// locks an existing mutex
void thread_mutex_lock (pthread_mutex_t *mutex) {

	if (mutex) {
		(void) pthread_mutex_lock (mutex);
	}

}

// unlocks a locked mutex
void thread_mutex_unlock (pthread_mutex_t *mutex) {

	if (mutex) {
		(void) pthread_mutex_unlock (mutex);
	}

}

// destroys & frees an allocated mutex
void thread_mutex_delete (pthread_mutex_t *mutex) {

	if (mutex) {
		(void) pthread_mutex_destroy (mutex);
		free (mutex);
	}

}

#pragma endregion

#pragma region cond

// allocates & initializes a new cond that should be deleted after use
pthread_cond_t *thread_cond_new (void) {

	pthread_cond_t *cond = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
	if (cond) {
		(void) pthread_cond_init (cond, NULL);
	}

	return cond;

}

// destroys & frees an allocated cond
void thread_cond_delete (pthread_cond_t *cond) {

	if (cond) {
		(void) pthread_cond_destroy (cond);
		free (cond);
	}

}

#pragma endregion
