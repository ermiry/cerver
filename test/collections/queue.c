#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>

#include <cerver/collections/queue.h>

#include "../test.h"
#include "check.h"
#include "data.h"

// bugs that this suite pinned:
// - queue_push read dlist_end () outside of the dlist lock
// coverage holes:
// - none known

#define QUEUE_TEST_SIZE						64
#define QUEUE_TEST_PRODUCERS				4
#define QUEUE_TEST_CONSUMERS				4
#define QUEUE_TEST_ITERATIONS				131072

typedef struct QueueTestArgs {

	Queue *queue;
	unsigned int thread;

	unsigned char *seen;
	pthread_mutex_t *seen_mutex;

	unsigned int *consumed;

} QueueTestArgs;

static void test_queue_fifo (void) {

	Queue *queue = queue_create (data_delete);
	test_check_ptr (queue);

	test_check_null_ptr (queue_pop (queue));
	test_check (queue_size (queue) == 0, NULL);

	for (unsigned int i = 0; i < QUEUE_TEST_SIZE; i++) {
		test_check_unsigned_eq (queue_push (queue, data_new (i, i)), 0, NULL);
	}

	test_check_unsigned_eq (queue_push (queue, NULL), 1, NULL);

	collections_check_dlist (queue->dlist, QUEUE_TEST_SIZE);

	for (unsigned int i = 0; i < QUEUE_TEST_SIZE / 2; i++) {
		Data *data = (Data *) queue_pop (queue);
		test_check_ptr (data);
		test_check_unsigned_eq (data->idx, i, NULL);
		data_delete (data);
	}

	collections_check_dlist (queue->dlist, QUEUE_TEST_SIZE / 2);

	// the rest is destroyed by the reset
	queue_reset (queue);
	collections_check_dlist (queue->dlist, 0);

	queue_delete (queue);

}

static void *queue_test_producer (void *args_ptr) {

	QueueTestArgs *args = (QueueTestArgs *) args_ptr;

	for (unsigned int i = 0; i < QUEUE_TEST_ITERATIONS; i++) {
		unsigned int value = (args->thread * QUEUE_TEST_ITERATIONS) + i;
		test_check_unsigned_eq (queue_push (args->queue, data_new (args->thread, value)), 0, NULL);
	}

	return NULL;

}

static void queue_test_mark_seen (QueueTestArgs *args, const Data *data) {

	(void) pthread_mutex_lock (args->seen_mutex);

	test_check (args->seen[data->value] == 0, "a value was popped twice");
	args->seen[data->value] = 1;
	*args->consumed += 1;

	(void) pthread_mutex_unlock (args->seen_mutex);

}

static bool queue_test_done (QueueTestArgs *args) {

	(void) pthread_mutex_lock (args->seen_mutex);

	bool retval = (*args->consumed == (QUEUE_TEST_PRODUCERS * QUEUE_TEST_ITERATIONS));

	(void) pthread_mutex_unlock (args->seen_mutex);

	return retval;

}

static void *queue_test_consumer (void *args_ptr) {

	QueueTestArgs *args = (QueueTestArgs *) args_ptr;

	// every producer keeps its own order
	unsigned int last[QUEUE_TEST_PRODUCERS];
	bool has_last[QUEUE_TEST_PRODUCERS] = { false };

	while (!queue_test_done (args)) {
		Data *data = (Data *) queue_pop (args->queue);
		if (data) {
			if (has_last[data->idx]) {
				test_check (data->value > last[data->idx], "a producer order was broken");
			}

			last[data->idx] = data->value;
			has_last[data->idx] = true;

			queue_test_mark_seen (args, data);
			data_delete (data);
		}
	}

	return NULL;

}

static void test_queue_threads (void) {

	Queue *queue = queue_create (data_delete);

	const unsigned int total = QUEUE_TEST_PRODUCERS * QUEUE_TEST_ITERATIONS;
	unsigned char *seen = (unsigned char *) calloc (total, sizeof (unsigned char));
	test_check_ptr (seen);

	pthread_mutex_t seen_mutex;
	(void) pthread_mutex_init (&seen_mutex, NULL);

	unsigned int consumed = 0;

	pthread_t producers[QUEUE_TEST_PRODUCERS];
	pthread_t consumers[QUEUE_TEST_CONSUMERS];
	QueueTestArgs producer_args[QUEUE_TEST_PRODUCERS];
	QueueTestArgs consumer_args[QUEUE_TEST_CONSUMERS];

	for (unsigned int i = 0; i < QUEUE_TEST_CONSUMERS; i++) {
		consumer_args[i] = (QueueTestArgs) { queue, i, seen, &seen_mutex, &consumed };
		(void) pthread_create (&consumers[i], NULL, queue_test_consumer, &consumer_args[i]);
	}

	for (unsigned int i = 0; i < QUEUE_TEST_PRODUCERS; i++) {
		producer_args[i] = (QueueTestArgs) { queue, i, seen, &seen_mutex, &consumed };
		(void) pthread_create (&producers[i], NULL, queue_test_producer, &producer_args[i]);
	}

	for (unsigned int i = 0; i < QUEUE_TEST_PRODUCERS; i++) (void) pthread_join (producers[i], NULL);
	for (unsigned int i = 0; i < QUEUE_TEST_CONSUMERS; i++) (void) pthread_join (consumers[i], NULL);

	test_check_unsigned_eq (consumed, total, NULL);
	for (unsigned int i = 0; i < total; i++) {
		test_check (seen[i] == 1, "a value was never popped");
	}

	collections_check_dlist (queue->dlist, 0);

	(void) pthread_mutex_destroy (&seen_mutex);
	free (seen);

	queue_delete (queue);

}

void collections_tests_queue (void) {

	(void) printf ("Testing COLLECTIONS queue...\n");

	test_queue_fifo ();
	test_queue_threads ();

	(void) printf ("Done!\n");

}
