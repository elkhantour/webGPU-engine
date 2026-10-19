#ifndef _COMPUTE_PIPELINE_LAYOUT_REMAP_H_
#define _COMPUTE_PIPELINE_LAYOUT_REMAP_H_

#include "utils/path.h"
#include "backend/compute/colormap.h"
#include "runtime/pipeline/compute.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor colormap_bind_group = {
    .label = "Group 0 - colormap",
    .entryCount = 4,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // src_texture
                .visibility = WGPUShaderStage_Compute,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 1, // src_sampler
                .visibility = WGPUShaderStage_Compute,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                .binding = 2, // dst_texture
                .visibility = WGPUShaderStage_Compute,
                .storageTexture =
                    (WGPUStorageTextureBindingLayout){
                        .access = WGPUStorageTextureAccess_WriteOnly,
                        .format = WGPUTextureFormat_RGBA8Unorm,
                        .viewDimension = WGPUTextureViewDimension_2D,
                    },
            },
            {
                .binding = 3, // uniform
                .visibility = WGPUShaderStage_Compute,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(ColormapUniform),
                    },
            },
        },
};

static const ComputePipelineStateObject layout_colormap = {
    .label = "Compute Pipeline Bind Groups - colormap layout",
    .shader_path = PATH_SHADER("colormap/colormap.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&colormap_bind_group},
};



#endif
