#ifndef _PROBE_REFLECTION_PLANE_H_
#define _PROBE_REFLECTION_PLANE_H_

#include <cglm/types.h>
#include <stddef.h>
#include <stdint.h>

#include "./core.h"
#include "backend/compute/core.h"
#include "backend/registry.h"
#include "backend/render_pass/core.h"
#include "backend/render_pass/render_pass.h"
#include "backend/ubo.h"
#include "cglm/cglm.h"
#include "runtime/camera/core.h"
#include "runtime/geometry/aabb/aabb.h"
#include "runtime/mesh/core.h"
#include "runtime/probe/uniform.h"
#include "utils/dyli.h"
#include "utils/name.h"

#define PROBE_REFLECTION_PLANE_LIST_LAYER_COUNT 32

typedef struct {
  name_t name;
  reg_id_t id;
  vec3 position;
  vec3 normal;
  float signed_distance;
  vec3 scale;
  vec3 tangent;
  vec3 bitangent;
  float near;
  float far;
  float distance;
  Camera camera;
  Camera const *ref_camera;
  AABB boundbox;
  uint32_t texture_layer;
  MeshRefList excluded_meshes;
  ProbeListSlot ubo_uniform;
  UBOSlot ubo_camera;
} ProbeReflectionPlane;

typedef struct {
  ProbeReflectionPlane *entries;
  size_t capacity;
  size_t count;
  RenderPass pass;
  ComputePass kawase;
} ProbeReflectionPlaneList;

typedef struct {
  const char *name;
  float near;
  float far;
  vec3 scale;
  vec3 normal;
  vec3 position;
  float distance;
  Camera const *camera;
} ProbeReflectionPlaneDescriptor;

/* === Plane List === */
DynamicListStatus probe_reflection_plane_list_create(ProbeReflectionPlaneList *,
                                                     const size_t);

DynamicListStatus probe_reflection_plane_list_insert(ProbeReflectionPlaneList *,
                                                     ProbeReflectionPlane *);

ProbeReflectionPlane *
probe_reflection_plane_list_new_entry(ProbeReflectionPlaneList *);

DynamicListStatus probe_reflection_plane_list_remove(ProbeReflectionPlaneList *,
                                                     ProbeReflectionPlane *);

DynamicListStatus
probe_reflection_plane_list_destroy(ProbeReflectionPlaneList *);

/* === Plane === */
void probe_reflection_plane_create(ProbeReflectionPlane *,
                                   ProbeReflectionPlaneDescriptor *);

void plane_reflection_destroy(ProbeReflectionPlane *);

void probe_reflection_plane_disable_mesh(ProbeReflectionPlane *, Mesh *);
void probe_reflection_plane_enable_mesh(ProbeReflectionPlane *, Mesh *);

void probe_reflection_plane_update_uniform(ProbeReflectionPlane *);
void probe_reflection_plane_update_camera(ProbeReflectionPlane *);
void probe_reflection_plane_update_boundbox(ProbeReflectionPlane *);

static inline const char *
probe_reflection_plane_get_name(ProbeReflectionPlane *plane) {
  return plane->name;
}

static inline void probe_reflection_plane_set_name(ProbeReflectionPlane *plane,
                                                   const char *name) {
  name_copy(name, plane->name);
}

#endif
