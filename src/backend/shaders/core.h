#ifndef _RENDERER_SHADERS_CORE_H_
#define _RENDERER_SHADERS_CORE_H_

#include <stdint.h>
#include <webgpu/webgpu.h>

#include "runtime/pipeline/pipeline.h"

// Standards pipelines
#define RENDER_PIPELINE_UNDEFINED FLT_MAX

// clang-format off
#define STD_RENDER_PIPELINES(_)                                                \
  _( Billboard        )                                                        \
  _( Default          )                                                        \
  _( Grid             )                                                        \
  _( Line             )                                                        \
  _( PBR              )                                                        \
  _( PBR_DoubleSided  )                                                        \
  _( PBR_Alpha        )                                                        \
  _( Screen           )                                                        \
  _( Shadow           )                                                        \
  _( Skybox           )                                                        \
  _( Solid            )                                                        \
  _( Unlit            )                                                        \
  _( Unlit_Stencil    )                                                        \
  _( GlassProbeGrid   )                                                        \
  _( GlassProbePlane  )                                                        \
  _( Reflection       )                                                        \
  _( Blit             )                                                        \
  _( Outline          )                                                        \
  _( Stencil          )                                                        \
  _( Bloom            )                                                        \
  _( Composite        )							       \
  _( DepthVis         )
// clang-format on

typedef enum {
#define _(Label) RenderPipelineType_##Label,
  STD_RENDER_PIPELINES(_)
#undef _
      RenderPipelineType_COUNT
} RenderPipelineType;

#define STD_COMPUTE_PIPELINES(_)                                               \
  _(Mipmap)                                                                    \
  _(Kawase)                                                                    \
  _(BGRA2RGBA)                                                                 \
  _(Colormap)

typedef enum {
#define _(Label) ComputePipelineType_##Label,
  STD_COMPUTE_PIPELINES(_)
#undef _
      ComputePipelineType_COUNT
} ComputePipelineType;

extern RenderPipeline *g_std_render_pipelines[RenderPipelineType_COUNT];
extern ComputePipeline *g_std_compute_pipelines[ComputePipelineType_COUNT];

EXTERN_C_BEGIN

void standard_render_pipelines_init(const RenderPipelineMultisampleCount);
void standard_render_pipelines_destroy();
void standard_render_pipelines_clear_handles();
void standard_compute_pipelines_init();

static inline RenderPipeline *const *
std_render_pipeline(const RenderPipelineType type) {
  return &g_std_render_pipelines[type];
}

static inline const ComputePipeline *
std_compute_pipeline(const ComputePipelineType type) {
  return g_std_compute_pipelines[type];
}

static inline RenderPipelineType
std_render_pipeline_type(const RenderPipeline *pipeline) {

  for (uint8_t i = 0; i < RenderPipelineType_COUNT; i++) {
    if (*std_render_pipeline((RenderPipelineType)i) == pipeline)
      return (RenderPipelineType)i;
  }

  return (RenderPipelineType)RENDER_PIPELINE_UNDEFINED;
}

const char *std_render_pipeline_label(const RenderPipelineType);
const char *std_compute_pipeline_label(const ComputePipelineType);

static inline const RenderPipelineBinding *
std_render_pipeline_bindings(const RenderPipelineType type) {
  return &(*std_render_pipeline(type))->bindings;
}

static inline const ComputePipelineBinding *
std_compute_pipeline_bindings(const ComputePipelineType type) {
  return &std_compute_pipeline(type)->bindings;
}

EXTERN_C_END

#endif
