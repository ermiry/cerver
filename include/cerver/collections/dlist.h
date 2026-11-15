/**
 * @file cerver/collections/dlist.h
 * @brief A DoubleList: a doubly linked list of pointers with its own mutex.
 */

#ifndef _COLLECTIONS_DLIST_H_
#define _COLLECTIONS_DLIST_H_

#include <stdbool.h>

#include <pthread.h>

/**
 * @defgroup collections Collections
 * @brief The data structures of cerver: lists, trees, tables, queues, pools, free lists and arenas.
 *
 * Unless they say otherwise, the collections keep pointers to the data of the caller and they take their own
 * mutex in each method.
 */

/**
 * @defgroup collections_dlist Double list
 * @ingroup collections
 * @brief A doubly linked list of pointers.
 *
 * The methods take the mutex of the list, except the ones that end with _unsafe and the macros, that are for a
 * caller that already holds it or that has the list for itself.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An element of a list.
 */
typedef struct ListElement {

	/** @brief The previous element, NULL at the start. */
	struct ListElement *prev;
	/** @brief The data, the list keeps the pointer. */
	void *data;
	/** @brief The next element, NULL at the end. */
	struct ListElement *next;

} ListElement;

/**
 * @brief A doubly linked list.
 */
typedef struct DoubleList {

	/** @brief The elements of the list. */
	size_t size;

	/** @brief The first element, NULL in an empty list. */
	ListElement *start;
	/** @brief The last element, NULL in an empty list. */
	ListElement *end;

	/** @brief Releases the data of an element, NULL to keep it. */
	void (*destroy)(void *data);
	/** @brief Compares two data, for the searches, the removes, the sort and the inserts in order. */
	int (*compare)(const void *one, const void *two);

	/** @brief Protects the list. */
	pthread_mutex_t *mutex;

} DoubleList;

/**
 * @brief The first element of a list.
 *
 * @param list the list
 */
#define dlist_start(list) ((list)->start)

/**
 * @brief The last element of a list.
 *
 * @param list the list
 */
#define dlist_end(list) ((list)->end)

/**
 * @brief The data of an element.
 *
 * @param element the element
 */
#define dlist_element_data(element) ((element)->data)

/**
 * @brief The next element of an element.
 *
 * @param element the element
 */
#define dlist_element_next(element) ((element)->next)

/**
 * @brief Iterates a list from its start, without its mutex.
 *
 * @param dlist the list
 * @param le the ListElement pointer that takes each element
 */
#define dlist_for_each(dlist, le)					\
	for (le = dlist->start; le; le = le->next)

/**
 * @brief Iterates a list from its end, without its mutex.
 *
 * @param dlist the list
 * @param le the ListElement pointer that takes each element
 */
#define dlist_for_each_backwards(dlist, le)			\
	for (le = dlist->end; le; le = le->prev)

/**
 * @brief Sets the method that compares two data.
 *
 * @param[in,out] list the list, NULL is ignored
 * @param[in] compare returns a negative value if one < two, 0 if they are equal and a positive value if one > two
 */
extern void dlist_set_compare (
	DoubleList *list,
	int (*compare)(const void *one, const void *two)
);

/**
 * @brief Sets the method that releases the data of an element.
 *
 * @param[in,out] list the list, NULL is ignored
 * @param[in] destroy the method, NULL to keep the data
 */
extern void dlist_set_destroy (
	DoubleList *list,
	void (*destroy)(void *data)
);

/**
 * @brief Returns the number of elements of a list.
 *
 * @param[in] dlist the list
 * @return the elements, 0 for a NULL list
 */
extern size_t dlist_size (const DoubleList *dlist);

/**
 * @brief Returns whether a list is empty.
 *
 * @param[in] dlist the list
 * @return true if it has no elements or it is NULL
 */
extern bool dlist_is_empty (const DoubleList *dlist);

/**
 * @brief Returns whether a list has elements.
 *
 * @param[in] dlist the list
 * @return true if it has at least one element, false if it is empty or NULL
 */
extern bool dlist_is_not_empty (const DoubleList *dlist);

/**
 * @brief Releases a list, its elements and, with the destroy method, their data.
 *
 * @param[in] dlist_ptr the list, it can be NULL
 */
extern void dlist_delete (void *dlist_ptr);

/**
 * @brief Releases a list only if it is empty.
 *
 * @param[in] dlist_ptr the list
 * @retval 0 the list was released
 * @retval 1 the list is NULL or it has elements, so it was not released
 */
extern int dlist_delete_if_empty (void *dlist_ptr);

/**
 * @brief Releases a list, its elements and their data only if it is not empty.
 *
 * @param[in] dlist_ptr the list
 * @retval 0 the list was released
 * @retval 1 the list is NULL or empty, so it was not released
 */
extern int dlist_delete_if_not_empty (void *dlist_ptr);

/**
 * @brief Creates an empty list.
 *
 * @param[in] destroy releases the data of an element, NULL to keep it (the data is not released)
 * @param[in] compare returns a negative value if one < two, 0 if they are equal and a positive value if one > two
 * @return the new list, released with dlist_delete (); NULL if it could not be allocated
 */
extern DoubleList *dlist_init (
	void (*destroy)(void *data),
	int (*compare)(const void *one, const void *two)
);

/**
 * @brief Removes every element and releases their data with the destroy method, but keeps the list.
 *
 * @param[in,out] dlist the list, it can be NULL
 */
extern void dlist_reset (DoubleList *dlist);

/**
 * @brief Removes every element but keeps their data, useful when another structure points to the same data.
 *
 * @param[in,out] dlist_ptr the list, it can be NULL
 */
extern void dlist_clear (void *dlist_ptr);

/**
 * @brief Removes every element, keeping their data, and releases the list.
 *
 * @param[in] dlist_ptr the list, it can be NULL
 */
extern void dlist_clear_and_delete (void *dlist_ptr);

/**
 * @brief Removes every element of a list that is not empty, keeping their data; releases a list that is empty.
 *
 * @param[in] dlist_ptr the list, it can be NULL
 * @note It reads the size with the mutex.
 */
extern void dlist_clear_or_delete (void *dlist_ptr);

// insert

/**
 * @brief Inserts data before an element.
 *
 * @param[in,out] dlist the list
 * @param[in] element the element, NULL (or the first element) to insert at the start
 * @param[in] data the data, the list keeps the pointer
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 */
extern int dlist_insert_before (
	DoubleList *dlist,
	ListElement *element, const void *data
);

/**
 * @brief Works as dlist_insert_before (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @param[in] element the element, NULL to insert at the start
 * @param[in] data the data
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 * @threadsafety Not thread safe.
 */
extern int dlist_insert_before_unsafe (
	DoubleList *dlist,
	ListElement *element, const void *data
);

/**
 * @brief Inserts data after an element.
 *
 * @param[in,out] dlist the list
 * @param[in] element the element, NULL to insert at the start
 * @param[in] data the data, the list keeps the pointer
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 */
extern int dlist_insert_after (
	DoubleList *dlist,
	ListElement *element, const void *data
);

/**
 * @brief Works as dlist_insert_after (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @param[in] element the element, NULL to insert at the start
 * @param[in] data the data
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 * @threadsafety Not thread safe.
 */
extern int dlist_insert_after_unsafe (
	DoubleList *dlist,
	ListElement *element, const void *data
);

/**
 * @brief Inserts data at a position.
 *
 * @param[in,out] dlist the list
 * @param[in] data the data, the list keeps the pointer
 * @param[in] pos the position from 0; one equal or bigger than the size inserts at the end
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 */
extern int dlist_insert_at (
	DoubleList *dlist,
	const void *data, const unsigned int pos
);

/**
 * @brief Inserts data at the start of the list.
 *
 * @param[in,out] dlist the list
 * @param[in] data the data, the list keeps the pointer
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 */
extern int dlist_insert_at_start (
	DoubleList *dlist, const void *data
);

/**
 * @brief Works as dlist_insert_at_start (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @param[in] data the data
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 * @threadsafety Not thread safe.
 */
extern int dlist_insert_at_start_unsafe (
	DoubleList *dlist, const void *data
);

/**
 * @brief Inserts data at the end of the list.
 *
 * @param[in,out] dlist the list
 * @param[in] data the data, the list keeps the pointer
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 */
extern int dlist_insert_at_end (
	DoubleList *dlist, const void *data
);

/**
 * @brief Works as dlist_insert_at_end (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @param[in] data the data
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, or the element could not be allocated
 * @threadsafety Not thread safe.
 */
extern int dlist_insert_at_end_unsafe (
	DoubleList *dlist, const void *data
);

/**
 * @brief Inserts data before the first element that is bigger, with the compare method of the list; equal data
 * keeps the order in which it was inserted.
 *
 * @param[in,out] dlist the list, with its compare method
 * @param[in] data the data, the list keeps the pointer
 * @retval 0 the data was inserted
 * @retval 1 a NULL list or data, a list without a compare method, or the element could not be allocated
 */
extern int dlist_insert_in_order (
	DoubleList *dlist, const void *data
);

// remove

/**
 * @brief Removes the first element whose data is equal to a query.
 *
 * @param[in,out] dlist the list
 * @param[in] query the query, compared with the data of each element
 * @param[in] compare returns 0 on a match, NULL to use the compare method of the list
 * @return the data of the element, which is not released; NULL if it was not found or there is no compare method
 */
extern void *dlist_remove (
	DoubleList *dlist,
	const void *query, int (*compare)(const void *one, const void *two)
);

/**
 * @brief Removes an element from the list.
 *
 * @param[in,out] dlist the list
 * @param[in] element an element of the list, NULL for the start
 * @return the data of the element, which is not released; NULL if the list is empty
 */
extern void *dlist_remove_element (
	DoubleList *dlist, ListElement *element
);

/**
 * @brief Works as dlist_remove_element (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @param[in] element an element of the list, NULL for the start
 * @return the data of the element, NULL if the list is empty
 * @threadsafety Not thread safe.
 */
extern void *dlist_remove_element_unsafe (
	DoubleList *dlist, ListElement *element
);

/**
 * @brief Removes the element at the start of the list.
 *
 * @param[in,out] dlist the list
 * @return the data of the element, which is not released; NULL if the list is empty
 */
extern void *dlist_remove_start (DoubleList *dlist);

/**
 * @brief Works as dlist_remove_start (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @return the data of the element, NULL if the list is empty
 * @threadsafety Not thread safe.
 */
extern void *dlist_remove_start_unsafe (DoubleList *dlist);

/**
 * @brief Removes the element at the end of the list.
 *
 * @param[in,out] dlist the list
 * @return the data of the element, which is not released; NULL if the list is empty
 */
extern void *dlist_remove_end (DoubleList *dlist);

/**
 * @brief Works as dlist_remove_end (), without the mutex.
 *
 * @param[in,out] dlist the list
 * @return the data of the element, NULL if the list is empty
 * @threadsafety Not thread safe.
 */
extern void *dlist_remove_end_unsafe (DoubleList *dlist);

/**
 * @brief Removes the element at an index.
 *
 * @param[in,out] dlist the list
 * @param[in] idx the index from 0
 * @return the data of the element, which is not released; NULL if the index is not valid
 */
extern void *dlist_remove_at (
	DoubleList *dlist, const unsigned int idx
);

/**
 * @brief Removes every element whose data matches a condition.
 *
 * @param[in,out] dlist the list
 * @param[in] compare gets the data of an element and the match, and returns true to remove it
 * @param[in] match the second argument of compare
 * @param[in] delete_data true to release the data that is removed with the destroy method of the list, or with
 * free () when the list has none
 * @return the elements that were removed
 */
extern unsigned int dlist_remove_with_condition (
	DoubleList *dlist,
	bool (*compare)(const void *one, const void *two),
	const void *match,
	bool delete_data
);

// traverse --- search

/**
 * @brief Calls a method with the data of each element, from the start.
 *
 * @param[in] dlist the list
 * @param[in] method gets the data of an element and the method args
 * @param[in] method_args the second argument of the method
 * @retval 0 the list was traversed
 * @retval 1 a NULL list or method
 * @note The method runs with the mutex of the list, which is recursive: it can read the same list, but it must not
 * insert or remove elements while the list is traversed.
 */
extern int dlist_traverse (
	const DoubleList *dlist,
	void (*method)(void *list_element_data, void *method_args),
	void *method_args
);

/**
 * @brief Searches the first element whose data is equal to a query.
 *
 * @param[in] dlist the list
 * @param[in] data the query
 * @param[in] compare returns 0 on a match, NULL to use the compare method of the list
 * @return the data of the element, NULL if it was not found or there is no compare method
 */
extern void *dlist_search (
	const DoubleList *dlist,
	const void *data,
	int (*compare)(const void *one, const void *two)
);

/**
 * @brief Searches the first element whose data is equal to a query.
 *
 * @param[in] dlist the list
 * @param[in] data the query
 * @param[in] compare returns 0 on a match, NULL to use the compare method of the list
 * @return the element, NULL if it was not found or there is no compare method
 * @warning The element is returned after the mutex is released: it is only valid while the list is not changed;
 * use dlist_get_apply () to use its data with the mutex.
 */
extern ListElement *dlist_get_element (
	const DoubleList *dlist,
	const void *data,
	int (*compare)(const void *one, const void *two)
);

/**
 * @brief Searches the first element whose data is equal to a query and runs a method with its data while the mutex
 * of the list is held, so another thread can not remove it in between.
 *
 * @param[in] dlist the list
 * @param[in] data the query
 * @param[in] compare returns 0 on a match, NULL to use the compare method of the list
 * @param[in] method gets the data of the element and the method args; it can read the same list (the mutex is
 * recursive) but must not change it
 * @param[in] method_args the second argument of the method
 * @retval 0 the element was found and the method ran
 * @retval 1 it was not found, there is no compare method or an argument is NULL
 * @since 1.9b-2
 */
extern int dlist_get_apply (
	const DoubleList *dlist, const void *data,
	int (*compare)(const void *one, const void *two),
	void (*method)(void *list_element_data, void *method_args), void *method_args
);

/**
 * @brief Returns the element at an index.
 *
 * @param[in] dlist the list
 * @param[in] idx the index from 0
 * @return the element, NULL if the index is not valid
 * @warning The element is returned after the mutex is released: it is only valid while the list is not changed.
 */
extern ListElement *dlist_get_element_at (
	const DoubleList *dlist, const unsigned int idx
);

/**
 * @brief Returns the data of the element at an index.
 *
 * @param[in] dlist the list
 * @param[in] idx the index from 0
 * @return the data, NULL if the index is not valid
 */
extern void *dlist_get_at (
	const DoubleList *dlist, const unsigned int idx
);

// sort

/**
 * @brief Sorts the list with a stable merge sort.
 *
 * @param[in,out] dlist the list
 * @param[in] compare orders two data, NULL to use the compare method of the list
 * @retval 0 the list was sorted
 * @retval 1 a NULL list or there is no compare method
 */
extern int dlist_sort (
	DoubleList *dlist, int (*compare)(const void *one, const void *two)
);

// other

/**
 * @brief Creates an array with the data of the elements; only the pointers are copied.
 *
 * @param[in] dlist the list
 * @param[out] count the elements of the array, 0 if it is NULL; it can be NULL
 * @return the new array, released with free (); NULL for a NULL or an empty list, or if it could not be allocated
 */
extern void **dlist_to_array (
	const DoubleList *dlist, size_t *count
);

/**
 * @brief Creates a list with the same data pointers, the same destroy and the same compare methods.
 *
 * @param[in] dlist the list
 * @return the new list, NULL for a NULL list or if it could not be allocated
 * @warning Both lists point to the same data: clear one of them with dlist_clear () before it is deleted.
 */
extern DoubleList *dlist_copy (const DoubleList *dlist);

/**
 * @brief Creates a list with a clone of the data of each element, the same destroy and the same compare methods.
 *
 * @param[in] dlist the list
 * @param[in] clone gets the data of an element and returns its clone, that the destroy method can release; a NULL
 * clone is not inserted
 * @return the new list, NULL for a NULL argument or if it could not be allocated
 */
extern DoubleList *dlist_clone (
	const DoubleList *dlist, void *(*clone) (const void *original)
);

/**
 * @brief Moves the second half of the list to a new list with the same destroy and compare methods.
 *
 * If the size is odd, the extra element stays in the first half; both lists can be deleted.
 *
 * @param[in,out] dlist the list, it keeps the first half
 * @return the new list with the second half, NULL for a NULL list, one with less than 2 elements, or if it could
 * not be allocated
 */
extern DoubleList *dlist_split_half (DoubleList *dlist);

/**
 * @brief Moves the elements whose data matches a condition to a new list with the same destroy and compare
 * methods.
 *
 * @param[in,out] dlist the list
 * @param[in] compare gets the data of an element and the match, and returns true to move it
 * @param[in] match the second argument of compare
 * @return the new list, empty if nothing matched; NULL for a NULL list or compare, or if it could not be allocated
 */
extern DoubleList *dlist_split_with_condition (
	DoubleList *dlist,
	bool (*compare)(const void *one, const void *two),
	const void *match
);

/**
 * @brief Moves the elements of a list to the end of another.
 *
 * @param[in,out] one the list that gets the elements
 * @param[in,out] two the list that is emptied, it can be deleted after
 * @note A NULL list, or the same list twice, is ignored.
 */
extern void dlist_merge_two (DoubleList *one, DoubleList *two);

/**
 * @brief Moves the elements of two lists whose data matches a condition to a new list, with the destroy and the
 * compare methods of the first one.
 *
 * @param[in,out] one the first list
 * @param[in,out] two the second list, it can be the same as one
 * @param[in] compare gets the data of an element and the match, and returns true to move it
 * @param[in] match the second argument of compare
 * @return the new list with the matches, NULL for a NULL argument or if it could not be allocated
 */
extern DoubleList *dlist_merge_two_with_condition (
	DoubleList *one, DoubleList *two,
	bool (*compare)(const void *one, const void *two),
	const void *match
);

/**
 * @brief Moves the elements of a list of lists to a new list, with the destroy and the compare methods of the
 * first one.
 *
 * @param[in,out] many_dlists a list whose data are lists; they are emptied and can be deleted after
 * @return the new list, NULL for a NULL or an empty list, or if it could not be allocated
 */
extern DoubleList *dlist_merge_many (DoubleList *many_dlists);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
