#include <stdlib.h>

#include "cerver/collections/dllist.h"

void *dlist_remove_element (DoubleList *dlist, ListElement *element);

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
    }

    return dlist;

}

void dlist_delete (void *dlist_ptr) {

    if (dlist_ptr) {
        DoubleList *dlist = (DoubleList *) dlist_ptr;

        if (dlist->size > 0) {
            void *data = NULL;

            while (dlist_size (dlist) > 0) {
                data = dlist_remove_element (dlist, NULL);
                if (data) {
                    if (dlist->destroy) dlist->destroy (data);
                    else free (data);
                }
            }
        }

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
    }

    return dlist;

}

// destroys all of the dlist's elements and their data but keeps the dlist
void dlist_reset (DoubleList *dlist) {

    if (dlist) {
        if (dlist_size (dlist) > 0) {
            void *data = NULL;
            while (dlist_size (dlist) > 0) {
                data = dlist_remove_element (dlist, NULL);
                if (data != NULL && dlist->destroy != NULL) dlist->destroy (data);
            }
        }

        dlist->start = NULL;
        dlist->end = NULL;
        dlist->size = 0;
    }

}

// only gets rid of the list elements, but the data is kept
// this is usefull if another dlist or structure points to the same data
void dlist_clean (DoubleList *dlist) {

    if (dlist) {
        void *data = NULL;
        while (dlist_size (dlist) > 0) 
            data = dlist_remove_element (dlist, NULL);
    }

}

/*** Elements ***/

// inserts the data in the double list after the specified element
// returns true on success, false on error or not found
bool dlist_insert_after (DoubleList *dlist, ListElement *element, void *data) {

    if (dlist && data) {
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

            return true;
        }
    }

    return false;

}

// finds the data using the query and the list comparator and the removes it from the list
// and deletes it using the list destroy method
// returns 0 on success, 1 on error or not found
int dlist_remove (DoubleList *dlist, void *query) {

    int retval = 1;

    if (dlist && query) {
        ListElement *ptr = dlist_start (dlist);

        if (dlist->compare) {
            bool first = true;
            while (ptr != NULL) {
                if (!dlist->compare (ptr->data, query)) {
                    // remove the list element
                    void *data = NULL;
                    if (first) data = dlist_remove_element (dlist, NULL);
                    else data = dlist_remove_element (dlist, ptr);
                    if (data) {
                        if (dlist->destroy) dlist->destroy (data);
                        else free (data);

                        retval = 0;
                    }
                }

                ptr = ptr->next;
                first = false;
            }
        }
    }

    return retval;

}

// removes the dlist element from the dlist and returns the data
// NULL for the start of the list
void *dlist_remove_element (DoubleList *dlist, ListElement *element) {

    if (dlist) {
        if (dlist_size (dlist) > 0) {
            ListElement *old;
            void *data = NULL;

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

            return data;
        }
    }

    return NULL;

}

/*** Traversing --- Searching ***/

// uses the list comparator to search using the data as the query
// returns the double list's element data
void *dlist_search (DoubleList *dlist, void *data) {

    if (dlist && data) {
        ListElement *ptr = dlist_start (dlist);

        if (dlist->compare) {
            while (ptr != NULL) {
                if (!dlist->compare (ptr->data, data)) return ptr->data;
                ptr = ptr->next;
            }
        }

        else {
            while (ptr != NULL) {
                if (ptr->data == data) return ptr->data;
                ptr = ptr->next;
            }
        }
    }

    return NULL;    

}

// searches the dlist and returns the dlist element associated with the data
// can use a compartor set in the dlist
ListElement *dlist_get_element (DoubleList *dlist, void *data) {

    if (dlist && data) {
        ListElement *ptr = dlist_start (dlist);

        if (dlist->compare) {
            while (ptr != NULL) {
                if (!dlist->compare (ptr->data, data)) return ptr;
                ptr = ptr->next;
            }
        }

        else {  
            while (ptr != NULL) {
                if (ptr->data == data) return ptr;
                ptr = ptr->next;
            }
        }

        return NULL;    // not found
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

int dlist_sort (DoubleList *dlist) {

    int retval = 1;

    if (dlist && dlist->compare) {
        dlist->start = dlist_merge_sort (dlist->start, dlist->compare);
        retval = 0;
    }

    return retval;

}