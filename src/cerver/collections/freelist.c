#include <stdlib.h>

#include <pthread.h>

#include "cerver/collections/freelist.h"

static inline void **freelist_element_next (
	const FreeList *list, void *element
) {

	return (void **) (((char *) element) + list->next_offset);

}

void *freelist_pop (FreeList *list) {

	void *element = NULL;

	if (list) {
		(void) pthread_mutex_lock (&list->mutex);

		element = list->head;
		if (element) {
			void **next = freelist_element_next (list, element);
			list->head = *next;
			*next = NULL;

			list->count -= 1;
		}

		(void) pthread_mutex_unlock (&list->mutex);
	}

	return element;

}

unsigned int freelist_push (FreeList *list, void *element) {

	unsigned int retval = 1;

	if (list && element) {
		(void) pthread_mutex_lock (&list->mutex);

		if (list->count < list->limit) {
			*freelist_element_next (list, element) = list->head;
			list->head = element;

			list->count += 1;

			retval = 0;
		}

		(void) pthread_mutex_unlock (&list->mutex);
	}

	return retval;

}

unsigned int freelist_size (FreeList *list) {

	unsigned int size = 0;

	if (list) {
		(void) pthread_mutex_lock (&list->mutex);

		size = list->count;

		(void) pthread_mutex_unlock (&list->mutex);
	}

	return size;

}

void freelist_drain (
	FreeList *list, void (*destroy) (void *element)
) {

	if (list) {
		(void) pthread_mutex_lock (&list->mutex);

		void *element = list->head;
		list->head = NULL;
		list->count = 0;

		(void) pthread_mutex_unlock (&list->mutex);

		while (element) {
			void *next = *freelist_element_next (list, element);

			if (destroy) destroy (element);

			element = next;
		}
	}

}
