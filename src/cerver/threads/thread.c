#include <stdlib.h>
#include <stdio.h>

#include <pthread.h>
#include <sys/prctl.h>

#include "cerver/types/types.h"
#include "cerver/utils/log.h"

// creates a custom detachable thread (will go away on its own upon completion)
// handle manually in linux, with no name
// in any other platform, created with sdl api and you can pass a custom name
u8 thread_create_detachable (void *(*work) (void *), void *args, const char *name) {

    u8 retval = 1;

    pthread_attr_t attr;
    pthread_t thread;

    int rc = pthread_attr_init (&attr);
    rc = pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create (&thread, &attr, work, args) != THREAD_OK)
        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to create detachable thread!");
    else retval = 0;

    return retval;

}

// sets thread name from inisde it
int thread_set_name (const char *name) {

    // use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration
    return prctl (PR_SET_NAME, name);

}