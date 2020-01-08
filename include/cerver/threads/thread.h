#ifndef _CERVER_THREADS_H_
#define _CERVER_THREADS_H_

#include "cerver/types/types.h"

// creates a custom detachable thread (will go away on its own upon completion)
extern u8 thread_create_detachable (void *(*work) (void *), void *args, const char *name);

// sets thread name from inisde it
extern int thread_set_name (const char *name);

#endif