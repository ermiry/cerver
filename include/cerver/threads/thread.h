#ifndef _CERVER_THREADS_H_
#define _CERVER_THREADS_H_

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

// creates a custom detachable thread (will go away on its own upon completion)
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 thread_create_detachable (pthread_t *thread, void *(*work) (void *), void *args);

// sets thread name from inisde it
CERVER_PUBLIC int thread_set_name (const char *name);

#endif