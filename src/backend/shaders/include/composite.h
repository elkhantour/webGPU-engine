#ifndef _PIPELINE_LAYOUT_COMPOSITE_H_
#define _PIPELINE_LAYOUT_COMPOSITE_H_

#include "blit.h"
#include "commons.h"
#include <webgpu/webgpu.h>
#include "utils/path.h"

typedef struct {
  float bloom_intensity;
  float vignette_strength;
  float vignette_feather;
  float exposure;
  float gamma;
  vec3 _pad;
} CompositeUniform;

static const WGPUBindGroupLayoutDescriptor composite_bind_group_layout = {
    .label = "Group 0 - Composite Pass",
    .entryCount = 4,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // scene texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 1, // bloom texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 2, // sampler
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                .binding = 3, // uniform buffer (params)
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(CompositeUniform),
                    },
            },
        },
};

static const RenderPipelineStateObject layout_composite = {
    .label = "Composite pass",
    .shader_path = PATH_SHADER("composite/composite.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&composite_bind_group_layout},
    .pipeline_attributes =
        {
            .primitive_state = &blit_primitive,
            .stencil_state = &blit_depth_stencil,
            .multisample_state = &blit_multisample,
            .vertex_state = &blit_vertex,

        },
    .bindings = {0},
};

#endif
