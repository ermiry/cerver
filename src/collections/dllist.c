#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "collections/dllist.h"

DoubleList *dlist_init (void (*destroy)(void *data), 
    int (*compare)(void *one, void *two)) {

    DoubleList *list = (DoubleList *) malloc (sizeof (DoubleList));

    if (list) {
        memset (list, 0, sizeof (DoubleList));
        list->destroy = destroy;
        list->compare = compare; 
    }

    return list;

}

void dlist_reset (DoubleList *list) {

    if (list) {
        if (LIST_SIZE (list) > 0) {
            void *data = NULL;
            while (LIST_SIZE (list) > 0) {
                data = dlist_remove_element (list, NULL);
                if (data != NULL && list->destroy != NULL) list->destroy (data);
            }
        }

        list->start = NULL;
        list->end = NULL;
        list->size = 0;
    }

}

// only gets rid of the List elemenst, but the data is kept
// this is used if another list or structure points to the same data
void dlist_clean (DoubleList *list) {

    if (list) {
        void *data = NULL;
        while (LIST_SIZE (list) > 0) 
            data = dlist_remove_element (list, NULL);

        free (list);
    }

}

void dlist_destroy (DoubleList *list) {

    if (list) {
        if (list->size > 0) {
            void *data = NULL;

            while (LIST_SIZE (list) > 0) {
                data = dlist_remove_element (list, NULL);
                if (data) {
                    if (list->destroy) list->destroy (data);
                    else free (data);
                }
            }
        }

        free (list);
    }

}

/*** ELEMENTS ***/

bool dlist_insert_after (DoubleList *list, ListElement *element, void *data) {

    if (list && data) {
        ListElement *new;
        if ((new = (ListElement *) malloc (sizeof (ListElement))) == NULL) 
            return false;

        new->data = (void *) data;

        if (element == NULL) {
            if (LIST_SIZE (list) == 0) list->end = new;
            else list->start->prev = new;
        
        new->next = list->start;
        new->prev = NULL;
        list->start = new;
        }

        else {
            if (element->next == NULL) list->end = new;

            new->next = element->next;
            new->prev = element;
            element->next = new;
        }

        list->size++;

        return true;
    }

    return false;

}

// removes the list element from the list and returns the data
void *dlist_remove_element (DoubleList *list, ListElement *element) {

    if (list) {
        ListElement *old;
        void *data = NULL;

        if (LIST_SIZE (list) == 0) return NULL;

        if (element == NULL) {
            data = list->start->data;
            old = list->start;
            list->start = list->start->next;
            if (list->start != NULL) list->start->prev = NULL;
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
                // we are at the start of the list
                if (prevElement == NULL) {
                    if (nextElement != NULL) nextElement->prev = NULL;
                    list->start = nextElement;
                }

                // we are at the end of the list
                if (nextElement == NULL) {
                    if (prevElement != NULL) prevElement->next = NULL;
                    list->end = prevElement;
                }
            }
        }

        free (old);
        list->size--;

        return data;
    }

    return NULL;

}

/*** TRAVERSING --- SEARCHING ***/

void *dlist_search (DoubleList *list, void *data) {

    if (list && data) {
        ListElement *ptr = LIST_START (list);
        while (ptr != NULL) {
            if (ptr->data == data) return ptr->data;
            ptr = ptr->next;
        }

        return NULL;    // not found
    }

    return NULL;    

}

bool dlist_is_in_list (DoubleList *list, void *data) {

    if (list && data) {
        ListElement *ptr = LIST_START (list);
        while (ptr != NULL) {
            if (ptr->data == data) return true;
            ptr = ptr->next;
        }

        return false;   // not found
    }

    return NULL;

}

// searches the list and returns the list element associated with the data
ListElement *dlist_get_ListElement (DoubleList *list, void *data) {

    if (list && data) {
        ListElement *ptr = LIST_START (list);
        while (ptr != NULL) {
            if (ptr->data == data) return ptr;
            ptr = ptr->next;
        }

        return NULL;    // not found
    }

    return NULL;    

}

/*** SORTING ***/

// Split a doubly linked list (DLL) into 2 DLLs of half sizes 
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
static ListElement *dllist_merge (int (*compare)(void *one, void *two), 
    ListElement *first, ListElement *second)  { 

    // If first linked list is empty 
    if (!first) return second; 
  
    // If second linked list is empty 
    if (!second) return first; 

    // Pick the smallest value 
    if (!compare (first->data, second->data)) {
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
ListElement *dlist_merge_sort (ListElement *head, int (*compare)(void *one, void *two)) {

    if (!head || !head->next) return head;

    ListElement *second = dllist_split (head);

    // recursivly sort each half
    head = dlist_merge_sort (head, compare);
    second = dlist_merge_sort (second, compare);

    // merge the two sorted halves 
    return dllist_merge (compare, head, second);

}

int dlist_sort (DoubleList *list) {

    int retval = 1;

    if (list && list->compare) {
        list->start = dlist_merge_sort (list->start, list->compare);
        retval = 0;
    }

    return retval;

}