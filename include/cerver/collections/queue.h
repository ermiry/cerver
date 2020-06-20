#ifndef _COLLECTIONS_QUEUE_H_
#define _COLLECTIONS_QUEUE_H_

#include <stdlib.h>

#include "dllist.h"

typedef struct Queue {

    DoubleList *dlist;
    void (*destroy)(void *data);

} Queue;

// returns how many elements are inside the queue
extern size_t queue_size (Queue *queue);

// creates a new queue
extern Queue *queue_create (void (*destroy)(void *data));

// uses the create method to populate the queue with n elements
// returns 0 on no error, 1 if at least one element failed to be inserted
extern int queue_init (Queue *queue, 
    void *(*create)(void), unsigned int n_elements);

// deletes the queue and all of its members using the destroy method
extern void queue_delete (Queue *queue);

// destroys all of the queue's elements and their data but keeps the queue
extern void queue_reset (Queue *queue);

// only gets rid of the queue's elements, but the data is kept
// this is usefull if another structure points to the same data
extern void queue_clear (Queue *queue);

// inserts the data at the end of the queue
// returns 0 on success, 1 on error
extern unsigned int queue_push (Queue *queue, void *data);

// returns the data that is first in the queue
extern void *queue_pop (Queue *queue);

// gets the oldest data (the one at the start)
extern void *queue_pop (Queue *queue);

#endif