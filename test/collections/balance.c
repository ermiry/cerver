#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerver/collections/avl.h>

#include "../test.h"
#include "data.h"

// bugs that this suite pinned:
// - the double rotations on insertion fixed the balance with the wrong node
//   (a segfault inserting 3, 1, 2)
// - removing a node with two children did not rebalance its parents
// - avl_clear_tree did not reset the size
// coverage holes:
// - concurrent inserts and removes (the tree uses a single mutex)

#define BALANCE_RANDOM_SIZE				512
#define BALANCE_RANDOM_ROUNDS			8
#define BALANCE_SMALL_SIZE				3

typedef struct BalanceResult {

	int height;
	size_t count;

} BalanceResult;

// checks the order, the stored balance and the AVL property of every node
static BalanceResult balance_check_node (
	const AVLNode *node, const Data *low, const Data *high
) {

	BalanceResult result = { 0, 0 };

	if (node) {
		const Data *data = (const Data *) node->id;

		if (low) test_check (data->idx > low->idx, "the tree order is broken on the left");
		if (high) test_check (data->idx < high->idx, "the tree order is broken on the right");

		BalanceResult left = balance_check_node (node->left, low, data);
		BalanceResult right = balance_check_node (node->right, data, high);

		int balance = right.height - left.height;
		test_check (balance >= -1 && balance <= 1, "the tree is not balanced");
		test_check ((int) node->balance == balance, "the stored balance factor is wrong");

		result.height = 1 + ((left.height > right.height) ? left.height : right.height);
		result.count = 1 + left.count + right.count;
	}

	return result;

}

static void balance_check_tree (const AVLTree *tree) {

	BalanceResult result = balance_check_node (tree->root, NULL, NULL);

	test_check (result.count == tree->size, "the tree size does not match its nodes");

}

static void balance_shuffle (unsigned int *values, const unsigned int count) {

	for (unsigned int i = count - 1; i > 0; i--) {
		unsigned int j = (unsigned int) rand () % (i + 1);
		unsigned int temp = values[i];
		values[i] = values[j];
		values[j] = temp;
	}

}

static void balance_insert_values (
	AVLTree *tree, const unsigned int *values, const unsigned int count
) {

	for (unsigned int i = 0; i < count; i++) {
		test_check_unsigned_eq (avl_insert_node (tree, data_new (values[i], values[i])), 0, NULL);
		balance_check_tree (tree);
	}

}

// the three orders that need a double rotation or a single one
static void test_balance_small_orders (void) {

	const unsigned int orders[][BALANCE_SMALL_SIZE] = {
		{ 3, 1, 2 }, { 1, 3, 2 }, { 1, 2, 3 }, { 3, 2, 1 }
	};

	const unsigned int n_orders = sizeof (orders) / sizeof (orders[0]);

	for (unsigned int i = 0; i < n_orders; i++) {
		AVLTree *tree = avl_init (data_comparator, data_delete);

		balance_insert_values (tree, orders[i], BALANCE_SMALL_SIZE);

		Data *root = (Data *) tree->root->id;
		test_check_unsigned_eq (root->idx, 2, NULL);

		avl_delete (tree);
	}

}

// removes the root while it has two children
static void test_balance_remove_two_children (void) {

	AVLTree *tree = avl_init (data_comparator, data_delete);

	const unsigned int values[] = { 50, 25, 75, 10, 30, 60, 90, 5, 27, 35, 95 };
	const unsigned int count = sizeof (values) / sizeof (values[0]);
	balance_insert_values (tree, values, count);

	const unsigned int removes[] = { 50, 25, 75, 60 };
	const unsigned int n_removes = sizeof (removes) / sizeof (removes[0]);
	for (unsigned int i = 0; i < n_removes; i++) {
		Data query = { removes[i], removes[i] };
		Data *removed = (Data *) avl_remove_node (tree, &query);
		test_check_ptr (removed);
		test_check_unsigned_eq (removed->idx, removes[i], NULL);
		data_delete (removed);

		balance_check_tree (tree);
	}

	test_check (tree->size == (count - n_removes), NULL);

	avl_delete (tree);

}

static void balance_random_round (void) {

	unsigned int values[BALANCE_RANDOM_SIZE];
	for (unsigned int i = 0; i < BALANCE_RANDOM_SIZE; i++) values[i] = i;

	AVLTree *tree = avl_init (data_comparator, data_delete);

	balance_shuffle (values, BALANCE_RANDOM_SIZE);
	balance_insert_values (tree, values, BALANCE_RANDOM_SIZE);

	// every value can be found
	for (unsigned int i = 0; i < BALANCE_RANDOM_SIZE; i++) {
		Data query = { i, i };
		Data *found = (Data *) avl_get_node_data_safe (tree, &query, NULL);
		test_check_ptr (found);
		test_check_unsigned_eq (found->idx, i, NULL);
	}

	// a missing value is not found and is not removed
	Data missing = { BALANCE_RANDOM_SIZE, BALANCE_RANDOM_SIZE };
	test_check_null_ptr (avl_get_node_data_safe (tree, &missing, NULL));
	test_check_null_ptr (avl_remove_node (tree, &missing));
	balance_check_tree (tree);

	// removes half of the values in a random order
	balance_shuffle (values, BALANCE_RANDOM_SIZE);
	for (unsigned int i = 0; i < BALANCE_RANDOM_SIZE / 2; i++) {
		Data query = { values[i], values[i] };
		Data *removed = (Data *) avl_remove_node (tree, &query);
		test_check_ptr (removed);
		test_check_unsigned_eq (removed->idx, values[i], NULL);
		data_delete (removed);

		balance_check_tree (tree);
	}

	// inserts them again
	balance_insert_values (tree, values, BALANCE_RANDOM_SIZE / 2);
	test_check (tree->size == BALANCE_RANDOM_SIZE, NULL);

	test_check_unsigned_eq (avl_clear_tree (tree, NULL), 0, NULL);
	test_check (tree->size == 0, NULL);
	test_check_null_ptr (tree->root);

	avl_delete (tree);

}

static void test_balance_random (void) {

	for (unsigned int round = 0; round < BALANCE_RANDOM_ROUNDS; round++) {
		balance_random_round ();
	}

}

void collections_tests_avl_balance (void) {

	(void) printf ("Testing COLLECTIONS avl balance...\n");

	test_balance_small_orders ();
	test_balance_remove_two_children ();
	test_balance_random ();

	(void) printf ("Done!\n");

}
