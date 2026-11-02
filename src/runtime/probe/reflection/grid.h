#ifndef _PROBE_GRID_H_
#define _PROBE_GRID_H_

#include <cglm/types.h>
#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#include "./core.h"
#include "backend/registry.h"
#include "backend/render_pass/core.h"
#include "runtime/geometry/aabb/aabb.h"
#include "runtime/probe/reflection/probe.h"
#include "utils/dyli.h"

#define PROBE_REFLECTION_RESOLUTION 512
#define PROBE_REFLECTION_GRID_DIMENSION 3

#define PROBE_REFLECTION_GRID_MAX_COUNT 3
// N^3 max probes per grid
#define PROBE_REFLECTION_LIST_MAX_COUNT                                        \
  PROBE_REFLECTION_GRID_MAX_COUNT                                              \
  *PROBE_REFLECTION_GRID_MAX_COUNT *PROBE_REFLECTION_GRID_MAX_COUNT

// 16 grid per list
#define PROBE_REFLECTION_GRID_LIST_CAPACITY 8

typedef struct {
  name_t name;
  reg_id_t id;
  ProbeReflectionList probes;
  WGPUTexture texture;
  WGPUTextureView view;
  ivec3 count;
  vec3 scale;
  vec3 position;
  AABB boundbox;
} ProbeReflectionGrid;

typedef struct {
  ProbeReflectionGrid **entries;
  size_t capacity;
  size_t count;
  RenderPass pass;
} ProbeReflectionGridList;

typedef struct {
  ivec3 count;
  vec3 scale;
  vec3 position;
  const char *name;
} ProbeReflectionGridDescriptor;

void probe_reflection_grid_create(ProbeReflectionGrid *,
                                  ProbeReflectionGridDescriptor *);

void probe_reflection_grid_destroy(ProbeReflectionGrid *);

void probe_reflection_grid_update_boundbox(ProbeReflectionGrid *);

static inline const char *
probe_reflection_grid_get_name(ProbeReflectionGrid *grid) {
  return grid->name;
}

static inline void probe_reflection_grid_set_name(ProbeReflectionGrid *grid,
                                                  const char *name) {
  name_copy(name, grid->name);
}

/* === Probe Grid List  === */

DynamicListStatus probe_reflection_grid_list_create(ProbeReflectionGridList *,
                                                    const size_t);

DynamicListStatus probe_reflection_grid_list_insert(ProbeReflectionGridList *,
                                                    ProbeReflectionGrid *);

ProbeReflectionGrid *
probe_reflection_grid_list_new_entry(ProbeReflectionGridList *);

DynamicListStatus probe_reflection_grid_list_remove(ProbeReflectionGridList *,
                                                    ProbeReflectionGrid *);

DynamicListStatus probe_reflection_grid_list_destroy(ProbeReflectionGridList *);

size_t probe_reflection_grid_list_probe_count(ProbeReflectionGridList *);
#endif
