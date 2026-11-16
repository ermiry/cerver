/**
 * @file cerver/collections/avl.h
 * @brief An AVL tree: a balanced binary search tree of pointers.
 */

#ifndef _COLLECTIONS_AVL_H_
#define _COLLECTIONS_AVL_H_

#include <stdbool.h>

#include <pthread.h>

#include "cerver/types/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup collections_avl AVL tree
 * @ingroup collections
 * @brief A balanced binary search tree of pointers, ordered by its comparator.
 * @{
 */

/**
 * @brief A node of the tree.
 */
typedef struct AVLNode {

	/** @brief The data of the node, which is also its key. */
	void *id;
	/** @brief The height of the right subtree minus the one of the left subtree, from -1 to 1. */
	signed char balance;
	struct AVLNode *left /**< @brief The subtree with the smaller data. */, *right /**< @brief The subtree with the bigger or equal data. */;

} AVLNode;

/**
 * @brief An AVL tree.
 *
 * @threadsafety The methods take its mutex, except avl_get_node_data ().
 */
typedef struct AVLTree {

	/** @brief The root, NULL in an empty tree. */
	AVLNode *root;

	/** @brief The nodes of the tree. */
	size_t size;

	/** @brief Orders the data of the nodes. */
	Comparator comparator;
	/** @brief Releases the data of a node, NULL to keep it. */
	void (*destroy)(void *data);

	/** @brief Protects the tree. */
	pthread_mutex_t *mutex;

} AVLTree;

/**
 * @brief Releases a tree, its nodes and, if the destroy method is set, their data.
 *
 * @param[in] tree the tree, it can be NULL
 */
extern void avl_delete (AVLTree *tree);

/**
 * @brief Sets the comparator that orders the data of the nodes.
 *
 * @param[in,out] tree the tree, NULL is ignored
 * @param[in] comparator the comparator
 */
extern void avl_set_comparator (
	AVLTree *tree, Comparator comparator
);

/**
 * @brief Sets the method that releases the data of the nodes.
 *
 * @param[in,out] tree the tree, NULL is ignored
 * @param[in] destroy the method; NULL keeps the data in memory, to be released by the caller
 */
extern void avl_set_destroy (
	AVLTree *tree, void (*destroy)(void *data)
);

/**
 * @brief Creates an empty tree.
 *
 * @param[in] comparator orders the data of the nodes, it is required to insert and remove
 * @param[in] destroy releases the data of a node, NULL to keep it
 * @return the new tree, released with avl_delete (); NULL if it could not be allocated
 */
extern AVLTree *avl_init (
	Comparator comparator, void (*destroy)(void *data)
);

/**
 * @brief Returns whether the tree is empty.
 *
 * @param[in] tree the tree
 * @return true if it has no nodes or it is NULL
 */
extern bool avl_is_empty (AVLTree *tree);

/**
 * @brief Returns whether the tree has nodes.
 *
 * @param[in] tree the tree
 * @return true if it has at least one node, false if it is empty or NULL
 */
extern bool avl_is_not_empty (AVLTree *tree);

/**
 * @brief Returns the number of nodes of the tree.
 *
 * @param[in] avl the tree
 * @return the nodes, 0 for a NULL tree
 */
extern size_t avl_size (const AVLTree *avl);

/**
 * @brief Searches the data of a node.
 *
 * @param[in] tree the tree
 * @param[in] id the query, compared with the data of each node
 * @param[in] comparator compares the data of a node with the query, NULL to use the comparator of the tree
 * @return the data of the node, NULL if it was not found
 * @threadsafety Not thread safe, it does not take the mutex: use avl_get_node_data_safe ().
 */
extern void *avl_get_node_data (
	AVLTree *tree, void *id, Comparator comparator
);

/**
 * @brief Works as avl_get_node_data (), with the mutex of the tree.
 *
 * @param[in] tree the tree
 * @param[in] id the query, compared with the data of each node
 * @param[in] comparator compares the data of a node with the query, NULL to use the comparator of the tree
 * @return the data of the node, NULL if it was not found
 * @warning The data is returned after the mutex is released: another thread can remove it; use
 * avl_get_node_data_apply () to use the data with the mutex.
 */
extern void *avl_get_node_data_safe (
	AVLTree *tree, void *id, Comparator comparator
);

/**
 * @brief Searches the data of a node and runs a method with it while the mutex of the tree is held, so another
 * thread can not remove it in between.
 *
 * @param[in] tree the tree
 * @param[in] id the query, compared with the data of each node
 * @param[in] comparator compares the data of a node with the query, NULL to use the comparator of the tree
 * @param[in] method gets the data of the node and the method args; it must not change the same tree
 * @param[in] method_args the second argument of the method
 * @retval 0 the node was found and the method ran
 * @retval 1 it was not found or an argument is NULL
 * @since 1.9b-2
 */
extern unsigned int avl_get_node_data_apply (
	AVLTree *tree, void *id, Comparator comparator,
	void (*method)(void *data, void *method_args), void *method_args
);

/**
 * @brief Inserts data in the tree, ordered by its comparator; data that is equal to another one is also inserted.
 *
 * @param[in,out] tree the tree, with its comparator
 * @param[in] data the data, the tree points to it from now on
 * @retval 0 the data was inserted
 * @retval 1 a NULL argument or the node could not be allocated
 */
extern unsigned int avl_insert_node (
	AVLTree *tree, void *data
);

/**
 * @brief Removes the node of some data from the tree.
 *
 * @param[in,out] tree the tree, with its comparator
 * @param[in] data the query, compared with the data of each node
 * @return the data of the node that was removed, which is not released; NULL if it was not found
 */
extern void *avl_remove_node (
	AVLTree *tree, void *data
);

/**
 * @brief Removes every node of the tree, and releases their data if there is a destroy method.
 *
 * @param[in,out] tree the tree
 * @param[in] destroy releases the data, NULL to use the one of the tree
 * @retval 0 the tree was cleared
 * @retval 1 the tree is NULL
 */
extern unsigned int avl_clear_tree (
	AVLTree *tree, void (*destroy)(void *data)
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
