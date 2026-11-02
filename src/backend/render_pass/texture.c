#include "texture.h"

#include <stddef.h>
#include <stdint.h>

#include "backend/context.h"
#include "backend/logger.h"
#include "backend/render_pass/core.h"
#include "backend/resource_manager.h"
#include "runtime/pipeline/render.h"
#include "runtime/texture/core.h"
#include "webgpu/webgpu.h"

static inline void render_pass_texture_view_destroy(WGPUTexture *,
                                                    WGPUTextureView *);

void render_pass_texture_view_destroy(WGPUTexture *texture,
                                      WGPUTextureView *view) {
  rem_destroy_texture(texture);
  rem_destroy_view(view);
}

/**

   ▗▄▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖▗▖ ▗▖▗▄▄▖ ▗▄▄▄▖ ▗▄▄▖
     █  ▐▌    ▝▚▞▘   █  ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌
     █  ▐▛▀▀▘  ▐▌    █  ▐▌ ▐▌▐▛▀▚▖▐▛▀▀▘ ▝▀▚▖
     █  ▐▙▄▄▖▗▞▘▝▚▖  █  ▝▚▄▞▘▐▌ ▐▌▐▙▄▄▖▗▄▄▞▘

   Create the texture and texture view for the multisampling rendering.

 */
void render_pass_texture_create_multisample(
    WGPUTexture *texture, WGPUTextureView *view,
    const RenderPassTextureDescriptor *desc, const RenderPassTextureFlag flag) {

  if (flag & RenderPassTextureFlag_ReleasePrevious)
    render_pass_texture_view_destroy(texture, view);

  WGPUTextureFormat format = desc->format;

  if (format == WGPUTextureFormat_Undefined)
    format = TEXTURE_FORMAT_ONSCREEN;

  if (desc->multisample == 0) {
    logger_add(LoggerFlag_Warning,
               "Multisample provided is not valid (%d), make sure the "
               "render pass is correctly initialised.",
               desc->multisample);
    return;
  }

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "MSAA Texture",
      .usage =
          WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
      .size =
          (WGPUExtent3D){
              .width = desc->width,
              .height = desc->height,
              .depthOrArrayLayers = 1,
          },
      .format = TEXTURE_FORMAT_ONSCREEN, // swapchain format
      .sampleCount = desc->multisample,
      .mipLevelCount = 1,
  });

  *view = rem_new_view(*texture, NULL);
}

/*
   Pass 1---.
   Pass 2---+--=> MSAA (4x) => RESOLVE (1X) => BLIT => SWAPCHAIN
   Pass 3---'
 */
void render_pass_texture_create_monosample(
    WGPUTexture *texture, WGPUTextureView *view,
    const RenderPassTextureDescriptor *desc, const RenderPassTextureFlag flag) {

  if (flag & RenderPassTextureFlag_ReleasePrevious)
    render_pass_texture_view_destroy(texture, view);

  WGPUTextureFormat format = desc->format;

  if (format == WGPUTextureFormat_Undefined)
    format = TEXTURE_FORMAT_ONSCREEN;

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Monosample Texture",
      .usage =
          WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
      .size =
          (WGPUExtent3D){
              .width = desc->width,
              .height = desc->height,
              .depthOrArrayLayers = 1,
          },
      .format = format, // swapchain format
      .sampleCount = PipelineMultisampleCount_1x,
      .mipLevelCount = 1,
  });

  *view = rem_new_view(*texture, NULL);
}

void render_pass_texture_create_color(RenderPass *pass,
                                      const RenderPassTextureDescriptor *desc,
                                      const RenderPassTextureFlag flag) {

  // create msaa texture as main color view
  if (pass->color.attachment.view == NULL) {

    if (desc->multisample == PipelineMultisampleCount_4x)
      render_pass_texture_create_multisample(
          &pass->color.texture, &pass->color.attachment.view, desc, flag);

    if (desc->multisample == PipelineMultisampleCount_1x)
      render_pass_texture_create_monosample(
          &pass->color.texture, &pass->color.attachment.view, desc, flag);
  }

  // create resolve texture (for blit/post-process passes)
  if (RenderPassType_OnScreen == pass->type) {

    if (PipelineMultisampleCount_1x == desc->multisample) {
      pass->color.resolve_view = pass->color.attachment.view;
      pass->color.attachment.resolveTarget = NULL;
    }

    if (PipelineMultisampleCount_4x == desc->multisample)
      render_pass_texture_create_monosample(
          &pass->color.resolve_texture, &pass->color.resolve_view, desc, flag);
  }
}

static inline void
render_pass_texture_depth(WGPUTexture *, WGPUTextureView *,
                          const RenderPassTextureDescriptor *,
                          const RenderPassTextureFlag flag);

void render_pass_texture_depth(WGPUTexture *texture, WGPUTextureView *view,
                               const RenderPassTextureDescriptor *desc,
                               const RenderPassTextureFlag flag) {

  if (flag & RenderPassTextureFlag_ReleasePrevious)
    render_pass_texture_view_destroy(texture, view);

  WGPUTextureFormat format = desc->format;
  WGPUTextureAspect aspect = WGPUTextureAspect_DepthOnly;

  if (format == WGPUTextureFormat_Undefined)
    format = TEXTURE_FORMAT_DEPTH;
  else if (format == WGPUTextureFormat_Depth24PlusStencil8)
    aspect = WGPUTextureAspect_All;

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Render Pass Depth Texture",
      .usage = WGPUTextureUsage_RenderAttachment,
      .size =
          (WGPUExtent3D){
              .width = desc->width,
              .height = desc->height,
              .depthOrArrayLayers = 1,
          },
      .format = format,
      .mipLevelCount = 1,
      .sampleCount = desc->multisample,
      .dimension = WGPUTextureDimension_2D,
  });

  *view = wgpuTextureCreateView(*texture,
                                &(WGPUTextureViewDescriptor){
                                    .label = "Render Pass Depth Texture View",
                                    .format = format,
                                    .dimension = WGPUTextureViewDimension_2D,
                                    .baseMipLevel = 0,
                                    .mipLevelCount = 1,
                                    .baseArrayLayer = 0,
                                    .arrayLayerCount = 1,
                                    .aspect = aspect,
                                });
}

/**
  Need to create a texture view for Z buffer stencil
  by default set depth based on draw call order (first ones in
  backgrounds...)

  => Need to create a depth texture: a hidden buffer storing depth values for
  each pixel
*/
void render_pass_texture_create_depth(RenderPass *pass,
                                      const RenderPassTextureDescriptor *desc,
                                      const RenderPassTextureFlag flag) {

  render_pass_texture_depth(&pass->depth.texture, &pass->depth.attachment.view,
                            desc, flag);
}

void render_pass_list_texture_create_shared_color(
    RenderPassList *list, const RenderPassTextureDescriptor *desc,
    WGPUTexture *dst_t, WGPUTextureView *dst_v,
    const RenderPassTextureFlag flag) {

  if (desc->multisample == PipelineMultisampleCount_1x) {
    render_pass_texture_create_monosample(&list->shared.color.texture,
                                          &list->shared.color.view, desc, flag);
  } else if (desc->multisample == PipelineMultisampleCount_4x) {
    render_pass_texture_create_multisample(
        &list->shared.color.texture, &list->shared.color.view, desc, flag);
  }

  {
    if (dst_t)
      *dst_t = list->shared.color.texture;

    if (dst_v)
      *dst_v = list->shared.color.view;
  }

  // replace all passes views with the shared one
  if (flag & RenderPassTextureFlag_AssignChildPasses)
    for (uint16_t i = 0; i < list->count; i++) {
      RenderPass *pass = &list->layers[i];

      // eventually release the old one
      if (flag & RenderPassTextureFlag_ReleasePrevious)
        render_pass_texture_destroy_color(pass);

      pass->color.attachment.view = list->shared.color.view;
      pass->color.texture = list->shared.color.texture;
    }
}

void render_pass_list_texture_create_shared_depth(
    RenderPassList *list, const RenderPassTextureDescriptor *desc,
    WGPUTexture *dst_t, WGPUTextureView *dst_v,
    const RenderPassTextureFlag flag) {

  render_pass_texture_depth(&list->shared.depth.texture,
                            &list->shared.depth.view, desc, flag);

  {
    if (dst_t)
      *dst_t = list->shared.depth.texture;

    if (dst_v)
      *dst_v = list->shared.depth.view;
  }

  // replace all passes views with the shared one
  if (flag & RenderPassTextureFlag_AssignChildPasses)
    for (uint16_t i = 0; i < list->count; i++) {
      RenderPass *pass = &list->layers[i];

      // eventually release the old one
      if (flag & RenderPassTextureFlag_ReleasePrevious)
        render_pass_texture_destroy_depth(pass);

      pass->depth.attachment.view = list->shared.depth.view;
      pass->depth.texture = list->shared.depth.texture;
    }
}

void render_pass_texture_destroy_color(RenderPass *pass) {
  rem_destroy_view(&pass->color.attachment.view);
  rem_destroy_texture(&pass->color.texture);
}
void render_pass_texture_destroy_depth(RenderPass *pass) {
  rem_destroy_view(&pass->depth.attachment.view);
  rem_destroy_texture(&pass->depth.texture);
}

void render_pass_list_destroy_shared_texture_color(RenderPassList *list) {
  rem_destroy_view(&list->shared.color.view);
  rem_destroy_texture(&list->shared.color.texture);
}

void render_pass_list_destroy_shared_texture_depth(RenderPassList *list) {
  rem_destroy_view(&list->shared.depth.view);
  rem_destroy_texture(&list->shared.depth.texture);
}
