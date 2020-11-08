#ifndef _COLLECTIONS_DLIST_H_
#define _COLLECTIONS_DLIST_H_

#include <stdbool.h>

#include <pthread.h>

typedef struct ListElement {

	struct ListElement *prev;
	void *data;
	struct ListElement *next;

} ListElement;

typedef struct DoubleList {

	size_t size;

	ListElement *start;
	ListElement *end;

	void (*destroy)(void *data);
	int (*compare)(const void *one, const void *two);

	pthread_mutex_t *mutex;

} DoubleList;

#define dlist_start(list) ((list)->start)
#define dlist_end(list) ((list)->end)

#define dlist_element_data(element) ((element)->data)
#define dlist_element_next(element) ((element)->next)

// sets a list compare function
// compare must return -1 if one < two, must return 0 if they are equal, and must return 1 if one > two
extern void dlist_set_compare (
	DoubleList *list,
	int (*compare)(const void *one, const void *two)
);

// sets list destroy function
extern void dlist_set_destroy (DoubleList *list, void (*destroy)(void *data));

// thread safe method to get the dlist's size
extern size_t dlist_size (const DoubleList *dlist);

extern bool dlist_is_empty (const DoubleList *dlist);

extern bool dlist_is_not_empty (const DoubleList *dlist);

extern void dlist_delete (void *dlist_ptr);

// only deletes the list if its empty (size == 0)
// returns 0 on success, 1 on NOT deleted
extern int dlist_delete_if_empty (void *dlist_ptr);

// only deletes the list if its NOT empty (size > 0)
// returns 0 on success, 1 on NOT deleted
extern int dlist_delete_if_not_empty (void *dlist_ptr);

// creates a new double list (double linked list)
// destroy is the method used to free up the data, NULL to use the default free
// compare must return -1 if one < two, must return 0 if they are equal, and must return 1 if one > two
extern DoubleList *dlist_init (
	void (*destroy)(void *data),
	int (*compare)(const void *one, const void *two)
);

// destroys all of the dlist's elements and their data but keeps the dlist
extern void dlist_reset (DoubleList *dlist);

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
extern void dlist_clear (void *dlist_ptr);

// clears the dlist - only gets rid of the list elements, but the data is kept
// and then deletes the dlist
extern void dlist_clear_and_delete (void *dlist_ptr);

// clears the dlist if it is NOT empty
// deletes the dlist if it is EMPTY
extern void dlist_clear_or_delete (void *dlist_ptr);

/*** insert ***/

// inserts the data in the double list BEFORE the specified element
// if element == NULL, data will be inserted at the start of the list
// returns 0 on success, 1 on error
extern int dlist_insert_before (DoubleList *dlist, ListElement *element, const void *data);

// inserts the data in the double list AFTER the specified element
// if element == NULL, data will be inserted at the start of the list
// returns 0 on success, 1 on error
extern int dlist_insert_after (DoubleList *dlist, ListElement *element, const void *data);

// inserts the data in the double list in the specified pos (0 indexed)
// if the pos is greater than the current size, it will be added at the end
// returns 0 on success, 1 on error
extern int dlist_insert_at (DoubleList *dlist, const void *data, const unsigned int pos);

// inserts at the start of the dlist, before the first element
// returns 0 on success, 1 on error
extern int dlist_insert_at_start (DoubleList *dlist, const void *data);

// inserts at the end of the dlist, after the last element
// returns 0 on success, 1 on error
extern int dlist_insert_at_end (DoubleList *dlist, const void *data);

/*** remove ***/

// finds the data using the query and the list comparator and the removes it from the list
// and returns the list element's data
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
extern void *dlist_remove (
	DoubleList *dlist, 
	const void *query, int (*compare)(const void *one, const void *two)
);

// removes the dlist element from the dlist and returns the data
// NULL for the start of the list
extern void *dlist_remove_element (DoubleList *dlist, ListElement *element);

// removes the dlist element from the dlist at the specified index 
// returns the data or NULL if index was invalid
extern void *dlist_remove_at (DoubleList *dlist, const unsigned int idx);

/*** Traversing --- Searching ***/

// traverses the dlist and for each element, calls the method by passing the list element data and the method args as both arguments
// this method is thread safe
// returns 0 on success, 1 on error
extern int dlist_traverse (
	const DoubleList *dlist, 
	void (*method)(void *list_element_data, void *method_args),
	void *method_args
);

// uses the list comparator to search using the data as the query
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
// returns the double list's element data
extern void *dlist_search (
	const DoubleList *dlist,
	const void *data,
	int (*compare)(const void *one, const void *two)
);

// searches the dlist and returns the dlist element associated with the data
// option to pass a custom compare method for searching
extern ListElement *dlist_get_element (
	const DoubleList *dlist,
	const void *data, 
	int (*compare)(const void *one, const void *two)
);

// traverses the dlist and returns the list element at the specified index
extern ListElement *dlist_get_element_at (
	const DoubleList *dlist, const unsigned int idx
);

// traverses the dlist and returns the data of the list element at the specified index
extern void *dlist_get_at (const DoubleList *dlist, const unsigned int idx);

/*** Sorting ***/

// uses merge sort to sort the list using the comparator
// option to pass a custom compare method for searching, if NULL, dlist's compare method will be used
// return 0 on succes 1 on error
extern int dlist_sort (
	DoubleList *dlist, int (*compare)(const void *one, const void *two)
);

/*** Other ***/

// returns a newly allocated array with the list elements inside it
// data will not be copied, only the pointers, so the list will keep the original elements
extern void **dlist_to_array (const DoubleList *dlist, size_t *count);

// returns a exact copy of the dlist
// creates the dlist's elements using the same data pointers as in the original dlist
// be carefull which dlist you delete first, as the other should use dlist_clear first before delete
// the new dlist's delete and comparator methods are set from the original
extern DoubleList *dlist_copy (const DoubleList *dlist);

// returns a exact clone of the dlist
// the element's data are created using your clone method
	// which takes as the original each element's data of the dlist
	// and should return the same structure type as the original method that can be safely deleted
	// with the dlist's delete method
// the new dlist's delete and comparator methods are set from the original
extern DoubleList *dlist_clone (
	const DoubleList *dlist, void *(*clone) (const void *original)
);

// splits the original dlist into two halfs
// if dlist->size is odd, extra element will be left in the first half (dlist)
// both lists can be safely deleted
// the new dlist's delete and comparator methods are set from the original
extern DoubleList *dlist_split_half (DoubleList *dlist);

#endif