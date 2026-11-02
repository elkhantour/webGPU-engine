#include "depth_preview.h"
#include "backend/context.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/shader/core.h"
#include "runtime/shader/update.h"
#include "utils/path.h"
#include <string.h>

typedef struct { // must match the WGSL struct (32 bytes)
  float near_z, far_z, lo, hi;
  uint32_t flags; // bit0: linearize, bit1: reversed Z
  uint32_t pad[3];
} DepthGpuParams;

static int is_depth_format(WGPUTextureFormat f) {
  switch (f) {
  case WGPUTextureFormat_Depth16Unorm:
  case WGPUTextureFormat_Depth24Plus:
  case WGPUTextureFormat_Depth24PlusStencil8:
  case WGPUTextureFormat_Depth32Float:
  case WGPUTextureFormat_Depth32FloatStencil8:
    return 1;
  default:
    return 0;
  }
}

int depth_preview_supports(WGPUTexture t) {
  if (!t)
    return 0;
  return wgpuTextureGetDimension(t) == WGPUTextureDimension_2D &&
         wgpuTextureGetSampleCount(t) == 1 &&
         (wgpuTextureGetUsage(t) & WGPUTextureUsage_TextureBinding) &&
         is_depth_format(wgpuTextureGetFormat(t));
}

// Create the pipeline once. Returns 1 if the pipeline is ready.
static int depth_preview_init(DepthPreview *dp) {

  if (dp->shader)
    return 1;
  if (dp->failed)
    return 0;

  WGPUDevice device = context_device();

  dp->shader = rem_new_shader();
  shader_create(dp->shader, &(ShaderCreateDescriptor){
                                .name = "Depth Vis",
                                .pipeline = std_render_pipeline(
                                    RenderPipelineType_DepthVis),
                            });

  if (!dp->shader) {
    depth_preview_destroy(dp);
    dp->failed = 1;
    return 0;
  }
  return 1;
}

WGPUTextureView depth_preview_render(DepthPreview *dp, WGPUTexture texture,
                                     int mip, int layer, uint32_t w, uint32_t h,
                                     const DepthPreviewParams *params) {

  if (!dp || !texture || !params || w == 0 || h == 0)
    return NULL;

  if (!depth_preview_init(dp))
    return NULL;

  WGPUDevice device = context_device();
  WGPUQueue queue = context_queue();

  // 1) RGBA8 target, same size as the shown mip
  if (!dp->target || dp->target_w != w || dp->target_h != h) {
    if (dp->target_view)
      wgpuTextureViewRelease(dp->target_view);
    if (dp->target)
      wgpuTextureRelease(dp->target);

    dp->target = wgpuDeviceCreateTexture(
        device, &(WGPUTextureDescriptor){
                    .label = "depth_preview_target",
                    .usage = WGPUTextureUsage_RenderAttachment |
                             WGPUTextureUsage_TextureBinding,
                    .dimension = WGPUTextureDimension_2D,
                    .size.width = w,
                    .size.height = h,
                    .size.depthOrArrayLayers = 1,
                    .format = WGPUTextureFormat_RGBA8Unorm,
                    .mipLevelCount = 1,
                    .sampleCount = 1,
                });

    dp->target_view =
        dp->target ? wgpuTextureCreateView(dp->target, NULL) : NULL;
    dp->target_w = w;
    dp->target_h = h;
    if (!dp->target_view)
      return NULL;
  }

  // 2) depth-only view of the selected mip/layer (as a 1-layer 2D array)
  if (!dp->src_view || dp->src_tex != texture || dp->src_mip != mip ||
      dp->src_layer != layer) {
    depth_preview_release_source(dp);

    WGPUTextureViewDescriptor vd = {
        .format = wgpuTextureGetFormat(texture),
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = (uint32_t)mip,
        .mipLevelCount = 1,
        .baseArrayLayer = (uint32_t)layer,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_DepthOnly,
    };
    dp->src_view = wgpuTextureCreateView(texture, &vd);
    if (!dp->src_view)
      return NULL;

    dp->src_tex = texture;
    dp->src_mip = mip;
    dp->src_layer = layer;
    shader_update_texture_view(dp->shader, 0, 0, dp->src_view, vd.format,
                               ShaderUpdateFlag_None);
  }

  // 3) params
  DepthGpuParams gpu = {
      .near_z = params->near_z,
      .far_z = params->far_z,
      .lo = params->range_min,
      .hi = params->range_max,
      .flags = (params->linearize ? 1u : 0u) | (params->reversed_z ? 2u : 0u),
  };
  shader_update_uniform_data(dp->shader, 0, 1, &gpu, ShaderUpdateFlag_None);

  // 4) draw the depth into the target (every call, so shadows stay live)
  WGPUCommandEncoderDescriptor ed = {0};
  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &ed);

  WGPURenderPassColorAttachment ca = {
      .view = dp->target_view,
#ifdef WGPU_DEPTH_SLICE_UNDEFINED
      .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
#endif
      .loadOp = WGPULoadOp_Clear,
      .storeOp = WGPUStoreOp_Store,
      .clearValue.r = 0.0,
      .clearValue.g = 0.0,
      .clearValue.b = 0.0,
      .clearValue.a = 1.0,
  };

  WGPURenderPassDescriptor rp = {
      .colorAttachmentCount = 1,
      .colorAttachments = &ca,
  };

  const RenderPipeline *pipeline = shader_pipeline(dp->shader);

  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
  wgpuRenderPassEncoderSetPipeline(pass, pipeline->handle);
  wgpuRenderPassEncoderSetBindGroup(
      pass, 0, dp->shader->bind_groups.entries->bind_group, 0, NULL);
  wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);

  WGPUCommandBufferDescriptor cd = {0};
  WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cd);
  wgpuQueueSubmit(queue, 1, &cmd);
  wgpuCommandBufferRelease(cmd);
  wgpuCommandEncoderRelease(encoder);

  return dp->target_view;
}

void depth_preview_release_source(DepthPreview *dp) {
  if (!dp || !dp->shader)
    return;

  WGPUBindGroup *bg = &dp->shader->bind_groups.entries->bind_group;
  if (*bg) {
    wgpuBindGroupRelease(*bg);
    *bg = NULL;
  }

  if (dp->src_view) {
    wgpuTextureViewRelease(dp->src_view);
    dp->src_view = NULL;
  }
  dp->src_tex = NULL;
  dp->src_mip = -1;
  dp->src_layer = -1;
}

void depth_preview_destroy(DepthPreview *dp) {
  if (!dp)
    return;

  depth_preview_release_source(dp);

  if (dp->target_view)
    wgpuTextureViewRelease(dp->target_view);

  if (dp->target)
    wgpuTextureRelease(dp->target);

  rem_destroy_shader(dp->shader);

  memset(dp, 0, sizeof *dp);
}
