#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include "cerver/collections/dllist.h"

static inline void list_element_delete (ListElement *le);

#pragma region internal

static ListElement *list_element_new (void) {

	ListElement *le = (ListElement *) malloc (sizeof (ListElement));
	if (le) {
		le->next = le->prev = NULL;
		le->data = NULL;
	}

	return le;

}

static inline void list_element_delete (ListElement *le) { if (le) free (le); }

static DoubleList *dlist_new (void) {

	DoubleList *dlist = (DoubleList *) malloc (sizeof (DoubleList));
	if (dlist) {
		dlist->size = 0;
		dlist->start = NULL;
		dlist->end = NULL;
		dlist->destroy = NULL;
		dlist->compare = NULL;

		dlist->mutex = NULL;
	}

	return dlist;

}

static void *dlist_internal_remove_element (DoubleList *dlist, ListElement *element) {

	if (dlist) {
		void *data = NULL;
		if (dlist_size (dlist) > 0) {
			ListElement *old;

			if (element == NULL) {
				data = dlist->start->data;
				old = dlist->start;
				dlist->start = dlist->start->next;
				if (dlist->start != NULL) dlist->start->prev = NULL;
			}

			else {
				data = element->data;
				old = element;

				ListElement *prevElement = element->prev;
				ListElement *nextElement = element->next;

				if (prevElement != NULL && nextElement != NULL) {
					prevElement->next = nextElement;
					nextElement->prev = prevElement;
				}

				else {
					// we are at the start of the dlist
					if (prevElement == NULL) {
						if (nextElement != NULL) nextElement->prev = NULL;
						dlist->start = nextElement;
					}

					// we are at the end of the dlist
					if (nextElement == NULL) {
						if (prevElement != NULL) prevElement->next = NULL;
						dlist->end = prevElement;
					}
				}
			}

			list_element_delete (old);
			dlist->size--;

			if (dlist->size == 0) {
				dlist->start = NULL;
				dlist->end = NULL;
			}
		}

		return data;
	}

	return NULL;

}

#pragma endregion

void dlist_delete (void *dlist_ptr) {

	if (dlist_ptr) {
		DoubleList *dlist = (DoubleList *) dlist_ptr;

		pthread_mutex_lock (dlist->mutex);

		if (dlist->size > 0) {
			void *data = NULL;

			while (dlist_size (dlist) > 0) {
				data = dlist_internal_remove_element (dlist, NULL);
				if (data) {
					if (dlist->destroy) dlist->destroy (data);
					else free (data);
				}
			}
		}

		pthread_mutex_unlock (dlist->mutex);
		pthread_mutex_destroy (dlist->mutex);
		free (dlist->mutex);

		free (dlist);
	}

}

void dlist_set_compare (DoubleList *dlist, int (*compare)(const void *one, const void *two)) { if (dlist) dlist->compare = compare; }

void dlist_set_destroy (DoubleList *dlist, void (*destroy)(void *data)) { if (dlist) dlist->destroy = destroy; }

DoubleList *dlist_init (void (*destroy)(void *data), int (*compare)(const void *one, const void *two)) {

	DoubleList *dlist = dlist_new ();

	if (dlist) {
		dlist->destroy = destroy;
		dlist->compare = compare;

		dlist->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (dlist->mutex, NULL);
	}

	return dlist;

}

// destroys all of the dlist's elements and their data but keeps the dlist
void dlist_reset (DoubleList *dlist) {

	if (dlist) {
		pthread_mutex_lock (dlist->mutex);

		if (dlist_size (dlist) > 0) {
			void *data = NULL;
			while (dlist_size (dlist) > 0) {
				data = dlist_internal_remove_element (dlist, NULL);
				if (data != NULL && dlist->destroy != NULL) dlist->destroy (data);
			}
		}

		dlist->start = NULL;
		dlist->end = NULL;
		dlist->size = 0;

		pthread_mutex_unlock (dlist->mutex);
	}

}

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
void dlist_clean (DoubleList *dlist) {

	if (dlist) {
		pthread_mutex_lock (dlist->mutex);

		void *data = NULL;
		while (dlist_size (dlist) > 0) 
			data = dlist_internal_remove_element (dlist, NULL);

		pthread_mutex_unlock (dlist->mutex);
	}

}

/*** insert ***/

// inserts the data in the double list BEFORE the specified element
// if element == NULL, data will be inserted at the start of the list
// returns 0 on success, 1 on error
int dlist_insert_before (DoubleList *dlist, ListElement *element, void *data) {

	int retval = 1;

	if (dlist && data) {
		pthread_mutex_lock (dlist->mutex);

		ListElement *le = list_element_new ();
		if (le) {
			le->data = (void *) data;

			if (element == NULL) {
				if (dlist_size (dlist) == 0) dlist->end = le;
				else dlist->start->prev = le;
			
				le->next = dlist->start;
				le->prev = NULL;
				dlist->start = le;
			}

			else {
				element->prev->next = le;
				le->next = element;
				le->prev = element->prev;
				element->prev = le;
			}

			dlist->size++;

			retval = 0;
		}

		pthread_mutex_unlock (dlist->mutex);
	}

	return retval;

}

// inserts the data in the double list AFTER the specified element
// if element == NULL, data will be inserted at the start of the list
// returns 0 on success, 1 on error
int dlist_insert_after (DoubleList *dlist, ListElement *element, void *data) {

	int retval = 1;

	if (dlist && data) {
		pthread_mutex_lock (dlist->mutex);

		ListElement *le = list_element_new ();
		if (le) {
			le->data = (void *) data;

			if (element == NULL) {
				if (dlist_size (dlist) == 0) dlist->end = le;
				else dlist->start->prev = le;
			
				le->next = dlist->start;
				le->prev = NULL;
				dlist->start = le;
			}

			else {
				if (element->next == NULL) dlist->end = le;

				le->next = element->next;
				le->prev = element;
				element->next = le;
			}

			dlist->size++;

			retval = 0;
		}

		pthread_mutex_unlock (dlist->mutex);
	}

	return retval;

}

// inserts the data in the double list in the specified pos (0 indexed)
// if the pos is greater than the current size, it will be added at the end
// returns 0 on success, 1 on error
int dlist_insert_at (DoubleList *dlist, void *data, unsigned int pos) {

	int retval = 1;

	if (dlist && data) {
		// insert at the start of the list
		if (pos == 0) retval = dlist_insert_before (dlist, NULL, data);
		else if (pos > dlist_size (dlist)) {
			// insert at the end of the list
			retval = dlist_insert_after (dlist, dlist_end (dlist), data);
		}

		else {
			unsigned int count = 0;
			ListElement *le = dlist_start (dlist);
			while (le) {
				if (count == pos) break;
				
				count++;
				le = le->next;
			}

			retval = dlist_insert_before (dlist, le, data);
		}
	}

	return retval;

}

/*** remove ***/

// finds the data using the query and the list comparator and the removes it from the list
// and returns the list element's data
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
void *dlist_remove (DoubleList *dlist, void *query, int (*compare)(const void *one, const void *two)) {

	void *retval = NULL;

	if (dlist && query) {
		int (*comp)(const void *one, const void *two) = compare ? compare : dlist->compare;

		if (comp) {
			pthread_mutex_lock (dlist->mutex);

			ListElement *ptr = dlist_start (dlist);

			bool first = true;
			while (ptr != NULL) {
				if (!comp (ptr->data, query)) {
					// remove the list element
					void *data = NULL;
					if (first) data = dlist_internal_remove_element (dlist, NULL);
					else data = dlist_internal_remove_element (dlist, ptr);
					if (data) {
						// if (dlist->destroy) dlist->destroy (data);
						// else free (data);

						retval = data;
					}

					break;
				}

				ptr = ptr->next;
				first = false;
			}

			pthread_mutex_unlock (dlist->mutex);
		}
	}

	return retval;

}

// removes the dlist element from the dlist and returns the data
// NULL for the start of the list
void *dlist_remove_element (DoubleList *dlist, ListElement *element) {

	if (dlist) {
		pthread_mutex_lock (dlist->mutex);

		void *data = dlist_internal_remove_element (dlist, element);

		pthread_mutex_unlock (dlist->mutex);

		return data;
	}

	return NULL;

}

// removes the dlist element from the dlist at the specified index 
// returns the data or NULL if index was invalid
void *dlist_remove_at (DoubleList *dlist, unsigned int idx) {

	void *retval = NULL;

	if (dlist) {
		if (idx < dlist->size) {
			pthread_mutex_lock (dlist->mutex);

			bool first = true;
			unsigned int i = 0;
			ListElement *ptr = dlist_start (dlist);
			while (ptr != NULL) {
				if (i == idx) {
					// remove the list element
					void *data = NULL;
					if (first) data = dlist_internal_remove_element (dlist, NULL);
					else data = dlist_internal_remove_element (dlist, ptr);
					if (data) {
						retval = data;
					}

					break;
				}

				first = false;
				ptr = ptr->next;
				i++;
			}

			pthread_mutex_unlock (dlist->mutex);
		}
	}

	return retval;

}

/*** Traversing --- Searching ***/

// traverses the dlist and for each element, calls the method by passing the list element data and the method args as both arguments
// this method is thread safe
// returns 0 on success, 1 on error
int dlist_traverse (DoubleList *dlist, void (*method)(void *list_element_data, void *method_args), void *method_args) {

	int retval = 1;

	if (dlist && method) {
		pthread_mutex_lock (dlist->mutex);

		for (ListElement *le = dlist_start (dlist); le; le = le->next) {
			method (le->data, method_args);
		}

		pthread_mutex_unlock (dlist->mutex);

		retval = 0;
	}
	
	return retval;

}

// uses the list comparator to search using the data as the query
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
// returns the double list's element data
void *dlist_search (DoubleList *dlist, void *data, int (*compare)(const void *one, const void *two)) {

	if (dlist && data) {
		int (*comp)(const void *one, const void *two) = compare ? compare : dlist->compare;

		if (comp) {
			ListElement *ptr = dlist_start (dlist);

			while (ptr != NULL) {
				if (!comp (ptr->data, data)) return ptr->data;
				ptr = ptr->next;
			}
		}
	}

	return NULL;    

}

// searches the dlist and returns the dlist element associated with the data
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
ListElement *dlist_get_element (DoubleList *dlist, void *data, 
	int (*compare)(const void *one, const void *two)) {

	if (dlist && data) {
		int (*comp)(const void *one, const void *two) = compare ? compare : dlist->compare;

		if (comp) {
			ListElement *ptr = dlist_start (dlist);
			while (ptr != NULL) {
				if (!dlist->compare (ptr->data, data)) return ptr;
				ptr = ptr->next;
			}
		}
	}

	return NULL;

}

// traverses the dlist and returns the list element at the specified index
ListElement *dlist_get_element_at (DoubleList *dlist, unsigned int idx) {

	if (dlist) {
		if (idx < dlist->size) {
			unsigned int i = 0;
			ListElement *ptr = dlist_start (dlist);
			while (ptr != NULL) {
				if (i == idx) return ptr;

				ptr = ptr->next;
				i++;
			}
		}
	}

	return NULL;

}

// traverses the dlist and returns the data of the list element at the specified index
void *dlist_get_at (DoubleList *dlist, unsigned int idx) {

	if (dlist) {
		if (idx < dlist->size) {
			ListElement *le = dlist_get_element_at (dlist, idx);
			return le ? le->data : NULL;
		}
	}

	return NULL;

}

/*** Sorting ***/

// Split a doubly linked dlist (DLL) into 2 DLLs of half sizes 
static ListElement *dllist_split (ListElement *head) { 

	ListElement *fast = head, *slow = head; 

	while (fast->next && fast->next->next) { 
		fast = fast->next->next; 
		slow = slow->next; 
	} 

	ListElement *temp = slow->next; 
	slow->next = NULL; 

	return temp; 

}  

// Function to merge two linked lists 
static ListElement *dllist_merge (int (*compare)(const void *one, const void *two), 
	ListElement *first, ListElement *second)  { 

	// If first linked dlist is empty 
	if (!first) return second; 
  
	// If second linked dlist is empty 
	if (!second) return first; 

	// Pick the smallest value 
	if (compare (first->data, second->data) <= 0) {
		first->next = dllist_merge (compare, first->next, second); 
		first->next->prev = first; 
		first->prev = NULL; 
		return first; 
	}

	else {
		second->next = dllist_merge (compare, first, second->next); 
		second->next->prev = second; 
		second->prev = NULL; 
		return second; 
	}

} 

// merge sort
static ListElement *dlist_merge_sort (ListElement *head, 
	int (*compare)(const void *one, const void *two)) {

	if (!head || !head->next) return head;

	ListElement *second = dllist_split (head);

	// recursivly sort each half
	head = dlist_merge_sort (head, compare);
	second = dlist_merge_sort (second, compare);

	// merge the two sorted halves 
	return dllist_merge (compare, head, second);

}

// uses merge sort to sort the list using the comparator
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
// return 0 on succes 1 on error
int dlist_sort (DoubleList *dlist, int (*compare)(const void *one, const void *two)) {

	int retval = 1;

	if (dlist && dlist->compare) {
		int (*comp)(const void *one, const void *two) = compare ? compare : dlist->compare;

		if (comp) {
			pthread_mutex_lock (dlist->mutex);

			dlist->start = dlist_merge_sort (dlist->start, comp);
			retval = 0;

			pthread_mutex_unlock (dlist->mutex);
		}
	}

	return retval;

}

/*** Other ***/

void **dlist_to_array (DoubleList *dlist, size_t *count) {

	void **array = NULL;

	if (dlist) {
		array = (void **) calloc (dlist->size, sizeof (void *));
		if (array) {
			unsigned int idx = 0;
			ListElement *ptr = dlist_start (dlist);
			while (ptr != NULL) {
				array[idx] = ptr->data;

				ptr = ptr->next;
				idx++;
			}

			if (count) *count = dlist->size;
		}
	}

	return array;

}