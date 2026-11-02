#include "scene_editor_mesh_system.h"
#include "backend/shadow_map/core.h"
#include "backend/shadow_map/draw.h"
#include "runtime/light/list.h"
#include "runtime/light/uniform.h"
#include "runtime/probe/core.h"
#include "runtime/probe/uniform.h"
#include "runtime/scene/editor_mesh/core.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/scene/selection/core.h"
#include "runtime/systems/mesh_route_system.h"
#include <stdint.h>

void sem_list_system_toggle_visibility(SceneEditorMeshListArray *array,
                                       Scene *scene, Renderer *renderer,
                                       const RegEntryType *types, size_t count,
                                       bool active) {

  for (size_t i = 0; i < array->count; i++) {

    SceneEditorMeshList *sem_list = &array->entries[i];
    const RegEntry *entry = reg_lookup(sem_list->id);

    for (size_t j = 0; j < count; j++) {
      if (entry->type == types[j]) {

        for (size_t k = 0; k < sem_list->count; k++) {

          Mesh *mesh = sem_list->entries[k].mesh;

          if (active) {
            mesh_route_system_show_mesh(scene, renderer, mesh);
          } else {
            mesh_route_system_hide_mesh(scene, renderer,
                                        sem_list->entries[k].mesh);
          }
        }

        break;
      }
    }
  }
}

/*

    ▗▖   ▗▄▄▄▖ ▗▄▄▖▗▖ ▗▖▗▄▄▄▖▗▄▄▖
    ▐▌     █  ▐▌   ▐▌ ▐▌  █ ▐▌
    ▐▌     █  ▐▌▝▜▌▐▛▀▜▌  █  ▝▀▚▖
    ▐▙▄▄▖▗▄█▄▖▝▚▄▞▘▐▌ ▐▌  █ ▗▄▄▞▘


 */

// Base transform

#define _(Type, Label)                                                         \
  void sem_system_##Label##_set_position(const SEMTransform *desc) {           \
                                                                               \
    Type *light = (Type *)desc->sem->target;                                   \
                                                                               \
    glm_vec3_copy((float *)desc->value, light->position);                      \
                                                                               \
    Label##_uniform_update(light);                                             \
    mesh_set_position(desc->sem->mesh, desc->value);                           \
                                                                               \
    ubo_update_queue_insert(desc->ubo, UBOType_LightList,                      \
                            desc->light_list->ubo_slot.id);                    \
  }                                                                            \
                                                                               \
  void sem_system_##Label##_set_rotation(const SEMTransform *desc) {}          \
  void sem_system_##Label##_set_scale(const SEMTransform *desc) {}             \
                                                                               \
  void sem_list_system_##Label##_set_position(const SEMListTransform *desc) {  \
    for (size_t i = 0; i < desc->sem_list->count; i++) {                       \
      SceneEditorMesh *sem = &desc->sem_list->entries[i];                      \
      sem->transform_callback[GizmoMode_Position](&(const SEMTransform){       \
          .light_list = desc->light_list,                                      \
          .probe_list = desc->probe_list,                                      \
          .renderer = desc->renderer,                                          \
          .sem = sem,                                                          \
          .ubo = desc->ubo,                                                    \
          .value = desc->value,                                                \
      });                                                                      \
    }                                                                          \
  }                                                                            \
  void sem_list_system_##Label##_set_rotation(const SEMListTransform *desc) {} \
  void sem_list_system_##Label##_set_scale(const SEMListTransform *desc) {}

SEM_LIGHT_ITEMS(_);
#undef _

// Shadow transform
static inline void sem_system_point_light_update_shadow(const SEMTransform *);
static inline void sem_system_spot_light_update_shadow(const SEMTransform *);
static inline void sem_system_sun_light_update_shadow(const SEMTransform *);

void sem_system_point_light_update_shadow(const SEMTransform *desc) {

  PointLight *light = (PointLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  Renderer *renderer = desc->renderer;

  if (renderer_draw_mode(desc->renderer) == RendererDrawMode_Texture) {

    // update light views properties (CPU) + update UBO entries
    point_light_projection_update(light);

    // add to write queue (CPU > GPU)
    for (uint8_t i = 0; i < PROJECTION_VIEW_COUNT; i++)
      ubo_update_queue_insert(ubo, UBOType_ViewProjection,
                              light->ubo_projection[i].id);

    ShadowMap *sm = &renderer->shadow_map;
    const size_t caster_index = shadow_map_point_light_index(sm, light);

    shadow_map_draw_point_light(&renderer->shadow_map, caster_index, NULL);
  }
}

void sem_system_spot_light_update_shadow(const SEMTransform *desc) {

  SpotLight *light = (SpotLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  Renderer *renderer = desc->renderer;

  if (renderer_draw_mode(desc->renderer) == RendererDrawMode_Texture) {

    spot_light_projection_update(light);

    ubo_update_queue_insert(ubo, UBOType_ViewProjection,
                            light->ubo_projection.id);

    ShadowMap *sm = &renderer->shadow_map;
    const size_t caster_index = shadow_map_spot_light_index(sm, light);

    shadow_map_draw_spot_light(&renderer->shadow_map, caster_index, NULL);
  }
}

void sem_system_sun_light_update_shadow(const SEMTransform *desc) {

  SunLight *light = (SunLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  Renderer *renderer = desc->renderer;

  if (renderer_draw_mode(desc->renderer) == RendererDrawMode_Texture) {

    sun_light_projection_update(light);

    ubo_update_queue_insert(ubo, UBOType_ViewProjection,
                            light->ubo_projection.id);

    ShadowMap *sm = &renderer->shadow_map;
    const size_t caster_index = shadow_map_sun_light_index(sm, light);

    shadow_map_draw_sun_light(&renderer->shadow_map, caster_index, NULL);
  }
}

// TODO make macro
void sem_system_point_light_shadow_set_position(const SEMTransform *desc) {

  PointLight *light = (PointLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  LightList *lights = desc->light_list;

  glm_vec3_copy((float *)desc->value, light->position);

  point_light_uniform_update(light);
  ubo_update_queue_insert(ubo, UBOType_LightList, lights->ubo_slot.id);

  mesh_set_position(desc->sem->mesh, desc->value);
  sem_system_point_light_update_shadow(desc);
}

void sem_system_spot_light_shadow_set_position(const SEMTransform *desc) {

  SpotLight *light = (SpotLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  LightList *lights = desc->light_list;

  glm_vec3_copy((float *)desc->value, light->position);

  spot_light_uniform_update(light);
  ubo_update_queue_insert(ubo, UBOType_LightList, lights->ubo_slot.id);

  mesh_set_position(desc->sem->mesh, desc->value);
  sem_system_spot_light_update_shadow(desc);
}

void sem_system_sun_light_shadow_set_position(const SEMTransform *desc) {

  SunLight *light = (SunLight *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  LightList *lights = desc->light_list;

  glm_vec3_copy((float *)desc->value, light->position);

  sun_light_uniform_update(light);
  ubo_update_queue_insert(ubo, UBOType_LightList, lights->ubo_slot.id);

  mesh_set_position(desc->sem->mesh, desc->value);
  sem_system_sun_light_update_shadow(desc);
}

void sem_system_point_light_shadow_set_rotation(const SEMTransform *desc) {}
void sem_system_point_light_shadow_set_scale(const SEMTransform *desc) {}

void sem_system_sun_light_shadow_set_rotation(const SEMTransform *desc) {}
void sem_system_sun_light_shadow_set_scale(const SEMTransform *desc) {}

void sem_system_spot_light_shadow_set_rotation(const SEMTransform *desc) {}
void sem_system_spot_light_shadow_set_scale(const SEMTransform *desc) {}

void sem_list_system_light_shadow_set_position(const SEMListTransform *desc) {

  for (size_t i = 0; i < desc->sem_list->count; i++) {
    SceneEditorMesh *sem = &desc->sem_list->entries[i];
    sem->transform_callback[GizmoMode_Position](&(const SEMTransform){
        .light_list = desc->light_list,
        .probe_list = desc->probe_list,
        .renderer = desc->renderer,
        .sem = sem,
        .ubo = desc->ubo,
        .value = desc->value,
    });
  }
}
void sem_list_light_shadow_set_rotation(const SEMListTransform *desc) {}
void sem_list_light_shadow_set_scale(const SEMListTransform *desc) {}

/*

    ▗▄▄▖ ▗▄▖ ▗▖  ▗▖▗▄▄▄▖▗▄▄▖  ▗▄▖
   ▐▌   ▐▌ ▐▌▐▛▚▞▜▌▐▌   ▐▌ ▐▌▐▌ ▐▌
   ▐▌   ▐▛▀▜▌▐▌  ▐▌▐▛▀▀▘▐▛▀▚▖▐▛▀▜▌
   ▝▚▄▄▖▐▌ ▐▌▐▌  ▐▌▐▙▄▄▖▐▌ ▐▌▐▌ ▐▌


*/

void sem_system_camera_set_position(const SEMTransform *desc) {

  // transform target
  camera_set_position(desc->sem->target, desc->value);

  // transform mesh
  mesh_set_position(desc->sem->mesh, desc->value);
}

/**

                          ▗▄▄▖▗▄▄▖ ▗▄▄▄▖▗▄▄▄
                         ▐▌   ▐▌ ▐▌  █  ▐▌  █
                         ▐▌▝▜▌▐▛▀▚▖  █  ▐▌  █
                         ▝▚▄▞▘▐▌ ▐▌▗▄█▄▖▐▙▄▄▀

             ▗▄▄▖ ▗▄▄▄▖▗▄▄▄▖▗▖   ▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖▗▄▄▄▖ ▗▄▖ ▗▖  ▗▖
             ▐▌ ▐▌▐▌   ▐▌   ▐▌   ▐▌   ▐▌     █    █  ▐▌ ▐▌▐▛▚▖▐▌
             ▐▛▀▚▖▐▛▀▀▘▐▛▀▀▘▐▌   ▐▛▀▀▘▐▌     █    █  ▐▌ ▐▌▐▌ ▝▜▌
             ▐▌ ▐▌▐▙▄▄▖▐▌   ▐▙▄▄▖▐▙▄▄▖▝▚▄▄▖  █  ▗▄█▄▖▝▚▄▞▘▐▌  ▐▌



 */

/**
   Update the position list according to the origin on top the casual mesh
   translation.
 */
void sem_system_probe_reflection_grid_bound_set_position(
    const SEMTransform *desc) {

  ProbeReflectionGrid *grid = (ProbeReflectionGrid *)desc->sem->target;
  mesh_set_position(desc->sem->mesh, desc->value);
}

void sem_system_probe_reflection_grid_bound_set_scale(
    const SEMTransform *desc) {
  // mesh_set_scale(desc->mesh->mesh, desc->offset);
}

void sem_system_probe_reflection_grid_set_position(const SEMTransform *desc) {

  mesh_set_position(desc->sem->mesh, desc->value);

  ProbeReflection *probe = (ProbeReflection *)desc->sem->target;
  glm_vec3_copy(desc->sem->mesh->position, probe->position);

  ProbeList *probes = desc->probe_list;

  // update uniform cpu side
  probe_reflection_update_uniform(probe);

  // add to upload queue
  ubo_update_queue_insert(desc->ubo, UBOType_ProbeList, probes->ubo_slot.id);

  // update view cpu side
  probe_reflection_update_camera(probe);

  // add to upload queue
  for (uint8_t i = 0; i < PROBE_REFLECTION_VIEW_COUNT; i++)
    ubo_update_queue_insert(desc->ubo, UBOType_Camera, probe->ubo_camera[i].id);
}

void sem_system_probe_reflection_grid_set_rotation(const SEMTransform *desc) {}

void sem_system_probe_reflection_grid_set_scale_(const SEMTransform *desc) {
  // print_vec3(desc->offset);
}

/**

                       ▗▄▄▖ ▗▖    ▗▄▖ ▗▖  ▗▖▗▄▄▄▖
                       ▐▌ ▐▌▐▌   ▐▌ ▐▌▐▛▚▖▐▌▐▌
                       ▐▛▀▘ ▐▌   ▐▛▀▜▌▐▌ ▝▜▌▐▛▀▀▘
                       ▐▌   ▐▙▄▄▖▐▌ ▐▌▐▌  ▐▌▐▙▄▄▖

            ▗▄▄▖ ▗▄▄▄▖▗▄▄▄▖▗▖   ▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖▗▄▄▄▖ ▗▄▖ ▗▖  ▗▖
            ▐▌ ▐▌▐▌   ▐▌   ▐▌   ▐▌   ▐▌     █    █  ▐▌ ▐▌▐▛▚▖▐▌
            ▐▛▀▚▖▐▛▀▀▘▐▛▀▀▘▐▌   ▐▛▀▀▘▐▌     █    █  ▐▌ ▐▌▐▌ ▝▜▌
            ▐▌ ▐▌▐▙▄▄▖▐▌   ▐▙▄▄▖▐▙▄▄▖▝▚▄▄▖  █  ▗▄█▄▖▝▚▄▞▘▐▌  ▐▌


   The below function detects which scene meshes are within the probe
   radius/bound-box and update each meshes uniform so register or clear the
   probes index and count so the mesh shader can reference the right probe index
   for the reflection computing.

   By default the meshes reflection only reflecte the skybox. However if a mesh
   is within a probe reflection bound/radius, it takes the probe ID as to render
   the respective reflection texture in the shader.

   Note that only one reflection plane or grid can be active per mesh.

   NOTE:
   Currently this function in unused as automatically assigning reflected meshes
   cause various issue in deciding if a mesh should be self-reflected or not, it
   uselessly complexify the overall process.

   As a solution to this we directly assign/ bind each mesh a probe directly to
   have more efficient and optimized control on the probe reflection handle and
   self reflection.
 */
void sem_system_probe_reflection_plane_update_mesh_uniform(
    const SEMTransform *desc) {

  MeshRefList reflection_meshes;
  // FIXME
  // renderer_query_meshes_with_flags(&desc->renderer->batches,
  //                                RendererMeshFlag_Reflection,
  //                                &reflection_meshes);

  ProbeReflectionPlane *probe = (ProbeReflectionPlane *)desc->sem->target;
  UBOManager *ubo = desc->ubo;
  ProbeList *probes = desc->probe_list;

  probe_reflection_plane_update_boundbox(probe);

  for (uint8_t i = 0; i < reflection_meshes.count; i++) {

    Mesh *mesh = reflection_meshes.entries[i];

    MeshUniform *uniform = mesh_uniform(mesh);
    bool intersect =
        aabb_intersect(&probe->boundbox, &mesh->topology.boundbox.world);

    if (intersect) {
      mesh_uniform_set_probe_reflection_plane(mesh, ubo);
      // FIXME mesh_route_system_hide_mesh(desc->, desc->renderer,
      // pipeline_mesh);
    } else {
      mesh_uniform_clear_probe_reflection_plane(mesh, ubo);
      // FIXME render_pass_enable_mesh(&probes->reflection_plane.pass,
      // pipeline_mesh);
    }
  }
}

void sem_system_probe_reflection_plane_set_position(const SEMTransform *desc) {

  mesh_set_position(desc->sem->mesh, desc->value);

  ProbeReflectionPlane *probe = (ProbeReflectionPlane *)desc->sem->target;
  glm_vec3_copy(desc->sem->mesh->position, probe->position);
  probe->signed_distance = glm_dot(probe->normal, probe->position);

  // update uniform cpu side
  probe_reflection_plane_update_uniform(probe);

  // add to upload queue
  ubo_update_queue_insert(desc->ubo, UBOType_ProbeList,
                          desc->probe_list->ubo_slot.id);

  // update view cpu side
  probe_reflection_plane_update_camera(probe);

  // add to upload queue
  ubo_update_queue_insert(desc->ubo, UBOType_Camera, probe->ubo_camera.id);
}

void sem_system_probe_reflection_plane_set_rotation(const SEMTransform *desc) {}

void sem_system_probe_reflection_plane_set_scale(const SEMTransform *desc) {
  // print_vec3(desc->offset);
}
