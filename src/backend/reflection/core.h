#ifndef _RENDERER_PROBE_CORE_H_
#define _RENDERER_PROBE_CORE_H_

#include "backend/compute/core.h"
#include "backend/renderer/core.h"
#include "backend/render_pass/core.h"
#include "runtime/texture/core.h"
#include "utils/defines.h"
#include "webgpu/webgpu.h"

#define PROBE_REFLECTION_MIPMAP_COUNT 1

typedef struct {
  const size_t layer_count;
  const WGPUTextureViewDimension view_dimension;
  const TextureResolution resolution;
  const RenderPassLayoutListDescriptor *draw_list;
  const RenderPipelineMultisampleCount multisample;
} RendererProbeReflectionDescriptor;

EXTERN_C_BEGIN

void renderer_probe_reflection_create_pass(
    Renderer *, RenderPass *, const RendererProbeReflectionDescriptor *);

void renderer_probe_reflection_update_resolution(
    RenderPass *, const TextureResolution, const WGPUTextureViewDimension);

void probe_reflection_list_draw_preprocessor(const RenderPass *, Mesh *,
                                             void *);

EXTERN_C_END
#endif
