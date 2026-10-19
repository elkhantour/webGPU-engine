#ifndef _PIPELINE_LAYOUT_stencil_H_
#define _PIPELINE_LAYOUT_stencil_H_

#include "utils/path.h"
#include "commons.h"
#include <webgpu/webgpu.h>

static const WGPUDepthStencilState selectable_stencil = {
    .format = TEXTURE_FORMAT_DEPTH_STENCIL,
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_Less,
    .stencilFront =
        (WGPUStencilFaceState){
            .compare = WGPUCompareFunction_Always,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_Replace,
        },
    .stencilBack =
        (WGPUStencilFaceState){
            .compare = WGPUCompareFunction_Always,
            .failOp = WGPUStencilOperation_Keep,
            .depthFailOp = WGPUStencilOperation_Keep,
            .passOp = WGPUStencilOperation_Replace,
        },
    .stencilReadMask = 0xFF,
    .stencilWriteMask = 0xFF,
};


static const RenderPipelineStateObject layout_stencil = {
    .label = "Pipeline Bind Groups - Stencil",
    .shader_path = PATH_SHADER("stencil/stencil.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mvp_layout},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes = {.stencil_state = &selectable_stencil},
};

#endif
