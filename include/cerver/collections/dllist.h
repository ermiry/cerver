#ifndef _COLLECTIONS_DLIST_H_
#define _COLLECTIONS_DLIST_H_

#include <stdbool.h>

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

} DoubleList;

#define dlist_size(list) ((list)->size)

#define dlist_start(list) ((list)->start)
#define dlist_end(list) ((list)->end)

#define dlist_element_data(element) ((element)->data)
#define dlist_element_next(element) ((element)->next)

extern void dlist_delete (void *dlist_ptr);

// sets a list compare function
// compare must return -1 if one < two, must return 0 if they are equal, and must return 1 if one > two
extern void dlist_set_compare (DoubleList *list, int (*compare)(const void *one, const void *two));

// sets list destroy function
extern void dlist_set_destroy (DoubleList *list, void (*destroy)(void *data));

// creates a new double list (double linked list)
// destroy is the method used to free up the data, NULL to use the default free
// compare must return -1 if one < two, must return 0 if they are equal, and must return 1 if one > two
extern DoubleList *dlist_init (void (*destroy)(void *data),
    int (*compare)(const void *one, const void *two));

// destroys all of the dlist's elements and their data but keeps the dlist
extern void dlist_reset (DoubleList *);

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
extern void dlist_clean (DoubleList *);

/*** Elements ***/

// inserts the data in the double list after the specified element
// return true on success, false on error
extern bool dlist_insert_after (DoubleList *dlist, ListElement *element, void *data);

// finds the data using the query and the list comparator and the removes it from the list
// and deletes it using the list destroy method
// returns 0 on success, 1 on error or not found
extern int dlist_remove (DoubleList *dlist, void *query);

// removes the dlist element from the dlist and returns the data
// NULL for the start of the list
extern void *dlist_remove_element (DoubleList *dlist, ListElement *element);

/*** Traversing --- Searching ***/

// uses the list comparator to search using the data as the query
// returns the double list's element data
extern void *dlist_search (DoubleList *dlist, void *data);

// searches the dlist and returns the dlist element associated with the data
// can use a compartor set in the dlist
extern ListElement *dlist_get_element (DoubleList *dlist, void *data);

/*** Sorting ***/

// uses merge sort to sort the list using the comparator
// return 0 on succes 1 on error
extern int dlist_sort (DoubleList *list);

#endif