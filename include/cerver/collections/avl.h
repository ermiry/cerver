#ifndef _COLLECTIONS_AVL_H_
#define _COLLECTIONS_AVL_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/types/types.h"

typedef struct AVLNode {

	void *id;
	char balance;
	struct AVLNode *left, *right;

} AVLNode;

typedef struct AVLTree {

	AVLNode *root;

	size_t size;

	Comparator comparator;
	void (*destroy)(void *data);

	pthread_mutex_t *mutex;

} AVLTree;

// deletes an avl tree - the clear tree method is called
// nodes' data is only deleted if the destroy method was set
extern void avl_delete (AVLTree *tree);

// sets the avl tree comparator function
// that will be used for the balance factor
extern void avl_set_comparator (
	AVLTree *tree, Comparator comparator
);

// sets the avl tree destroy function that will be used to delete the data inside the tree
// if it is NULL, the values will remain in memory, so you will need to free them yourself
extern void avl_set_destroy (
	AVLTree *tree, void (*destroy)(void *data)
);

// creates and inits a new avl tree
extern AVLTree *avl_init (
	Comparator comparator, void (*destroy)(void *data)
);

// returns TRUE if the tree is empty
extern bool avl_is_empty (AVLTree *tree);

// returns TRUE if the tree is NOT empty
extern bool avl_is_not_empty (AVLTree *tree);

// returns the number of elements that are currently inserted in the tree
extern size_t avl_size (const AVLTree *avl);

// returns content of the required node
// option to pass a different comparator than the one that was originally set
extern void *avl_get_node_data (
	AVLTree *tree, void *id, Comparator comparator
);

// works as avl_get_node_data () but this method is thread safe
// will lock tree mutex to perform search
extern void *avl_get_node_data_safe (
	AVLTree *tree, void *id, Comparator comparator
);

// inserts a new node in the tree
// returns 0 on success, 1 on error
extern unsigned int avl_insert_node (
	AVLTree *tree, void *data
);

// removes a node from the tree associated with the data
// the node's data is returned if removed from tree (if it was found)
extern void *avl_remove_node (
	AVLTree *tree, void *data
);

// removes all nodes from an avl tree
// the nodes data is only destroyed if a destroy method is set
// ability to use a custom destroy method
// other than the one set in avl_init () or avl_set_destroy ()
// returns 0 on success, 1 on error
extern unsigned int avl_clear_tree (
	AVLTree *tree, void (*destroy)(void *data)
);

#endif
