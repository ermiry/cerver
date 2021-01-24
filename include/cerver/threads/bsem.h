#ifndef _THREADS_BSEM_H_
#define _THREADS_BSEM_H_

#include <pthread.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Binary semaphore */
typedef struct bsem {

	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	int v;

} bsem;

CERVER_PUBLIC bsem *bsem_new (void);

CERVER_PUBLIC void bsem_delete (void *bsem_ptr);

// inits semaphore to 1 or 0
CERVER_PUBLIC void bsem_init (bsem *bsem_p, int value);

// resets semaphore to 0
CERVER_PUBLIC void bsem_reset (bsem *bsem_p);

// posts to at least one thread
CERVER_PUBLIC void bsem_post (bsem *bsem_p);

// posts to all threads
CERVER_PUBLIC void bsem_post_all (bsem *bsem_p);

// waits on semaphore until semaphore has value 0
CERVER_PUBLIC void bsem_wait (bsem *bsem_p);

#ifdef __cplusplus
}
#endif

#endif