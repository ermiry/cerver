#ifndef _COLLECTIONS_LLIST_H_
#define _COLLECTIONS_LLIST_H_

#include <stdlib.h>

typedef struct ListNode {

    int idx;
    void *data;
    struct ListNode *next;

} ListNode;

typedef struct LList {

    size_t size;

    ListNode *start;
    ListNode *end;
    void (*destroy) (void *);

} LList;

#define llist_size(list) ((list)->size)
#define llist_start(list) ((list)->start)
#define llist_end(list) ((list)->end)
#define llist_data(node) ((node)->data)

extern LList *llist_init (void (*destroy) (void *));
extern void llist_destroy (LList *list);

extern void llist_insert_next (LList *list, ListNode *node, void *data);
extern void *llist_remove (LList *list, ListNode *node);

extern ListNode *llist_get_list_node (LList *list, void *data);

#endif