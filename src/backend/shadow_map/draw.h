#ifndef _SHADOW_MAP_DRAW_H_
#define _SHADOW_MAP_DRAW_H_

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#include "backend/profiler.h"
#include "backend/render_pass/core.h"
#include "backend/resource_manager.h"
#include "backend/ubo.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"

#include "runtime/mesh/draw.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/debug/core.h"
#include "runtime/scene/debug/debug.h"
#include "utils/defines.h"
#include "utils/projection.h"

#include "backend/render_pass/core.h"
#include "backend/render_pass/draw.h"
#include "backend/shaders/core.h"
#include "core.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/debug/view.h"
#include "webgpu/webgpu.h"

typedef struct {
  const WGPUCommandEncoder command_encoder;
  Projection *views;
  const size_t layer;
  const ubo_id_t ubo_offset;
} ShadowMapDrawDirLightDescriptor;

typedef struct {
  const ShadowMapLightType type;
  const size_t layer;
  const ubo_id_t ubo_offset;
  WGPUCommandEncoder command_encoder;
} ShadowMapDrawDescriptor;

EXTERN_C_BEGIN

static inline void shadow_map_draw_spot_light(ShadowMap *, const size_t,
                                              const WGPUCommandEncoder);

static inline void shadow_map_draw_sun_light(ShadowMap *, const size_t,
                                             const WGPUCommandEncoder);

static inline void shadow_map_draw_point_light(ShadowMap *, const size_t,
                                               const WGPUCommandEncoder);

static inline void
shadow_map_draw_dir_light(ShadowMap *, const ShadowMapDrawDirLightDescriptor *);

static inline void shadow_map_draw_all(ShadowMap *);

static inline void shadow_map_draw_all_point_light(ShadowMap *);
static inline void shadow_map_draw_all_spot_light(ShadowMap *);
static inline void shadow_map_draw_all_sun_light(ShadowMap *);

static inline void shadow_map_draw(ShadowMap *,
                                   const ShadowMapDrawDescriptor *);

static int debug_view_count = 0;

/**
   The building shadow phase is segmented in numerous steps:

   1. BINDING:
   For each scene point and spot lights, we bind the relative views to
   the mesh shadow shader view matrix uniform

   2. DRAWING:
   Once the view matrix is bound, the mesh is ready to drawn with its shadow
  shader. For each light views, each mesh will be renderder (under a certain
  point of view)

   3. STORING:
  The rendered mesh will be stored in a dedicated Texture that will be held in a
  arrayed texture in the Light list

  4. TRANSFERT
  Once the rendered textures are stored, they will be uploaded in the mesh
  "default shader" as a texture and sampler to be read and reused for calculate
  the shadow

 */
void shadow_map_draw(ShadowMap *sm, const ShadowMapDrawDescriptor *desc) {

  /*  Create a new "nested" texture view for each layer that points back to the
     texture。Both "global Texture view" and "indexed Texture view" point toward
                               the same texture

                           .----------------------.
                           |      WGPUTexture     |
                           '----------------------'
                   .-------------------'-------------------.
                   |                                       |
   .----------------------------------.   .-------------------------------.
   |    WGPUTextureView<all layers>   |   |    WGPUTextureView<layer N>   |
   '----------------------------------'   '-------------------------------'
     Final Texture with all layers         Target a specific texture layer

                   |                                      |
                   |                                      |
                   |		 	 .-------------------------------.
                   |			 |          1.Renderpass         |
                   |			 '-------------------------------'
                   |                         Render to specific layer
                   |
                   |
    .-------------------------------.
    |  2. Shader<texture 2D array>  |
    '-------------------------------'
        Bind the Array to shader

        In our case we create 2 layer views: one for color, and another for
      depth. The color one will mostly be used for debugging purpose, whereas
           the depth one will be used for comparison in texture shader

   */

  profiler_latency_start(sm->profiler, ProfilerLatencyType_ShadowPass);
  {
    ShadowMapLight *light_group = &sm->lights[desc->type];
    RenderPass *render_pass = &light_group->render_pass;

    WGPUTextureView layer_view_depth =
        light_group->view.layers.depth[desc->layer];

    WGPUTextureView layer_view_color =
        light_group->view.layers.color[desc->layer];

    WGPUTextureView cached_view_color = render_pass->color.attachment.view;
    WGPUTextureView cached_view_depth = render_pass->depth.attachment.view;

    // sometimes pass encoder may be already set if we batch update all lights
    if (desc->command_encoder == NULL)
      render_pass_im_begin(render_pass);
    {

      RenderPassDrawOptions layer_views = {
          .color = layer_view_color,
          .depth = layer_view_depth,
      };

      render_pass_im_set_views(render_pass, &layer_views);

      // update each mesh shadow uniforms with current light view
      MeshDrawPacketList *drawn_meshes =
          &light_group->render_pass.layouts.entries->drawn_meshes;

      for (size_t i = 0; i < drawn_meshes->count; i++) {
        Mesh *mesh = drawn_meshes->entries[i].mesh;
        Shader *shadow_shader = mesh_shader(mesh, MeshShader_Shadow);
        shader_update_bind_group_offset(shadow_shader, 0, 0, desc->ubo_offset,
                                        ShaderUpdateFlag_None);
      }

      render_pass_im_draw(render_pass);

      // set back original view
      RenderPassDrawOptions source_views = {
          .color = cached_view_color,
          .depth = cached_view_depth,
      };
      render_pass_im_set_views(render_pass, &source_views);
    }
    if (desc->command_encoder == NULL)
      render_pass_im_end(render_pass);

    if (sm->debug.scene_debug && debug_view_count++ < sm->debug.max_views)
      scene_debug_view_create(sm->debug.scene_debug, layer_view_color);
  }
  profiler_latency_end(sm->profiler, ProfilerLatencyType_ShadowPass);
}

/**
   ▗▄▄▄ ▗▄▄▖  ▗▄▖ ▗▖ ▗▖     ▗▄▖ ▗▖   ▗▖
   ▐▌  █▐▌ ▐▌▐▌ ▐▌▐▌ ▐▌    ▐▌ ▐▌▐▌   ▐▌
   ▐▌  █▐▛▀▚▖▐▛▀▜▌▐▌ ▐▌    ▐▛▀▜▌▐▌   ▐▌
   ▐▙▄▄▀▐▌ ▐▌▐▌ ▐▌▐▙█▟▌    ▐▌ ▐▌▐▙▄▄▖▐▙▄▄▖

    Shadow map array Generation:
    - bind light view projection
    - render each mesh under lights POV
    - render to the shadow array
 */

void shadow_map_draw_all(ShadowMap *sm) {

  // logger_add(LoggerFlag_Process, "Computing all shadow maps...");

  /*
     When using "global" shadow encoder and passing it through all the passes
     create an async conflict issue: the point light view probably takes longer
     to be calculated and gets overriden by the the sun point of view.

     A solution to this is to add an offset or use a command encoder per light

     The overall flow would be:

     1. set the shadow pipeline minBindSize to:
     .------------------------------------------------------------------------.
     |                      Mp * Nvp + Ms + Mn * sizeof(mat4)                 |
     |	               ex:  16 *  6  + 16 + 16 *    64                        |
     |                                                                        |
     |  - Mp: max point light                                                 |
     |  - Nvp: number of point light view (6)                                 |
     |  - Ms: max spot light                                                  |
     |  - Mn: max sun light                                                   |
     '------------------------------------------------------------------------'

     2. For each light: push the view to the ViewArray at correct index (cache?)
     .------------------------------------------------------------------------.
     |  Offset 0 × sizeof(mat4)       → point light view matrix face 0        |
     |  Offset 1 × sizeof(mat4)       → point light view matrix face 1        |
     |  ...                                                                   |
     |  Offset 5 × sizeof(mat4)       → point light view matrix face 5        |
     |  Offset 6 × sizeof(mat4)       → sun light view matrix                 |
     |  ...                                                                   |
     '------------------------------------------------------------------------'

      3. Write to each mesh shadow view uniform the views with the
      index * sizeof(mat4) as offset. (shader_update_uniform)

      4. Draw the mesh with the same offset (wgpuRenderPassEncoderSetBindGroup)

   */

  ShadowMapLight *point_lights = &sm->lights[ShadowMapLightType_Point];
  ShadowMapLight *dir_lights = &sm->lights[ShadowMapLightType_Directional];

  const size_t point_count = point_lights->caster.point.count;
  const size_t spot_count = dir_lights->caster.directional.spot.count;
  const size_t sun_count = dir_lights->caster.directional.sun.count;

  debug_view_count = 0;

  /* ==== Point Lights ==== */
  {
    RenderPass *render_pass = &point_lights->render_pass;
    WGPUCommandEncoder encoder = render_pass_im_begin(render_pass);

    for (size_t p = 0; p < point_count; p++)
      shadow_map_draw_point_light(sm, p, encoder);

    render_pass_im_end(render_pass);
  }

  /* ==== Directional Lights (Spot + Sun) ==== */
  {
    RenderPass *render_pass = &dir_lights->render_pass;
    WGPUCommandEncoder encoder = render_pass_im_begin(render_pass);

    for (size_t p = 0; p < spot_count; p++)
      shadow_map_draw_spot_light(sm, p, encoder);

    for (size_t p = 0; p < sun_count; p++)
      shadow_map_draw_sun_light(sm, shadow_map_sun_layer(sm, p), encoder);

    render_pass_im_end(render_pass);
  }
}

/**
   ▗▄▄▄ ▗▄▄▖  ▗▄▖ ▗▖ ▗▖     ▗▄▄▖ ▗▄▖ ▗▖    ▗▄▖
   ▐▌  █▐▌ ▐▌▐▌ ▐▌▐▌ ▐▌    ▐▌   ▐▌ ▐▌▐▌   ▐▌ ▐▌
   ▐▌  █▐▛▀▚▖▐▛▀▜▌▐▌ ▐▌     ▝▀▚▖▐▌ ▐▌▐▌   ▐▌ ▐▌
   ▐▙▄▄▀▐▌ ▐▌▐▌ ▐▌▐▙█▟▌    ▗▄▄▞▘▝▚▄▞▘▐▙▄▄▖▝▚▄▞▘

 */

void shadow_map_draw_point_light(ShadowMap *sm, const size_t index,
                                 const WGPUCommandEncoder encoder) {

  PointLight *light =
      sm->lights[ShadowMapLightType_Point].caster.point.entries[index];

  // render scene and store depth map for each view
  for (size_t v = 0; v < light->views.count; v++) {

    // Render scene (create shadow render pass to texture layer)
    size_t layer = index * light->views.count + v;

    ShadowMapDrawDescriptor draw_desc = {
        .type = ShadowMapLightType_Point,
        .layer = layer,
        .command_encoder = encoder,
        .ubo_offset = light->ubo_projection[v].id,
    };
    shadow_map_draw(sm, &draw_desc);
  }
}

/**
   Function to handle both spot and sun light drawing
 */
void shadow_map_draw_dir_light(ShadowMap *sm,
                               const ShadowMapDrawDirLightDescriptor *desc) {

  for (size_t v = 0; v < desc->views->count; v++) {
    ShadowMapDrawDescriptor draw_desc = {
        .type = ShadowMapLightType_Directional,
        .layer = desc->layer,
        .ubo_offset = desc->ubo_offset,
        .command_encoder = desc->command_encoder,
    };
    shadow_map_draw(sm, &draw_desc);
  }
}

void shadow_map_draw_sun_light(ShadowMap *sm, const size_t index,
                               const WGPUCommandEncoder encoder) {

  SunLight *light = sm->lights[ShadowMapLightType_Directional]
                        .caster.directional.sun.entries[index];

  ShadowMapDrawDirLightDescriptor draw_desc = {
      .command_encoder = encoder,
      .layer = shadow_map_sun_layer(sm, index),
      .ubo_offset = light->ubo_projection.id,
      .views = &light->views,
  };
  shadow_map_draw_dir_light(sm, &draw_desc);
}

void shadow_map_draw_spot_light(ShadowMap *sm, const size_t index,
                                const WGPUCommandEncoder encoder) {

  SpotLight *light = sm->lights[ShadowMapLightType_Directional]
                         .caster.directional.spot.entries[index];

  ShadowMapDrawDirLightDescriptor draw_desc = {
      .command_encoder = encoder,
      .layer = index,
      .ubo_offset = light->ubo_projection.id,
      .views = &light->views,
  };
  shadow_map_draw_dir_light(sm, &draw_desc);
}

void shadow_map_draw_all_point_light(ShadowMap *sm) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Point];
  const size_t count = group->caster.point.count;

  RenderPass *render_pass = &group->render_pass;
  WGPUCommandEncoder encoder = render_pass_im_begin(render_pass);

  for (size_t p = 0; p < count; p++)
    shadow_map_draw_point_light(sm, p, encoder);

  render_pass_im_end(render_pass);
}

void shadow_map_draw_all_spot_light(ShadowMap *sm) {
  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  const size_t count = group->caster.directional.spot.count;

  RenderPass *render_pass = &group->render_pass;
  WGPUCommandEncoder encoder = render_pass_im_begin(render_pass);

  for (size_t p = 0; p < count; p++)
    shadow_map_draw_spot_light(sm, p, encoder);

  render_pass_im_end(render_pass);
}

void shadow_map_draw_all_sun_light(ShadowMap *sm) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  const size_t count = group->caster.directional.sun.count;

  RenderPass *render_pass = &group->render_pass;
  WGPUCommandEncoder encoder = render_pass_im_begin(render_pass);

  for (size_t p = 0; p < count; p++)
    shadow_map_draw_sun_light(sm, p, encoder);

  render_pass_im_end(render_pass);
}

EXTERN_C_END
#endif
