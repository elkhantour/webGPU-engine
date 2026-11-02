#include "add.h"

#include <stdint.h>
#include <stdio.h>

#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/shaders/core.h"
#include "backend/ubo.h"
#include "core.h"
#include "debug/core.h"
#include "layer.h"
#include "runtime/camera/core.h"
#include "runtime/camera/list.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/light/uniform.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/list.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/mesh/uniform.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/core.h"
#include "runtime/probe/reflection/grid.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/probe/reflection/probe.h"
#include "runtime/probe/uniform.h"
#include "runtime/scene/editor_mesh/camera/camera.h"
#include "runtime/scene/editor_mesh/light/ambient.h"
#include "runtime/scene/editor_mesh/light/point.h"
#include "runtime/scene/editor_mesh/light/spot.h"
#include "runtime/scene/editor_mesh/light/sun.h"
#include "runtime/scene/editor_mesh/list.h"
#include "runtime/scene/editor_mesh/probe/probe.h"
#include "runtime/scene/stat.h"
#include "utils/projection.h"

/**
    ▗▄▄▖ ▗▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖    ▗▄▄▄▖▗▄▄▄ ▗▄▄▄▖▗▄▄▄▖▗▄▖ ▗▄▄▖
   ▐▌   ▐▌   ▐▌   ▐▛▚▖▐▌▐▌       ▐▌   ▐▌  █  █    █ ▐▌ ▐▌▐▌ ▐▌
    ▝▀▚▖▐▌   ▐▛▀▀▘▐▌ ▝▜▌▐▛▀▀▘    ▐▛▀▀▘▐▌  █  █    █ ▐▌ ▐▌▐▛▀▚▖
   ▗▄▄▞▘▝▚▄▄▖▐▙▄▄▖▐▌  ▐▌▐▙▄▄▖    ▐▙▄▄▖▐▙▄▄▀▗▄█▄▖  █ ▝▚▄▞▘▐▌ ▐▌

              ▗▄▖ ▗▄▄▖    ▗▖▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖▗▄▄▖
             ▐▌ ▐▌▐▌ ▐▌   ▐▌▐▌   ▐▌     █ ▐▌
             ▐▌ ▐▌▐▛▀▚▖   ▐▌▐▛▀▀▘▐▌     █  ▝▀▚▖
             ▝▚▄▞▘▐▙▄▞▘▗▄▄▞▘▐▙▄▄▖▝▚▄▄▖  █ ▗▄▄▞▘

 */
SceneEditorMeshList *scene_add_point_light(Scene *scene,
                                           PointLightDescriptor *desc,
                                           const LightCreateFlag flag,
                                           PointLight **dest) {

  PointLightList *list = &scene->lights.point;

  // create sun light
  PointLight *light = light_list_new_point_light(list);

  if (light == NULL) {
    logger_add(LoggerFlag_Error, "Scene spot light capacity reached maximum.");
    return NULL;
  }

  point_light_create(light, desc);

  if (dest)
    *dest = light;

  light->ubo_uniform = light_list_uniform_new_entry(
      scene->lights.ubo_slot.uniform, LightType_Point);

  point_light_uniform_update(light);

  // create mesh/gizmo
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes, RegEntryType_SceneEditorMeshList_PointLight);

  SEMCreateDescriptor sem_desc = {
      .camera = scene->active_camera,
      .viewport = &scene->viewport,
      .target_list_index = list->count - 1,
  };

  if (flag & LightCreateFlag_Shadow) {

    for (uint8_t i = 0; i < PROJECTION_VIEW_COUNT; i++)
      light->ubo_projection[i] =
          ubo_new_entry(scene->ubo, UBOType_ViewProjection);

    point_light_projection_update(light);

    for (uint8_t i = 0; i < PROJECTION_VIEW_COUNT; i++)
      ubo_upload_entry(scene->ubo, UBOType_ViewProjection,
                       &light->ubo_projection[i]);


    sem_point_light_shadow_create(sem, light, &sem_desc);
  } else {
    sem_point_light_create(sem, light, &sem_desc);
  }

  light_list_uniform_update(&scene->lights);
  ubo_upload_entry(scene->ubo, UBOType_LightList, &scene->lights.ubo_slot);

  return sem;
}

SceneEditorMeshList *scene_add_spot_light(Scene *scene,
                                          SpotLightDescriptor *desc,
                                          const LightCreateFlag flag,
                                          SpotLight **dest) {

  SpotLightList *list = &scene->lights.spot;

  // create sun light
  SpotLight *light = light_list_new_spot_light(list);

  if (light == NULL) {
    logger_add(LoggerFlag_Error, "Scene spot light capacity reached maximum.");
    return NULL;
  }

  spot_light_create(light, desc);

  if (dest)
    *dest = light;

  light->ubo_uniform = light_list_uniform_new_entry(
      scene->lights.ubo_slot.uniform, LightType_Spot);
  spot_light_uniform_update(light);

  // create mesh/gizmo
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes, RegEntryType_SceneEditorMeshList_SpotLight);

  SEMCreateDescriptor sem_desc = {
      .camera = scene->active_camera,
      .viewport = &scene->viewport,
      .target_list_index = list->count - 1,
  };

  if (flag & LightCreateFlag_Shadow) {

    light->ubo_projection = ubo_new_entry(scene->ubo, UBOType_ViewProjection);

    spot_light_projection_update(light);
    ubo_upload_entry(scene->ubo, UBOType_ViewProjection,
                     &light->ubo_projection);

    sem_spot_light_shadow_create(sem, light, &sem_desc);
  } else {
    sem_spot_light_create(sem, light, &sem_desc);
  }

  // update UBO
  light_list_uniform_update(&scene->lights);
  ubo_upload_entry(scene->ubo, UBOType_LightList, &scene->lights.ubo_slot);

  return sem;
}

SceneEditorMeshList *scene_add_ambient_light(Scene *scene,
                                             AmbientLightDescriptor *desc,
                                             AmbientLight **dest) {

  AmbientLightList *list = &scene->lights.ambient;

  // create sun light
  AmbientLight *light = light_list_new_ambient_light(list);

  if (light == NULL) {
    logger_add(LoggerFlag_Error, "Scene sun light capacity reached maximum.");
    return NULL;
  }

  ambient_light_create(light, desc);

  if (dest)
    *dest = light;

  light->ubo_uniform = light_list_uniform_new_entry(
      scene->lights.ubo_slot.uniform, LightType_Ambient);

  ambient_light_uniform_update(light);

  // create mesh/gizmo
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes, RegEntryType_SceneEditorMeshList_AmbientLight);

  sem_ambient_light_create(sem, light,
                           &(SEMCreateDescriptor){
                               .camera = scene->active_camera,
                               .viewport = &scene->viewport,
                               .target_list_index = list->count - 1,
                           });

  light_list_uniform_update(&scene->lights);
  ubo_upload_entry(scene->ubo, UBOType_LightList, &scene->lights.ubo_slot);

  return sem;
}

SceneEditorMeshList *scene_add_sun_light(Scene *scene, SunLightDescriptor *desc,
                                         const LightCreateFlag flag,
                                         SunLight **dest) {

  SunLightList *list = &scene->lights.sun;

  // create sun light
  SunLight *light = light_list_new_sun_light(list);

  if (light == NULL) {
    logger_add(LoggerFlag_Error, "Scene sun light capacity reached maximum.");
    return NULL;
  }

  sun_light_create(light, desc);

  if (dest)
    *dest = light;

  light->ubo_uniform = light_list_uniform_new_entry(
      scene->lights.ubo_slot.uniform, LightType_Sun);

  sun_light_uniform_update(light);

  // create mesh/gizmo
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes, RegEntryType_SceneEditorMeshList_SunLight);

  SEMCreateDescriptor sem_desc = {
      .camera = scene->active_camera,
      .viewport = &scene->viewport,
      .target_list_index = list->count - 1, // DEBUG UNSURE,
  };

  if (flag & LightCreateFlag_Shadow) {

    light->ubo_projection = ubo_new_entry(scene->ubo, UBOType_ViewProjection);

    sun_light_projection_update(light);
    ubo_upload_entry(scene->ubo, UBOType_ViewProjection,
                     &light->ubo_projection);

    sem_sun_light_shadow_create(sem, light, &sem_desc);

  } else {
    sem_sun_light_create(sem, light, &sem_desc);
  }

  light_list_uniform_update(&scene->lights);
  ubo_upload_entry(scene->ubo, UBOType_LightList, &scene->lights.ubo_slot);

  return sem;
}

/**
   "Create" a new uninitialized camera in the scene camera list and return the
   newly created item's pointer.

        ScenePool
       .---------.
       |   ...   |
       |---------|              SEM<T>
       |  cam N  | ---.      .-----------.       Scene Render Layer
       '---------'    '--->  |   target  |           .-----------.
                      .--->  |  meshes*  | --------> |  mesh 1*  |
                      |      '-----------'           |-----------|
    Scene Mesh Pool   |                              |  mesh 2*  |
       .----------.   |                              |-----------|
       |   ...    |   |                              |    ...    |
       |----------|   |                              '-----------'
       |  mesh N  | --'
       '----------'

 */
SceneEditorMeshList *scene_add_camera(Scene *scene,
                                      const CameraCreateDescriptor *desc,
                                      Camera **dest) {

  // init scene camera
  Camera *new_cam = camera_list_new_camera(&scene->cameras);
  camera_create(new_cam, desc);

  if (dest)
    *dest = new_cam;

  // create gizmo
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes, RegEntryType_SceneEditorMeshList_Camera);

  sem_camera_create(sem, new_cam,
                    &(SEMCreateDescriptor){
                        .camera = scene->active_camera,
                        .viewport = &scene->viewport,
                        .target_list_index = scene->cameras.count - 1,
                    });

  return sem;
}

SceneEditorMeshList *
scene_add_probe_reflection_grid(Scene *scene,
                                ProbeReflectionGridDescriptor *desc,
                                ProbeReflectionGrid **dest) {

  ProbeReflectionGrid *new_grid =
      probe_reflection_grid_list_new_entry(&scene->probes.reflection_probe);

  probe_reflection_grid_create(new_grid, desc);

  if (dest)
    *dest = new_grid;

  // create scene object
  SceneEditorMeshList *sem_grid = sem_list_array_new_entry(
      &scene->editor_meshes,
      RegEntryType_SceneEditorMeshList_ProbeReflectionGrid);

  sem_probe_reflection_grid_create(sem_grid, new_grid,
                                   &(SEMCreateDescriptor){
                                       .camera = scene->active_camera,
                                       .viewport = &scene->viewport,
                                       .target_list_index = 0,
                                   });

  // add probes to ubo list
  for (uint16_t i = 0; i < new_grid->probes.count; i++) {
    ProbeReflection *probe = new_grid->probes.entries[i];
    UBOManager *ubo = scene->ubo;

    {
      probe->ubo_uniform = probe_list_uniform_new_entry(
          scene->probes.ubo_slot.uniform, ProbeType_ReflectionProbe);
      probe_reflection_update_uniform(probe);
    }

    {
      // add each views
      for (uint8_t v = 0; v < PROBE_REFLECTION_VIEW_COUNT; v++)
        probe->ubo_camera[v] = ubo_new_entry(scene->ubo, UBOType_Camera);
      probe_reflection_update_camera(probe);
    }
  }

  // update UBO for probe count
  probe_list_update_uniform(&scene->probes);
  ubo_upload_entry(scene->ubo, UBOType_ProbeList, &scene->probes.ubo_slot);

  return sem_grid;
}

SceneEditorMeshList *
scene_add_probe_reflection_plane(Scene *scene,
                                 ProbeReflectionPlaneDescriptor *desc,
                                 ProbeReflectionPlane **dest) {

  ProbeReflectionPlane *probe =
      probe_reflection_plane_list_new_entry(&scene->probes.reflection_plane);

  if (dest)
    *dest = probe;

  probe_reflection_plane_create(probe, desc);

  // create scene object
  SceneEditorMeshList *sem = sem_list_array_new_entry(
      &scene->editor_meshes,
      RegEntryType_SceneEditorMeshList_ProbeReflectionPlane);

  probe_reflection_plane_create(probe, desc);

  sem_probe_reflection_plane_create(
      sem, probe,
      &(SEMCreateDescriptor){
          .camera = scene->active_camera,
          .viewport = &scene->viewport,
          .target_list_index = SCENE_EDITOR_MESH_TARGET_UNDEFINED,
      });

  // add probes to ubo list
  UBOManager *ubo = scene->ubo;

  {
    probe->ubo_uniform = probe_list_uniform_new_entry(
        scene->probes.ubo_slot.uniform, ProbeType_ReflectionPlane);
    probe_reflection_plane_update_uniform(probe);
  }

  {
    probe->ubo_camera = ubo_new_entry(scene->ubo, UBOType_Camera);
    probe_reflection_plane_update_camera(probe);
  }

  // update Probe List UBO for probe count
  probe_list_update_uniform(&scene->probes);
  ubo_upload_entry(scene->ubo, UBOType_ProbeList, &scene->probes.ubo_slot);

  return sem;
}

/**
  ▗▖  ▗▖▗▄▄▄▖ ▗▄▄▖▗▖ ▗▖
  ▐▛▚▞▜▌▐▌   ▐▌   ▐▌ ▐▌
  ▐▌  ▐▌▐▛▀▀▘ ▝▀▚▖▐▛▀▜▌
  ▐▌  ▐▌▐▙▄▄▖▗▄▄▞▘▐▌ ▐▌

 */

SceneStatus scene_add_mesh(Scene *scene, Mesh *mesh, const char *layer) {

  // bind new mesh uniform to UBO and copy previous mesh uniform data
  mesh_ref_list_insert(&scene->meshes, mesh);
  mesh->ubo_slot = ubo_new_entry(scene->ubo, UBOType_Mesh);
  mesh_uniform_update(mesh);
  ubo_upload_entry(scene->ubo, UBOType_Mesh, &mesh->ubo_slot);

  return SceneStatus_Success;
}

/**
   Add a list of mesh pointers (presumably from the scene mesh pool) to a
   pipeline. Meaning each meshes are going to be build depending on the pipeline
   and the current render mode.
 */
void scene_add_mesh_ref_list(Scene *scene, MeshRefList *list,
                             const char *layer) {
  for (size_t i = 0; i < list->count; i++)
    scene_add_mesh(scene, list->entries[i], layer);
}
