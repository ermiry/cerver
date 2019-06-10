#ifndef _DLIST_H_
#define _DLIST_H_

#include <stdlib.h>
#include <stdbool.h>

typedef struct ListElement {

    struct ListElement *prev;
    void *data;
    struct ListElement *next;

} ListElement;

typedef struct List {

    size_t size;

    ListElement *start;
    ListElement *end;

    void (*destroy)(void *data);
    int (*compare)(void *one, void *two);

} DoubleList;

#define LIST_SIZE(list) ((list)->size)

#define LIST_START(list) ((list)->start)
#define LIST_END(list) ((list)->end)

#define LIST_DATA(element) ((element)->data)
#define LIST_NEXT(element) ((element)->next)

// compare must return 0 if one < two otherwise return 1
extern DoubleList *dlist_init (void (*destroy)(void *data),
    int (*compare)(void *one, void *two));
extern void dlist_reset (DoubleList *);
// only gets rid of the List elemenst, but the data is kept
extern void dlist_clean (DoubleList *);
extern void dlist_destroy (DoubleList *);

// Elements
extern bool dlist_insert_after (DoubleList *, ListElement *, void *data);
extern void *dlist_remove_element (DoubleList *, ListElement *);

// Searching
extern void *dlist_search (DoubleList *, void *data);
extern bool dlist_is_in_list (DoubleList *, void *data);
ListElement *dlist_get_ListElement (DoubleList *, void *data);

// merge sort
// return 0 on succes 1 on error
extern int dlist_sort (DoubleList *list);

#endif