#ifndef _COMPUTE_PIPELINE_LAYOUT_BGRA2RGBA_H_
#define _COMPUTE_PIPELINE_LAYOUT_BGRA2RGBA_H_

#include "utils/path.h"
#include "backend/compute/kawase.h"
#include "runtime/pipeline/compute.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor bgra2rgba_bind_group = {
    .label = "Group 0 - bgra2rgba",
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

static const ComputePipelineStateObject layout_bgra2rgba = {
    .label = "Compute Pipeline Bind Groups - bgra2rgba",
    .shader_path = PATH_SHADER("bgra2rgba/bgra2rgba.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&bgra2rgba_bind_group},
};

#endif
