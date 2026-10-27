#include "core.h"

#include <cglm/ivec2.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backend/logger.h"
#include "utils/vector/core.h"

static size_t edge_key_hash(EdgeKey key, size_t capacity) {
  /*
    1. Use large number so even if a and b are similar they hit different
    numbers a ^ b or a + b give bad spread
    2. % capacity ensure the result fit in the bucket array (we actually use
    this hash as the index so it's necessary for it to be within the capacity
    range)
   */
  return ((size_t)key[0] * 7386093u ^ (size_t)key[1] * 19349663u) % capacity;
}

static bool edge_key_equal(EdgeKey a, EdgeKey b) {
  return a[0] == b[0] && a[1] == b[1];
}

void edge_hash_set_create(EdgeHashSet *set, size_t capacity) {

  set->entries = calloc(capacity, sizeof(EdgeBucket));
  set->occupied = calloc(capacity, sizeof(size_t));
  set->count = 0;

  if (set->entries == NULL) {
    logger_add(LoggerFlag_Print,
               "Couldn't allocate memory for new hash set.\n");
    set->capacity = 0;
    return;
  }

  set->capacity = capacity;
}

static bool edge_hash_set_grow(EdgeHashSet *set) {
  size_t new_capacity = set->capacity * 2;

  EdgeBucket *new_entries = calloc(new_capacity, sizeof(EdgeBucket));
  size_t *new_occupied = malloc(new_capacity * sizeof(size_t));
  if (!new_entries || !new_occupied) {
    free(new_entries);
    free(new_occupied);
    return false;
  }

  // reinsert existing keys using the new capacity, preserving insertion order
  for (size_t i = 0; i < set->count; i++) {
    EdgeBucket *old = &set->entries[set->occupied[i]];

    size_t index = edge_key_hash(old->key, new_capacity);
    while (new_entries[index].occupied)
      index = (index + 1) % new_capacity;

    glm_ivec2_copy(old->key, new_entries[index].key);
    new_entries[index].occupied = true;
    new_occupied[i] = index;
  }

  free(set->entries);
  free(set->occupied);
  set->entries = new_entries;
  set->occupied = new_occupied;
  set->capacity = new_capacity;
  return true;
}

void edge_hash_set_destroy(EdgeHashSet *set) {
  free(set->entries);
  free(set->occupied);
  set->occupied = NULL;
  set->entries = NULL;
  set->capacity = 0;
  set->count = 0;
}

bool edge_hash_set_insert(EdgeHashSet *set, EdgeKey key) {

  // check if entry doesn't already exists
  if (edge_hash_set_find(set, key))
    return false;

  // check capacity
  if (set->count >= set->capacity * 3 / 4) {
    if (!edge_hash_set_grow(set)) {
      logger_add(LoggerFlag_Print, "Couldn't grow edge hash set.\n");
      return false;
    }
  }

  // generate index from hash
  size_t index = edge_key_hash(key, set->capacity);
  while (set->entries[index].occupied)
    // TODO: Check for value equal as well
    index = (index + 1) % set->capacity;

  // insert new bucket
  EdgeBucket *new_bucket = &set->entries[index];
  glm_ivec2_copy(key, new_bucket->key);
  new_bucket->occupied = true;

  // insert new occupied index
  set->occupied[set->count] = index;

  // update count
  set->count++;
  return true;
}

bool edge_hash_set_find(const EdgeHashSet *set, EdgeKey key) {

  // check if entry doesn't already exists
  size_t index = edge_key_hash(key, set->capacity);
  size_t start = index;

  while (set->entries[index].occupied) {

    // already exist
    if (edge_key_equal(key, set->entries[index].key))
      return true;

    index = (index + 1) % set->capacity;

    // full loop
    if (index == start)
      break;
  }

  return false;
}

bool edge_key_is_null(EdgeKey key) { return ivec2_equal(key, (ivec2){0, 0}); }
