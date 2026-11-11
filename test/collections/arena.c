#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <cerver/types/string.h>

#include <cerver/collections/arena.h>

#include "../test.h"

// bugs that this suite pinned:
// - none, the arena is new in this version
// coverage holes:
// - an arena shared between threads (it is not thread safe on purpose)

#define ARENA_TEST_CHUNK_SIZE				256
#define ARENA_TEST_SMALL_ALLOCATIONS		512
#define ARENA_TEST_SMALL_SIZE				24
#define ARENA_TEST_LARGE_SIZE				4096
#define ARENA_TEST_ROUNDS					8
#define ARENA_TEST_PATTERN					0x5a

static bool arena_test_is_aligned (const void *ptr) {

	return ((uintptr_t) ptr % ARENA_ALIGNMENT) == 0;

}

static void test_arena_create (void) {

	Arena *arena = arena_create (0);
	test_check_ptr (arena);
	test_check (arena->chunk_size == ARENA_DEFAULT_CHUNK_SIZE, NULL);
	test_check (arena->used == 0, NULL);
	test_check (arena->n_allocations == 0, NULL);
	arena_delete (arena);

	// the chunk size is aligned
	arena = arena_create (ARENA_TEST_CHUNK_SIZE + 1);
	test_check (arena->chunk_size == (ARENA_TEST_CHUNK_SIZE + ARENA_ALIGNMENT), NULL);
	arena_delete (arena);

	// NULL and empty requests
	test_check_null_ptr (arena_alloc (NULL, ARENA_TEST_SMALL_SIZE));
	arena = arena_create (ARENA_TEST_CHUNK_SIZE);
	test_check_null_ptr (arena_alloc (arena, 0));
	arena_delete (arena);
	arena_delete (NULL);
	arena_reset (NULL);

}

// many small allocations cross many chunks, every one is aligned and writable
static void test_arena_small (void) {

	Arena *arena = arena_create (ARENA_TEST_CHUNK_SIZE);

	unsigned char *allocations[ARENA_TEST_SMALL_ALLOCATIONS];
	for (unsigned int i = 0; i < ARENA_TEST_SMALL_ALLOCATIONS; i++) {
		allocations[i] = (unsigned char *) arena_alloc (arena, ARENA_TEST_SMALL_SIZE);
		test_check_ptr (allocations[i]);
		test_check (arena_test_is_aligned (allocations[i]), "an allocation is not aligned");

		(void) memset (allocations[i], (int) (i & 0xff), ARENA_TEST_SMALL_SIZE);
	}

	// no allocation overwrote another one
	for (unsigned int i = 0; i < ARENA_TEST_SMALL_ALLOCATIONS; i++) {
		for (unsigned int j = 0; j < ARENA_TEST_SMALL_SIZE; j++) {
			test_check (allocations[i][j] == (unsigned char) (i & 0xff), "an allocation was overwritten");
		}
	}

	test_check (arena->n_allocations == ARENA_TEST_SMALL_ALLOCATIONS, NULL);

	arena_delete (arena);

}

// an allocation bigger than a chunk gets its own block, released by the reset
static void test_arena_large (void) {

	Arena *arena = arena_create (ARENA_TEST_CHUNK_SIZE);

	unsigned char *large = (unsigned char *) arena_alloc (arena, ARENA_TEST_LARGE_SIZE);
	test_check_ptr (large);
	test_check (arena_test_is_aligned (large), NULL);
	(void) memset (large, ARENA_TEST_PATTERN, ARENA_TEST_LARGE_SIZE);

	unsigned char *small = (unsigned char *) arena_calloc (arena, ARENA_TEST_SMALL_SIZE);
	test_check_ptr (small);
	for (unsigned int i = 0; i < ARENA_TEST_SMALL_SIZE; i++) test_check (small[i] == 0, NULL);

	test_check (large[ARENA_TEST_LARGE_SIZE - 1] == ARENA_TEST_PATTERN, NULL);

	arena_reset (arena);
	test_check_null_ptr (arena->large);

	arena_delete (arena);

}

// the reset reuses the same chunks, so the second round gets the same memory
static void test_arena_reset (void) {

	Arena *arena = arena_create (ARENA_TEST_CHUNK_SIZE);

	void *first = NULL;
	for (unsigned int round = 0; round < ARENA_TEST_ROUNDS; round++) {
		void *ptr = arena_alloc (arena, ARENA_TEST_SMALL_SIZE);
		if (!first) first = ptr;
		test_check_ptr_eq (ptr, first);

		for (unsigned int i = 0; i < ARENA_TEST_SMALL_ALLOCATIONS; i++) {
			test_check_ptr (arena_alloc (arena, ARENA_TEST_SMALL_SIZE));
		}

		(void) arena_alloc (arena, ARENA_TEST_LARGE_SIZE);

		arena_reset (arena);
		test_check (arena->used == 0, NULL);
		test_check (arena->n_allocations == 0, NULL);
	}

	arena_delete (arena);

}

static void test_arena_strings (void) {

	Arena *arena = arena_create (ARENA_TEST_CHUNK_SIZE);

	const char bytes[] = { 'a', '\0', 'b', 'c' };
	String *str = str_create_from_bytes_arena (arena, bytes, sizeof (bytes));
	test_check_ptr (str);
	test_check (str->len == sizeof (bytes), NULL);
	test_check (!memcmp (str->str, bytes, sizeof (bytes)), NULL);
	test_check (str->str[sizeof (bytes)] == '\0', NULL);

	String *empty = str_create_from_bytes_arena (arena, NULL, 0);
	test_check_ptr (empty);
	test_check (empty->len == 0, NULL);
	test_check_str_empty (empty->str);

	// a string bigger than a chunk
	char big[ARENA_TEST_LARGE_SIZE];
	(void) memset (big, ARENA_TEST_PATTERN, ARENA_TEST_LARGE_SIZE);
	String *large = str_create_from_bytes_arena (arena, big, ARENA_TEST_LARGE_SIZE);
	test_check_ptr (large);
	test_check (large->len == ARENA_TEST_LARGE_SIZE, NULL);

	arena_delete (arena);

}

void collections_tests_arena (void) {

	(void) printf ("Testing COLLECTIONS arena...\n");

	test_arena_create ();
	test_arena_small ();
	test_arena_large ();
	test_arena_reset ();
	test_arena_strings ();

	(void) printf ("Done!\n");

}
