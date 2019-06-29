/* ********************************
 * Original Author:       Johan Hanssen Seferidis
 * License:	     MIT
 ********************************/

#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/threads/thpool.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static volatile int threads_keep_alive;
static volatile int threads_on_hold;

static struct job *jobqueue_pull (jobqueue *jobqueue_p);
static void *thread_do (thread *thread_p);

/* ======================== SYNCHRONISATION ========================= */


// inits semaphore to 1 or 0 
static void bsem_init (bsem *bsem_p, int value) {

	if (bsem_p) {
		if (value == 0 || value == 1) {
			pthread_mutex_init (&(bsem_p->mutex), NULL);
			pthread_cond_init (&(bsem_p->cond), NULL);
			bsem_p->v = value;
		}
	}
	
}

// resets the semaphore to 0
static void bsem_reset (bsem *bsem_p) { bsem_init (bsem_p, 0); }

// post to at least one thread
static void bsem_post (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock(&bsem_p->mutex);
		bsem_p->v = 1;
		pthread_cond_signal(&bsem_p->cond);
		pthread_mutex_unlock(&bsem_p->mutex);
	}
}

// post to all threads
static void bsem_post_all (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock(&bsem_p->mutex);
		bsem_p->v = 1;
		pthread_cond_broadcast(&bsem_p->cond);
		pthread_mutex_unlock(&bsem_p->mutex);
	}
	
}

// wait on semaphore until semaphore has value 0
static void bsem_wait (bsem *bsem_p) {

	if (bsem_p) {
		pthread_mutex_lock (&bsem_p->mutex);

		while (bsem_p->v != 1) {
			pthread_cond_wait (&bsem_p->cond, &bsem_p->mutex);
		} 

		bsem_p->v = 0;
		pthread_mutex_unlock (&bsem_p->mutex);
	}

}

/* ============================ THREAD ============================== */

static thread *thread_new (void) {

	thread *t = (thread *) malloc (sizeof (thread));
	if (t) {
		memset (t, 0, sizeof (thread));
		t->thpool_p = NULL;
	}

	return t;

}

static inline void thread_delete (thread *t) { if (t) free (t); }

static int thread_init (threadpool *thpool, thread **thread_p, unsigned int id) {

	int retval = 1;

	if (thpool) {
		*thread_p = thread_new ();

		(*thread_p)->thpool_p = thpool;
		(*thread_p)->id       = id;

		if (!pthread_create (&(*thread_p)->pthread, NULL, (void *) thread_do, (*thread_p))) {
			if (!pthread_detach ((*thread_p)->pthread)) {
				// #ifdef THPOOL_DEBUG
				// cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
				// 	c_string_create ("Created thread %i in thpool %s",
				// 	id, thpool->name->str));
				// #endif
				thpool->num_threads_alive += 1;
				retval = 0;
			}

			else {
				#ifdef THPOOL_DEBUG
				cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to detach thread in thpool!");
				#endif
			}
		}

		else {
			#ifdef THPOOL_DEBUG
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to create thread in thpool!");
			#endif
		}
	}

	return retval;

}

// sets the calling thread on hold
static void thread_hold (int sig_id) {

	(void) sig_id;
	threads_on_hold = 1;
	while (threads_on_hold) {
		sleep (1);
	} 

}

static void *thread_do (thread *thread_p) {

	if (thread_p) {
		char *thread_name = c_string_create ("%s-%d", 
			thread_p->thpool_p->name->str, thread_p->id);

		#if defined (__linux__)
			prctl (PR_SET_NAME, thread_name);
		#elif defined (__APPLE__) && defined (__MACH__)
			pthread_setname_np (thread_name);
		#else
			cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
				"thread_do (): pthread_setname_np is not supported on this system.");
		#endif

		// #ifdef THPOOL_DEBUG
		// fprintf (stdout, "Hello from thread %s\n", thread_name);
		// #endif

		// register signal handler
		struct sigaction act;
		sigemptyset (&act.sa_mask);
		act.sa_flags = 0;
		act.sa_handler = thread_hold;
		if (sigaction (SIGUSR1, &act, NULL) == -1) {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
				"thread_do(): cannot handle SIGUSR1");
		}

		// mark the thread as initialized
		pthread_mutex_lock (&thread_p->thpool_p->thcount_lock);
		// thread_p->thpool_p->num_threads_alive += 1;
		pthread_mutex_unlock (&thread_p->thpool_p->thcount_lock);

		while (threads_keep_alive) {
			bsem_wait (thread_p->thpool_p->job_queue->has_jobs);

			if (threads_keep_alive){
				pthread_mutex_lock (&thread_p->thpool_p->thcount_lock);
				thread_p->thpool_p->num_threads_working++;
				pthread_mutex_unlock (&thread_p->thpool_p->thcount_lock);

				// get a job from the queue and execute it
				void (*func_buff)(void*);
				void *arg_buff;
				job *job_p = jobqueue_pull (thread_p->thpool_p->job_queue);
				if (job_p) {
					func_buff = job_p->function;
					arg_buff  = job_p->arg;
					func_buff (arg_buff);
					free (job_p);
				}

				pthread_mutex_lock (&thread_p->thpool_p->thcount_lock);

				thread_p->thpool_p->num_threads_working--;

				if (!thread_p->thpool_p->num_threads_working) 
					pthread_cond_signal (&thread_p->thpool_p->threads_all_idle);

				pthread_mutex_unlock (&thread_p->thpool_p->thcount_lock);
			}
		}

		pthread_mutex_lock (&thread_p->thpool_p->thcount_lock);
		thread_p->thpool_p->num_threads_alive --;
		pthread_mutex_unlock (&thread_p->thpool_p->thcount_lock);
	}

	else {
		cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
			"thread_do (): passed a NULL thread ptr");
	}

	return NULL;

}

/* ============================ JOB QUEUE =========================== */

static job *job_new (void) {

	job *j = (job *) malloc (sizeof (job));
	if (j) {
		j->prev = NULL;
		j->arg = NULL;
		j->function = NULL;
	}

	return j;

}

static jobqueue *jobqueue_new (void) {

	jobqueue *job_queue = (jobqueue *) malloc (sizeof (jobqueue));
	if (job_queue) {
		memset (job_queue, 0, sizeof (jobqueue));
		job_queue->front = NULL;
		job_queue->rear = NULL;
		job_queue->has_jobs = (struct bsem *) malloc (sizeof (struct bsem));
		if (!job_queue->has_jobs) {
			free (job_queue);
			job_queue = NULL;
		}
	}

	return job_queue;

}

// creates and initializes a new jobqueue
static jobqueue *jobqueue_init () {

	jobqueue *job_queue = jobqueue_new ();
	if (job_queue) {
		job_queue->len = 0;

		pthread_mutex_init (&(job_queue->rwmutex), NULL);
		bsem_init (job_queue->has_jobs, 0);
	}

}

static void jobqueue_clear (jobqueue *jobqueue_p) {

	if (jobqueue_p) {
		while (jobqueue_p->len) free (jobqueue_pull (jobqueue_p));

		jobqueue_p->front = NULL;
		jobqueue_p->rear  = NULL;
		bsem_reset (jobqueue_p->has_jobs);
		jobqueue_p->len = 0;
	}

}

// add a job to the queue
static void jobqueue_push (jobqueue *jobqueue_p, job *newjob){

	if (jobqueue_p) {
		pthread_mutex_lock (&jobqueue_p->rwmutex);
		newjob->prev = NULL;

		switch (jobqueue_p->len) {
			/* if no jobs in queue */
			case 0: {
				jobqueue_p->front = newjob;
				jobqueue_p->rear  = newjob;
			} break;

			/* if jobs in queue */
			default: {
				jobqueue_p->rear->prev = newjob;
				jobqueue_p->rear = newjob;
			} break;
		}

		jobqueue_p->len++;

		bsem_post (jobqueue_p->has_jobs);
		pthread_mutex_unlock (&jobqueue_p->rwmutex);
	}

}

// gets a job from the queue
static struct job *jobqueue_pull (jobqueue *jobqueue_p) {

	if (jobqueue_p) {
		pthread_mutex_lock (&jobqueue_p->rwmutex);

		job *job_p = jobqueue_p->front;
		switch (jobqueue_p->len){
			/* if no jobs in queue */
			case 0: break;

			/* if one job in queue */
			case 1: {
				jobqueue_p->front = NULL;
				jobqueue_p->rear  = NULL;
				jobqueue_p->len = 0;
			} break;
						
			/* if >1 jobs in queue */
			default: {
				jobqueue_p->front = job_p->prev;
				jobqueue_p->len--;
				/* more than one job in queue -> post it */
				bsem_post (jobqueue_p->has_jobs);
			} break;
		}

		pthread_mutex_unlock (&jobqueue_p->rwmutex);
		return job_p;
	}

	return NULL;
	
}

// free all queue resources back to the system 
static void jobqueue_delete (jobqueue *job_queue) {

	if (job_queue) {
		jobqueue_clear (job_queue);
		free (job_queue);
	}

}

/* ============================ THPOOL ============================== */

static threadpool *thpool_new (const char *name, unsigned int num_threads) {

	threadpool *thpool = (threadpool *) malloc (sizeof (threadpool));
	if (thpool) {
		memset (thpool, 0, sizeof (threadpool));
		thpool->name = str_new (name);
		thpool->threads = (thread **) calloc (num_threads, sizeof (thread));
		thpool->job_queue = NULL;
	}

	return thpool;

}

static void thpool_delete (threadpool *thpool) {

	if (thpool) {
		str_delete (thpool->name);
		free (thpool);
	}

}

// creates a new threadpool
threadpool *thpool_create (const char *name, unsigned int num_threads) {

	threadpool *thpool = thpool_new (name, num_threads);
	if (thpool) {
		threads_on_hold = 0;
		threads_keep_alive = 1;

		thpool->num_threads_alive = 0;
		thpool->num_threads_working = 0;

		thpool->job_queue = jobqueue_init ();
		if (!thpool->job_queue) {
			#ifdef THPOOL_DEBUG
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
				"Failed to init thpool's job queue!");
			#endif
			thpool_delete (thpool);
		}

		else {
			pthread_mutex_init (&(thpool->thcount_lock), NULL);
			pthread_cond_init (&thpool->threads_all_idle, NULL);

			// init threads
			thpool->num_threads_alive = 0;
			for (unsigned int i = 0; i < num_threads; i++)
				thread_init (thpool, &thpool->threads[i], i);

			// #ifdef THPOOL_DEBUG
			// cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
			// 	c_string_create ("Thpool %s alive threads: %d", 
			// 	thpool->name->str, thpool->num_threads_alive));
			// #endif

			// wait for threads to init
			// while (thpool->num_threads_alive != num_threads) {}
		}
	}

	return thpool;

}

// add work to the thread pool
int thpool_add_work (threadpool *thpool_p, void (*function_p)(void*), void* arg_p) {

	int retval = 1;

	if (thpool_p) {
		job *newjob = job_new ();
		if (newjob) {
			newjob->function = function_p;
			newjob->arg = arg_p;

			jobqueue_push (thpool_p->job_queue, newjob);

			retval = 0;
		}
	}

	return retval;

}

// wait until all jobs have finished
void thpool_wait (threadpool *thpool_p) {

	if (thpool_p) {
		pthread_mutex_lock (&thpool_p->thcount_lock);
		while (thpool_p->job_queue->len || thpool_p->num_threads_working) {
			pthread_cond_wait (&thpool_p->threads_all_idle, &thpool_p->thcount_lock);
		}
		pthread_mutex_unlock (&thpool_p->thcount_lock);
	}

}

// pause all threads in threadpool
void thpool_pause (threadpool *thpool_p) {

	for (unsigned int n = 0; n < thpool_p->num_threads_alive; n++){
		pthread_kill (thpool_p->threads[n]->pthread, SIGUSR1);
	}

}

// resume all threads in threadpool
void thpool_resume (threadpool *thpool_p) {

    // resuming a single threadpool hasn't been
    // implemented yet, meanwhile this supresses
    // the warnings
    (void) thpool_p;

	threads_on_hold = 0;

}

int thpool_num_threads_working (threadpool *thpool_p) {

	return thpool_p->num_threads_working;

}

void thpool_destroy (threadpool *thpool_p) {

	if (thpool_p) {
		volatile int threads_total = thpool_p->num_threads_alive;

		/* End each thread 's infinite loop */
		threads_keep_alive = 0;

		/* Give one second to kill idle threads */
		double TIMEOUT = 1.0;
		time_t start, end;
		double tpassed = 0.0;
		time (&start);
		while (tpassed < TIMEOUT && thpool_p->num_threads_alive){
			bsem_post_all (thpool_p->job_queue->has_jobs);
			time (&end);
			tpassed = difftime(end,start);
		}

		/* Poll remaining threads */
		while (thpool_p->num_threads_alive){
			bsem_post_all (thpool_p->job_queue->has_jobs);
			sleep(1);
		}

		jobqueue_delete (thpool_p->job_queue);

		for (unsigned int n = 0; n < threads_total; n++) {
			thread_delete (thpool_p->threads[n]);
		}

		free (thpool_p->threads);

		thpool_delete (thpool_p);
	}
	
}