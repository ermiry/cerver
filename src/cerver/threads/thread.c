#include <stdlib.h>
#include <stdio.h>

#include <stdarg.h>

#include <errno.h>
#include <pthread.h>

#include <sys/prctl.h>

#include "cerver/types/types.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

#pragma region threads

// creates a custom detachable thread (will go away on its own upon completion)
// returns 0 on success, 1 on error
u8 thread_create_detachable (
	pthread_t *thread,
	void *(*work) (void *), void *args
) {

	u8 retval = 1;

	if (thread && work) {
		pthread_attr_t attr = { 0 };
		if (!pthread_attr_init (&attr)) {
			if (!pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED)) {
				if (pthread_create (thread, &attr, work, args) == THREAD_OK) {
					retval = 0;     // success
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_NONE,
						"Failed to create detachable thread!"
					);

					perror ("Error");
				}
			}
		}
	}

	return retval;

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
