#ifndef _PIPELINE_LAYOUT_SHADOW_H_
#define _PIPELINE_LAYOUT_SHADOW_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/render.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"
#include "commons.h"
#include "backend/renderer/shadow_map/core.h"

#include <webgpu/webgpu.h>

static const PipelineBindingMVP shadow_mvp = {
    .group = 0,
    .projection = 0,
    .view = 1,
    .model = 2,
};

static const WGPUMultisampleState shadow_multisample = {
    .alphaToCoverageEnabled = false,
    .mask = 0xFFFFFFFF,
    .count = PipelineMultisampleCount_1x,
};

static const WGPUDepthStencilState shadow_stencil = {
    .format = SHADOW_DEPTH_FORMAT,
    .depthWriteEnabled = true,
    .depthCompare = WGPUCompareFunction_Less,
};


static const RenderPipelineStateObject layout_shadow = {
    .label = "Pipeline Bind Groups - Shadow",
    .shader_path = PATH_SHADER("shadow/shadow.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mp_layout},
    .pipeline_attributes =
        {
            .multisample_state = &shadow_multisample,
            .stencil_state = &shadow_stencil,
            .primitive_state = &primitive_double_sided,
        },
};

#endif
