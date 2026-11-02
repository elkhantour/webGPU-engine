#include "core.h"

#include <emscripten/emscripten.h>
#include <stdint.h>
#include <string.h>

#include "backend/ao_bake/core.h"
#include "backend/clock.h"
#include "backend/compute/core.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/postfx/core.h"
#include "backend/profiler.h"
#include "backend/render_pass/core.h"
#include "backend/render_pass/draw.h"
#include "backend/render_pass/texture.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "backend/shadow_map/core.h"
#include "backend/shadow_map/draw.h"
#include "backend/stat.h"
#include "backend/std_texture/core.h"
#include "backend/ubo.h"
#include "emscripten/html5.h"
#include "emscripten/html5_webgpu.h"
#include "runtime/input/core.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/texture/core.h"
#include "utils/name.h"
#include "webgpu/webgpu.h"

static void renderer_render(void *);

typedef RenderPassLayoutListDescriptor
    renderer_draw_lists[RENDERER_DRAW_MODE_COUNT][RENDERER_LAYER_COUNT];

static inline void renderer_pass_layout_from_batch(Renderer *,
                                                   renderer_draw_lists);

static inline void renderer_pass_create(Renderer *, renderer_draw_lists);

void renderer_create(Renderer *renderer, const RendererCreateDescriptor *desc) {

  renderer->id = reg_register(renderer, RegEntryType_Renderer);
  renderer->background = desc->background;
  renderer->width = desc->width ? desc->width : context_width();
  renderer->height = desc->height ? desc->height : context_height();
  renderer->draw_mode = RendererDrawMode_Solid;
  renderer->multisample = PipelineMultisampleCount_1x; // matches context
  renderer->dpi = desc->dpi == RENDERER_DPI_AUTO
                      ? emscripten_get_device_pixel_ratio()
                      : desc->dpi;

  profiler_init(&renderer->profiler);
}

/**
   Generate the layouts from the batches and create the respective render pass
   configuration for each passes/layers (scene/outline/gizmo...)

    We do not include this feature in the renderer initial creation function
    cause this process is mostly related to the case of wanting a 3D projectw
    with pipelines etc...

    For simplier cases of just wanting the renderer for handling the loops
    callback, we won't need such functions.
 */
void renderer_create_layouts(Renderer *renderer) {
  renderer_draw_lists draw_lists = {0};
  renderer_pass_layout_from_batch(renderer, draw_lists);
  renderer_pass_create(renderer, draw_lists);
}

void renderer_destroy(Renderer *renderer) {}

/**
   Based on the renderer Draw Layouts, it first select the entry base on the
   renderer mode (texture/solid/wireframe).
 */
void renderer_draw_layout_callback(void *data) {

  Renderer *renderer = (Renderer *)data;

  // retrieve render mode
  const RendererDrawMode mode = renderer->draw_mode;
  render_pass_list_draw(&renderer->mesh_pass[__builtin_ctz(mode)]);
}

/**
   Draw callbackas are basically list of functions that will be called during
   the draw loop. Those hooks accept additional user data in argument. A current
   example of hooks are:
   [
     update_camera_matrix(),
     draw_pipelines()
     ]

   Note that the user data longevity is not handled by the hook, meaning it's
   the developper responsibility to manage the lifecycle of the data
   (allocating, freeing...)
 */
void renderer_add_draw_callback(Renderer *renderer,
                                renderer_draw_callback callback, void *data,
                                const int modes) {

  // add hook to corressponding mode
  for (uint8_t i = 0; i < RENDERER_DRAW_MODE_COUNT; i++) {
    if (modes & (1 << i)) {

      // do not add if max hook reached
      if (renderer->callbacks[i].count == RENDERER_MAX_HOOK) {
        logger_add(LoggerFlag_Warning, "Max draw hook reached.\n");
        return;
      }

      renderer->callbacks[i].entries[renderer->callbacks[i].count++] =
          (RendererDrawCallback){callback, data};
    }
  }
}

/**
   Renderer Main Loop, basically just loop through the registered callbacks.
 */
void renderer_render(void *data) {

  Renderer *renderer = (Renderer *)data;

  profiler_latency_end(&renderer->profiler, ProfilerLatencyType_MainLoop);
  profiler_latency_start(&renderer->profiler, ProfilerLatencyType_MainLoop);

  // Call draw callbacks of active renderere draw mode
  RendererDrawCallbackList *callback_list =
      &renderer->callbacks[__builtin_ctz(renderer->draw_mode)];

  // call callbacks, pass renderer and data
  for (size_t i = 0; i < callback_list->count; i++) {
    RendererDrawCallback *cb = &callback_list->entries[i];
    cb->callback(cb->data);
  }

  // update clock delta
  clock_update_delta(&g_clock);
}

/**
   Draw a scene with a specified draw mode along with the render pass that comes
   with it (ao, shadow mapping...). Also call the main loop.
 */
void renderer_draw(Renderer *renderer) {

  if (renderer == NULL) {
    logger_add(LoggerFlag_Error,
               "Unable to start renderer main loop as renderer is NULL.");
    return;
  }

  // call main loop
  emscripten_set_main_loop_arg(renderer_render, (void *)renderer, 0, 1);
}

void renderer_set_draw_mode(Renderer *renderer, const RendererDrawMode mode) {
  renderer->draw_mode = mode;
}

/**
   Recreate scene render pass list textures based on the given dimensions and
   multisample count. Since Scene render pass list texture is a mix of shared
   texture we manually pick and update them.

   Function primarily used in the UI when we adjust the scene width and height.
 */
void renderer_update_pass_texture(
    Renderer *rd, int width, int height,
    const RenderPipelineMultisampleCount multisample, const double dpi) {

  int real_width = (int)(width * dpi);
  int real_height = (int)(height * dpi);

  // Update textures
  for (uint8_t mode = 0; mode < RENDERER_DRAW_MODE_COUNT; mode++) {

    RenderPassList *pass_list = &rd->mesh_pass[mode];

    // === Color ===
    {
      // destroy previous and create new texture
      WGPUTextureView shared_color_view;

      RenderPassTextureDescriptor color_config = {
          .format = TEXTURE_FORMAT_ONSCREEN,
          .height = real_height,
          .width = real_width,
          .multisample = multisample,
      };

      render_pass_list_texture_create_shared_color(
          pass_list, &color_config, NULL, &shared_color_view,
          RenderPassTextureFlag_ReleasePrevious);

      // replace each passes color views with resized one
      for (int j = 0; j < RENDERER_LAYER_COUNT; j++) {
        RenderPass *pass = &pass_list->layers[j];
        WGPUTextureView previous_resolve = pass->color.resolve_view;
        RenderPipelineMultisampleCount previous_multisample = pass->multisample;

        /*
          If previously monosample it means the resolve texture/view corresponds
          to the list shared view/ texture.

          However we already release the shared view/texutre in the
          create_shared_color function, so we set the child pass resolve
          texture/view to NULL manually.

          Monosample:

               List                       Child Passes

                                        .-- pass 1 ------.
                               .-----> | Resolve Texture |
            .-shared --.      |        |-----------------|     .-------------.
           |  Texture  |------+    .-> | Resolve View    | --> | Attachment  |
           |-----------|      |   |    '-----------------'     '-------------'
           |  View     |------|---+
           '-----------'      |   |    .---pass 2--------.
                              '------> | Resolve Texture |
                                  |    |-----------------|     .-------------.
                                  '--> | Resolve View    | --> | Attachment  |
                                       '-----------------'     '-------------'

         */
        if (previous_multisample == PipelineMultisampleCount_1x) {
          pass->color.resolve_texture = NULL;
          pass->color.resolve_view = NULL;
        }

        pass->multisample = multisample;
        pass->color.attachment.view = shared_color_view;

        // Update resolve view for multisample passes
        if (PipelineMultisampleCount_4x == pass->multisample)
          render_pass_texture_create_monosample(
              &pass->color.resolve_texture, &pass->color.resolve_view,
              &color_config, RenderPassTextureFlag_ReleasePrevious);

        if (PipelineMultisampleCount_1x == pass->multisample) {
          pass->color.resolve_view = shared_color_view;
          pass->color.resolve_texture = NULL;
          pass->color.attachment.resolveTarget = NULL;
        }

        if (j == RENDERER_LAYER_COUNT - 1) {

          post_fx_update_scene_view(&pass->post_fx, pass->color.resolve_view);
          if (RendererDrawMode_Texture & (1 << mode))
            // recreate the bloom independent texture with the new resolution
            post_fx_bloom_update_texture_resolution(&pass->post_fx, real_width,
                                                    real_height);
        }
      }
    }

    // === Depth ===
    {
      // destroy previous and create new texture
      WGPUTextureView shared_depth_view;

      render_pass_list_texture_create_shared_depth(
          pass_list,
          &(RenderPassTextureDescriptor){
              .format = TEXTURE_FORMAT_DEPTH_STENCIL,
              .height = real_height,
              .width = real_width,
              .multisample = multisample,

          },
          NULL, &shared_depth_view, RenderPassTextureFlag_ReleasePrevious);

      // replace each passes color views with resized one
      for (int j = 0; j < RENDERER_LAYER_COUNT - 1; j++)
        pass_list->layers[j].depth.attachment.view = shared_depth_view;

      // create individual depth texture for gizmo pass
      render_pass_texture_create_depth(&pass_list->layers[RendererLayer_Gizmo],
                                       &(RenderPassTextureDescriptor){
                                           .format = TEXTURE_FORMAT_DEPTH,
                                           .height = real_height,
                                           .width = real_width,
                                           .multisample = multisample,
                                       },
                                       RenderPassTextureFlag_ReleasePrevious);
    }
  }
}

/**
   Generates the renderer pass layout from the pre-configured batch.
   Combine each draw mode respective shader and topology with each batch
   pipeline. We also take care of segmenting each batch per layer.
 */
void renderer_pass_layout_from_batch(Renderer *renderer,
                                     renderer_draw_lists draw_lists) {

  // create a draw list configuration for each draw mode (Solid, Wireframe...)
  // and each layer (Default, Outline & Gizmo)
  static const struct {
    const MeshShader shader;
    const mesh_get_topology_callback topology_callback;
  } draw_mode_attributes[] = {
      // clang-format off
      [ RendererDrawMode_Texture   ]  =   { MeshShader_Texture,     mesh_topology_base       },
      [ RendererDrawMode_Solid     ]  =   { MeshShader_Solid,       mesh_topology_base       },
      [ RendererDrawMode_Wireframe ]  =   { MeshShader_Wireframe,   mesh_topology_wireframe  },
      [ RendererDrawMode_Boundbox  ]  =   { MeshShader_Wireframe,   mesh_topology_boundbox   },
      // clang-format on
  };

  for (uint8_t mode = 0; mode < RENDERER_DRAW_MODE_COUNT; mode++) {

    for (uint8_t layer = 0; layer < RENDERER_LAYER_COUNT; layer++) {

      for (size_t k = 0; k < RendererMeshPreset_COUNT; k++) {

        const RendererMeshConfig *key = &RENDERER_MESH_CONFIG_PRESET[k];

        // skip if not in current layer or draw mode
        if (key->layer != layer || (key->draw_mode & (1 << mode)) == 0)
          continue;

        // edge case for Fixed mesh that always draw 'texture shader' and
        // 'base topology' no matter the renderer mode
        const RendererDrawMode target_mode =
            (RendererMeshFlag_Fixed & key->flags) ? RendererDrawMode_Texture
                                                  : (1 << mode);

        const size_t count = draw_lists[mode][layer].count;

        draw_lists[mode][layer].entries[count] = (RenderPassLayoutDescriptor){
            .pipeline = key->pipeline,
            .shader = draw_mode_attributes[target_mode].shader,
            .topology_callback =
                draw_mode_attributes[target_mode].topology_callback,
        };

        draw_lists[mode][layer].count++;
      }
    }
  }
}

void renderer_pass_create(Renderer *renderer, renderer_draw_lists draw_lists) {

  RenderPassList *pass_list = renderer->mesh_pass;

  const double ratio = renderer_dpi(renderer);
  const int render_width = renderer_width(renderer) * ratio;
  const int render_height = renderer_height(renderer) * ratio;

  const RenderPipelineMultisampleCount multisample = renderer->multisample;
  static const char *PASS_NAME[] = {"Boundbox", "Wireframe", "Solid",
                                    "Texture"};

  for (uint8_t mode = 0; mode < RENDERER_DRAW_MODE_COUNT; mode++) {

    render_pass_list_create(&pass_list[mode]);
    WGPUTextureView shared_color_view;
    WGPUTextureView shared_depth_view;

    // create shared view for scene and outline since they share the same
    // stencil
    {
      RenderPassTextureDescriptor shared_texture_color_config = {
          .format = TEXTURE_FORMAT_ONSCREEN,
          .height = render_height,
          .width = render_width,
          .multisample = multisample,
      };

      render_pass_list_texture_create_shared_color(
          &pass_list[mode], &shared_texture_color_config, NULL,
          &shared_color_view, RenderPassTextureFlag_None);

      RenderPassTextureDescriptor shared_texture_depth_config = {
          .format = TEXTURE_FORMAT_DEPTH_STENCIL,
          .height = render_height,
          .width = render_width,
          .multisample = multisample,
      };

      render_pass_list_texture_create_shared_depth(
          &pass_list[mode], &shared_texture_depth_config, NULL,
          &shared_depth_view, RenderPassTextureFlag_None);
    }

    /*
                  ▗▄▄▖ ▗▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖
                 ▐▌   ▐▌   ▐▌   ▐▛▚▖▐▌▐▌
                  ▝▀▚▖▐▌   ▐▛▀▀▘▐▌ ▝▜▌▐▛▀▀▘
                 ▗▄▄▞▘▝▚▄▄▖▐▙▄▄▖▐▌  ▐▌▐▙▄▄▖

                    ▗▄▄▖  ▗▄▖  ▗▄▄▖ ▗▄▄▖
                    ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌
                    ▐▛▀▘ ▐▛▀▜▌ ▝▀▚▖ ▝▀▚▖
                    ▐▌   ▐▌ ▐▌▗▄▄▞▘▗▄▄▞▘

     */

    RenderPassColorAttachment scene_color_attachment = {
        .attachment = {
            .view = shared_color_view,
            .clearValue = renderer->background,
            .loadOp = WGPULoadOp_Clear,
            .storeOp = WGPUStoreOp_Store,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        }};

    RenderPassDepthAttachment scene_depth_attachment = {
        .format = TEXTURE_FORMAT_DEPTH_STENCIL,
        .attachment = {
            .view = shared_depth_view,
            .depthReadOnly = false,
            .depthClearValue = 1.0f,
            .depthStoreOp = WGPUStoreOp_Discard,
            .depthLoadOp = WGPULoadOp_Clear,
            .stencilLoadOp = WGPULoadOp_Clear,
            .stencilStoreOp = WGPUStoreOp_Store,
            .stencilClearValue = 0,
            .stencilReadOnly = false,
        }};

    name_t scene_pass_name;
    name_compose(scene_pass_name, "%s Scene Render Pass", PASS_NAME[mode]);
    const RenderPassCreateDescriptor scene_pass = {
        .type = RenderPassType_OnScreen,
        .label = scene_pass_name,
        .multisample = multisample,
        .width = render_width,
        .height = render_height,
        .color = &scene_color_attachment,
        .depth = &scene_depth_attachment,
        .draw_list = &draw_lists[mode][RendererLayer_Default],
    };

    render_pass_list_insert_pass(&pass_list[mode], &scene_pass);

    /*
         ▗▄▄▖▗▄▄▄▖▗▖   ▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖▗▄▄▄▖ ▗▄▖ ▗▖  ▗▖
        ▐▌   ▐▌   ▐▌   ▐▌   ▐▌     █    █  ▐▌ ▐▌▐▛▚▖▐▌
         ▝▀▚▖▐▛▀▀▘▐▌   ▐▛▀▀▘▐▌     █    █  ▐▌ ▐▌▐▌ ▝▜▌
        ▗▄▄▞▘▐▙▄▄▖▐▙▄▄▖▐▙▄▄▖▝▚▄▄▖  █  ▗▄█▄▖▝▚▄▞▘▐▌  ▐▌

                    ▗▄▄▖  ▗▄▖  ▗▄▄▖ ▗▄▄▖
                    ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌
                    ▐▛▀▘ ▐▛▀▜▌ ▝▀▚▖ ▝▀▚▖
                    ▐▌   ▐▌ ▐▌▗▄▄▞▘▗▄▄▞▘

     */

    RenderPassColorAttachment outline_color_attachment = {
        .attachment = {
            .view = shared_color_view,
            .clearValue = 0,
            .loadOp = WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        }};

    RenderPassDepthAttachment outline_depth_attachment = {
        .attachment = {
            .view = shared_depth_view,
            .depthReadOnly = false,
            .depthClearValue = 1.0f,
            .depthStoreOp = WGPUStoreOp_Discard,
            .depthLoadOp = WGPULoadOp_Clear,
            .stencilLoadOp = WGPULoadOp_Load,
            .stencilStoreOp = WGPUStoreOp_Store,
            .stencilClearValue = 0,
            .stencilReadOnly = false,
        }};

    name_t outline_pass_name;
    name_compose(outline_pass_name, "%s Outline Render Pass", PASS_NAME[mode]);
    const RenderPassCreateDescriptor outline_pass = {
        .type = RenderPassType_OnScreen,
        .label = outline_pass_name,
        .multisample = multisample,
        .width = render_width,
        .height = render_height,
        .color = &outline_color_attachment,
        .depth = &outline_depth_attachment,
        .draw_list = &draw_lists[mode][RendererLayer_Outline],
    };

    render_pass_list_insert_pass(&pass_list[mode], &outline_pass);

    /*
                ▗▄▄▖▗▄▄▄▖▗▄▄▄▄▖▗▖  ▗▖ ▗▄▖
               ▐▌     █     ▗▞▘▐▛▚▞▜▌▐▌ ▐▌
               ▐▌▝▜▌  █   ▗▞▘  ▐▌  ▐▌▐▌ ▐▌
               ▝▚▄▞▘▗▄█▄▖▐▙▄▄▄▖▐▌  ▐▌▝▚▄▞▘

                  ▗▄▄▖  ▗▄▖  ▗▄▄▖ ▗▄▄▖
                  ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌
                  ▐▛▀▘ ▐▛▀▜▌ ▝▀▚▖ ▝▀▚▖
                  ▐▌   ▐▌ ▐▌▗▄▄▞▘▗▄▄▞▘

     */

    RenderPassColorAttachment gizmo_color_attachment = {
        .attachment = {
            .view = shared_color_view,
            .clearValue = 0,
            .loadOp = WGPULoadOp_Load,
            .storeOp = WGPUStoreOp_Store,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        }};

    RenderPassDepthAttachment gizmo_depth_attachment = {
        .attachment = {
            .view = RENDER_PASS_VIEW_UNDEFINED,
            .depthReadOnly = false,
            .depthClearValue = 1.0f,
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Discard,
        }};

    name_t gizmo_pass_name;
    name_compose(gizmo_pass_name, "%s Gizmo Render Pass", PASS_NAME[mode]);
    const RenderPassCreateDescriptor gizmo_pass = {
        .type = RenderPassType_OnScreen,
        .label = gizmo_pass_name,
        .multisample = multisample,
        .width = render_width,
        .height = render_height,
        .color = &gizmo_color_attachment,
        .depth = &gizmo_depth_attachment,
        .draw_list = &draw_lists[mode][RendererLayer_Gizmo],
    };

    RenderPass *last_pass =
        render_pass_list_insert_pass(&pass_list[mode], &gizmo_pass);

    /*

       ▗▄▄▖  ▗▄▖  ▗▄▄▖▗▄▄▄▖    ▗▄▄▄▖▗▖  ▗▖
       ▐▌ ▐▌▐▌ ▐▌▐▌     █      ▐▌    ▝▚▞▘
       ▐▛▀▘ ▐▌ ▐▌ ▝▀▚▖  █      ▐▛▀▀▘  ▐▌
       ▐▌   ▝▚▄▞▘▗▄▄▞▘  █      ▐▌   ▗▞▘▝▚▖


       We use two kinds of Post Effect pipeline depending on draw mode:

       .-------------.
       |  Boundbox   | --------.
       '-------------'         |
       .-------------.         |        .- fx ------.
       |  Wireframe  | --------+------> |    Blit   |
       '-------------'         |        '-----------'
       .-------------.         |
       |    Solid    | --------'
       '-------------'
       .-------------.                   .- fx ----.     .- fx ------.
       |   Texture   | ----------------> |  Bloom  | --> | Composite |
       '-------------'                   '---------'     '-----------'

       The composite acts as an "advanced blit" with more functionalities (tone
       mapping, exposure, vignette, bloom...)

     */

    PostFxDescriptor post_fx_desc = {
        .scene_view = last_pass->color.resolve_view,
        .width = (const TextureResolution)render_width,
        .height = (const TextureResolution)render_height,
        .profiler = &renderer->profiler,
    };

    post_fx_init(&last_pass->post_fx, &post_fx_desc);

    if (RendererDrawMode_Solid & (1 << mode) ||
        RendererDrawMode_Wireframe & (1 << mode) ||
        RendererDrawMode_Boundbox & (1 << mode)) {

      post_fx_toggle_effect(&last_pass->post_fx, PostFxType_Blit);
      post_fx_update_effect_view(&last_pass->post_fx, PostFxType_Blit,
                                 PostFxViewIndex_Scene,
                                 last_pass->color.resolve_view);

    } else {
      post_fx_toggle_effect(&last_pass->post_fx, PostFxType_Bloom);
      post_fx_toggle_effect(&last_pass->post_fx, PostFxType_Composite);
    }
  }
}
