#ifndef _PIPELINE_LAYOUT_BLOOM_H_
#define _PIPELINE_LAYOUT_BLOOM_H_

#include "blit.h"
#include "commons.h"
#include "runtime/texture/core.h"
#include <webgpu/webgpu.h>
#include "utils/path.h"

typedef struct {
  float threshold;
  float knee;
  uint32_t blur;
  uint32_t downscale;
} BloomUniform;

static const WGPUBindGroupLayoutDescriptor bloom_bind_group_layout = {
    .label = "Group 0 - Bloom Texture + Params",
    .entryCount = 3,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // source texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 1, // sampler
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                .binding = 2, // uniform buffer (params)
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(BloomUniform),
                    },
            },
        },
};

static const WGPUColorTargetState bloom_color_target = {
    .blend = RENDER_PIPELINE_SET_KEEP_BLEND,
    .writeMask = WGPUColorWriteMask_All,
    .format = TEXTURE_FORMAT_OFFSCREEN,
};

static const RenderPipelineStateObject layout_bloom = {
    .label = "Pipeline Bloom Pass",
    .shader_path = PATH_SHADER("bloom/bloom.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&bloom_bind_group_layout},
    .pipeline_attributes =
        {
            .primitive_state = &blit_primitive,
            .stencil_state = &blit_depth_stencil,
            .multisample_state = &blit_multisample,
            .vertex_state = &blit_vertex,
            .color_state = &bloom_color_target,
        },
    .bindings = {0},
};

#endif
