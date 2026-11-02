#include "core.h"
#include "backend/resource_manager.h"

typedef struct {
  WGPUTexture *color;
  WGPUTexture *depth;
  WGPUTextureView *color_view;
  WGPUTextureView *depth_view;
  const TextureResolution resolution;
  const RenderPipelineMultisampleCount multisample;
  const size_t layer_count;
  const WGPUTextureViewDimension view_dimension;
} ProbeReflectionTextureDescriptor;

static inline void
probe_reflection_list_create_texture(const ProbeReflectionTextureDescriptor *);

void probe_reflection_list_create_texture(
    const ProbeReflectionTextureDescriptor *desc) {

  /*
    === COLOR ===
   */

  *desc->color = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Probe Reflection List Texture Color",
      .size =
          (WGPUExtent3D){
              .width = desc->resolution,
              .height = desc->resolution,
              .depthOrArrayLayers = desc->layer_count,
          },
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .usage = WGPUTextureUsage_TextureBinding   // read texture in shader
               | WGPUTextureUsage_StorageBinding // write texture in shader
               | WGPUTextureUsage_CopyDst        // upload the input data
               | WGPUTextureUsage_RenderAttachment,
      .dimension = WGPUTextureDimension_2D,
      .mipLevelCount = PROBE_REFLECTION_MIPMAP_COUNT,
      .sampleCount = 1,
  });

  *desc->color_view = rem_new_view(
      *desc->color, &(WGPUTextureViewDescriptor){
                        .label = "Probe Reflection List View Color",
                        .format = wgpuTextureGetFormat(*desc->color),
                        .dimension = desc->view_dimension,
                        .baseMipLevel = PROBE_REFLECTION_MIPMAP_COUNT -
                                        1, // Use level 1 for blurriness
                        .mipLevelCount = 1,
                        .baseArrayLayer = 0,
                        .arrayLayerCount = desc->layer_count,
                        .aspect = WGPUTextureAspect_Undefined,
                    });

  /*
    === DEPTH ===
   */

  *desc->depth = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Probe Reflection List Texture Depth",
      .size =
          (WGPUExtent3D){
              .width = desc->resolution,
              .height = desc->resolution,
              .depthOrArrayLayers = desc->layer_count,
          },
      .format = TEXTURE_FORMAT_DEPTH,
      .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_TextureBinding,
      .dimension = WGPUTextureDimension_2D,
      .mipLevelCount = 1,
      .sampleCount = 1,
  });

  *desc->depth_view = rem_new_view(
      *desc->depth, &(WGPUTextureViewDescriptor){
                        .label = "Probe Reflection List View Depth",
                        .dimension = desc->view_dimension,
                        .format = wgpuTextureGetFormat(*desc->depth),
                        .baseMipLevel = 0,
                        .mipLevelCount = 1,
                        .baseArrayLayer = 0,
                        .arrayLayerCount = desc->layer_count,
                        .aspect = WGPUTextureAspect_DepthOnly,
                    });
}

void renderer_probe_reflection_create_pass(
    Renderer *renderer, RenderPass *handle,
    const RendererProbeReflectionDescriptor *desc) {

  // create list textures array
  WGPUTexture color_texture, depth_texture;
  WGPUTextureView color_view, depth_view;

  probe_reflection_list_create_texture(&(ProbeReflectionTextureDescriptor){
      .color = &color_texture,
      .depth = &depth_texture,
      .color_view = &color_view,
      .depth_view = &depth_view,
      .resolution = desc->resolution,
      .view_dimension = desc->view_dimension,
      .layer_count = desc->layer_count,
  });

  // create render pass preset
  render_pass_create(
      handle,
      &(RenderPassCreateDescriptor){
          .type = RenderPassType_OffScreen,
          .label = "Probe Reflection List Render Pass",
          .height = desc->resolution,
          .width = desc->resolution,
          .draw_list = desc->draw_list,
          .multisample = PipelineMultisampleCount_1x,
          .color =
              &(RenderPassColorAttachment){
                  .texture = color_texture,
                  .attachment =
                      {
                          .clearValue = {0},
                          .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                          .loadOp = WGPULoadOp_Clear,
                          .storeOp = WGPUStoreOp_Store,
                          .view = color_view,
                      },
              },
          .depth =
              &(RenderPassDepthAttachment){
                  .texture = depth_texture,
                  .attachment =
                      {
                          .depthClearValue = 1.0f,
                          .depthLoadOp = WGPULoadOp_Clear,
                          .depthStoreOp = WGPUStoreOp_Store,
                          .depthReadOnly = false,
                          .view = depth_view,
                      },
              },
      });
}

void renderer_probe_reflection_update_resolution(
    RenderPass *pass, const TextureResolution resolution,
    const WGPUTextureViewDimension view_dimension) {

  if (resolution == wgpuTextureGetWidth(pass->color.texture) ||
      resolution == wgpuTextureGetHeight(pass->color.texture))
    return;

  uint32_t layer_count = wgpuTextureGetDepthOrArrayLayers(pass->color.texture);

  // === clean up ===
  {
    rem_destroy_view(&pass->color.views[0]);
    rem_destroy_texture(&pass->color.texture);

    rem_destroy_view(&pass->depth.views[0]);
    rem_destroy_texture(&pass->depth.texture);
  }

  probe_reflection_list_create_texture(&(ProbeReflectionTextureDescriptor){
      .color = &pass->color.texture,
      .color_view = &pass->color.views[0],
      .depth = &pass->depth.texture,
      .depth_view = &pass->depth.views[0],
      .view_dimension = view_dimension,
      .layer_count = layer_count,
      .resolution = resolution,
  });

  pass->color.attachment.view = pass->color.views[0];
  pass->depth.attachment.view = pass->depth.views[0];
}



void probe_reflection_list_draw_preprocessor(const RenderPass *pass, Mesh *mesh,
                                             void *data) {

  ProbeReflectionListPreprocessorData *cast_data =
      (ProbeReflectionListPreprocessorData *)data;

  Shader *shader = mesh_shader(mesh, MeshShader_Reflection);
  shader_update_bind_group_offset(shader, 0, 1, cast_data->camera_offset,
                                  ShaderUpdateFlag_None);
}
