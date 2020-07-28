#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <time.h>
#include <unistd.h>
// #include <errno.h>

// #define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "cerver/threads/thpool.h"
#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"

static void *thread_do (void *thread_ptr);

#pragma region thread

struct _PoolThread {

	int id;
	pthread_t thread_id;
	Thpool *thpool;

};

typedef struct _PoolThread PoolThread;

static PoolThread *pool_thread_new (void) {

	PoolThread *thread = (PoolThread *) malloc (sizeof (PoolThread));
	if (thread) {
		thread->id = -1;
		thread->thread_id = 0;
		thread->thpool = NULL;
	}

	return thread;

}

static void pool_thread_delete (void *thread_ptr) {

	if (thread_ptr) free (thread_ptr);

}

static PoolThread *pool_thread_create (int id, Thpool *thpool) {

	PoolThread *thread = pool_thread_new ();
	if (thread) {
		thread->id = id;
		thread->thpool = thpool;
	}

	return thread;

}

static unsigned int pool_thread_init (PoolThread *thread) {

	unsigned int retval = 1;

	if (thread) {
		if (!pthread_create (&thread->thread_id, NULL, thread_do, thread)) {
			if (!pthread_detach (thread->thread_id)) {
				retval = 0;
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region thpool

static Thpool *thpool_new (void) {

	Thpool *thpool = (Thpool *) malloc (sizeof (Thpool));
	if (thpool) {
		thpool->name = NULL;

		thpool->n_threads = 0;
		thpool->threads = NULL;

		thpool->keep_alive = false;
		thpool->num_threads_alive = 0;
		thpool->num_threads_working = 0;

		thpool->mutex = NULL;
		thpool->threads_all_idle = NULL;

		thpool->job_queue = NULL;
	}

	return thpool;

}

static void thpool_delete (void *thpool_ptr) {

	if (thpool_ptr) {
		Thpool *thpool = (Thpool *) thpool_ptr;

		if (thpool->name) free ((char *) thpool->name);

		if (thpool->threads) {
			for (unsigned int i = 0; i < thpool->n_threads; i++) {
				pool_thread_delete (thpool->threads[i]);
			}

			free (thpool->threads);
		}

		pthread_mutex_destroy (thpool->mutex);
		free (thpool->mutex);

		pthread_cond_destroy (thpool->threads_all_idle);
		free (thpool->threads_all_idle);

		job_queue_delete (thpool->job_queue);

		free (thpool_ptr);
	}

}

#pragma endregion

#pragma region internal

static void *thread_do (void *thread_ptr) {

	if (thread_ptr) {
		PoolThread *thread = (PoolThread *) thread_ptr;
		Thpool *thpool = thread->thpool;

		// set name
		if (thpool->name) {
			char thread_name[64] = { 0 };
			snprintf (thread_name, 64, "thpool-%s-%d", thpool->name, thread->id);
			printf ("%s\n", thread_name);
			prctl (PR_SET_NAME, thread_name);
		}

		// mark thread as alive
		pthread_mutex_lock (thpool->mutex);
		thpool->num_threads_alive += 1;
		pthread_mutex_unlock (thpool->mutex);

		while (thpool->keep_alive) {
			bsem_wait (thpool->job_queue->has_jobs);
			if (thpool->keep_alive) {
				pthread_mutex_lock (thpool->mutex);
				thpool->num_threads_working += 1;
				pthread_mutex_unlock (thpool->mutex);

				// get job to execute
				Job *job = job_queue_pull (thpool->job_queue);
				if (job) {
					if (job->method)
						job->method (job->args);
					
					job_delete (job);
				}

				pthread_mutex_lock (thpool->mutex);

				thpool->num_threads_working -= 1;

				if (!thpool->num_threads_working)
					pthread_cond_signal (thpool->threads_all_idle);

				pthread_mutex_unlock (thpool->mutex);
			}
		}

		pthread_mutex_lock (thpool->mutex);
		thpool->num_threads_alive -= 1;
		pthread_mutex_unlock (thpool->mutex);
	}

	return NULL;

}

#pragma endregion

#pragma region public

// creates a new thpool with n threads
Thpool *thpool_create (unsigned int n_threads) {

	Thpool *thpool = thpool_new ();
	if (thpool) {
		thpool->n_threads = n_threads;
		thpool->threads = (PoolThread **) calloc (thpool->n_threads, sizeof (PoolThread));
		if (thpool->threads) {
			thpool->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			pthread_mutex_init (thpool->mutex, NULL);

			thpool->threads_all_idle = (pthread_cond_t *) malloc (sizeof (pthread_cond_t));
			pthread_cond_init (thpool->threads_all_idle, NULL);

			thpool->job_queue = job_queue_create ();
		}

		else {
			// error!
			thpool_delete (thpool);
			thpool = NULL;
		}
	}

	return thpool;

}

// initializes the thpool
// must be called after thpool_create ()
// returns 0 on success, 1 on error
unsigned int thpool_init (Thpool *thpool) {

	unsigned int retval = 1;

	if (thpool) {
		// initialize threads
		thpool->keep_alive = true;
		for (unsigned int i = 0; i < thpool->n_threads; i++) {
			thpool->threads[i] = pool_thread_create (i, thpool);
			pool_thread_init (thpool->threads[i]);
		}

		// wait for threads to initialize
		while (thpool->num_threads_alive != thpool->n_threads) {}

		retval = 0;
	}

	return retval;

}

// sets the name for the thpool
void thpool_set_name (Thpool *thpool, const char *name) {

	if (thpool) {
		size_t len = strlen (name);
		thpool->name = (char *) calloc (len + 1, sizeof (char));

		char *to = (char *) thpool->name;
		char *from = (char *) name;

		while (*from) *to++ = *from++;
    	*to = '\0';
	}

}

// gets the current number of threads that are alive (running) in the thpool
unsigned int thpool_get_num_threads_alive (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		pthread_mutex_lock (thpool->mutex);
		retval = thpool->num_threads_alive;
		pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// gets the current number of threads that are busy working in a job
unsigned int thpool_get_num_threads_working (Thpool *thpool) {

	unsigned int retval = 0;

	if (thpool) {
		pthread_mutex_lock (thpool->mutex);
		retval = thpool->num_threads_working;
		pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// returns true if the thpool does NOT have any working thread
bool thpool_is_empty (Thpool *thpool) {

	bool retval = false;

	if (thpool) {
		pthread_mutex_lock (thpool->mutex);

		retval = (thpool->num_threads_working == 0);
		
		pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// returns true if the thpool has ALL its threads working
bool thpool_is_full (Thpool *thpool) {

	bool retval = false;

	if (thpool) {
		pthread_mutex_lock (thpool->mutex);

		retval = (thpool->num_threads_working == thpool->num_threads_alive);
		
		pthread_mutex_unlock (thpool->mutex);
	}

	return retval;

}

// adds a work to the thpool's job queue
// it will be executed once it is the next in line and a thread is free
int thpool_add_work (Thpool *thpool, void (*work) (void *), void *args) {

	int retval = 1;

	if (thpool && work) {
		Job *job = job_create (work, args);
		retval = job_queue_push (thpool->job_queue, job);
	}

	return retval;

}

// wait until all jobs have finished
void thpool_wait (Thpool *thpool) {

	if (thpool) {
		pthread_mutex_lock (thpool->mutex);

		while (thpool->job_queue->queue->size || thpool->num_threads_working) {
			pthread_cond_wait (thpool->threads_all_idle, thpool->mutex);
		}

		pthread_mutex_unlock (thpool->mutex);
	}
	
}

// destroys the thpool and deletes all of its data
void thpool_destroy (Thpool *thpool) {

	if (thpool) {
		// end each thread's infinite loop
		thpool->keep_alive = false;

		// give one second to kill idle threads
		double timeout = 1.0;
		time_t start, end;
		double tpassed = 0.0;
		time (&start);
		while ((tpassed < timeout) && thpool->num_threads_alive){
			bsem_post_all (thpool->job_queue->has_jobs);
			time (&end);
			tpassed = difftime (end,start);
		}

		// poll remaining threads
		while (thpool->num_threads_alive){
			bsem_post_all (thpool->job_queue->has_jobs);
			sleep (1);
		}

		thpool_delete (thpool);
	}

}

#pragma endregion