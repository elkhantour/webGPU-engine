#ifndef _PIPELINE_LAYOUT_OUTLINE_H_
#define _PIPELINE_LAYOUT_OUTLINE_H_

#include "utils/path.h"
#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUPrimitiveState outline_primitive = {
    .cullMode = WGPUCullMode_None,
    .topology = WGPUPrimitiveTopology_TriangleList,
    .frontFace = WGPUFrontFace_CCW,
};

static const WGPUDepthStencilState outline_stencil = {
    .format = TEXTURE_FORMAT_DEPTH_STENCIL,
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_Always,
    .stencilFront =
        (WGPUStencilFaceState){.compare = WGPUCompareFunction_NotEqual,
                               .failOp = WGPUStencilOperation_Keep,
                               .depthFailOp = WGPUStencilOperation_Keep,
                               .passOp = WGPUStencilOperation_Keep},
    .stencilBack =
        (WGPUStencilFaceState){.compare = WGPUCompareFunction_NotEqual,
                               .failOp = WGPUStencilOperation_Keep,
                               .depthFailOp = WGPUStencilOperation_Keep,
                               .passOp = WGPUStencilOperation_Keep},
    .stencilReadMask = 0xFF,
    .stencilWriteMask = 0x00,
};

static const RenderPipelineStateObject layout_outline = {
    .label = "Pipeline Bind Groups - Outline",
    .shader_path = PATH_SHADER("outline/outline.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mvp_layout},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes =
        {
            .stencil_state = &outline_stencil,
            .primitive_state = &outline_primitive,
        },
};

#endif
