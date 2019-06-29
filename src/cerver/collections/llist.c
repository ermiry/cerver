#include <stdlib.h>

#include "cerver/collections/llist.h"

static ListNode *llist_node_create (void *data) {

    ListNode *new_node = (ListNode *) malloc (sizeof (ListNode));
    if (new_node) {
        new_node->data = data;
        new_node->next = NULL;
    }

    return new_node;

}

static void llist_node_destroy (ListNode *node, void (*destroy) (void *)) {

    if (node) {
        if (node->data) {
            if (destroy) destroy (node->data);
            else free (node->data);
        }

        free (node);
    }

}

LList *llist_init (void (*destroy) (void *)) {

    LList *new_list = (LList *) malloc (sizeof (LList));
    if (new_list) {
        new_list->size = 0;
        new_list->start = NULL;
        new_list->end = NULL;
        new_list->destroy = destroy;
    }

    return new_list;

}

void llist_destroy (LList *list) {

    if (list) {
        if (list->size > 0) {
            ListNode *ptr = list->start, *next;
            while (ptr) {
                next = ptr->next;
                llist_node_destroy (ptr, list->destroy);
                ptr = next;
            }
        }
        
        free (list);
    }

}

void llist_insert_next (LList *list, ListNode *node, void *data) {

    ListNode *new_node = llist_node_create (data);
    if (new_node) {
        // insert at the start of the list
        if (!node) {
            if (llist_size (list) == 0) list->end = new_node;

            new_node->next = NULL;
            new_node->idx = 0;
            list->start = new_node;
        }

        // insert somewhere else
        else {
            if (!node->next) {
                list->end = new_node;
                new_node->idx = list->size;
            } 

            new_node->next = node->next;
            node->next = new_node;
            new_node->idx = node->next->idx;

            int newIdx = new_node->idx + 1;
            for (ListNode *n = new_node->next; n != NULL; n = n->next) {
                n->idx = newIdx;
                newIdx++;
            }
        }

        list->size++;
    }

}

void *llist_remove (LList *list, ListNode *node) {

    void *retval = NULL;

    if (list && list->size > 0) {
        ListNode *old = NULL;
    
        // remove the start of the list
        if (!node) {
            retval = list->start->data;
            old = list->start;
            list->start = list->start->next;

            int newIdx = 0;
            for (ListNode *n = list->start; n != NULL; n = n->next) {
                n->idx = 0;
                newIdx++;
            }
        }

        else {
            retval = node->data;
            old = node;
            
            // get the previous node
            ListNode *prevNode = list->start;
            int i = 0;
            while (i < (node->idx - 1)) {
                prevNode = prevNode->next;
                i++;
            } 

            prevNode->next = old->next;
            int newIdx = node->idx;
            for (ListNode *n = old->next; n != NULL; n = n->next) {
                n->idx = newIdx;
                newIdx++;
            }
        }

        if (old) free (old);
        list->size--;
    }

    return retval;

}

// searches the list and returns the list node associated with the data
ListNode *llist_get_list_node (LList *list, void *data) {

    if (list && data) {
        ListNode *ptr = llist_start (list);
        while (ptr != NULL) {
            if (ptr->data == data) return ptr;
            ptr = ptr->next;
        }
    }

    return NULL;

}

// TODO: add a node get data
// TODO: add a list sort based on a comparator