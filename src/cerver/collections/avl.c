#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include "cerver/collections/avl.h"

unsigned int avl_clear_tree (AVLTree *tree, void (*destroy)(void *data));

static AVLTree *avl_new (void) {

	AVLTree *tree = (AVLTree *) malloc (sizeof (AVLTree));
	if (tree) {
		tree->root = NULL;
		tree->comparator = NULL;
		tree->destroy = NULL;

		tree->mutex = NULL;
	}

	return tree;

}

void avl_delete (AVLTree *tree) {

	if (tree) {
		avl_clear_tree (tree, tree->destroy);

		pthread_mutex_destroy (tree->mutex);
		free (tree->mutex);

		free (tree);
	}

}

void avl_set_comparator (AVLTree *tree, Comparator comparator) { if (tree) tree->comparator = comparator; }

void avl_set_destroy (AVLTree *tree, void (*destroy)(void *data)) { if (tree) tree->destroy = destroy; }

AVLTree *avl_init (Comparator comparator, void (*destroy)(void *data)) {

	AVLTree *tree = avl_new ();
	if (tree) {
		tree->comparator = comparator;
		if (destroy) tree->destroy = destroy;

		tree->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (tree->mutex, NULL);
	}

	return tree;

}

static void avl_internal_clear_tree (AVLNode **node, void (*destroy)(void *data)) {

	if (*node) {
		AVLNode *ptr = *node;
		avl_internal_clear_tree (&(ptr->right), destroy);
		avl_internal_clear_tree (&(ptr->left), destroy);

		if (destroy) destroy (ptr->id);
		// else free (ptr->id);

		free (ptr);
		*node = NULL;
	}

}

// removes all nodes from an avl tree, the node nada is only destroyed if a destroy method is set
// returns 0 on success, 1 on error
unsigned int avl_clear_tree (AVLTree *tree, void (*destroy)(void *data)) {

	if (tree) {
		pthread_mutex_lock (tree->mutex);

		avl_internal_clear_tree (&tree->root, tree->destroy);

		pthread_mutex_unlock (tree->mutex);

		return 0;	// success
	}

	return 1; 		// error

}

bool avl_is_empty (AVLTree *tree) { return (tree->root ? true : false ); }

// returns content of required node
// option to pass a different comparator than the one that was originally set
void *avl_get_node_data (AVLTree *tree, void *id, Comparator comparator) {

	if (tree && id) {
		Comparator comp = comparator ? comparator : tree->comparator;

		if (comp) {
			AVLNode *node = tree->root;
			while (node != NULL) {
				switch (comp (node->id, id)) {
					case 0: return node->id; 
					case 1: node = node->left; break;
					case -1: node = node->right; break;
					default: return NULL;
				}
			}
		}
	}

	return NULL;

}

static void avl_insert_node_r (AVLNode **parent, Comparator comparator, void *id, char *flag) ;

// user function for insertion
void avl_insert_node (AVLTree *tree, void *data) {

	if (tree && data) {
		char flag = 0;

		pthread_mutex_lock (tree->mutex);

		avl_insert_node_r (&(tree->root), tree->comparator, data, &flag);

		pthread_mutex_unlock (tree->mutex);
	}

}

static void *avl_remove_node_r (AVLTree *tree, AVLNode **parent, Comparator comparator, void *id, char *flag);

// user function to remove a node
void *avl_remove_node (AVLTree *tree, void *data) {

	void *retval = NULL;

	if (tree && data) {
		char flag = 0;

		pthread_mutex_lock (tree->mutex);

		retval = avl_remove_node_r (tree, &tree->root, tree->comparator, data, &flag);

		pthread_mutex_unlock (tree->mutex);
	}

	return retval;

}

// returns if the node is in the tree
bool avl_node_in_tree (AVLTree *tree, void *id) {

	AVLNode *node = tree->root;
	while (node) {
		switch (tree->comparator (node->id, id)) {
			case 0: return true;
			case 1: node = node->left;
			case -1: node = node ->right;
			default: return false;
		}
	}

	return false;

}

#pragma regigion internal

static AVLNode *avl_node_new (void *data) {

	AVLNode *node = (AVLNode *) malloc (sizeof (AVLNode));

	if (node) {
		node->id = data;
		// memcpy (node->id, data, sizeof (data));
		node->left = NULL;
		node->right = NULL;
		node->balance = 0;
	}

	return node;

}

static void avl_right_rotation (AVLNode **parent) {

	AVLNode *aux = (*parent)->left;

	(*parent)->left = aux->right;
	aux->right = *parent;
	*parent = aux;

}

static void avl_left_rotation (AVLNode **parent) {

	AVLNode *aux = (*parent)->right;

	(*parent)->right = aux->left;
	aux->left = *parent;
	*parent = aux;

}

// fix balance based on given information
static void avl_balance_fix (AVLNode *parent, int dependency) {

	parent->balance = 0;

	switch (dependency) {
		case -1:
			parent->left->balance  = 0;
			parent->right->balance = 1;
			break;
		case 0:
			parent->left->balance  = 0;
			parent->right->balance = 0;
			break;
		case 1:
			parent->left->balance  = -1;
			parent->right->balance = 0;
			break;
		default: break;
	}

}

// verify and treat an insertion in right subtree
static void avl_treat_left_insertion (AVLNode **parent, char *flag) {

	(*parent)->balance--;

	switch ((*parent)->balance) {
		case 0: *flag = 0; break;
		case -2:
			if ((*parent)->left->balance == -1) {
				avl_right_rotation (parent);
				(*parent)->balance = 0;
				(*parent)->right->balance = 0;
			} 
			
			else {
				int balance = (*parent)->left->right->balance;
				avl_left_rotation (&(*parent)->left);
				avl_right_rotation (parent);
				avl_balance_fix (*parent, balance);
			}
			*flag = 0;
			break;
		default: break;
	}

}

// verify and treat a insertion in left subtree
static void avl_treat_right_insertion (AVLNode **parent, char *flag) {

	(*parent)->balance++;

	switch ((*parent)->balance) {
		case 0: *flag = 0; break;
		case 2:
			if ((*parent)->right->balance == 1) {
				avl_left_rotation (parent);
				(*parent)->balance = 0;
				(*parent)->left->balance = 0;
			} 
			
			else {
				int balance = (*parent)->right->left->balance;
				avl_right_rotation (&(*parent)->right);
				avl_left_rotation (parent);
				avl_balance_fix (*parent, balance);
			}
			*flag = 0;
			break;
		default: break;
	}
}

// verify and treat a removal in left subtree
static void avl_treat_left_reduction (AVLNode **parent, char *flag) {

	(*parent)->balance++;

	short shortCut;
	switch ((*parent)->balance) {
		case 1: *flag = 0; break;
		case 2:
			shortCut = (*parent)->right->balance;
			switch (shortCut) {
				case 1:
					avl_left_rotation (parent);
					(*parent)->balance = 0;
					(*parent)->left->balance = 0;
					*flag = 1;
					break;
				case 0:
					avl_left_rotation (parent);
					(*parent)->balance = -1;
					(*parent)->left->balance = 1;
					*flag = 0;
					break;
				case -1:
					shortCut = (*parent)->right->left->balance;
					avl_right_rotation (&(*parent)->right);
					avl_left_rotation (parent);
					avl_balance_fix (*parent, shortCut);
					*flag = 1;
					break;
			}
			break;
		default: break;
	}

}

// verify and treat a removal in right subtree
static void avl_treat_right_reduction (AVLNode **parent, char *flag) {

	(*parent)->balance--;

	short shortCut;
	switch ((*parent)->balance){
		case -1: *flag = 0; break;
		case -2:
			shortCut = (*parent)->left->balance;
			switch (shortCut) {
				case -1:
					avl_right_rotation (parent);
					(*parent)->balance = 0;
					(*parent)->right->balance = 0;
					*flag = 1;
					break;
				case 0:
					avl_right_rotation(parent);
					(*parent)->balance = 1;
					(*parent)->right->balance = -1;
					*flag = 0;
					break;
				case 1:
					shortCut = (*parent)->left->right->balance;
					avl_left_rotation (&(*parent)->left);
					avl_right_rotation (parent);
					avl_balance_fix (*parent, shortCut);
					*flag  = 1;
					break;
			}
			break;
		default: break;
	}

}

// recursive function to insert in the tree
static void avl_insert_node_r (AVLNode **parent, Comparator comparator, void *id, char *flag) {

	// we are on a leaf, create node and set flag
	if (*parent == NULL){
		*parent = avl_node_new (id);
		*flag = 1;
	} 
	
	else {
		switch (comparator ((*parent)->id, id)){
			// go left
			case 1:
				avl_insert_node_r (&(*parent)->left, comparator, id, flag);
				if (*flag == 1) avl_treat_left_insertion (&(*parent), flag);
				break;

			// go right
			case -1:
			case  0:
				avl_insert_node_r (&(*parent)->right, comparator, id, flag);
				if (*flag == 1) avl_treat_right_insertion (&(*parent), flag);
				break;

			default: break;
		}
	}

}

// recursive function to remove a node from the tree
static void *avl_remove_node_r (AVLTree *tree, AVLNode **parent, Comparator comparator, void *id, char *flag) {

	//Wrong way
	if (*parent != NULL) {
		switch (comparator ((*parent)->id, id)) {
			case 1: {
				void *data = avl_remove_node_r (tree, &(*parent)->left, comparator, id, flag);
				if (*flag == 1) avl_treat_left_reduction (&(*parent), flag);
				return data;
			}   
			case -1: {
				void *data = avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
				if (*flag == 1) avl_treat_right_reduction (&(*parent), flag);
				return data;
			}
			case 0:
				if ((*parent)->right != NULL && (*parent)->left != NULL) {
					AVLNode *ptr = (*parent)->right, copy = *(*parent);

					while (ptr->left != NULL) ptr = ptr->left;

					(*parent)->id = ptr->id;
					ptr->id = copy.id;

					return avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
				}
				
				else {
					void *data = NULL;

					if ((*parent)->left != NULL) {
						AVLNode *p = (*parent)->left;
						*(*parent) = *(*parent)->left;
						free (p);

					} 
					
					else if ((*parent)->right != NULL) {
						AVLNode* p = (*parent)->right;
						*(*parent) = *(*parent)->right;
						free (p);
					} 
					
					else {
						// if (tree->destroy) tree->destroy ((*parent)->id);
						// else free ((*parent)->id);
						data = (*parent)->id;
						free (*parent);
						*parent = NULL;
					}
					*flag = 1;
					return data;
				}
				break;

			default: break;
		}
	}

	return NULL;

}

#pragma endregion