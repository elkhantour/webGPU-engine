#ifndef _PIPELINE_LAYOUT_BILLBOARD_H_
#define _PIPELINE_LAYOUT_BILLBOARD_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor billboard_material_bind_group = {
    // Group 1
    .label = "Billboard Group 1 (Material Data)",
    .entryCount = 3,
    .entries =
        (WGPUBindGroupLayoutEntry[3]){
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize =
                            sizeof(uint32_t), // or 256-byte aligned
                    },
            },
        },
};

static const RenderPipelineStateObject layout_billboard = {
    .label = "Pipeline Bind Groups - Billboard",
    .shader_path = PATH_SHADER("billboard/billboard.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &billboard_material_bind_group},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes = {
        .blend_state = &blend_alpha,
    }};

#endif
