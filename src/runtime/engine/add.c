#include "add.h"
#include "backend/logger.h"
#include "backend/reflection/draw.h"
#include "backend/render_pass/core.h"
#include "backend/renderer/core.h"
#include "backend/renderer/mesh_build.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "backend/shadow_map/core.h"
#include "backend/shadow_map/draw.h"
#include "runtime/engine/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/mesh_cache.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/scene/selection/core.h"
#include "runtime/scene/stat.h"
#include "runtime/systems/mesh_route_system.h"
#include <stdint.h>

static void engine_scene_add_sem(Engine *, SceneEditorMeshList *);

static EngineStatus engine_add_mesh_core(Engine *, Mesh *, const char *,
                                         const RendererMeshConfig *,
                                         const EngineAddFlag);

static void engine_update_shadow_maps(Engine *);

Scene *engine_add_scene(Engine *engine, const SceneCreateDescriptor *desc) {

  if (engine->scenes.count >= ENGINE_SCENE_CAPACITY) {
    logger_add(LoggerFlag_Error, "Engine reached max scene capacity (%lu)",
               (unsigned long)ENGINE_SCENE_CAPACITY);
    return NULL;
  }

  Scene *scene = rem_new_scene();

  if (!scene) {
    logger_add(LoggerFlag_Error,
               "Resource manager unable to provide a new scene.");
    return NULL;
  }

  scene_create(scene, desc);
  engine->scenes.entries[engine->scenes.count++] = scene;

  return scene;
}

// === Shadow ===

/**
   Redraw the shadow maps from every mesh that has the Shadow flag. Called once
   per add/remove (never per mesh), and only in Texture draw mode, which is the
   only one that displays shadows.

   The scene cache must be up to date before calling (mesh_route_system_
   update_mesh / scene_mesh_cache_refresh_all).
 */
void engine_update_shadow_maps(Engine *engine) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  if (renderer->draw_mode != RendererDrawMode_Texture)
    return;

  const MeshRefList *shadow_meshes = NULL;
  const SceneMeshCacheStatus query = scene_mesh_query_with_flags(
      scene, RendererMeshFlag_Shadow, &shadow_meshes);

  // Sync the shadow mesh list with the light/sun/spot RenderPass ?

  if (query != SceneMeshCacheStatus_Success)
    return;

  shadow_map_draw_all(&renderer->shadow_map);
}

// === Add Light ===
SceneEditorMeshList *engine_scene_add_point_light(Engine *engine,
                                                  PointLightDescriptor *desc,
                                                  const LightCreateFlag flag,
                                                  PointLight **dest) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  PointLight *light;
  SceneEditorMeshList *sem_list =
      scene_add_point_light(scene, desc, flag, &light);

  if (dest)
    *dest = light;

  // recompute shadow map if render mode
  if (flag & LightCreateFlag_Shadow) {

    ShadowMap *sm = &renderer->shadow_map;
    shadow_map_register_point_light(sm, light);

    if (renderer_draw_mode(renderer) == RendererDrawMode_Texture) {
      ShadowMapLight *light_group = &sm->lights[ShadowMapLightType_Point];
      const size_t index = light_group->caster.point.count - 1;
      shadow_map_draw_point_light(sm, index, NULL);
    }
  }

  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

SceneEditorMeshList *engine_scene_add_spot_light(Engine *engine,
                                                 SpotLightDescriptor *desc,
                                                 const LightCreateFlag flag,
                                                 SpotLight **dest) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  SpotLight *light;
  SceneEditorMeshList *sem_list =
      scene_add_spot_light(scene, desc, flag, &light);

  if (dest)
    *dest = light;

  if (flag & LightCreateFlag_Shadow) {

    ShadowMap *sm = &renderer->shadow_map;
    shadow_map_register_spot_light(sm, light);

    if (renderer_draw_mode(renderer) == RendererDrawMode_Texture) {
      ShadowMapLight *light_group = &sm->lights[ShadowMapLightType_Directional];
      const size_t index = light_group->caster.directional.spot.count - 1;
      shadow_map_draw_spot_light(sm, index, NULL);
    }
  }

  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

SceneEditorMeshList *engine_scene_add_sun_light(Engine *engine,
                                                SunLightDescriptor *desc,
                                                const LightCreateFlag flag,
                                                SunLight **dest) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  SunLight *light;
  SceneEditorMeshList *sem_list =
      scene_add_sun_light(scene, desc, flag, &light);

  if (dest)
    *dest = light;

  // recompute shadow map if render mode
  if (flag & LightCreateFlag_Shadow) {

    ShadowMap *sm = &renderer->shadow_map;
    shadow_map_register_sun_light(sm, light);

    if (renderer_draw_mode(renderer) == RendererDrawMode_Texture) {
      ShadowMapLight *light_group = &sm->lights[ShadowMapLightType_Directional];
      const size_t index = light_group->caster.directional.sun.count - 1;
      shadow_map_draw_sun_light(sm, index, NULL);
    }
  }

  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

SceneEditorMeshList *
engine_scene_add_ambient_light(Engine *engine, AmbientLightDescriptor *desc,
                               AmbientLight **dest) {

  Scene *scene = engine_get_active_scene(engine);
  SceneEditorMeshList *sem_list = scene_add_ambient_light(scene, desc, dest);
  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

// === Add Probe ===
SceneEditorMeshList *
engine_scene_add_probe_reflection_grid(Engine *engine,
                                       ProbeReflectionGridDescriptor *desc,
                                       ProbeReflectionGrid **dest) {

  Scene *scene = engine_get_active_scene(engine);

  SceneEditorMeshList *sem_list =
      scene_add_probe_reflection_grid(scene, desc, dest);
  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

SceneEditorMeshList *
engine_scene_add_probe_reflection_plane(Engine *engine,
                                        ProbeReflectionPlaneDescriptor *desc,
                                        ProbeReflectionPlane **dest) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  // add draw callback if first probe
  if (scene->probes.reflection_plane.count == 0) {

    // TODO: FREE RESOURCE
    RendererDrawPlaneRelfectionData *data =
        calloc(1, sizeof(RendererDrawPlaneRelfectionData));

    if (data != NULL) {
      data->list = &scene->probes.reflection_plane;
      data->renderer = renderer;

      renderer_add_draw_callback(renderer, renderer_draw_plane_reflection,
                                 (void *)data, RendererDrawMode_Texture);
    } else {
      // never register the callback with NULL data: it would crash on draw
      logger_add(LoggerFlag_Error,
                 "Unable to allocate plane reflection draw data.");
    }
  }

  SceneEditorMeshList *sem_list =
      scene_add_probe_reflection_plane(scene, desc, dest);
  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

// === Add Camera ===
SceneEditorMeshList *engine_scene_add_camera(Engine *engine,
                                             const CameraCreateDescriptor *desc,
                                             Camera **dest) {

  Scene *scene = engine_get_active_scene(engine);
  SceneEditorMeshList *sem_list = scene_add_camera(scene, desc, dest);
  engine_scene_add_sem(engine, sem_list);

  return sem_list;
}

/**
   Compared to casual meshes, Scene Editor Objects (lights/ camera) need to
   follow a more specific path when it comes to be added to the scene and
   especially how they are handled for selection.

   SEM are segmented into:
   1. Meshes list: a visual helper/ representaiton of the entity
   2. Target: the actual data

   As a result in order to add them to the scene we need to add their meshes
   list in the scene pool, but need to add them in a different "branch" of the
   selection system (SceneSelectionType_SEM).

   We use the below function to do such operation.
 */
void engine_scene_add_sem(Engine *engine, SceneEditorMeshList *list) {

  Scene *scene = engine_get_active_scene(engine);

  for (size_t i = 0; i < list->count; i++) {
    Mesh *mesh = list->entries[i].mesh;

    engine_scene_add_mesh_custom(
        engine, mesh, NULL,
        // default configuration for Scene Editor Meshes
        &(RendererMeshConfigDescriptor){
            .flags = RendererMeshFlag_Fixed,
            .layer = RendererLayer_Default,
            .pipeline = std_render_pipeline_type(
                (*mesh_shader(mesh, MeshShader_Texture)->pipeline)),
            .draw_mode = RendererDrawMode_All,
        },
        // Note that we do not add the mesh to ui
        // tree since we actually add the whole
        // List id below. Same for the selection.
        EngineAddFlag_TreeHide | EngineAddFlag_Unselectable);

    scene_selection_register_mesh(&scene->selection, mesh, list->id,
                                  SceneSelectionType_SEM);
  }

  // EDITORONLY
  if (engine->gui)
    gui_tree_insert(&engine->gui->tree, list->id);
}

// === Add Mesh ===

/**
   Single entry point for adding a mesh. The mesh owns its render config, the
   scene owns the mesh list, and everything else is derived:

     1. config   -> written on the mesh (Hidden flag if added hidden)
     2. scene    -> mesh added to the scene
     3. build    -> shaders/buffers (must precede routing: draw packets copy
                    buffers and bindgroups from the built shader)
     4. route    -> scene cache refresh + insertion in the matching layouts
     5. shadow   -> shadow maps redrawn once, if the mesh casts shadows
     6. editor   -> selection registration + GUI tree

   Note there is no per-batch/per-shader insertion anymore: the router derives
   the layouts of each draw mode from the mesh config and its own shaders.
 */
EngineStatus engine_add_mesh_core(Engine *engine, Mesh *mesh, const char *layer,
                                  const RendererMeshConfig *config,
                                  const EngineAddFlag flag) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);
  JobSystem *job_system = engine_get_job_system(engine);

  /*
     CONFIG:
     the mesh owns its config. "Hide" is just the Hidden flag, so a mesh added
     hidden is simply never routed.
   */
  RendererMeshConfig mesh_config = *config;
  if (flag & EngineAddFlag_Hide)
    mesh_config.flags |= RendererMeshFlag_Hidden;

  mesh_set_render_config(mesh, &mesh_config);

  /*
     SCENE:
     add to the scene mesh list.
   */
  if (scene_add_mesh(scene, mesh, layer) != SceneStatus_Success) { // ASSUMED
    logger_add(LoggerFlag_Error, "Unable to add mesh '%s' to the scene.",
               mesh->name);
    return EngineStatus_UnfoundEntity; // ASSUMED: use your generic error
  }

  /*
     LAYER:
     add to scene layers ('Default' layer if NULL).
     (Delete if scene_add_mesh already does it.)
   */
  scene_layer_set_insert_mesh(&scene->layers,
                              layer ? layer : SCENE_LAYER_DEFAULT, mesh);

  /*
     BUILD:
     shaders, buffers, bindgroups. Must be done before routing.
   */
  renderer_build_mesh(renderer, scene, job_system, mesh, config->flags);

  /*
     ROUTE:
     refresh the scene cache and insert the mesh in the layouts that accept it
     (does nothing if Hidden). Also updates the scene counts.
   */
  mesh_route_system_update_mesh(scene, renderer, mesh);

  /*
     SHADOW:
     the mesh casts shadows, so redraw the shadow maps (once, after routing).
   */
  SceneSelectionType selection_type = SceneSelectionType_Mesh;
  if (mesh_config.flags & RendererMeshFlag_Shadow) {
    engine_update_shadow_maps(engine);
    selection_type = SceneSelectionType_MeshShadow;
  }

  /*
     [EDITORONLY] SELECTION:
     register the mesh to the selection system so it can be detected with
     raycast. Highlighting later toggles RendererMeshFlag_Selection on the mesh
     followed by mesh_route_system_update_mesh.
   */
  if ((EngineAddFlag_Unselectable & flag) == 0)
    scene_selection_register_mesh(&scene->selection, mesh, mesh->id,
                                  selection_type);

  /*
     [EDITORONLY] TREE:
     Add the mesh to the GUI tree
   */
  if (engine_get_gui(engine) && (flag & EngineAddFlag_TreeHide) == 0 &&
      mesh->parent == NULL)
    gui_tree_insert(&engine_get_gui(engine)->tree, mesh->id);

  return EngineStatus_Success;
}

/*
   Automatically map the renderer config from the mesh Texture shader pipeline.
   The pipelines of the other draw modes (solid/wireframe/boundbox) are derived
   by the router from the mesh's own shaders, so one add is enough.
 */
EngineStatus engine_scene_add_mesh(Engine *engine, Mesh *mesh,
                                   const char *layer,
                                   const EngineAddFlag flag) {

  Shader *shader = mesh_shader(mesh, MeshShader_Texture);
  if (shader == NULL) {
    logger_add(LoggerFlag_Error,
               "Unable to add mesh '%s': it has no Texture shader.",
               mesh->name);
    return EngineStatus_UnfoundEntity;
  }

  const RenderPipelineType pipeline =
      std_render_pipeline_type(*shader->pipeline);

  const RendererMeshConfig *config =
      renderer_mesh_config_from_pipeline(pipeline);
  if (config == NULL) {
    logger_add(LoggerFlag_Error,
               "Unable to add mesh '%s': no mesh config matches its pipeline.",
               mesh->name);
    return EngineStatus_UnfoundEntity;
  }

  return engine_add_mesh_core(engine, mesh, layer, config, flag);
}

/*
  Manually Define the mesh Renderer config. The descriptor is still looked up
  among the presets, because the layouts are generated from them: a config
  outside the presets would be routed to no layout.
 */
EngineStatus
engine_scene_add_mesh_custom(Engine *engine, Mesh *mesh, const char *layer,
                             const RendererMeshConfigDescriptor *desc,
                             const EngineAddFlag flag) {

  // validate BEFORE touching the scene
  const RendererMeshConfig *config = renderer_mesh_config_from_descriptor(desc);

  if (config == NULL) {
    logger_add(LoggerFlag_Error,
               "Unable to locate the configured preset for mesh '%s', make "
               "sure the configuration matches with one in the presets.",
               mesh->name);
    return EngineStatus_UnfoundEntity;
  }

  return engine_add_mesh_core(engine, mesh, layer, config, flag);
}

// === Remove Mesh ===

/**
   The layouts hold draw packets that point to the mesh (and to its buffers and
   bindgroups), so a mesh must be unrouted from every layout BEFORE it is
   removed from the scene and destroyed.
 */
EngineStatus engine_scene_remove_mesh(Engine *engine, Mesh *mesh) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  const bool casts_shadow =
      (mesh->render_config.flags & RendererMeshFlag_Shadow) != 0;

  mesh_route_system_unroute_mesh(scene, renderer, mesh);
  scene_remove_mesh(scene, mesh);

  if (casts_shadow) {
    scene_mesh_cache_refresh_all(&scene->mesh_cache);
    engine_update_shadow_maps(engine);
  }

  return EngineStatus_Success;
}

EngineStatus engine_scene_remove_mesh_ref_list(Engine *engine,
                                               MeshRefList *list) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  bool casts_shadow = false;
  for (size_t i = 0; i < list->count; i++) {
    Mesh *mesh = list->entries[i];
    casts_shadow |= (mesh->render_config.flags & RendererMeshFlag_Shadow) != 0;
    mesh_route_system_unroute_mesh(scene, renderer, mesh);
  }

  scene_remove_mesh_ref_list(scene, list);

  if (casts_shadow) {
    scene_mesh_cache_refresh_all(&scene->mesh_cache);
    engine_update_shadow_maps(engine);
  }

  return EngineStatus_Success;
}
