#include <stdlib.h>
#include <string.h>

#include "cerver/collections/arena.h"

// the data of a chunk or a large allocation starts after its header,
// aligned to ARENA_ALIGNMENT (malloc () already returns aligned memory)
struct _ArenaChunk {

	struct _ArenaChunk *next;
	size_t size;
	size_t used;

};

typedef struct _ArenaChunk ArenaChunk;

struct _ArenaLarge {

	struct _ArenaLarge *next;

};

typedef struct _ArenaLarge ArenaLarge;

#pragma region internal

static size_t arena_align (const size_t size) {

	return (size + (ARENA_ALIGNMENT - 1)) & ~((size_t) ARENA_ALIGNMENT - 1);

}

static unsigned char *arena_chunk_data (ArenaChunk *chunk) {

	return ((unsigned char *) chunk) + arena_align (sizeof (ArenaChunk));

}

static unsigned char *arena_large_data (ArenaLarge *large) {

	return ((unsigned char *) large) + arena_align (sizeof (ArenaLarge));

}

static ArenaChunk *arena_chunk_create (const size_t size) {

	ArenaChunk *chunk = (ArenaChunk *) malloc (arena_align (sizeof (ArenaChunk)) + size);
	if (chunk) {
		chunk->next = NULL;
		chunk->size = size;
		chunk->used = 0;
	}

	return chunk;

}

static void arena_chunks_delete (ArenaChunk *chunk) {

	while (chunk) {
		ArenaChunk *next = chunk->next;
		free (chunk);
		chunk = next;
	}

}

static void arena_large_delete_all (ArenaLarge *large) {

	while (large) {
		ArenaLarge *next = large->next;
		free (large);
		large = next;
	}

}

// an allocation bigger than a chunk gets its own block
static void *arena_alloc_large (Arena *arena, const size_t size) {

	void *retval = NULL;

	ArenaLarge *large = (ArenaLarge *) malloc (arena_align (sizeof (ArenaLarge)) + size);
	if (large) {
		large->next = arena->large;
		arena->large = large;

		retval = arena_large_data (large);
	}

	return retval;

}

// moves to the next chunk, creating it if needed
static ArenaChunk *arena_next_chunk (Arena *arena) {

	ArenaChunk *chunk = arena->current->next;
	if (!chunk) {
		chunk = arena_chunk_create (arena->chunk_size);
		if (chunk) arena->current->next = chunk;
	}

	if (chunk) {
		chunk->used = 0;
		arena->current = chunk;
	}

	return chunk;

}

#pragma endregion

#pragma region public

Arena *arena_create (const size_t chunk_size) {

	Arena *arena = (Arena *) malloc (sizeof (Arena));
	if (arena) {
		arena->chunk_size = arena_align (chunk_size ? chunk_size : ARENA_DEFAULT_CHUNK_SIZE);

		arena->chunks = arena_chunk_create (arena->chunk_size);
		arena->current = arena->chunks;

		arena->large = NULL;

		arena->used = 0;
		arena->n_allocations = 0;

		if (!arena->chunks) {
			free (arena);
			arena = NULL;
		}
	}

	return arena;

}

void arena_delete (Arena *arena) {

	if (arena) {
		arena_chunks_delete (arena->chunks);
		arena_large_delete_all (arena->large);

		free (arena);
	}

}

void *arena_alloc (Arena *arena, const size_t size) {

	void *retval = NULL;

	if (arena && size) {
		const size_t aligned = arena_align (size);

		if (aligned > arena->chunk_size) {
			retval = arena_alloc_large (arena, aligned);
		}

		else {
			ArenaChunk *chunk = arena->current;
			if ((chunk->size - chunk->used) < aligned) chunk = arena_next_chunk (arena);

			if (chunk) {
				retval = arena_chunk_data (chunk) + chunk->used;
				chunk->used += aligned;
			}
		}

		if (retval) {
			arena->used += aligned;
			arena->n_allocations += 1;
		}
	}

	return retval;

}

void *arena_calloc (Arena *arena, const size_t size) {

	void *retval = arena_alloc (arena, size);
	if (retval) (void) memset (retval, 0, size);

	return retval;

}

// every chunk is kept to be reused, so a thread that handles
// big requests does not allocate them again, the large allocations are released
void arena_reset (Arena *arena) {

	if (arena) {
		arena_large_delete_all (arena->large);
		arena->large = NULL;

		arena->chunks->used = 0;
		arena->current = arena->chunks;

		arena->used = 0;
		arena->n_allocations = 0;
	}

}

#pragma endregion
