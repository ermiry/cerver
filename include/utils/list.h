#ifndef LIST_H
#define LIST_H

#include <stdbool.h>

typedef struct ListElement {

    struct ListElement *prev;
    void *data;
    struct ListElement *next;

} ListElement;

typedef struct List {

    unsigned int size;

    void (*destroy)(void *data);

    ListElement *start;
    ListElement *end;

} List;

#define LIST_SIZE(list) ((list)->size)

#define LIST_START(list) ((list)->start)
#define LIST_END(list) ((list)->end)

#define LIST_DATA(element) ((element)->data)
#define LIST_NEXT(element) ((element)->next)


extern List *initList (void (*destroy)(void *data));
extern void *removeElement (List *, ListElement *);
extern void destroyList (List *);
extern void resetList (List *);
extern bool insertAfter (List *, ListElement *, void *data);

// only gets rid of the List elemenst, but the data is kept
extern void cleanUpList (List *);

// SEARCHING
extern void *searchList (List *, void *data);
extern bool isInList (List *, void *data);
ListElement *getListElement (List *, void *data);

/*** SORTING ***/

// extern ListElement *mergeSort (ListElement *head);

#endif