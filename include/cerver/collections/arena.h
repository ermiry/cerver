#ifndef _COLLECTIONS_ARENA_H_
#define _COLLECTIONS_ARENA_H_

#include <stddef.h>

#include "cerver/config.h"

// the size of every chunk, an allocation that does not fit is a large allocation
#define ARENA_DEFAULT_CHUNK_SIZE			(1 << 14)

// every allocation is aligned to this size
#define ARENA_ALIGNMENT						16

#ifdef __cplusplus
extern "C" {
#endif

struct _ArenaChunk;
struct _ArenaLarge;

// a bump allocator: the memory is released all at once with arena_reset ()
// it is NOT thread safe, every thread must use its own arena
typedef struct Arena {

	size_t chunk_size;

	// the chunks are kept by arena_reset () to be reused
	struct _ArenaChunk *chunks;
	struct _ArenaChunk *current;

	struct _ArenaLarge *large;

	size_t used;
	size_t n_allocations;

} Arena;

// creates a new arena with chunks of chunk_size bytes,
// 0 uses ARENA_DEFAULT_CHUNK_SIZE
CERVER_PUBLIC Arena *arena_create (const size_t chunk_size);

// releases every chunk and every large allocation
CERVER_PUBLIC void arena_delete (Arena *arena);

// returns size bytes aligned to ARENA_ALIGNMENT, or NULL on error
// the memory is not initialized
CERVER_PUBLIC void *arena_alloc (Arena *arena, const size_t size);

// works as arena_alloc () but the memory is set to 0
CERVER_PUBLIC void *arena_calloc (Arena *arena, const size_t size);

// releases every allocation, the chunks are kept to be reused
// and the allocations bigger than a chunk are freed
CERVER_PUBLIC void arena_reset (Arena *arena);

#ifdef __cplusplus
}
#endif

#endif
