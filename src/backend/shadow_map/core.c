#include "core.h"

#include "backend/context.h"
#include "backend/logger.h"
#include "backend/render_pass/core.h"
#include "backend/renderer/mesh_config.h"
#include "backend/resource_manager.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include "utils/math.h"
#include "webgpu/webgpu.h"

static inline ShadowMapStatus create_render_pass(ShadowMapLight *,
                                                 const TextureResolution,
                                                 const TextureResolution);

static inline ShadowMapStatus light_group_init(ShadowMapLight *,
                                               const ShadowMapLightType,
                                               const TextureResolution,
                                               const size_t,
                                               const WGPUTextureViewDimension);

static inline ShadowMapStatus light_group_destroy(ShadowMapLight *,
                                                  const ShadowMapLightType);

/**
                             For each shadow light:

               .-----------------.           .-----------------.
               |  Color Texture  |           |  Depth Texture  |
               '-----------------'           '-----------------'
                       |                              |
               .=======|====== RENDER LIGHT POV ======|=======.
               |       |                              |       |
               |       |     .----- vertex ----.      |       |
               |       |     |    Depth Pass   | <----|       |
               |       |     '-----------------'      |       |
               |       |              |               |       |
               |       |     .--- fragment ---.       |       |
               |       |---> |   Color Pass   |       |       |
               |       |     '----------------'       |       |
               |       |                              |       |
               '=======|==============================|======='
                       |_____________.    .___________|
                                     |   |
                            .---------------------.
                            | Light Texture Array |
                            '---------------------'

      Spot and Sun lights are all together stacked up in the same "directional
      light" texture array.
      Compared to the point lights that have a dedicated
      cubemap uniform entry.

      Another semantic to note is that Spot Lights and Sun Lights are both
      encompassed under the "Directional Light" term.

      The order is the following:

      .--------------. -----.
      |   Layer 0    |      |
      |--------------|      |
      |   Layer 1    |      |     .---------------.
      |--------------|       >----|  Spot Lights  | --.
      |   Layer 2    |      |     '--------------'    |
      |--------------|      |                         |
      |   Layer 3    | _____'                         |    .-------------------.
      |--------------| -----.                          >--| Directional Lights |
      |   Layer 4    |      |                         |   '--------------------'
      |--------------|      |     .--------------.    |
      |   Layer 5    |       >----|  Sun Lights  |----'
      |--------------|      |     '--------------'
      |   Layer 6    |      |
      '--------------' -----'

   */

ShadowMapStatus shadow_map_init(ShadowMap *sm,
                                const ShadowMapInitDescriptor *desc) {

  logger_add(LoggerFlag_Process, "Creating scene shadow map textures...");

  sm->profiler = desc->profiler;
  sm->debug = desc->debug;

  struct {
    const size_t layer_count;
    const WGPUTextureViewDimension dimension;
    RenderPass *pass;
  } light_config[ShadowMapLightType_COUNT] = {
      [ShadowMapLightType_Point] =
          {
              LIGHT_MAX_CAPACITY * LIGHT_POINT_VIEWS,
              WGPUTextureViewDimension_CubeArray,
              &sm->lights[ShadowMapLightType_Point].render_pass,
          },
      [ShadowMapLightType_Directional] =
          {
              LIGHT_MAX_CAPACITY,
              WGPUTextureViewDimension_2DArray,
              &sm->lights[ShadowMapLightType_Directional].render_pass,
          },
  };

  for (ShadowMapLightType i = 0; i < ShadowMapLightType_COUNT; i++) {
    ShadowMapLight *light_group = &sm->lights[i];

    light_group_init(light_group, (ShadowMapLightType)i, SHADOW_MAP_SIZE,
                     light_config[i].layer_count, light_config[i].dimension);

    create_render_pass(light_group, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
  }

  return ShadowMapStatus_Success;
}

ShadowMapStatus shadow_map_destroy(ShadowMap *sm) {

  for (ShadowMapLightType i = 0; i < ShadowMapLightType_COUNT; i++)
    light_group_destroy(&sm->lights[i], (ShadowMapLightType)i);

  *sm = (ShadowMap){0};

  return ShadowMapStatus_Success;
}

ShadowMapStatus light_group_init(ShadowMapLight *group,
                                 const ShadowMapLightType type,
                                 const TextureResolution res,
                                 const size_t layer_count,
                                 const WGPUTextureViewDimension dimension) {

  WGPUTexture *color_texture = &group->texture.color;
  WGPUTexture *depth_texture = &group->texture.depth;

  WGPUTextureView *depth_view = &group->view.depth;
  WGPUTextureView *color_view = &group->view.depth;

  // Create global texture
  shadow_map_texture_create(&(ShadowPassTextureDescriptor){
      .dimension = dimension,
      .layer_count = layer_count,
      .width = SHADOW_MAP_SIZE,
      .height = SHADOW_MAP_SIZE,
      .color_texture = color_texture,
      .color_view = color_view,
      .depth_texture = depth_texture,
      .depth_view = depth_view,
  });

  // Cached Individual Texture Views for each lights for faster access during
  // draw/update

  dyli_create((void *)&group->view.layers.color, &group->view.layers.capacity,
              &group->view.layers.count, sizeof(WGPUTextureView), layer_count,
              "Shadow Map Layer Color");

  dyli_create((void *)&group->view.layers.depth, &group->view.layers.capacity,
              &group->view.layers.count, sizeof(WGPUTextureView), layer_count,
              "Shadow Map Layer Depth");

  for (size_t i = 0; i < layer_count; i++) {
    group->view.layers.depth[i] =
        rem_new_view(group->texture.depth,
                     &(WGPUTextureViewDescriptor){
                         .label = "Shadow per layer texture view - Depth",
                         .format = SHADOW_DEPTH_FORMAT,
                         .dimension = WGPUTextureViewDimension_2D,
                         .baseArrayLayer = i,
                         .arrayLayerCount = 1,
                         .mipLevelCount = 1,
                         .baseMipLevel = 0,
                     });

    group->view.layers.color[i] =
        rem_new_view(group->texture.color,
                     &(WGPUTextureViewDescriptor){
                         .label = "Shadow per layer texture view - Color",
                         .format = SHADOW_COLOR_FORMAT,
                         .dimension = WGPUTextureViewDimension_2D,
                         .baseArrayLayer = i,
                         .arrayLayerCount = 1,
                         .mipLevelCount = 1,
                         .baseMipLevel = 0,
                     });
  }

  switch (type) {

  case ShadowMapLightType_Point:
    dyli_create((void *)&group->caster.point.entries,
                &group->caster.point.capacity, &group->caster.point.count,
                sizeof(PointLight *), SHADOW_MAP_LAYER_CAPACITY,
                "Shadow Map Caster - Point");
    break;

  case ShadowMapLightType_Directional:
    dyli_create((void *)&group->caster.directional.sun.entries,
                &group->caster.directional.sun.capacity,
                &group->caster.directional.sun.count, sizeof(SunLight *),
                SHADOW_MAP_LAYER_CAPACITY, "Shadow Map Caster - Sun");

    dyli_create((void *)&group->caster.directional.spot.entries,
                &group->caster.directional.spot.capacity,
                &group->caster.directional.spot.count, sizeof(SpotLight *),
                SHADOW_MAP_LAYER_CAPACITY, "Shadow Map Caster - Spot");
    break;

  default:
    break;
  }

  return ShadowMapStatus_Success;
}

ShadowMapStatus light_group_destroy(ShadowMapLight *group,
                                    const ShadowMapLightType type) {

  rem_destroy_texture(&group->texture.color);
  rem_destroy_texture(&group->texture.depth);

  rem_destroy_view(&group->view.color);
  rem_destroy_view(&group->view.depth);

  for (size_t i = 0; i < group->view.layers.count; i++) {
    rem_destroy_view(&group->view.layers.color[i]);
    rem_destroy_view(&group->view.layers.depth[i]);
  }

  dyli_free((void *)&group->view.layers.color, &group->view.layers.capacity,
            &group->view.layers.count);

  dyli_free((void *)&group->view.layers.depth, &group->view.layers.capacity,
            &group->view.layers.count);

  render_pass_destroy(&group->render_pass);

  switch (type) {

  case ShadowMapLightType_Point:
    dyli_free((void *)&group->caster.point.entries,
              &group->caster.point.capacity, &group->caster.point.count);
    break;

  case ShadowMapLightType_Directional:
    dyli_free((void *)&group->caster.directional.sun.entries,
              &group->caster.directional.sun.capacity,
              &group->caster.directional.sun.count);

    dyli_free((void *)&group->caster.directional.spot.entries,
              &group->caster.directional.spot.capacity,
              &group->caster.directional.spot.count);
    break;

  default:
    break;
  }

  return ShadowMapStatus_Success;
}

/**
   Create the two shadow textures (color and depth) for the point lights
   In our semantic we use both terms Maps and Textures, however they both
   serve different purpose.
   - The Shadow Texture holds the Shadow mapping.
   - The Shadow map is the result from our rendering.
 */
ShadowMapStatus
shadow_map_texture_create(const ShadowPassTextureDescriptor *desc) {

  // texture
  WGPUTextureDescriptor texture_descriptor_base = {
      .size =
          (WGPUExtent3D){
              .width = desc->width,
              .height = desc->height,
              .depthOrArrayLayers = desc->layer_count,
          },
      .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_TextureBinding,
      .dimension = WGPUTextureDimension_2D,
      .mipLevelCount = 1,
      .sampleCount = 1,
  };

  WGPUTextureDescriptor texture_descriptor_color = texture_descriptor_base;
  texture_descriptor_color.label = "Light shadow texture - Color";
  texture_descriptor_color.format = SHADOW_COLOR_FORMAT;

  WGPUTextureDescriptor texture_descriptor_depth = texture_descriptor_base;
  texture_descriptor_depth.label = "Light shadow texture - Depth";
  texture_descriptor_depth.format = SHADOW_DEPTH_FORMAT;

  // Create color texture
  WGPUTexture color_texture = rem_new_texture(&texture_descriptor_color);

  if (desc->color_texture)
    *desc->color_texture = color_texture;

  WGPUTextureView color_view = rem_new_view(
      color_texture, &(WGPUTextureViewDescriptor){
                         .label = "Light Shadow: global texture view - Color",
                         .format = SHADOW_COLOR_FORMAT,
                         .dimension = desc->dimension,
                         .baseMipLevel = 0,
                         .mipLevelCount = 1,
                         .baseArrayLayer = 0,
                         .arrayLayerCount = desc->layer_count,
                         .aspect = WGPUTextureAspect_Undefined,
                     });

  if (desc->color_view)
    *desc->color_view = color_view;

  // Create depth texture
  WGPUTexture depth_texture = rem_new_texture(&texture_descriptor_depth);

  if (desc->depth_texture)
    *desc->depth_texture = depth_texture;

  WGPUTextureView depth_view = rem_new_view(
      depth_texture, &(WGPUTextureViewDescriptor){
                         .label = "Light Shadow: global texture view - Depth",
                         .dimension = desc->dimension,
                         .format = SHADOW_DEPTH_FORMAT,
                         .baseMipLevel = 0,
                         .mipLevelCount = 1,
                         .baseArrayLayer = 0,
                         .arrayLayerCount = desc->layer_count,
                         .aspect = WGPUTextureAspect_DepthOnly,
                     });

  if (desc->depth_view)
    *desc->depth_view = depth_view;

  return ShadowMapStatus_Success;
}

ShadowMapStatus
shadow_map_update_light_resolution(ShadowMapLight *light,
                                   const TextureResolution resolution,
                                   const WGPUTextureViewDimension dimension) {

  if (resolution == wgpuTextureGetWidth(light->texture.color) ||
      resolution == wgpuTextureGetHeight(light->texture.color))
    return ShadowMapStatus_Skip;

  RenderPass *pass = &light->render_pass;
  const uint32_t layer_count =
      wgpuTextureGetDepthOrArrayLayers(light->texture.color);

  // clean up
  rem_destroy_texture(&light->texture.color);
  rem_destroy_texture(&light->texture.depth);

  rem_destroy_view(&light->view.color);
  rem_destroy_view(&light->view.depth);

  // recreate the texture with new dimension
  shadow_map_texture_create(&(ShadowPassTextureDescriptor){
      .dimension = dimension,
      .layer_count = layer_count,
      .width = resolution,
      .height = resolution,
      .color_texture = &light->texture.color,
      .color_view = &light->view.color,
      .depth_texture = &light->texture.depth,
      .depth_view = &light->view.depth,
  });

  // share new handles to existing render pass
  // TODO: add a render_pass_update_color(...)
  pass->color.texture = light->texture.color;
  pass->depth.texture = light->texture.depth;

  pass->color.views[0] = light->view.color;
  pass->depth.views[0] = light->view.depth;

  pass->color.attachment.view = pass->color.views[0];
  pass->depth.attachment.view = pass->depth.views[0];

  return ShadowMapStatus_Success;
}

ShadowMapStatus create_render_pass(ShadowMapLight *light_group,
                                   const TextureResolution width,
                                   const TextureResolution height) {

  WGPUTexture color_texture = light_group->texture.color;
  WGPUTexture depth_texture = light_group->texture.depth;

  WGPUTextureView depth_view = light_group->view.depth;
  WGPUTextureView color_view = light_group->view.depth;

  RenderPass *render_pass = &light_group->render_pass;

  RenderPassLayoutListDescriptor draw_list = {
      .count = 1,
      .entries =
          {
              {
                  .shader = MeshShader_Shadow,
                  .pipeline = RenderPipelineType_Shadow,
                  .topology_callback = mesh_topology_base,
              },
          },
  };

  // set render pass
  render_pass_create(
      render_pass, &(RenderPassCreateDescriptor){
                       .type = RenderPassType_OffScreen,
                       .label = "Shadow Map Pass",
                       .color =
                           &(RenderPassColorAttachment){
                               .texture = color_texture,
                               .attachment =
                                   {
                                       .view = color_view,
                                       .clearValue = {0.0f, 0.0f, 0.0f, 1.0f},
                                       .loadOp = WGPULoadOp_Clear,
                                       .storeOp = WGPUStoreOp_Store,
                                       .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                                   },
                           },
                       .depth =
                           &(RenderPassDepthAttachment){
                               .texture = depth_texture,
                               .attachment =
                                   {
                                       .view = depth_view,
                                       .depthClearValue = 1.0f,
                                       .depthLoadOp = WGPULoadOp_Clear,
                                       .depthStoreOp = WGPUStoreOp_Store,
                                   },
                           },
                       .height = height,
                       .width = width,
                       .multisample = PipelineMultisampleCount_1x,
                       .draw_list = &draw_list,
                   });

  return ShadowMapStatus_Success;
}

ShadowMapStatus shadow_map_toggle_mesh(ShadowMap *sm, Mesh *mesh,
                                       bool remove_all) {

  for (size_t i = 0; i < ShadowMapLightType_COUNT; i++) {

    ShadowMapLight *group = &sm->lights[i];
    RenderPass *pass = &group->render_pass;

    for (size_t j = 0; j < pass->layouts.count; j++) {
      RenderPassLayout *layout = &pass->layouts.entries[j];

      bool toggled =
          render_pass_layout_toggle_mesh(layout, mesh, RendererDrawMode_Texture,
                                         RendererLayer_Default, remove_all);

      // DEBUG
      // printf("[%lu] mesh count: %lu \n", i, layout->drawn_meshes.count);
    }
  }

  return ShadowMapStatus_Success;
}
