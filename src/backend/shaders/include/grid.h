#ifndef _PIPELINE_LAYOUT_GRID_H_
#define _PIPELINE_LAYOUT_GRID_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/scene/editor_mesh/grid/grid.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"

#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor grid_bind_group = {
    // Group 1 (Grid Data)
    .label = "Group 1 - Grid",
    .entryCount = 1,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // uGrid
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(GridUniform),
                    },
            },
        },
};


static const WGPUDepthStencilState grid_stencil = {
    .format = TEXTURE_FORMAT_DEPTH_STENCIL,
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_Less,
};

static const RenderPipelineStateObject layout_grid = {
    .label = "Pipeline Bind Groups - Grid",
    .shader_path = PATH_SHADER("grid/grid.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &grid_bind_group},
    .pipeline_attributes =
        {
            // double sided
            .primitive_state = &primitive_double_sided,
            .stencil_state = &grid_stencil,
            .blend_state = &blend_alpha,
        },
    .bindings = {.mvp = &mvp_binding},
};

#endif
