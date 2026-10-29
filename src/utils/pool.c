#include "pool.h"

#include <stdlib.h>
#include <string.h>

void pool_init(Pool *p, uint32_t elem_size, uint32_t chunk_slots) {
  memset(p, 0, sizeof *p);

  if (chunk_slots == 0)
    chunk_slots = 1;

  uint32_t shift = 0;
  while ((1u << shift) < chunk_slots && shift < 24)
    shift++;

  p->elem_size = elem_size;
  p->stride = (elem_size + 7u) & ~7u; /* 8-byte aligned slots */
  p->chunk_shift = shift;
  p->chunk_size = 1u << shift;
  p->chunk_mask = p->chunk_size - 1u;
  /* generations first, then data starting on a 16-byte boundary */
  p->data_offset = (p->chunk_size * (uint32_t)sizeof(uint32_t) + 15u) & ~15u;
}

static int pool_add_chunk(Pool *p) {
  if (p->chunk_count == p->chunk_cap) {
    uint32_t new_capacity = p->chunk_cap ? p->chunk_cap * 2 : 8;
    uint8_t **new_chunk = realloc(p->chunks, new_capacity * sizeof *new_chunk);
    if (!new_chunk)
      return 0;
    p->chunks = new_chunk;
    p->chunk_cap = new_capacity;
  }

  size_t bytes = (size_t)p->data_offset + (size_t)p->chunk_size * p->stride;
  uint8_t *chunk = calloc(1, bytes); /* gens start at 0 = never used */
  if (!chunk)
    return 0;

  p->chunks[p->chunk_count++] = chunk;
  return 1;
}

int pool_reserve(Pool *p, uint32_t slots) {
  while ((uint64_t)p->chunk_count * p->chunk_size < slots) {
    if (!pool_add_chunk(p))
      return 0;
  }
  return 1;
}

uint32_t pool_alloc(Pool *p, uint32_t *out_gen) {
  uint32_t i;

  if (p->free_count) {
    i = p->free_list[--p->free_count];
  } else {
    if ((p->count >> p->chunk_shift) >= p->chunk_count && !pool_add_chunk(p))
      return POOL_INVALID;
    i = p->count++;
  }

  memset(pool_slot_ptr(p, i), 0, p->stride);
  *out_gen = ++*pool_gen_ptr(p, i); /* even -> odd = alive */
  return i;
}

int pool_free(Pool *p, uint32_t index, uint32_t gen) {
  if (!pool_get(p, index, gen))
    return 1; /* already stale */

  ++*pool_gen_ptr(p, index); /* odd -> even */

  if (p->free_count == p->free_cap) {
    uint32_t nc = p->free_cap ? p->free_cap * 2 : 64;
    uint32_t *nf = realloc(p->free_list, nc * sizeof *nf);
    if (!nf)
      return 0; /* slot leaks, nothing dangles */
    p->free_list = nf;
    p->free_cap = nc;
  }
  p->free_list[p->free_count++] = index;
  return 1;
}

int pool_index_of(const Pool *p, const void *ptr, uint32_t *out_index,
                  uint32_t *out_gen) {
  const uintptr_t addr = (uintptr_t)ptr;
  const size_t span = (size_t)p->chunk_size * p->stride;

  for (uint32_t c = 0; c < p->chunk_count; c++) {
    const uintptr_t base = (uintptr_t)(p->chunks[c] + p->data_offset);
    if (addr < base || addr >= base + span)
      continue;

    const size_t off = (size_t)(addr - base);
    if (off % p->stride)
      return 0; /* not the start of a slot */

    const uint32_t i = (c << p->chunk_shift) + (uint32_t)(off / p->stride);
    if (i >= p->count)
      return 0;

    const uint32_t g = *pool_gen_ptr(p, i);
    if ((g & 1u) == 0)
      return 0; /* slot is free */

    *out_index = i;
    *out_gen = g;
    return 1;
  }
  return 0;
}

void pool_shutdown(Pool *p) {
  for (uint32_t c = 0; c < p->chunk_count; c++)
    free(p->chunks[c]);
  free(p->chunks);
  free(p->free_list);
  memset(p, 0, sizeof *p);
}
