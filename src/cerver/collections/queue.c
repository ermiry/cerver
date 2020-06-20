#include <stdlib.h>

#include "cerver/collections/dllist.h"
#include "cerver/collections/queue.h"

#pragma region internal

static Queue *queue_new (void) {

    Queue *queue = (Queue *) malloc (sizeof (Queue));
    if (queue) {
        queue->dlist = NULL;
        queue->destroy = NULL;
    }

    return queue;

}

#pragma endregion

// returns how many elements are inside the queue
size_t queue_size (Queue *queue) {

    size_t retval = 0;

    if (queue) {
        retval = dlist_size (queue->dlist);
    }

    return retval;

}

Queue *queue_create (void (*destroy)(void *data)) {

    Queue *queue = queue_new ();
    if (queue) {
        queue->dlist = dlist_init (destroy, NULL);
        queue->destroy = destroy;
    }

    return queue;

}

void queue_delete (Queue *queue) {

    if (queue) {
        dlist_delete (queue->dlist);

        free (queue);
    }

}

// destroys all of the dlist's elements and their data but keeps the dlist
void queue_reset (Queue *queue) {

    if (queue) {
        dlist_reset (queue->dlist);
    }

}

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
void queue_clear (Queue *queue) {

    if (queue) {
        dlist_clear (queue->dlist);
    }

}

// inserts the data at the end of the queue
unsigned int queue_push (Queue *queue, void *data) {

    unsigned int retval = 1;

    if (queue && data) {
        retval = dlist_insert_after (
            queue->dlist,
            dlist_end (queue->dlist),
            data
        );
    }

    return retval;

}

// gets the oldest data (the one at the start)
void *queue_pop (Queue *queue) {

    void *retval = NULL;

    if (queue) {
        retval = dlist_remove_element (
            queue->dlist,
            NULL
        );
    }

    return retval;

}