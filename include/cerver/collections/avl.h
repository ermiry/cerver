#ifndef _COLLECTIONS_AVL_H_
#define _COLLECTIONS_AVL_H_

#include <stdbool.h>

typedef struct AVLNode {

	void *id;
	char balance;
	struct AVLNode* left, *right;

} AVLNode;

typedef int (*Comparator)(const void*, const void*);

typedef struct AVLTree {

	AVLNode* root;
	Comparator comparator;
	void (*destroy)(void *data);

} AVLTree;

// creates a new avl tree
extern AVLTree *avl_new (void);

// deletes an avl tree and all of its values, the clear tree method is called
extern void avl_delete (AVLTree *tree);

// sets the avl tree comparator function that will be used for the balance factor
extern void avl_set_comparator (AVLTree *tree, Comparator comparator);

// sets the avl tree destroy function that will be used to delete the data inside the tree
// if it is NULL, the values will remain in memory, so you will need to free them yourself
extern void avl_set_destroy (AVLTree *tree, void (*destroy)(void *data));

// creates and inits a new avl tree
extern AVLTree *avl_init (Comparator comparator, void (*destroy)(void *data));

// removes all nodes from an avl tree, the node nada is only destroyed if a destroy method is set
extern void avl_clear_tree (AVLNode **node, void (*destroy)(void *data));

// check if the tree is empty
extern bool avl_is_empty (AVLTree *tree);

// get the node data associated with that id
extern void *avl_get_node_data (AVLTree *tree, void *id);

// inserts a new node in the tree
extern void avl_insert_node (AVLTree *tree, void *data);

// removes a node from the tree associated iwth the data
extern void *avl_remove_node (AVLTree *tree, void *data);

// checks if a node is in the tree
extern bool avl_node_in_tree (AVLTree *tree, void *id);

#endif
