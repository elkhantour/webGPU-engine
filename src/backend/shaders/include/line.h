#ifndef _PIPELINE_LAYOUT_LINE_H_
#define _PIPELINE_LAYOUT_LINE_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/viewport/viewport.h"
#include "utils/color.h"
#include "utils/path.h"
#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUBindGroupLayoutDescriptor line_bind_group = {
    // Group 1
    .label = "Group 1 - Color",
    .entryCount = 2,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // Color
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(color),
                    },
            },
            {
                .binding = 1, // Thickness
                .visibility = WGPUShaderStage_Vertex,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(float),
                    },
            },
        },
};

static const WGPUPrimitiveState line_primitive = {
    .frontFace = WGPUFrontFace_CCW,
    .cullMode = WGPUCullMode_None,
    .topology = WGPUPrimitiveTopology_TriangleList,
    .stripIndexFormat = WGPUIndexFormat_Undefined,
};

static const RenderPipelineStateObject layout_line = {
    .label = "Pipeline Bind Groups - Line",
    .shader_path = PATH_SHADER("line/line.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &line_bind_group},
    .pipeline_attributes = {.primitive_state = &line_primitive},
    .bindings = {.mvp = &mvp_binding},
};

#endif
