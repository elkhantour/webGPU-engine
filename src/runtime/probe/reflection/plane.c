#include "plane.h"

#include <cglm/affine-pre.h>
#include <cglm/cam.h>
#include <cglm/mat4.h>
#include <cglm/vec3.h>
#include <float.h>
#include <math.h>
#include <stdint.h>

#include "backend/compute/kawase.h"
#include "backend/registry.h"
#include "backend/render_pass/draw.h"
#include "backend/resource_manager.h"
#include "backend/ubo.h"
#include "core.h"
#include "runtime/camera/core.h"
#include "runtime/camera/uniform.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/scene/debug/view.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include "utils/vector/core.h"
#include "webgpu/webgpu.h"

#include "runtime/scene/scene.h"

DynamicListStatus
probe_reflection_plane_list_create(ProbeReflectionPlaneList *list,
                                   const size_t capacity) {

  return dyli_create((void *)&list->entries, &list->capacity, &list->count,
                     sizeof(ProbeReflectionPlane), capacity,
                     "Plane Reflection list");
}

DynamicListStatus
probe_reflection_plane_list_insert(ProbeReflectionPlaneList *list,
                                   ProbeReflectionPlane *entry) {

  // temporary (shader only accept static array for now)
  if (list->count == PROBE_REFLECTION_PLANE_LIST_LAYER_COUNT)
    return DynamicListStatus_UndefError;

  return dyli_insert((void *)&list->entries, &list->capacity, &list->count,
                     sizeof(ProbeReflectionPlane), (void *)entry, 1,
                     "Probe Reflection Plane list");
}

ProbeReflectionPlane *
probe_reflection_plane_list_new_entry(ProbeReflectionPlaneList *list) {

  // temporary (shader only accept static array for now)
  if (list->count == PROBE_REFLECTION_PLANE_LIST_LAYER_COUNT)
    return NULL;

  ProbeReflectionPlane *plane = rem_new_plane_reflection();

  if (plane == NULL) {
    logger_add(LoggerFlag_Error,
               "Couldn't create new plane reflection. Max capacity reached.");
    return NULL;
  }

  DynamicListStatus insert = probe_reflection_plane_list_insert(list, plane);

  if (insert != DynamicListStatus_Success) {
    logger_add(LoggerFlag_Error,
               "Couldn't insert new probe reflection plane. Error code: %d.",
               insert);
    return NULL;
  }

  plane->texture_layer = list->count - 1;

  return plane;
}

DynamicListStatus
probe_reflection_plane_list_remove(ProbeReflectionPlaneList *list,
                                   ProbeReflectionPlane *entry) {
  return dyli_remove((void *)list->entries, &list->count,
                     sizeof(ProbeReflectionPlane), (void *)entry,
                     "Probe Reflection Plane list");
}

DynamicListStatus
probe_reflection_plane_list_destroy(ProbeReflectionPlaneList *list) {
  return dyli_free((void *)list->entries, &list->capacity, &list->count);
}

void probe_reflection_plane_create(ProbeReflectionPlane *probe,
                                   ProbeReflectionPlaneDescriptor *desc) {

  probe_reflection_plane_set_name(
      probe, desc->name == 0 ? "Probe Reflection Plane" : desc->name);

  // Define init attribute
  glm_vec3_copy(desc->position, probe->position);
  glm_vec3_copy(desc->scale, probe->scale);
  glm_vec3_copy(desc->normal, probe->normal);

  vec3_tangent(probe->normal, probe->tangent);
  vec3_bitangent(probe->normal, probe->tangent, probe->bitangent);

  probe->id = reg_register(probe, RegEntryType_ProbeReflectionPlane);
  probe->near = desc->near;
  probe->far = desc->far;
  probe->distance = desc->distance;
  probe->ref_camera = desc->camera;
  probe->signed_distance = glm_dot(probe->normal, probe->position);

  mesh_ref_list_create(&probe->excluded_meshes, 128);

  // create "fake camera" that will actually just copy the reference camera
  // reflected position/ angle, thus we don't need to pass "sensitivy"/ "mode"
  // attributes
  camera_create(&probe->camera, &(CameraCreateDescriptor){0});

  probe_reflection_plane_update_boundbox(probe);
}

void probe_reflection_plane_update_uniform(ProbeReflectionPlane *probe) {

  ProbeReflectionPlaneUniform *uniform =
      (ProbeReflectionPlaneUniform *)
          probe->ubo_uniform.uniform.reflection_plane;

  glm_vec3_copy(probe->position, uniform->position);
  glm_vec3_copy(probe->scale, uniform->scale);
  glm_vec3_copy(probe->normal, uniform->normal);
  glm_vec3_copy(probe->tangent, uniform->tangent);
  glm_vec3_copy(probe->bitangent, uniform->bitangent);

  glm_mat4_copy(probe->camera.view, uniform->view);

  uniform->texture_layer = probe->texture_layer;
  uniform->near = probe->near;
  uniform->far = probe->far;
  uniform->distance = probe->distance;
  uniform->signed_distance = probe->signed_distance;
}

void probe_reflection_plane_update_camera(ProbeReflectionPlane *probe) {

  // transfert the reference camera reflected attributes to the probes shallow
  // camera
  vec3_reflect_point((float *)probe->ref_camera->position,
                     (float *)probe->normal, probe->signed_distance,
                     probe->camera.position);

  vec3_reflect_dir((float *)probe->ref_camera->forward, (float *)probe->normal,
                   probe->camera.forward);

  vec3_reflect_dir((float *)probe->ref_camera->up, (float *)probe->normal,
                   probe->camera.up);

  glm_vec3_add(probe->camera.position, probe->camera.forward,
               probe->camera.target);

  glm_lookat(probe->camera.position, probe->camera.target, probe->camera.up,
             probe->camera.view);

  camera_uniform_update(&probe->camera);

  // transfert attribute to UBO slot
  CameraUniform *uniform = camera_uniform(&probe->camera);
  ubo_slot_set_uniform(&probe->ubo_camera, (void *)uniform,
                       sizeof(CameraUniform));
}

void probe_reflection_plane_update_boundbox(ProbeReflectionPlane *probe) {

  vec3 half = {
      probe->scale[0] * 0.5f,
      probe->distance * 0.5f,
      probe->scale[1] * 0.5f,
  };

  // if probe is aligned to axis (faster that oriented)
  if (fabsf(glm_vec3_dot(probe->normal, (vec3){1.0f, 0.0f, 0.0f})) > 0.999f ||
      fabsf(glm_vec3_dot(probe->normal, (vec3){0.0f, 1.0f, 0.0f})) > 0.999f ||
      fabsf(glm_vec3_dot(probe->normal, (vec3){0.0f, 0.0f, 1.0f})) > 0.999f) {

    glm_vec3_copy(probe->position, probe->boundbox.min);
    glm_vec3_copy(probe->position, probe->boundbox.max);

    glm_vec3_sub(probe->position, half, probe->boundbox.min);
    glm_vec3_add(probe->position, half, probe->boundbox.max);

  } else {

    vec3 inv_half;
    glm_vec3_scale(half, -1.0f, inv_half);

    // build transform matrix for TBN
    mat3 tbn;

    glm_vec3_copy(probe->tangent, tbn[0]);
    glm_vec3_copy(probe->bitangent, tbn[1]);
    glm_vec3_copy(probe->normal, tbn[2]);

    mat4 matrix;
    glm_mat4_identity(matrix);
    glm_mat4_ins3(tbn, matrix);
    glm_translate(matrix, probe->position);

    glm_vec3_copy((vec3){FLT_MAX, FLT_MAX, FLT_MAX}, probe->boundbox.min);
    glm_vec3_copy((vec3){FLT_MIN, FLT_MIN, FLT_MIN}, probe->boundbox.max);

    for (uint8_t x = 0; x < 2; x++) {
      for (uint8_t y = 0; y < 2; y++) {
        for (uint8_t z = 0; z < 2; z++) {

          vec3 local_corner = {
              x ? half[0] : inv_half[0],
              y ? half[1] : inv_half[1],
              z ? half[2] : inv_half[2],
          };

          vec3 world_corner;
          glm_mat4_mulv3(matrix, local_corner, 1.0f, world_corner);

          glm_vec3_minv(probe->boundbox.min, world_corner, probe->boundbox.min);
          glm_vec3_minv(probe->boundbox.max, world_corner, probe->boundbox.max);
        }
      }
    }
  }
}

/**
   Remove the given mesh from the plane list render pass lists.

   We need to remove some meshes from the initial draw list to prevent self
   reflection.

  This function is primarily used when the probe reflection or a mesh is
  moving and we compute if a mesh is included in the probe list.

   TODO:
   Since currently all plane shared a common list render pass, we need for
  each plane to create a "black list" of meshes that then will be removed each
  render pass. Maybe another solution would be to create a dedicated
  renderpass to each plane. Cause rn every draw call we have to enable/disable
  dynamically each plane meshes which is probably costy.

 */
void probe_reflection_plane_disable_mesh(ProbeReflectionPlane *plane,
                                         Mesh *mesh) {
  mesh_ref_list_insert(&plane->excluded_meshes, mesh);
}

void probe_reflection_plane_enable_mesh(ProbeReflectionPlane *plane,
                                        Mesh *mesh) {
  mesh_ref_list_remove(&plane->excluded_meshes, mesh);
}

void plane_reflection_destroy(ProbeReflectionPlane *plane) {
  // TODO
}
