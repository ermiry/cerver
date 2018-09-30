/*** This is a custom implementation of a list ***/

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "game.h" 

#include "utils/list.h"

List *initList (void (*destroy)(void *data)) {

    List *list = (List *) malloc (sizeof (List));

    if (list != NULL) {
        list->size = 0;
        list->destroy = destroy;
        list->start = NULL;
        list->end = NULL;
    }

    return list;

}

// just remove the element from the list and return the data
// this is used to send the removed data to the object pool
void *removeElement (List *list, ListElement *element) {

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

void destroyList (List *list) {

    if (list->size > 0) {
        void *data = NULL;

        while (LIST_SIZE (list) > 0) {
            data = removeElement (list, NULL);
            if (data != NULL && list->destroy != NULL) list->destroy (data);
        }
    }

    free (list);

}

void resetList (List *list) {

    if (LIST_SIZE (list) > 0) {
        void *data = NULL;
        while (LIST_SIZE (list) > 0) {
            data = removeElement (list, NULL);
            if (data != NULL && list->destroy != NULL) list->destroy (data);
        }
    }

    list->start = NULL;
    list->end = NULL;
    list->size = 0;

}

// only gets rid of the List elemenst, but the data is kept
void cleanUpList (List *list) {

    void *data = NULL;
    while (LIST_SIZE (list) > 0) 
        data = removeElement (list, NULL);

    free (list);

}

bool insertAfter (List *list, ListElement *element, void *data) {

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

/*** TRAVERSING --- SEARCHING ***/

// void *searchById (List *list, GameComponent type, i32 id) {

//     ListElement *ptr = LIST_START (list);
//     while (ptr != NULL) { 
        
//         //if (ptr->data == data) return ptr->data;
//         ptr = ptr->next;
//     }

//     // not found
//     return NULL;

// }

void *searchList (List *list, void *data) {

    ListElement *ptr = LIST_START (list);
    while (ptr != NULL) {
        if (ptr->data == data) return ptr->data;
        ptr = ptr->next;
    }

    // not found
    return NULL;

}

bool isInList (List *list, void *data) {

    ListElement *ptr = LIST_START (list);
    while (ptr != NULL) {
        if (ptr->data == data) {
            fprintf (stdout, "Is in list\n");
            return true;
        } 
        ptr = ptr->next;
    }

    // not found
    return false;

}

// searches the list and returns the list element associated with the data
ListElement *getListElement (List *list, void *data) {

    ListElement *ptr = LIST_START (list);
    while (ptr != NULL) {
        if (ptr->data == data) return ptr;
        ptr = ptr->next;
    }

    // not found
    return NULL;

}

/*** SORTING ***/

// FIXME: as of 27/09/2018 this only works for sorting the leaderboard data!!

// Split a doubly linked list (DLL) into 2 DLLs of half sizes 
/* ListElement *split (ListElement *head) { 

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
ListElement *merge (ListElement *first, ListElement *second)  { 

    // If first linked list is empty 
    if (!first) return second; 
  
    // If second linked list is empty 
    if (!second) return first; 

    u32 firstScore = ((LBEntry *) first->data)->score;
    u32 secondScore = ((LBEntry *) second->data)->score;
  
    // Pick the smallest value 
    if (firstScore < secondScore)  { 
        first->next = merge (first->next, second); 
        first->next->prev = first; 
        first->prev = NULL; 
        return first; 
    } 

    else { 
        second->next = merge (first,second->next); 
        second->next->prev = second; 
        second->prev = NULL; 
        return second; 
    } 

} 
  
ListElement *mergeSort (ListElement *head) {

    if (!head || !head->next) return head;

    ListElement *second = split (head);

    // recursivly sort each half
    head = mergeSort (head);
    second = mergeSort (second);

    // Merge the two sorted halves 
    return merge (head, second);

} */