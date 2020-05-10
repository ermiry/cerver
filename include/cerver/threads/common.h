#ifndef _CERVER_THREADS_COMMON_H_
#define _CERVER_THREADS_COMMON_H_

#include <pthread.h>

/* Binary semaphore */
typedef struct bsem {

	pthread_mutex_t mutex;
	pthread_cond_t   cond;
	int v;
	
} bsem;

extern bsem *bsem_new (void);

extern void bsem_delete (void *bsem_ptr);

#endif