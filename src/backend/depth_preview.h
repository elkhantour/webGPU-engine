#ifndef _DEPTH_PREVIEW_H_
#define _DEPTH_PREVIEW_H_

/*
 * Depth texture preview.
 *
 * ImGui can only display filterable float 2D color textures, so depth
 * textures (shadow maps, ...) are drawn by a small fullscreen pass into an
 * RGBA8 target, which can then be shown like any other 2D texture.
 *
 * Usage:
 *   DepthPreview dp = {0};                       // zero-init is the empty
 * state if (depth_preview_supports(tex)) { DepthPreviewParams p =
 * DEPTH_PREVIEW_PARAMS_DEFAULT; WGPUTextureView v = depth_preview_render(&dp,
 * tex, mip, layer, w, h, &p);
 *     // v is a 2D RGBA8 view owned by `dp` (NULL on failure)
 *   }
 *   depth_preview_destroy(&dp);
 *
 * Not thread-safe. Submits its own command buffer on every render call.
 */

#include "runtime/shader/core.h"
#include "utils/defines.h"
#include "webgpu/webgpu.h"
#include <stdint.h>

typedef struct DepthPreview {
  int failed; // pipeline creation failed once, don't retry

  // RGBA8 target the depth is drawn into (what the UI displays)
  WGPUTexture target;
  WGPUTextureView target_view;
  uint32_t target_w, target_h;

  // depth-only view of the selected mip/layer and its bind group
  WGPUTexture src_tex;
  WGPUTextureView src_view;
  int src_mip, src_layer;

  Shader *shader;
} DepthPreview;

typedef struct {
  float near_z, far_z;        // only used when linearize is set
  float range_min, range_max; // displayed range, [0, 1]
  int linearize;              // perspective depth -> linear
  int reversed_z;             // reversed-Z projection
} DepthPreviewParams;

#define DEPTH_PREVIEW_PARAMS_DEFAULT                                           \
  { 0.1f, 100.0f, 0.0f, 1.0f, 0, 0 }

EXTERN_C_BEGIN

// 1 if the texture can be previewed: single-sampled 2D depth texture with
// TextureBinding usage.
int depth_preview_supports(WGPUTexture texture);

// Draws the given mip/layer of `texture` into the preview target (resized to
// w x h) and returns its view, or NULL on failure. The view is owned by `dp`
// and stays valid until the next render with a different size or destroy.
WGPUTextureView depth_preview_render(DepthPreview *dp, WGPUTexture texture,
                                     int mip, int layer, uint32_t w, uint32_t h,
                                     const DepthPreviewParams *params);

// Drop the reference to the source texture (call when the selection changes
// or the source texture is about to be destroyed). Keeps the pipeline/target.
void depth_preview_release_source(DepthPreview *dp);

// Release everything. `dp` is reusable (zeroed) afterwards.
void depth_preview_destroy(DepthPreview *dp);

EXTERN_C_END

#endif
