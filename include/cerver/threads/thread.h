#ifndef _CERVER_THREADS_H_
#define _CERVER_THREADS_H_

#include "cerver/types/types.h"

// creates a custom detachable thread (will go away on its own upon completion)
// handle manually in linux, with no name
// in any other platform, created with sdl api and you can pass a custom name
u8 thread_create_detachable (void *(*work) (void *), void *args, const char *name);

// sets thread name from inisde it
int thread_set_name (const char *name);

#endif