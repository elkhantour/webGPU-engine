#ifndef _SCENE_MESH_CACHE_H_
#define _SCENE_MESH_CACHE_H_

#include "backend/renderer/mesh_config.h"
#include "backend/shaders/core.h"
#include "utils/defines.h"

#include "runtime/mesh/core.h"

typedef enum {
  SceneMeshCacheStatus_Success,
  SceneMeshCacheStatus_InvalidKey,
  SceneMeshCacheStatus_UndefError,
} SceneMeshCacheStatus;

typedef enum {
  SceneMeshCacheType_Flag,
  SceneMeshCacheType_WithoutFlag,
  SceneMeshCacheType_Layer,
  SceneMeshCacheType_Pipeline,
} SceneMeshCacheType;

// #define (not static const) so they are valid array sizes in C as well as C++
#define SCENE_MESH_CACHE_FLAG_COUNT RENDERER_FLAG_COUNT
#define SCENE_MESH_CACHE_LAYER_COUNT RENDERER_LAYER_COUNT
#define SCENE_MESH_CACHE_PIPELINE_COUNT RenderPipelineType_COUNT

typedef struct {
  size_t default_capacity;
  const MeshRefList *source; // master mesh list (owned by the scene)

  // cached filtered lists (entries == NULL means "not built yet")
  MeshRefList flags[SCENE_MESH_CACHE_FLAG_COUNT];
  MeshRefList without_flags[SCENE_MESH_CACHE_FLAG_COUNT];
  MeshRefList layers[SCENE_MESH_CACHE_LAYER_COUNT];
  MeshRefList pipelines[SCENE_MESH_CACHE_PIPELINE_COUNT];
} SceneMeshCache;

EXTERN_C_BEGIN

// Lifecycle

// `source` must outlive the cache and keep a stable address.
SceneMeshCacheStatus scene_mesh_cache_init(SceneMeshCache *,
                                           const MeshRefList *source,
                                           const size_t capacity);

SceneMeshCacheStatus scene_mesh_cache_destroy(SceneMeshCache *);

// Free every cached list but keep the source (they rebuild lazily on get).
SceneMeshCacheStatus scene_mesh_cache_clear(SceneMeshCache *);

// Point the cache to another source (e.g. scene switch). Clears all caches.
SceneMeshCacheStatus scene_mesh_cache_set_source(SceneMeshCache *,
                                                 const MeshRefList *source);

const MeshRefList *scene_mesh_cache_source(const SceneMeshCache *);

// Access
//
// `key` is the single-bit mask for Flag / WithoutFlag / Layer, and the
// pipeline enum value for Pipeline.
// Builds the cache from the source if it doesn't exist yet.
// *result is read-only and valid until the next refresh/clear/destroy.
SceneMeshCacheStatus scene_mesh_cache_get(SceneMeshCache *,
                                          const SceneMeshCacheType,
                                          const size_t key,
                                          const MeshRefList **result);

// Refresh

// Update one cache against the source (builds it if missing).
SceneMeshCacheStatus scene_mesh_cache_refresh(SceneMeshCache *,
                                              const SceneMeshCacheType,
                                              const size_t key);

// Update every cache that currently exists. Caches never queried are skipped.
SceneMeshCacheStatus scene_mesh_cache_refresh_all(SceneMeshCache *);

EXTERN_C_END

#endif
