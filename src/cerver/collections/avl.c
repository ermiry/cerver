#include <stdlib.h>
#include <stdbool.h>

#include <pthread.h>

#include "cerver/collections/avl.h"

static void *avl_internal_get_node_data (AVLTree *tree, void *id, Comparator comparator);
static void avl_internal_clear_tree (AVLNode **node, void (*destroy)(void *data));

static unsigned int avl_insert_node_r (AVLNode **parent, Comparator comparator, void *id, char *flag);
static void *avl_remove_node_r (AVLTree *tree, AVLNode **parent, Comparator comparator, void *id, char *flag);

unsigned int avl_clear_tree (AVLTree *tree, void (*destroy)(void *data));

static AVLTree *avl_new (void) {

	AVLTree *tree = (AVLTree *) malloc (sizeof (AVLTree));
	if (tree) {
		tree->root = NULL;

		tree->size = 0;

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
		tree->destroy = destroy;

		tree->mutex = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (tree->mutex, NULL);
	}

	return tree;

}

// returns TRUE if the tree is empty
bool avl_is_empty (AVLTree *tree) {

	bool retval = true;

	if (tree) {
		pthread_mutex_lock (tree->mutex);

		retval = !(tree->root);

		pthread_mutex_unlock (tree->mutex);
	}

	return retval;

}

// returns TRUE if the tree is NOT empty
bool avl_is_not_empty (AVLTree *tree) {

	bool retval = true;

	if (tree) {
		pthread_mutex_lock (tree->mutex);

		retval = !(tree->root);

		pthread_mutex_unlock (tree->mutex);
	}

	return retval;

}

// returns the number of elements that are currently inserted in the tree
size_t avl_size (const AVLTree *avl) {

	size_t retval = 0;

	if (avl) {
		pthread_mutex_lock (avl->mutex);

		retval = avl->size;

		pthread_mutex_unlock (avl->mutex);
	}

	return retval;

}

// returns content of the required node
// option to pass a different comparator than the one that was originally set
void *avl_get_node_data (AVLTree *tree, void *id, Comparator comparator) {

	if (tree && id) {
		Comparator comp = comparator ? comparator : tree->comparator;
		if (comp) {
			return avl_internal_get_node_data (tree, id, comp);
		}
	}

	return NULL;

}

// works as avl_get_node_data () but this method is thread safe
// will lock tree mutex to perform search
void *avl_get_node_data_safe (AVLTree *tree, void *id, Comparator comparator) {

	void *retval = NULL;

	if (tree && id) {
		pthread_mutex_lock (tree->mutex);

		Comparator comp = comparator ? comparator : tree->comparator;
		if (comp) {
			retval = avl_internal_get_node_data (tree, id, comp);
		}

		pthread_mutex_unlock (tree->mutex);
	}

	return retval;

}

// inserts a new node in the tree
// returns 0 on success, 1 on error
unsigned int avl_insert_node (AVLTree *tree, void *data) {

	unsigned int retval = 1;

	if (tree && data) {
		char flag = 0;

		pthread_mutex_lock (tree->mutex);

		retval = avl_insert_node_r (&(tree->root), tree->comparator, data, &flag);

		pthread_mutex_unlock (tree->mutex);
	}

	return retval;

}

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

// removes all nodes from an avl tree, the node nada is only destroyed if a destroy method is set
// returns 0 on success, 1 on error
unsigned int avl_clear_tree (AVLTree *tree, void (*destroy)(void *data)) {

	unsigned int retval = 1;

	if (tree) {
		pthread_mutex_lock (tree->mutex);

		avl_internal_clear_tree (&tree->root, tree->destroy);

		pthread_mutex_unlock (tree->mutex);

		retval = 0;
	}

	return retval;

}

#pragma regigion internal

static AVLNode *avl_node_new (void *data) {

	AVLNode *node = (AVLNode *) malloc (sizeof (AVLNode));

	if (node) {
		node->id = data;
		node->left = NULL;
		node->right = NULL;
		node->balance = 0;
	}

	return node;

}

static void *avl_internal_get_node_data (AVLTree *tree, void *id, Comparator comparator) {

	int ret = 0;
	AVLNode *node = tree->root;
	while (node != NULL) {
		ret = comparator (node->id, id);

		if (ret < 0) node = node->right;
		else if (!ret) return node->id;
		else node = node->left;
	}

	return NULL;

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
				if ((*parent)->left->right) {
					int balance = (*parent)->left->right->balance;
					avl_left_rotation (&(*parent)->left);
					avl_right_rotation (parent);
					avl_balance_fix (*parent, balance);
				}
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
				if ((*parent)->right->left) {
					int balance = (*parent)->right->left->balance;
					avl_right_rotation (&(*parent)->right);
					avl_left_rotation (parent);
					avl_balance_fix (*parent, balance);
				}
			}
			*flag = 0;
			break;
		default: break;
	}
}

// verify and treat a removal in left subtree
static void avl_treat_left_reduction (AVLNode **parent, char *flag) {

	(*parent)->balance++;

	switch ((*parent)->balance) {
		case 1: *flag = 0; break;
		case 2:
			if ((*parent)->right) {
				short shortCut = (*parent)->right->balance;
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
			}
			break;
		default: break;
	}

}

// verify and treat a removal in right subtree
static void avl_treat_right_reduction (AVLNode **parent, char *flag) {

	(*parent)->balance--;

	switch ((*parent)->balance){
		case -1: *flag = 0; break;
		case -2:
			if ((*parent)->left) {
				short shortCut = (*parent)->left->balance;
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
			}
			break;
		default: break;
	}

}

// recursive function to insert in the tree
static unsigned int avl_insert_node_r (AVLNode **parent, Comparator comparator, void *id, char *flag) {

	unsigned int retval = 1;

	// we are on a leaf, create node and set flag
	if (*parent == NULL){
		*parent = avl_node_new (id);
		*flag = 1;

		retval = 0;
	} 
	
	else {
		if (comparator ((*parent)->id, id) > 0) {
			// go left
			retval = avl_insert_node_r (&(*parent)->left, comparator, id, flag);
			if (*flag == 1) avl_treat_left_insertion (&(*parent), flag);
		}

		else {
			// go right
			retval = avl_insert_node_r (&(*parent)->right, comparator, id, flag);
			if (*flag == 1) avl_treat_right_insertion (&(*parent), flag);
		}

		// switch (comparator ((*parent)->id, id)) {
		// 	// go left
		// 	case 1:
		// 		avl_insert_node_r (&(*parent)->left, comparator, id, flag);
		// 		if (*flag == 1) avl_treat_left_insertion (&(*parent), flag);
		// 		break;

		// 	// go right
		// 	case -1:
		// 	case  0:
		// 		avl_insert_node_r (&(*parent)->right, comparator, id, flag);
		// 		if (*flag == 1) avl_treat_right_insertion (&(*parent), flag);
		// 		break;

		// 	default: break;
		// }
	}

	return retval;

}

// recursive function to remove a node from the tree
static void *avl_remove_node_r (AVLTree *tree, AVLNode **parent, Comparator comparator, void *id, char *flag) {

	if (*parent != NULL) {
		int ret = comparator ((*parent)->id, id);

		if (ret < 0) {
			void *data = avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
			if (*flag == 1) avl_treat_right_reduction (&(*parent), flag);
			return data;
		}

		else if (!ret) {
			if ((*parent)->right != NULL && (*parent)->left != NULL) {
				AVLNode *ptr = (*parent)->right, copy = *(*parent);

				while (ptr->left != NULL) ptr = ptr->left;

				(*parent)->id = ptr->id;
				ptr->id = copy.id;

				return avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
			}
			
			else {
				void *data = (*parent)->id;

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
		}

		else {
			void *data = avl_remove_node_r (tree, &(*parent)->left, comparator, id, flag);
			if (*flag == 1) avl_treat_left_reduction (&(*parent), flag);
			return data;
		}




		// switch (comparator ((*parent)->id, id)) {
		// 	case 1: {
		// 		void *data = avl_remove_node_r (tree, &(*parent)->left, comparator, id, flag);
		// 		if (*flag == 1) avl_treat_left_reduction (&(*parent), flag);
		// 		return data;
		// 	}

		// 	case -1: {
		// 		void *data = avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
		// 		if (*flag == 1) avl_treat_right_reduction (&(*parent), flag);
		// 		return data;
		// 	}
			
		// 	case 0:
		// 		if ((*parent)->right != NULL && (*parent)->left != NULL) {
		// 			AVLNode *ptr = (*parent)->right, copy = *(*parent);

		// 			while (ptr->left != NULL) ptr = ptr->left;

		// 			(*parent)->id = ptr->id;
		// 			ptr->id = copy.id;

		// 			return avl_remove_node_r (tree, &(*parent)->right, comparator, id, flag);
		// 		}
				
		// 		else {
		// 			void *data = (*parent)->id;

		// 			if ((*parent)->left != NULL) {
		// 				AVLNode *p = (*parent)->left;
		// 				*(*parent) = *(*parent)->left;
		// 				free (p);

		// 			} 
					
		// 			else if ((*parent)->right != NULL) {
		// 				AVLNode* p = (*parent)->right;
		// 				*(*parent) = *(*parent)->right;
		// 				free (p);
		// 			} 
					
		// 			else {
		// 				// if (tree->destroy) tree->destroy ((*parent)->id);
		// 				// else free ((*parent)->id);
		// 				data = (*parent)->id;
		// 				free (*parent);
		// 				*parent = NULL;
		// 			}

		// 			*flag = 1;

		// 			return data;
		// 		}
		// 		break;

		// 	default: break;
		// }
	}

	return NULL;

}

#pragma endregion