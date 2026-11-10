#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <time.h>

#include "cerver/collections/pool.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

// the capacity of a priority is doubled each time it grows
#define JOB_QUEUE_RING_GROWTH_FACTOR		2

#define JOB_QUEUE_MICROSECONDS_PER_SECOND			1000000
#define JOB_QUEUE_NANOSECONDS_PER_MICROSECOND		1000

void job_queue_clear (JobQueue *job_queue);

#pragma region job

void *job_new (void) {

	Job *job = (Job *) malloc (sizeof (Job));
	if (job) {
		job->id = 0;
		job->work = NULL;
		job->args = NULL;
	}

	return job;

}

void job_delete (void *job_ptr) {

	if (job_ptr) free (job_ptr);

}

int job_comparator (const void *a, const void *b) {

	if (a && b) {
		Job *job_a = (Job *) a;
		Job *job_b = (Job *) b;

		if (job_a->id < job_b->id) return -1;
		else if (job_a->id == job_b->id) return 0;
		else return 1;
	}

	if (a) return -1;
	return 1;

}

// allocates a new job, job_get () takes one from the pool of a queue instead
Job *job_create (void (*work) (void *args), void *args) {

	Job *job = (Job *) job_new ();
	if (job) {
		job->work = work;
		job->args = args;
	}

	return job;

}

// takes a job from the pool of the queue, it allocates one only if the pool is empty
Job *job_get (JobQueue *job_queue) {

	Job *job = job_queue ? (Job *) pool_pop (job_queue->pool) : NULL;
	if (job) job->id = 0;

	return job;

}

void job_reset (Job *job) {

	job->id = 0;
	job->work = NULL;
	job->args = NULL;

}

// returns a job to the pool of the queue, to be used again by job_get ()
void job_return (
	JobQueue *job_queue, Job *job
) {

	if (job_queue && job) {
		job_reset (job);
		(void) pool_push (job_queue->pool, job);
	}

}

#pragma endregion

#pragma region handler

void *job_handler_new (void) {

	JobHandler *job_handler = (JobHandler *) malloc (sizeof (JobHandler));
	if (job_handler) {
		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		job_handler->mutex = NULL;
		job_handler->cond = NULL;

		job_handler->done = false;

		job_handler->data = NULL;
		job_handler->data_delete = NULL;
	}

	return job_handler;

}

void job_handler_delete (void *job_handler_ptr) {

	if (job_handler_ptr) {
		JobHandler *job_handler = (JobHandler *) job_handler_ptr;

		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		thread_mutex_delete (job_handler->mutex);
		thread_cond_delete (job_handler->cond);

		if (job_handler->data_delete)
			job_handler->data_delete (job_handler->data);

		job_handler->data = NULL;
		job_handler->data_delete = NULL;

		free (job_handler);
	}

}

void *job_handler_create (void) {

	JobHandler *job_handler = (JobHandler *) job_handler_new ();
	if (job_handler) {
		job_handler->cond = thread_cond_new ();
		job_handler->mutex = thread_mutex_new ();
	}

	return job_handler;

}

JobHandler *job_handler_get (JobQueue *job_queue) {

	return job_queue ? (JobHandler *) pool_pop (job_queue->pool) : NULL;

}

void job_handler_reset (JobHandler *job_handler) {

	if (job_handler) {
		job_handler->cerver = NULL;
		job_handler->connection = NULL;

		job_handler->done = false;

		if (job_handler->data_delete)
			job_handler->data_delete (job_handler->data);

		job_handler->data = NULL;
		job_handler->data_delete = NULL;
	}

}

// wake up waiting thread
void job_handler_signal (JobHandler *handler) {

	if (handler) {
		(void) pthread_mutex_lock (handler->mutex);

		handler->done = true;

		(void) pthread_cond_signal (handler->cond);
		(void) pthread_mutex_unlock (handler->mutex);
	}

}

void job_handler_return (
	JobQueue *job_queue, JobHandler *job_handler
) {

	if (job_queue && job_handler) {
		job_handler_reset (job_handler);
		(void) pool_push (job_queue->pool, job_handler);
	}

}

void job_handler_wait (
	JobQueue *job_queue,
	void *data, void (*data_delete) (void *data_ptr)
) {

	JobHandler *handler = job_handler_get (job_queue);
	if (handler) {
		handler->data = data;
		handler->data_delete = data_delete;

		// push handler to queue and wait for results
		if (!job_queue_push (job_queue, handler)) {
			// wait for response
			(void) pthread_mutex_lock (handler->mutex);

			while (!handler->done) {
				(void) pthread_cond_wait (handler->cond, handler->mutex);
			}

			(void) pthread_mutex_unlock (handler->mutex);
		}

		job_handler_return (job_queue, handler);
	}

}

#pragma endregion

#pragma region time

static u64 job_queue_elapsed_microseconds (
	const struct timespec *start, const struct timespec *end
) {

	const long long elapsed =
		((long long) (end->tv_sec - start->tv_sec) * JOB_QUEUE_MICROSECONDS_PER_SECOND)
		+ ((long long) (end->tv_nsec - start->tv_nsec) / JOB_QUEUE_NANOSECONDS_PER_MICROSECOND);

	return (elapsed > 0) ? (u64) elapsed : 0;

}

static u64 job_queue_elapsed_since (const struct timespec *start) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return job_queue_elapsed_microseconds (start, &now);

}

// the absolute CLOCK_MONOTONIC time after timeout ms
static void job_queue_deadline (struct timespec *deadline, const u32 timeout) {

	(void) clock_gettime (CLOCK_MONOTONIC, deadline);

	deadline->tv_sec += (time_t) (timeout / JOB_QUEUE_MILLISECONDS_PER_SECOND);
	deadline->tv_nsec += (long) (timeout % JOB_QUEUE_MILLISECONDS_PER_SECOND)
		* JOB_QUEUE_NANOSECONDS_PER_MILLISECOND;

	if (deadline->tv_nsec >= JOB_QUEUE_NANOSECONDS_PER_SECOND) {
		deadline->tv_sec += 1;
		deadline->tv_nsec -= JOB_QUEUE_NANOSECONDS_PER_SECOND;
	}

}

#pragma endregion

#pragma region ring

static void job_queue_ring_delete (JobQueueRing *ring) {

	free (ring->entries);

	ring->entries = NULL;
	ring->head = 0;
	ring->count = 0;
	ring->capacity = 0;

}

// the entries keep their order from the head, starting at 0 in the new array
// returns 0 on success, 1 if the ring could not grow
static unsigned int job_queue_ring_grow (JobQueueRing *ring) {

	unsigned int retval = 1;

	const size_t capacity = ring->capacity ?
		(ring->capacity * JOB_QUEUE_RING_GROWTH_FACTOR) : JOB_QUEUE_DEFAULT_RING_CAPACITY;

	JobQueueEntry *entries = (JobQueueEntry *) malloc (capacity * sizeof (JobQueueEntry));
	if (entries) {
		for (size_t index = 0; index < ring->count; index++) {
			entries[index] = ring->entries[(ring->head + index) % ring->capacity];
		}

		free (ring->entries);

		ring->entries = entries;
		ring->head = 0;
		ring->capacity = capacity;

		retval = 0;
	}

	return retval;

}

// returns 0 on success, 1 if there is no room for the job
static unsigned int job_queue_ring_push (JobQueueRing *ring, void *job) {

	unsigned int retval = 1;

	if ((ring->count < ring->capacity) || !job_queue_ring_grow (ring)) {
		JobQueueEntry *entry = &ring->entries[(ring->head + ring->count) % ring->capacity];
		entry->job = job;
		(void) clock_gettime (CLOCK_MONOTONIC, &entry->queued);

		ring->count += 1;

		retval = 0;
	}

	return retval;

}

static JobQueueEntry job_queue_ring_pop (JobQueueRing *ring) {

	JobQueueEntry entry = ring->entries[ring->head];

	ring->head = (ring->head + 1) % ring->capacity;
	ring->count -= 1;

	return entry;

}

// removes the entry at the position from the head, the ones after it move one place
static JobQueueEntry job_queue_ring_remove (JobQueueRing *ring, const size_t position) {

	JobQueueEntry entry = ring->entries[(ring->head + position) % ring->capacity];

	for (size_t index = position; (index + 1) < ring->count; index++) {
		ring->entries[(ring->head + index) % ring->capacity] =
			ring->entries[(ring->head + index + 1) % ring->capacity];
	}

	ring->count -= 1;

	return entry;

}

#pragma endregion

#pragma region queue

JobQueue *job_queue_new (void) {

	JobQueue *job_queue = (JobQueue *) malloc (sizeof (JobQueue));
	if (job_queue) {
		(void) memset (job_queue, 0, sizeof (JobQueue));

		job_queue->type = JOB_QUEUE_TYPE_NONE;

		job_queue->pool = NULL;

		(void) pthread_mutex_init (&job_queue->mutex, NULL);

		// the deadlines of the waits are in CLOCK_MONOTONIC
		pthread_condattr_t attributes;
		(void) pthread_condattr_init (&attributes);
		(void) pthread_condattr_setclock (&attributes, CLOCK_MONOTONIC);
		(void) pthread_cond_init (&job_queue->has_jobs, &attributes);
		(void) pthread_cond_init (&job_queue->has_request, &attributes);
		(void) pthread_condattr_destroy (&attributes);

		job_queue->n_priorities = JOB_QUEUE_DEFAULT_PRIORITIES;
		job_queue->count = 0;

		job_queue->starvation_limit = JOB_QUEUE_DEFAULT_STARVATION_LIMIT;
		job_queue->starvation_streak = 0;

		job_queue->signals = 0;
		job_queue->signaled = false;

		job_queue->waiting = false;
		job_queue->requested_id = 0;
		job_queue->requested_job = NULL;

		job_queue->running = false;
		job_queue->handler_thread_id = 0;
		job_queue->handler = NULL;

		job_queue->destroy = NULL;
	}

	return job_queue;

}

void job_queue_delete (void *job_queue_ptr) {

	if (job_queue_ptr) {
		JobQueue *job_queue = (JobQueue *) job_queue_ptr;

		// its thread must end before anything it uses is released
		(void) job_queue_stop (job_queue);

		job_queue_clear (job_queue);

		for (unsigned int priority = 0; priority < JOB_QUEUE_MAX_PRIORITIES; priority++) {
			job_queue_ring_delete (&job_queue->rings[priority]);
		}

		pool_delete (job_queue->pool);

		(void) pthread_cond_destroy (&job_queue->has_request);
		(void) pthread_cond_destroy (&job_queue->has_jobs);
		(void) pthread_mutex_destroy (&job_queue->mutex);

		free (job_queue);
	}

}

static void job_queue_create_jobs (JobQueue *job_queue) {

	job_queue->pool = pool_create (job_delete);
	if (job_queue->pool) {
		pool_set_create (job_queue->pool, job_new);
		pool_set_produce_if_empty (job_queue->pool, true);

		(void) pool_init (
			job_queue->pool,
			job_new,
			JOB_QUEUE_POOL_INIT
		);
	}

	job_queue->destroy = job_delete;

}

static void job_queue_create_handlers (JobQueue *job_queue) {

	job_queue->pool = pool_create (job_handler_delete);
	if (job_queue->pool) {
		pool_set_create (job_queue->pool, job_handler_create);
		pool_set_produce_if_empty (job_queue->pool, true);

		(void) pool_init (
			job_queue->pool,
			job_handler_create,
			JOB_QUEUE_POOL_INIT
		);
	}

	job_queue->destroy = job_handler_delete;

}

static unsigned int job_queue_priorities_valid (const unsigned int n_priorities) {

	unsigned int priorities = n_priorities;

	if (!priorities) priorities = 1;
	else if (priorities > JOB_QUEUE_MAX_PRIORITIES) priorities = JOB_QUEUE_MAX_PRIORITIES;

	return priorities;

}

// creates a queue with n_priorities priorities (1 to JOB_QUEUE_MAX_PRIORITIES)
JobQueue *job_queue_create_with_priorities (
	const JobQueueType type, const unsigned int n_priorities
) {

	JobQueue *job_queue = job_queue_new ();
	if (job_queue) {
		job_queue->type = type;
		job_queue->n_priorities = job_queue_priorities_valid (n_priorities);

		switch (job_queue->type) {
			case JOB_QUEUE_TYPE_JOBS:
				job_queue_create_jobs (job_queue);
				break;

			case JOB_QUEUE_TYPE_HANDLERS:
				job_queue_create_handlers (job_queue);
				break;

			default: break;
		}
	}

	return job_queue;

}

// creates a queue with JOB_QUEUE_DEFAULT_PRIORITIES priorities
JobQueue *job_queue_create (const JobQueueType type) {

	return job_queue_create_with_priorities (type, JOB_QUEUE_DEFAULT_PRIORITIES);

}

void job_queue_set_handler (
	JobQueue *queue, void (*handler) (void *data)
) {

	if (queue) queue->handler = handler;

}

// sets how many jobs can be taken ahead of a waiting job of a lower priority
// before it is taken (JOB_QUEUE_DEFAULT_STARVATION_LIMIT),
// JOB_QUEUE_STARVATION_DISABLED always takes the highest priority first
void job_queue_set_starvation_limit (
	JobQueue *job_queue, const unsigned int limit
) {

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		job_queue->starvation_limit = limit;
		job_queue->starvation_streak = 0;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

// the priority that job_queue_push () uses, the one in the middle
unsigned int job_queue_default_priority (const JobQueue *job_queue) {

	return job_queue ? (job_queue->n_priorities / 2) : 0;

}

// returns how many jobs are in the queue
size_t job_queue_size (JobQueue *job_queue) {

	size_t size = 0;

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		size = job_queue->count;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

	return size;

}

// returns the milliseconds that the oldest job in the queue has waited, 0 if it is empty
u64 job_queue_oldest_wait (JobQueue *job_queue) {

	u64 oldest = 0;

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		for (unsigned int priority = 0; priority < job_queue->n_priorities; priority++) {
			const JobQueueRing *ring = &job_queue->rings[priority];
			if (ring->count) {
				const u64 waited = job_queue_elapsed_since (&ring->entries[ring->head].queued);
				if (waited > oldest) oldest = waited;
			}
		}

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

	return oldest / THPOOL_MICROSECONDS_PER_MILLISECOND;

}

#pragma endregion

#pragma region push

// called with the queue's lock
static unsigned int job_queue_push_locked (
	JobQueue *job_queue, void *job_ptr, const unsigned int priority
) {

	const unsigned int selected = (priority < job_queue->n_priorities) ?
		priority : (job_queue->n_priorities - 1);

	unsigned int retval = job_queue_ring_push (&job_queue->rings[selected], job_ptr);
	if (!retval) {
		job_queue->count += 1;

		job_queue->stats.total_jobs += 1;
		job_queue->stats.priority_jobs[selected] += 1;
		if (job_queue->count > job_queue->stats.max_queued) {
			job_queue->stats.max_queued = job_queue->count;
		}

		(void) pthread_cond_signal (&job_queue->has_jobs);
	}

	return retval;

}

static unsigned int job_queue_push_internal (
	JobQueue *job_queue, void *job_ptr, const unsigned int priority
) {

	(void) pthread_mutex_lock (&job_queue->mutex);

	const unsigned int retval = job_queue_push_locked (job_queue, job_ptr, priority);

	(void) pthread_mutex_unlock (&job_queue->mutex);

	return retval;

}

// adds a new job to the queue with a priority, 0 is the highest one
// a priority that does not exist is the lowest one
// returns 0 on success, 1 on error
unsigned int job_queue_push_priority (
	JobQueue *job_queue, void *job_ptr, const unsigned int priority
) {

	return (job_queue && job_ptr) ?
		job_queue_push_internal (job_queue, job_ptr, priority) : 1;

}

// adds a new job to the queue with the default priority
// returns 0 on success, 1 on error
unsigned int job_queue_push (JobQueue *job_queue, void *job_ptr) {

	return job_queue_push_priority (
		job_queue, job_ptr, job_queue_default_priority (job_queue)
	);

}

// creates & adds a new job to the queue with a priority
// returns 0 on success, 1 on error
unsigned int job_queue_push_job_priority (
	JobQueue *job_queue,
	void (*work) (void *args), void *args,
	const unsigned int priority
) {

	unsigned int retval = 1;

	Job *job = job_get (job_queue);
	if (job) {
		job->work = work;
		job->args = args;

		retval = job_queue_push_internal (job_queue, job, priority);
		if (retval) job_return (job_queue, job);
	}

	return retval;

}

// creates & adds a new job to the queue
// returns 0 on success, 1 on error
unsigned int job_queue_push_job (
	JobQueue *job_queue,
	void (*work) (void *args), void *args
) {

	return job_queue_push_job_priority (
		job_queue, work, args, job_queue_default_priority (job_queue)
	);

}

// the job goes directly to the thread that waits for its id
static unsigned int job_queue_push_job_with_id_internal (
	JobQueue *job_queue, Job *job
) {

	unsigned int retval = 1;

	(void) pthread_mutex_lock (&job_queue->mutex);

	if (
		job_queue->waiting && !job_queue->requested_job
		&& (job_queue->requested_id == job->id)
	) {
		job_queue->requested_job = job;
		job_queue->stats.total_jobs += 1;

		(void) pthread_cond_broadcast (&job_queue->has_request);

		retval = 0;
	}

	else {
		retval = job_queue_push_locked (
			job_queue, job, job_queue_default_priority (job_queue)
		);
	}

	(void) pthread_mutex_unlock (&job_queue->mutex);

	return retval;

}

// adds a job with an id, a thread that waits for it in job_queue_request () gets it directly
// returns 0 on success, 1 on error
unsigned int job_queue_push_job_with_id (
	JobQueue *job_queue,
	const u64 job_id,
	void (*work) (void *args), void *args
) {

	unsigned int retval = 1;

	Job *job = job_get (job_queue);
	if (job) {
		job->id = job_id;
		job->work = work;
		job->args = args;

		retval = job_queue_push_job_with_id_internal (job_queue, job);
		if (retval) job_return (job_queue, job);
	}

	return retval;

}

// creates & adds a new handler to the queue
// returns 0 on success, 1 on error
unsigned int job_queue_push_handler (
	JobQueue *job_queue,
	struct _Cerver *cerver,
	struct _Connection *connection,
	void *data, void (*data_delete) (void *data_ptr)
) {

	unsigned int retval = 1;

	JobHandler *job_handler = job_handler_get (job_queue);
	if (job_handler) {
		job_handler->cerver = cerver;
		job_handler->connection = connection;

		job_handler->data = data;
		job_handler->data_delete = data_delete;

		retval = job_queue_push (job_queue, job_handler);
		if (retval) job_handler_return (job_queue, job_handler);
	}

	return retval;

}

#pragma endregion

#pragma region pull

// the highest priority with jobs, or a lower one when the jobs taken ahead of it
// reached the starvation limit, called with the queue's lock
// returns the priority, or -1 if the queue is empty
static int job_queue_select_priority (JobQueue *job_queue) {

	int first = -1;
	for (unsigned int priority = 0; (priority < job_queue->n_priorities) && (first < 0); priority++) {
		if (job_queue->rings[priority].count) first = (int) priority;
	}

	int selected = first;
	if ((first >= 0) && (job_queue->starvation_limit != JOB_QUEUE_STARVATION_DISABLED)) {
		int lower = -1;
		for (unsigned int priority = (unsigned int) first + 1; (priority < job_queue->n_priorities) && (lower < 0); priority++) {
			if (job_queue->rings[priority].count) lower = (int) priority;
		}

		if (lower < 0) {
			job_queue->starvation_streak = 0;
		}

		else if (job_queue->starvation_streak >= job_queue->starvation_limit) {
			job_queue->starvation_streak = 0;
			job_queue->stats.promoted_jobs += 1;
			selected = lower;
		}

		else {
			job_queue->starvation_streak += 1;
		}
	}

	return selected;

}

// takes the next job, called with the queue's lock
static void *job_queue_take (JobQueue *job_queue) {

	void *job = NULL;

	const int priority = job_queue_select_priority (job_queue);
	if (priority >= 0) {
		const JobQueueEntry entry = job_queue_ring_pop (&job_queue->rings[priority]);
		job_queue->count -= 1;

		const u64 waited = job_queue_elapsed_since (&entry.queued);
		job_queue->stats.pulled_jobs += 1;
		job_queue->stats.wait_total_microseconds += waited;
		job_queue->stats.wait_histogram[thpool_histogram_bucket (waited)] += 1;

		job = entry.job;
	}

	return job;

}

// waits for the next job with the queue's lock, until the deadline (NULL for none),
// a signal or interrupted (args), an interrupted wait leaves the jobs in the queue
static void *job_queue_pull_wait (
	JobQueue *job_queue, const struct timespec *deadline,
	bool (*interrupted) (const void *args), const void *args
) {

	void *job = NULL;

	const u64 signals = job_queue->signals;

	bool waiting = true;
	while (waiting) {
		if (interrupted && interrupted (args)) {
			waiting = false;
		}

		else if ((job = job_queue_take (job_queue)) || (job_queue->signals != signals)) {
			waiting = false;
		}

		else if (deadline) {
			if (pthread_cond_timedwait (&job_queue->has_jobs, &job_queue->mutex, deadline) == ETIMEDOUT) {
				job = job_queue_take (job_queue);
				if (!job) job_queue->stats.timed_out_waits += 1;

				waiting = false;
			}
		}

		else {
			(void) pthread_cond_wait (&job_queue->has_jobs, &job_queue->mutex);
		}
	}

	return job;

}

// gets the next job without waiting, NULL if the queue is empty
void *job_queue_pull (JobQueue *job_queue) {

	void *job = NULL;

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		job = job_queue_take (job_queue);

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

	return job;

}

// waits up to timeout ms (or JOB_QUEUE_WAIT_FOREVER) for the next job
// returns NULL if the deadline passed or job_queue_signal () was called
void *job_queue_pull_timeout (JobQueue *job_queue, const u32 timeout) {

	void *job = NULL;

	if (job_queue) {
		struct timespec deadline = { 0, 0 };
		if (timeout != JOB_QUEUE_WAIT_FOREVER) job_queue_deadline (&deadline, timeout);

		(void) pthread_mutex_lock (&job_queue->mutex);

		job = job_queue_pull_wait (
			job_queue,
			(timeout != JOB_QUEUE_WAIT_FOREVER) ? &deadline : NULL,
			NULL, NULL
		);

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

	return job;

}

// waits for the next job until interrupted (args) is true,
// the thread that changes what interrupted () reads must call job_queue_signal () after it,
// interrupted () is called with the queue's lock, so the wake up is never lost
// returns NULL when it was interrupted or signaled
void *job_queue_pull_interruptible (
	JobQueue *job_queue,
	bool (*interrupted) (const void *args), const void *args
) {

	void *job = NULL;

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		job = job_queue_pull_wait (job_queue, NULL, interrupted, args);

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

	return job;

}

#pragma endregion

#pragma region request

// a job with the id that is already in the queue, called with the queue's lock
static void *job_queue_request_find (JobQueue *job_queue, const u64 job_id) {

	void *match = NULL;

	for (unsigned int priority = 0; (priority < job_queue->n_priorities) && !match; priority++) {
		JobQueueRing *ring = &job_queue->rings[priority];

		for (size_t position = 0; (position < ring->count) && !match; position++) {
			const Job *job = (const Job *) ring->entries[(ring->head + position) % ring->capacity].job;
			if (job->id == job_id) {
				match = job_queue_ring_remove (ring, position).job;
				job_queue->count -= 1;
			}
		}
	}

	return match;

}

// waits with the queue's lock for the job with the id to be pushed
static void *job_queue_request_wait (
	JobQueue *job_queue, const u64 job_id, const struct timespec *deadline
) {

	job_queue->waiting = true;
	job_queue->requested_id = job_id;
	job_queue->requested_job = NULL;

	bool waiting = true;
	while (waiting && !job_queue->requested_job) {
		if (deadline) {
			waiting = (pthread_cond_timedwait (
				&job_queue->has_request, &job_queue->mutex, deadline
			) != ETIMEDOUT);
		}

		else {
			(void) pthread_cond_wait (&job_queue->has_request, &job_queue->mutex);
		}
	}

	void *match = job_queue->requested_job;
	if (!match) job_queue->stats.timed_out_waits += 1;

	job_queue->waiting = false;
	job_queue->requested_id = 0;
	job_queue->requested_job = NULL;

	return match;

}

static void *job_queue_request_internal (
	JobQueue *job_queue, const u64 job_id, const u32 timeout
) {

	struct timespec deadline = { 0, 0 };
	if (timeout != JOB_QUEUE_WAIT_FOREVER) job_queue_deadline (&deadline, timeout);

	(void) pthread_mutex_lock (&job_queue->mutex);

	void *match = job_queue_request_find (job_queue, job_id);
	if (!match) {
		match = job_queue_request_wait (
			job_queue, job_id, (timeout != JOB_QUEUE_WAIT_FOREVER) ? &deadline : NULL
		);
	}

	if (match) job_queue->stats.requested_jobs += 1;

	(void) pthread_mutex_unlock (&job_queue->mutex);

	return match;

}

// requests to get an specific job from the queue by matching id
// blocks and waits until the requested job is available
void *job_queue_request (JobQueue *job_queue, const u64 job_id) {

	return job_queue ?
		job_queue_request_internal (job_queue, job_id, JOB_QUEUE_WAIT_FOREVER) : NULL;

}

// requests a job by its id and waits up to timeout ms for it
// returns NULL if it did not arrive before the deadline
void *job_queue_request_timeout (
	JobQueue *job_queue, const u64 job_id, const u32 timeout
) {

	return job_queue ?
		job_queue_request_internal (job_queue, job_id, timeout) : NULL;

}

#pragma endregion

#pragma region stats

// records that a job that was pulled finished its work, it started at start (CLOCK_MONOTONIC)
void job_queue_record_run (JobQueue *job_queue, const struct timespec *start) {

	if (job_queue && start) {
		const u64 elapsed = job_queue_elapsed_since (start);

		(void) pthread_mutex_lock (&job_queue->mutex);

		job_queue->stats.completed_jobs += 1;
		job_queue->stats.run_total_microseconds += elapsed;
		job_queue->stats.run_histogram[thpool_histogram_bucket (elapsed)] += 1;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

// copies the stats of the queue
void job_queue_get_stats (JobQueue *job_queue, JobQueueStats *stats) {

	if (job_queue && stats) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		*stats = job_queue->stats;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

void job_queue_stats_print (JobQueue *job_queue) {

	if (job_queue) {
		JobQueueStats stats;
		job_queue_get_stats (job_queue, &stats);

		cerver_log_msg ("Job queue total jobs: %lu", stats.total_jobs);
		cerver_log_msg ("Job queue pulled jobs: %lu", stats.pulled_jobs);
		cerver_log_msg ("Job queue completed jobs: %lu", stats.completed_jobs);
		cerver_log_msg ("Job queue requested jobs: %lu", stats.requested_jobs);
		cerver_log_msg ("Job queue timed out waits: %lu", stats.timed_out_waits);
		cerver_log_msg ("Job queue promoted jobs: %lu", stats.promoted_jobs);
		cerver_log_msg ("Job queue max queued: %lu", stats.max_queued);

		for (unsigned int priority = 0; priority < job_queue->n_priorities; priority++) {
			cerver_log_msg ("Job queue priority %u jobs: %lu", priority, stats.priority_jobs[priority]);
		}

		cerver_log_msg ("Job queue wait total: %lu us", stats.wait_total_microseconds);
		cerver_log_msg ("Job queue run total: %lu us", stats.run_total_microseconds);
	}

}

#pragma endregion

#pragma region thread

static bool job_queue_is_stopped (const void *job_queue_ptr) {

	const JobQueue *job_queue = (const JobQueue *) job_queue_ptr;

	return !__atomic_load_n (&job_queue->running, __ATOMIC_ACQUIRE);

}

static void *job_queue_jobs (void *job_queue_ptr) {

	JobQueue *job_queue = (JobQueue *) job_queue_ptr;

	while (!job_queue_is_stopped (job_queue)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			job_queue, job_queue_is_stopped, job_queue
		);

		if (job) {
			struct timespec start = { 0, 0 };
			(void) clock_gettime (CLOCK_MONOTONIC, &start);

			if (job->work) job->work (job->args);

			job_queue_record_run (job_queue, &start);

			job_return (job_queue, job);
		}
	}

	return NULL;

}

static void *job_queue_handlers (void *job_queue_ptr) {

	JobQueue *job_queue = (JobQueue *) job_queue_ptr;

	while (!job_queue_is_stopped (job_queue)) {
		JobHandler *job_handler = (JobHandler *) job_queue_pull_interruptible (
			job_queue, job_queue_is_stopped, job_queue
		);

		if (job_handler) {
			struct timespec start = { 0, 0 };
			(void) clock_gettime (CLOCK_MONOTONIC, &start);

			job_queue->handler (job_handler->data);

			job_queue_record_run (job_queue, &start);

			job_handler_signal (job_handler);
		}
	}

	return NULL;

}

static unsigned int job_queue_start_internal (JobQueue *job_queue) {

	unsigned int retval = 1;

	__atomic_store_n (&job_queue->running, true, __ATOMIC_RELEASE);
	// joined by job_queue_stop (), before the queue can be deleted
	switch (job_queue->type) {
		case JOB_QUEUE_TYPE_JOBS:
			retval = thread_create_joinable (
				&job_queue->handler_thread_id,
				job_queue_jobs,
				job_queue
			);
			break;

		case JOB_QUEUE_TYPE_HANDLERS:
			retval = thread_create_joinable (
				&job_queue->handler_thread_id,
				job_queue_handlers,
				job_queue
			);
			break;

		default: break;
	}

	return retval;

}

unsigned int job_queue_start (JobQueue *job_queue) {

	unsigned int errors = 0;

	if (job_queue) {
		if (job_queue_start_internal (job_queue)) {
			__atomic_store_n (&job_queue->running, false, __ATOMIC_RELEASE);
			errors = 1;
		}
	}

	return errors;

}

// waits until there is a job in the queue or the queue is signaled,
// a signal that arrives before the wait is kept
void job_queue_wait (JobQueue *job_queue) {

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		while (!job_queue->count && !job_queue->signaled) {
			(void) pthread_cond_wait (&job_queue->has_jobs, &job_queue->mutex);
		}

		job_queue->signaled = false;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

// wakes up every thread that waits in the queue
void job_queue_signal (JobQueue *job_queue) {

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		job_queue->signals += 1;
		job_queue->signaled = true;

		(void) pthread_cond_broadcast (&job_queue->has_jobs);
		(void) pthread_cond_broadcast (&job_queue->has_request);

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

// waits for the queue thread to end its current job
// a job that stops its own queue can not wait for itself,
// so its thread is detached and ends after the job returns
static void job_queue_join (JobQueue *job_queue) {

	if (job_queue->handler_thread_id) {
		if (pthread_equal (job_queue->handler_thread_id, pthread_self ())) {
			(void) pthread_detach (job_queue->handler_thread_id);
		}

		else {
			(void) pthread_join (job_queue->handler_thread_id, NULL);
		}

		job_queue->handler_thread_id = 0;
	}

}

unsigned int job_queue_stop (JobQueue *job_queue) {

	unsigned int retval = 1;

	if (job_queue) {
		// the queue thread reads it while it waits for jobs
		if (__atomic_exchange_n (&job_queue->running, false, __ATOMIC_ACQ_REL)) {
			job_queue_signal (job_queue);
			job_queue_join (job_queue);
			retval = 0;
		}
	}

	return retval;

}

// clears the job queue -> destroys all jobs
void job_queue_clear (JobQueue *job_queue) {

	if (job_queue) {
		(void) pthread_mutex_lock (&job_queue->mutex);

		for (unsigned int priority = 0; priority < job_queue->n_priorities; priority++) {
			JobQueueRing *ring = &job_queue->rings[priority];
			while (ring->count) {
				const JobQueueEntry entry = job_queue_ring_pop (ring);
				if (job_queue->destroy) job_queue->destroy (entry.job);
			}
		}

		job_queue->count = 0;
		job_queue->signaled = false;

		(void) pthread_mutex_unlock (&job_queue->mutex);
	}

}

#pragma endregion
