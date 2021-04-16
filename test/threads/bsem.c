#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <cerver/threads/bsem.h>

#include "../test.h"

static bsem *test_bsem_create (void) {

	bsem *b = bsem_new ();

	test_check_ptr (b);
	test_check_null_ptr (b->cond);
	test_check_null_ptr (b->mutex);
	test_check_int_eq (b->v, 0, NULL);

	return b;

}

static void test_bsem_init_zero (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 0);

	test_check_ptr (b->mutex);
	test_check_ptr (b->cond);
	test_check_int_eq (b->v, 0, NULL);

	bsem_delete (b);

}

static void test_bsem_init_one (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 1);

	test_check_ptr (b->mutex);
	test_check_ptr (b->cond);
	test_check_int_eq (b->v, 1, NULL);

	bsem_delete (b);

}

static void test_bsem_init_bad (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 2);

	test_check_null_ptr (b->mutex);
	test_check_null_ptr (b->cond);
	test_check_int_eq (b->v, 0, NULL);

	bsem_delete (b);

}

static void test_bsem_post (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 0);

	test_check_ptr (b->mutex);
	test_check_ptr (b->cond);
	test_check_int_eq (b->v, 0, NULL);

	bsem_post (b);
	test_check_int_eq (b->v, 1, NULL);

	bsem_delete (b);

}

static void test_bsem_post_all (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 0);

	test_check_ptr (b->mutex);
	test_check_ptr (b->cond);
	test_check_int_eq (b->v, 0, NULL);

	bsem_post_all (b);
	test_check_int_eq (b->v, 1, NULL);

	bsem_delete (b);

}

static void test_bsem_reset (void) {

	bsem *b = test_bsem_create ();

	bsem_init (b, 0);

	test_check_ptr (b->mutex);
	test_check_ptr (b->cond);
	test_check_int_eq (b->v, 0, NULL);

	bsem_post (b);
	test_check_int_eq (b->v, 1, NULL);

	bsem_reset (b);
	test_check_int_eq (b->v, 0, NULL);

	bsem_delete (b);

}

void threads_tests_bsem (void) {

	(void) printf ("Testing THREADS bsem...\n");

	test_bsem_init_zero ();
	test_bsem_init_one ();
	test_bsem_init_bad ();
	test_bsem_post ();
	test_bsem_post_all ();
	test_bsem_reset ();

	(void) printf ("Done!\n");

}