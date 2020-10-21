#include <stdlib.h>
#include <string.h>

#include "cerver/threads/bsem.h"

#ifdef CERVER_DEBUG
#include "cerver/utils/log.h"
#endif

bsem *bsem_new (void) {

	bsem *bsem_p = (bsem *) malloc (sizeof (bsem));
	if (bsem_p) {
		bsem_p->mutex = NULL;
		bsem_p->cond = NULL;
		bsem_p->v = 0;
	}

	return bsem_p;

}

void bsem_delete (void *bsem_ptr) {

	if (bsem_ptr) {
		bsem *bsem_p = (bsem *) bsem_ptr;

		pthread_mutex_destroy (bsem_p->mutex);
		free (bsem_p->mutex);

		pthread_cond_destroy (bsem_p->cond);
		free (bsem_p->cond);

		free (bsem_ptr);
	}

}

// inits semaphore to 1 or 0
void bsem_init (bsem *bsem_p, int value) {

	if (bsem_p) {
		if (value == 0 || value == 1) {
			bsem_p->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			pthread_mutex_init (bsem_p->mutex, NULL);

			bsem_p->cond = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
			pthread_cond_init (bsem_p->cond, NULL);
			bsem_p->v = value;
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log_error ("bsem_init () - Binary semaphore can take only values 1 or 0");
			#endif
		}
	}

}

// resets semaphore to 0
void bsem_reset (bsem *bsem_p) {

	if (bsem_p) bsem_init (bsem_p, 0);

}

// posts to at least one thread
void bsem_post (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock (bsem_p->mutex);
		bsem_p->v = 1;
		pthread_cond_signal (bsem_p->cond);
		pthread_mutex_unlock (bsem_p->mutex);
	}

}

// posts to all threads
void bsem_post_all (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock (bsem_p->mutex);
		bsem_p->v = 1;
		pthread_cond_broadcast (bsem_p->cond);
		pthread_mutex_unlock (bsem_p->mutex);
	}

}

// waits on semaphore until semaphore has value 0
void bsem_wait (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock (bsem_p->mutex);
		while (bsem_p->v != 1) {
			pthread_cond_wait (bsem_p->cond, bsem_p->mutex);
		}

		bsem_p->v = 0;
		pthread_mutex_unlock (bsem_p->mutex);
	}

}