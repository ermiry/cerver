#ifndef _THREADS_BSEM_H_
#define _THREADS_BSEM_H_

#include <pthread.h>

/* Binary semaphore */
typedef struct bsem {

	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	int v;
	
} bsem;

extern bsem *bsem_new (void);

extern void bsem_delete (void *bsem_ptr);

// inits semaphore to 1 or 0
extern void bsem_init (bsem *bsem_p, int value);

// resets semaphore to 0
extern void bsem_reset (bsem *bsem_p);

// posts to at least one thread
extern void bsem_post (bsem *bsem_p);

// posts to all threads
extern void bsem_post_all (bsem *bsem_p);

// waits on semaphore until semaphore has value 0
extern void bsem_wait (bsem *bsem_p);

#endif