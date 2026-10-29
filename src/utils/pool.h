#ifndef POOL_H
#define POOL_H

/*
 * Chunked pool with generational slots.
 *
 *  - Slots live in fixed-size chunks that are never moved or reallocated,
 *    so a pointer from pool_get()/pool_slot_ptr() stays valid until the slot
 *    is freed.
 *  - Chunk size is chosen per pool (rounded up to a power of two), so pools
 *    of big singleton structs don't waste memory.
 *  - Each slot has a generation counter: odd = alive, even = free,
 *    0 = never used. Generation 0 is never alive.
 *
 * Not thread-safe.
 */

#include "utils/defines.h"
#include <stddef.h>
#include <stdint.h>

EXTERN_C_BEGIN

#define POOL_INVALID UINT32_MAX

/**

   chunk
   +---------------------+-----+------------------------------------+
   | gens[chunk_size]    | pad | data[chunk_size * stride]          |
   | one counter per slot|     | chunk_size entries, stride bytes ea|
   +---------------------+-----+------------------------------------+
   ^ chunk start               ^ chunk start + data_offset

*/

typedef struct Pool {
  uint8_t *
      *chunks; /* chunk: [gens[chunk_size]] pad [data[chunk_size * stride]] */
  uint32_t *free_list;  /* stack of free slot indices */
  uint32_t elem_size;   /* size requested by the caller */
  uint32_t stride;      /* elem_size rounded up to 8 bytes */
  uint32_t chunk_shift; /* log2(chunk_size) */
  uint32_t chunk_size;  /* slots per chunk */
  uint32_t chunk_mask;  /* chunk_size - 1 */
  uint32_t data_offset; /* byte offset of data inside a chunk (16-aligned) */
  uint32_t count;       /* high-water mark of slots ever handed out */
  uint32_t chunk_count, chunk_cap;
  uint32_t free_count, free_cap;
} Pool;

/* Initialise an empty pool. chunk_slots is rounded up to a power of two.
 * No memory is allocated until pool_reserve() or the first pool_alloc(). */
void pool_init(Pool *p, uint32_t elem_size, uint32_t chunk_slots);

/* Preallocate enough chunks for at least `slots` elements.
 * Returns 1 on success, 0 on out-of-memory (pool stays valid). */
int pool_reserve(Pool *p, uint32_t slots);

/* Free all chunks and bookkeeping. Does NOT destroy live elements. */
void pool_shutdown(Pool *p);

/* Allocate a zeroed slot. Returns its index and writes its generation to
 * *out_gen. Returns POOL_INVALID on out-of-memory. */
uint32_t pool_alloc(Pool *p, uint32_t *out_gen);

/* Release a slot and invalidate all handles to it.
 * Returns 1 on success (or if already stale), 0 if the free list couldn't
 * grow (slot is invalidated but not reusable). */
int pool_free(Pool *p, uint32_t index, uint32_t gen);

/* Find the slot that owns `ptr`. Returns 1 and fills index/gen if ptr points
 * at the start of a live slot of this pool, otherwise 0.
 * O(number of chunks). */
int pool_index_of(const Pool *p, const void *ptr, uint32_t *out_index,
                  uint32_t *out_gen);

static inline uint32_t *pool_gen_ptr(const Pool *p, uint32_t i) {
  return (uint32_t *)(void *)p->chunks[i >> p->chunk_shift] +
         (i & p->chunk_mask);
}

static inline void *pool_slot_ptr(const Pool *p, uint32_t i) {
  return p->chunks[i >> p->chunk_shift] + p->data_offset +
         (size_t)(i & p->chunk_mask) * p->stride;
}

/* Returns the element, or NULL if the handle is stale or invalid. */
static inline void *pool_get(const Pool *p, uint32_t index, uint32_t gen) {
  if (index >= p->count || (gen & 1u) == 0 || *pool_gen_ptr(p, index) != gen)
    return NULL;
  return pool_slot_ptr(p, index);
}

static inline uint32_t pool_live_count(const Pool *p) {
  return p->count - p->free_count;
}

static inline size_t pool_reserved_bytes(const Pool *p) {
  return (size_t)p->chunk_count *
         ((size_t)p->data_offset + (size_t)p->chunk_size * p->stride);
}

/*
 * Iterate over all live slots. gen_var must be a declared uint32_t.
 * Freeing the current slot inside the loop is safe.
 *
 *   uint32_t g;
 *   POOL_FOREACH_ALIVE(&pool, i, g) { ... pool_slot_ptr(&pool, i) ... }
 */
#define POOL_FOREACH_ALIVE(p, i, gen_var)                                      \
  for (uint32_t i = 0; i < (p)->count; i++)                                    \
    if (((gen_var) = *pool_gen_ptr((p), i)) & 1u)


EXTERN_C_END

#endif /* POOL_H */
