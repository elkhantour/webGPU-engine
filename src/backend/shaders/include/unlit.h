#ifndef _PIPELINE_LAYOUT_UNLIT_H_
#define _PIPELINE_LAYOUT_UNLIT_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor unlint_layout_bind_group = {
    // Group 1 (Billboard Material)
    .label = "Group 1 - Billboard Properties",
    .entryCount = 2,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // uColor
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(float) * 4, // vec4<f32>
                    },
            },
            {
                .binding = 1, // uFixedScale
                .visibility = WGPUShaderStage_Vertex,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(float), // f32
                    },
            },
        },
};

static const WGPUPrimitiveState unlit_prim = {
    .cullMode = WGPUCullMode_None,
    .frontFace = WGPUFrontFace_CCW,
    .stripIndexFormat = WGPUIndexFormat_Undefined,
    .topology = WGPUPrimitiveTopology_TriangleList,
};

static const WGPUDepthStencilState unlit_stencil = {
    .format = TEXTURE_FORMAT_DEPTH_STENCIL, // USED BECAUSE OF GIZMO, BUT
                                            // MAYBE NEED TO CREATE A NE
                                            // DEDICATED PIPELINE FOR GIZMO
    .depthWriteEnabled = true,
    .depthCompare = WGPUCompareFunction_Less,
};

static const WGPUDepthStencilState unlit_depth = {
    .format = TEXTURE_FORMAT_DEPTH, // FIXME USED BECAUSE OF GIZMO, BUT
                                    // MAYBE NEED TO CREATE A NE
                                    // DEDICATED PIPELINE FOR GIZMO
    .depthWriteEnabled = true,
    .depthCompare = WGPUCompareFunction_Less,
};

static const RenderPipelineStateObject layout_unlit_stencil = {
    .label = "Pipeline Bind Groups - Unlit Stencil",
    .shader_path = PATH_SHADER("unlit/unlit.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &unlint_layout_bind_group},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes =
        {
            .stencil_state = &unlit_stencil,
            .primitive_state = &unlit_prim,
        },
};

static const RenderPipelineStateObject layout_unlit = {
    .label = "Pipeline Bind Groups - Unlit",
    .shader_path = PATH_SHADER("unlit/unlit.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &unlint_layout_bind_group},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes =
        {
            .stencil_state = &unlit_depth,
            .primitive_state = &unlit_prim,
            .blend_state = &blend_alpha,
        },
};

#endif
