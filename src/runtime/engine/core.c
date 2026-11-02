#include "core.h"
#include "backend/compute/core.h"
#include "backend/compute/kawase.h"
#include "backend/context.h"
#include "backend/job_system.h"
#include "backend/logger.h"
#include "backend/renderer/core.h"
#include "backend/reflection/core.h"
#include "backend/reflection/draw.h"
#include "backend/shadow_map/draw.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "runtime/engine/add.h"
#include "runtime/light/list.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/add.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/systems/scene_system.h"
#include "runtime/systems/selection_system.h"
#include "runtime/systems/ubo_system.h"
#include <stdint.h>

static inline void engine_init_reflection_pass(ProbeList *, Renderer *,
                                               Scene *);
static inline void engine_init_gizmo(Engine *, Gizmo *);

/**
   Initialize the engine with default values
 */
EngineStatus engine_init(Engine *engine) {

  memset(engine, 0, sizeof(Engine));

  Renderer *renderer;
  {
    // === Renderer ===
    engine->renderer = rem_new_renderer();
    renderer_create(engine_get_renderer(engine),
                    &(RendererCreateDescriptor){
                        .background = (WGPUColor){0.14f, 0.14f, 0.14f, 1.0f},
                        .dpi = 1.0,
                    });
    renderer_create_layouts(engine_get_renderer(engine));
    renderer = engine_get_renderer(engine);
  }

  {
    // === Job System ===
    engine->job_system = rem_new_job_system();
    job_system_init(engine->job_system, 1);
  }

  UBOManager *ubo;
  {
    // === UBO ===
    ubo = rem_new_ubo();
    ubo_init(ubo);
  }

  Scene *scene;
  {
    // === Scene and Selection system
    scene = engine_add_scene(engine, &(SceneCreateDescriptor){
                                         .ubo = ubo,
                                         .viewport =
                                             &(ViewportCreateDescriptor){
                                                 .fov = 32.0f,
                                                 .near_clip = 0.1f,
                                                 .far_clip = 100.0f,
                                             },
                                     });
    engine_set_active_scene(engine, scene);

    selection_system_init(&scene->selection, scene, renderer);
  }

  {
    // === GUI ===
    engine->gui = rem_new_gui();
    gui_init(engine_get_gui(engine), &(GuiDescriptor){
                                         .active_scene = scene,
                                         .renderer = renderer,
                                         .theme = &g_theme,
                                         .dpi = context_dpi(),
                                     });
  }

  {
    // === Loop Callbacks ===

    // clang-format off
    renderer_add_draw_callback(renderer,
                               ubo_system_draw_callback,
			       (void *)ubo,
                               RendererDrawMode_All);

    renderer_add_draw_callback(renderer,
			       renderer_draw_layout_callback,
			       (void *)engine_get_renderer(engine),
			       RendererDrawMode_All);

    renderer_add_draw_callback(renderer,
			       gui_draw_callback,
			       (void *)engine_get_gui(engine),
                               RendererDrawMode_All);

    renderer_add_draw_callback(renderer,
			       job_system_draw_callback,
			       (void *)engine->job_system,
                               RendererDrawMode_All);
    // clang-format on
  }

  scene_system_create_grid(scene, engine_get_renderer(engine));
  engine_scene_add_mesh(engine, scene->grid, NULL,
                        EngineAddFlag_Unselectable | EngineAddFlag_TreeHide);

  shadow_map_init(&renderer->shadow_map, &(ShadowMapInitDescriptor){
                                             .lights = &scene->lights,
                                             .profiler = &renderer->profiler,
                                         });

  engine_init_reflection_pass(&scene->probes, engine_get_renderer(engine),
                              scene);

  ao_bake_init(&engine_get_renderer(engine)->texture.ambient_occlusion,
               &(AOBakeInitDescriptor){
                   .size = AO_TEXTURE_RESOLUTION,
                   .layer_count = AO_LAYER_COUNT,
               });

  engine_init_gizmo(engine, &scene->gizmo);
  scene_system_set_draw_mode(scene, engine_get_renderer(engine),
                             RendererDrawMode_Solid);

  return EngineStatus_Success;
}

EngineStatus engine_set_active_scene(Engine *engine, Scene *scene) {

  for (uint8_t i = 0; i < engine->scenes.count; i++) {
    if (engine->scenes.entries[i] == scene) {
      engine->scenes.active = scene;
      return EngineStatus_Success;
    }
  }

  logger_add(LoggerFlag_Warning, "Attempting to set active a scene that does "
                                 "not belong to the targeted engine.");

  return EngineStatus_UnfoundEntity;
}

void engine_init_reflection_pass(ProbeList *list, Renderer *renderer,
                                 Scene *scene) {

  const MeshRefList *reflective_mesh_lists;
  scene_mesh_query_from_pipeline(scene, RenderPipelineType_Reflection,
                                 &reflective_mesh_lists);

  RenderPassLayoutListDescriptor reflection_draw_list = {
      .count = reflective_mesh_lists->count,
  };

  for (uint8_t i = 0; i < reflective_mesh_lists->count; i++)
    reflection_draw_list.entries[i] = (RenderPassLayoutDescriptor){
        .shader = MeshShader_Reflection,
        .topology_callback = mesh_topology_base,
    };

  // grid reflection
  renderer_probe_reflection_create_pass(
      renderer, &list->reflection_probe.pass,
      &(RendererProbeReflectionDescriptor){
          .layer_count =
              PROBE_REFLECTION_LIST_MAX_COUNT * PROBE_REFLECTION_VIEW_COUNT,
          .draw_list = &reflection_draw_list,
          .multisample = PipelineMultisampleCount_1x,
          .resolution = TextureResolution_512,
          .view_dimension = WGPUTextureViewDimension_CubeArray,
      });

  // plane relfection
  renderer_probe_reflection_create_pass(
      renderer, &list->reflection_plane.pass,
      &(RendererProbeReflectionDescriptor){
          .layer_count = PROBE_REFLECTION_PLANE_LIST_LAYER_COUNT,
          .draw_list = &reflection_draw_list,
          .multisample = PipelineMultisampleCount_1x,
          .resolution = TextureResolution_512,
          .view_dimension = WGPUTextureViewDimension_2DArray,
      });

  compute_pass_kawase_create(
      &list->reflection_plane.kawase,
      &(ComputePassDescriptor){
          .label = "Plane Reflection Kawase Pass",
          .source_texture = list->reflection_plane.pass.color.texture,
      });
}

// Create transform gizmos and add them to editor gizmo
void engine_init_gizmo(Engine *engine, Gizmo *gizmo) {

  Scene *scene = engine_get_active_scene(engine);

  gizmo_create(gizmo, &(GizmoCreateDescriptor){
                          .camera = scene->active_camera,
                          .viewport = &scene->viewport,
                      });

  for (size_t i = 0; i < GIZMO_MODE_COUNT; i++) {
    for (size_t j = 0; j < gizmo->handles[i].count; j++) {
      Mesh *mesh = gizmo->handles[i].entries[j];
      engine_scene_add_mesh_custom(engine, mesh, SCENE_LAYER_GIZMO,
                                   &(RendererMeshConfigDescriptor){
                                       .layer = RendererLayer_Gizmo,
                                       .flags = RendererMeshFlag_Fixed,
                                       .pipeline = RenderPipelineType_Unlit,
                                       .draw_mode = RendererDrawMode_All,
                                   },
                                   EngineAddFlag_Hide |
                                       EngineAddFlag_Unselectable |
                                       EngineAddFlag_TreeHide);
    }
  }
}
