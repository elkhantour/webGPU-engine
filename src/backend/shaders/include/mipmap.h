#ifndef _COMPUTE_PIPELINE_LAYOUT_MIPMAP_H_
#define _COMPUTE_PIPELINE_LAYOUT_MIPMAP_H_

#include "utils/path.h"
#include "runtime/pipeline/compute.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor mipmap_bind_group = {
    .label = "Group 0 - Mipmap",
    .entryCount = 3,
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
        },
};

static const ComputePipelineStateObject layout_mipmap = {
    .label = "Compute Pipeline Bind Groups - Mipmap",
    .shader_path = PATH_SHADER("mipmap/mipmap.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mipmap_bind_group},
};

#endif
