#include "mesh_cache.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/ref_list.h"

#include <stdbool.h>
#include <string.h>

static const size_t MESH_CACHE_DEFAULT_CAPACITY = 16;

// ---------------------------------------------------------------------------
// membership rules: the single definition of "mesh belongs in this cache"
// ---------------------------------------------------------------------------

typedef bool (*MeshMatchFn)(const Mesh *mesh, size_t key);

static bool match_flag(const Mesh *mesh, size_t key) {
  return (mesh->render_config.flags & key) != 0;
}

static bool match_without_flag(const Mesh *mesh, size_t key) {
  return (mesh->render_config.flags & key) == 0;
}

static bool match_layer(const Mesh *mesh, size_t key) {
  return (mesh->render_config.layer & key) != 0;
}

static bool match_pipeline(const Mesh *mesh, size_t key) {
  return (size_t)mesh->render_config.pipeline == key;
}

// ---------------------------------------------------------------------------
// internals
// ---------------------------------------------------------------------------

// Single-bit mask -> array index. False on 0 or out of range.
static bool mask_to_index(size_t mask, size_t count, size_t *out) {
  if (mask == 0)
    return false;
  const size_t index = (size_t)__builtin_ctzll(mask);
  if (index >= count)
    return false;
  *out = index;
  return true;
}

// (type, key) -> the cache slot and its match function.
static bool cache_resolve(SceneMeshCache *cache, const SceneMeshCacheType type,
                          const size_t key, MeshRefList **slot,
                          MeshMatchFn *match) {
  size_t i;

  switch (type) {
  case SceneMeshCacheType_Flag:
    if (!mask_to_index(key, SCENE_MESH_CACHE_FLAG_COUNT, &i))
      return false;
    *slot = &cache->flags[i];
    *match = match_flag;
    return true;

  case SceneMeshCacheType_WithoutFlag:
    if (!mask_to_index(key, SCENE_MESH_CACHE_FLAG_COUNT, &i))
      return false;
    *slot = &cache->without_flags[i];
    *match = match_without_flag;
    return true;

  case SceneMeshCacheType_Layer:
    // "All" is not a single bit and has no slot: the query layer handles it
    *slot = &cache->layers[i];
    *match = match_layer;
    return true;

  case SceneMeshCacheType_Pipeline:
    if (key >= SCENE_MESH_CACHE_PIPELINE_COUNT)
      return false;
    *slot = &cache->pipelines[key];
    *match = match_pipeline;
    return true;
  }

  return false;
}

// Free a list and reset it so "entries == NULL" means "not built".
static void slot_free(MeshRefList *slot) {
  if (slot->entries != NULL)
    mesh_ref_list_free(slot);
  memset(slot, 0, sizeof(*slot));
}

static SceneMeshCacheStatus slot_build(const SceneMeshCache *cache,
                                       MeshRefList *slot, MeshMatchFn match,
                                       size_t key) {
  const MeshRefList *src = cache->source;
  const size_t capacity = cache->default_capacity > 0
                              ? cache->default_capacity
                              : MESH_CACHE_DEFAULT_CAPACITY;

  if (mesh_ref_list_create(slot, capacity) != DynamicListStatus_Success)
    return SceneMeshCacheStatus_UndefError;

  for (size_t i = 0; i < src->count; i++) {
    Mesh *mesh = src->entries[i];
    if (!match(mesh, key))
      continue;

    if (mesh_ref_list_insert(slot, mesh) == NULL) {
      slot_free(slot);
      return SceneMeshCacheStatus_UndefError;
    }
  }

  return SceneMeshCacheStatus_Success;
}

static SceneMeshCacheStatus slot_refresh(const SceneMeshCache *cache,
                                         MeshRefList *slot, MeshMatchFn match,
                                         size_t key) {
  const MeshRefList *src = cache->source;

  if (slot->entries == NULL)
    return slot_build(cache, slot, match, key);

  // 1. drop meshes that left the source or no longer match.
  //    Membership is checked BEFORE touching the mesh: a mesh removed from
  //    the source may already be freed.
  for (size_t i = slot->count; i-- > 0;) {
    Mesh *mesh = slot->entries[i];
    if (mesh_ref_list_find(src, mesh, NULL) == NULL || !match(mesh, key))
      mesh_ref_list_remove_at_index(slot, i);
  }

  // 2. add meshes that now match but aren't cached yet
  for (size_t i = 0; i < src->count; i++) {
    Mesh *mesh = src->entries[i];
    if (!match(mesh, key) || mesh_ref_list_find(slot, mesh, NULL) != NULL)
      continue;

    if (mesh_ref_list_insert(slot, mesh) == NULL)
      return SceneMeshCacheStatus_UndefError;
  }

  return SceneMeshCacheStatus_Success;
}

// Refresh every existing slot of an array. Slot i has key (1 << i) for the
// mask-based types and i for pipelines.
static SceneMeshCacheStatus
slots_refresh_existing(const SceneMeshCache *cache, MeshRefList *slots,
                       size_t count, MeshMatchFn match, bool key_is_mask) {
  SceneMeshCacheStatus result = SceneMeshCacheStatus_Success;

  for (size_t i = 0; i < count; i++) {
    if (slots[i].entries == NULL)
      continue;

    const size_t key = key_is_mask ? ((size_t)1 << i) : i;
    if (slot_refresh(cache, &slots[i], match, key) !=
        SceneMeshCacheStatus_Success)
      result = SceneMeshCacheStatus_UndefError; // keep going, report at end
  }

  return result;
}

static void slots_free(MeshRefList *slots, size_t count) {
  for (size_t i = 0; i < count; i++)
    slot_free(&slots[i]);
}

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

SceneMeshCacheStatus scene_mesh_cache_init(SceneMeshCache *cache,
                                           const MeshRefList *source,
                                           const size_t capacity) {
  if (source == NULL)
    return SceneMeshCacheStatus_UndefError;

  memset(cache, 0, sizeof(*cache)); // "entries == NULL" means "not built"
  cache->source = source;
  cache->default_capacity = capacity;
  return SceneMeshCacheStatus_Success;
}

SceneMeshCacheStatus scene_mesh_cache_clear(SceneMeshCache *cache) {
  slots_free(cache->flags, SCENE_MESH_CACHE_FLAG_COUNT);
  slots_free(cache->without_flags, SCENE_MESH_CACHE_FLAG_COUNT);
  slots_free(cache->layers, SCENE_MESH_CACHE_LAYER_COUNT);
  slots_free(cache->pipelines, SCENE_MESH_CACHE_PIPELINE_COUNT);
  return SceneMeshCacheStatus_Success;
}

SceneMeshCacheStatus scene_mesh_cache_destroy(SceneMeshCache *cache) {
  scene_mesh_cache_clear(cache);
  cache->source = NULL;
  return SceneMeshCacheStatus_Success;
}

SceneMeshCacheStatus scene_mesh_cache_set_source(SceneMeshCache *cache,
                                                 const MeshRefList *source) {
  if (source == NULL)
    return SceneMeshCacheStatus_UndefError;

  scene_mesh_cache_clear(cache); // old caches point to the old scene
  cache->source = source;
  return SceneMeshCacheStatus_Success;
}

const MeshRefList *scene_mesh_cache_source(const SceneMeshCache *cache) {
  return cache->source;
}

// ---------------------------------------------------------------------------
// access
// ---------------------------------------------------------------------------

SceneMeshCacheStatus scene_mesh_cache_get(SceneMeshCache *cache,
                                          const SceneMeshCacheType type,
                                          const size_t key,
                                          const MeshRefList **result) {
  MeshRefList *slot;
  MeshMatchFn match;

  if (!cache_resolve(cache, type, key, &slot, &match))
    return SceneMeshCacheStatus_InvalidKey;

  if (slot->entries == NULL) {
    const SceneMeshCacheStatus status = slot_build(cache, slot, match, key);
    if (status != SceneMeshCacheStatus_Success)
      return status;
  }

  *result = slot;
  return SceneMeshCacheStatus_Success;
}

// ---------------------------------------------------------------------------
// refresh
// ---------------------------------------------------------------------------

SceneMeshCacheStatus scene_mesh_cache_refresh(SceneMeshCache *cache,
                                              const SceneMeshCacheType type,
                                              const size_t key) {
  MeshRefList *slot;
  MeshMatchFn match;

  if (!cache_resolve(cache, type, key, &slot, &match))
    return SceneMeshCacheStatus_InvalidKey;

  return slot_refresh(cache, slot, match, key);
}

SceneMeshCacheStatus scene_mesh_cache_refresh_all(SceneMeshCache *cache) {
  SceneMeshCacheStatus result = SceneMeshCacheStatus_Success;

#define REFRESH(slots, count, match, is_mask)                                  \
  if (slots_refresh_existing(cache, slots, count, match, is_mask) !=           \
      SceneMeshCacheStatus_Success)                                            \
    result = SceneMeshCacheStatus_UndefError;

  REFRESH(cache->flags, SCENE_MESH_CACHE_FLAG_COUNT, match_flag, true)
  REFRESH(cache->without_flags, SCENE_MESH_CACHE_FLAG_COUNT, match_without_flag,
          true)
  REFRESH(cache->layers, SCENE_MESH_CACHE_LAYER_COUNT, match_layer, true)
  REFRESH(cache->pipelines, SCENE_MESH_CACHE_PIPELINE_COUNT, match_pipeline,
          false)

#undef REFRESH

  return result;
}
