#ifndef _CERVER_THREADS_H_
#define _CERVER_THREADS_H_

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#define THREAD_OK           0

#pragma region threads

// creates a custom detachable thread (will go away on its own upon completion)
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 thread_create_detachable (pthread_t *thread, void *(*work) (void *), void *args);

// sets thread name from inisde it
CERVER_PUBLIC int thread_set_name (const char *name);

#pragma endregion

#pragma region mutex

// allocates & initializes a new mutex that should be deleted after use
CERVER_PUBLIC pthread_mutex_t *pthread_mutex_new (void);

// destroys & frees an allocated mutex
CERVER_PUBLIC void pthread_mutex_delete (pthread_mutex_t *mutex);

#pragma endregion

#pragma region cond

// allocates & initializes a new cond that should be deleted after use
CERVER_PUBLIC pthread_cond_t *pthread_cond_new (void);

// destroys & frees an allocated cond
CERVER_PUBLIC void pthread_cond_delete (pthread_cond_t *cond);

#pragma endregion

#endif