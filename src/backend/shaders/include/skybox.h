#ifndef _PIPELINE_LAYOUT_SKYBOX_H_
#define _PIPELINE_LAYOUT_SKYBOX_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/texture/core.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"
#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor skybox_layout_bind_group = {
    // Group 1: Skybox resources (texture, sampler, blur factor)
    .label = "Group 1 - Skybox Resources",
    .entryCount = 3,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                // skybox_texture
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_Cube,
                        .multisampled = false,
                    },
            },
            {
                // skybox_sampler
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                // skybox_blur (float)
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(float),
                    },
            },
        },
};

static const WGPUPrimitiveState skybox_primitive = {
    .frontFace = WGPUFrontFace_CCW,
    .cullMode = WGPUCullMode_Front,
    .topology = WGPUPrimitiveTopology_TriangleList,
    .stripIndexFormat = WGPUIndexFormat_Undefined,
};

static const WGPUDepthStencilState skybox_stencil = {
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_LessEqual,
    .format = TEXTURE_FORMAT_DEPTH_STENCIL,
};

static const RenderPipelineStateObject layout_skybox = {
    .label = "Pipeline Bind Groups - Skybox",
    .shader_path = PATH_SHADER("skybox/skybox.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &skybox_layout_bind_group},
    .pipeline_attributes =
        {
            // set cull to front face (inside cube)
            .primitive_state = &skybox_primitive,
            // remove depth write, set depth comparison
            .stencil_state = &skybox_stencil,
        },
    .bindings = {.mvp = &mvp_binding},

};

#endif
