/**
 * @file cerver/collections/arena.h
 * @brief An arena: a bump allocator whose memory is released all at once.
 */

#ifndef _COLLECTIONS_ARENA_H_
#define _COLLECTIONS_ARENA_H_

#include <stddef.h>

#include "cerver/config.h"

/**
 * @defgroup collections_arena Arena
 * @ingroup collections
 * @brief A bump allocator of chunks: each allocation takes the next bytes of the current chunk, and arena_reset ()
 * releases every allocation at once.
 * @{
 */

/** @brief The default size in bytes of every chunk; an allocation that does not fit in a chunk is a large allocation. */
#define ARENA_DEFAULT_CHUNK_SIZE			(1 << 14)

/** @brief Every allocation is aligned to this size. */
#define ARENA_ALIGNMENT						16

#ifdef __cplusplus
extern "C" {
#endif

struct _ArenaChunk;
struct _ArenaLarge;

/**
 * @brief A bump allocator: the memory is released all at once with arena_reset () or arena_delete ().
 *
 * @threadsafety Not thread safe: every thread must use its own arena.
 */
typedef struct Arena {

	/** @brief The size in bytes of every chunk, aligned to #ARENA_ALIGNMENT. */
	size_t chunk_size;

	/** @brief The chunks, kept by arena_reset () to be reused. */
	struct _ArenaChunk *chunks;
	/** @brief The chunk that the next allocation takes its bytes from. */
	struct _ArenaChunk *current;

	/** @brief The allocations bigger than a chunk, each one with its own block. */
	struct _ArenaLarge *large;

	/** @brief The bytes allocated since the last reset, aligned. */
	size_t used;
	/** @brief The allocations since the last reset. */
	size_t n_allocations;

} Arena;

/**
 * @brief Creates an arena with its first chunk.
 *
 * @param[in] chunk_size the size in bytes of every chunk, aligned to #ARENA_ALIGNMENT; 0 uses
 * #ARENA_DEFAULT_CHUNK_SIZE
 * @return the new arena, released with arena_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Arena *arena_create (const size_t chunk_size);

/**
 * @brief Releases an arena, every chunk and every large allocation.
 *
 * @param[in] arena the arena, it can be NULL
 */
CERVER_PUBLIC void arena_delete (Arena *arena);

/**
 * @brief Allocates bytes from the arena, aligned to #ARENA_ALIGNMENT.
 *
 * A size bigger than a chunk gets its own block (a large allocation); otherwise the bytes come from the current
 * chunk, or from the next one when it does not fit.
 *
 * @param[in,out] arena the arena
 * @param[in] size the bytes
 * @return the memory, not initialized, valid until the next reset; NULL for a NULL arena, a size of 0, a size so
 * big that it wraps around when it is aligned, or if it could not be allocated
 */
CERVER_PUBLIC void *arena_alloc (Arena *arena, const size_t size);

/**
 * @brief Works as arena_alloc (), but the memory is set to 0.
 *
 * @param[in,out] arena the arena
 * @param[in] size the bytes
 * @return the memory, set to 0; NULL like arena_alloc ()
 */
CERVER_PUBLIC void *arena_calloc (Arena *arena, const size_t size);

/**
 * @brief Releases every allocation of the arena: the chunks are kept to be reused, the large allocations are
 * freed.
 *
 * @param[in,out] arena the arena, it can be NULL
 */
CERVER_PUBLIC void arena_reset (Arena *arena);

#ifdef __cplusplus
}
#endif

/** @} */

#endif
