#include <stdlib.h>
#include <stdio.h>

#include <errno.h>

#include <pthread.h>
#include <sys/prctl.h>

#include "cerver/types/types.h"
#include "cerver/utils/log.h"

// creates a custom detachable thread (will go away on its own upon completion)
// returns 0 on success, 1 on error
u8 thread_create_detachable (pthread_t *thread, void *(*work) (void *), void *args) {

    u8 retval = 1;

    if (thread) {
        pthread_attr_t attr;
        if (!pthread_attr_init (&attr)) {
            if (!pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED)) {
                if (!pthread_create (thread, &attr, work, args) != THREAD_OK) {
                    retval = 0;     // success
                }

                else {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to create detachable thread!");
                    perror ("Error");
                }
            }
        }
    }

    return retval;

}

// sets thread name from inisde it
int thread_set_name (const char *name) {

    // use prctl instead to prevent using _GNU_SOURCE flag and implicit declaration
    return prctl (PR_SET_NAME, name);

}