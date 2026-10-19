#ifndef _PIPELINE_LAYOUT_REFLECTION_H_
#define _PIPELINE_LAYOUT_REFLECTION_H_

#include "runtime/camera/camera.h"
#include "runtime/light/uniform.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/pipeline/render.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"
#include "pbr.h"
#include "runtime/texture/texture.h"
#include <webgpu/webgpu.h>

static const WGPUMultisampleState reflection_multisample = {
    .alphaToCoverageEnabled = false,
    .mask = 0xFFFFFFFF,
    .count = PipelineMultisampleCount_1x,
};

static const WGPUColorTargetState reflection_color = {
    .format = TEXTURE_FORMAT_OFFSCREEN,
    .writeMask = WGPUColorWriteMask_All,
    .blend = NULL,
};

static const WGPUDepthStencilState reflection_stencil = {
    .format = TEXTURE_FORMAT_DEPTH,
    .depthWriteEnabled = true,
    .depthCompare = WGPUCompareFunction_Less,
};

static const RenderPipelineStateObject layout_reflection = {
    .label = "Pipeline Bind Groups - Reflection",
    .shader_path = PATH_SHADER("reflection/reflection.wgsl"),
    .bind_groups_count = 3,
    .bind_groups =
        {
            &mvp_layout,
            &pbr_material_bind_group,
            &pbr_lighting_bind_group,
        },
    .pipeline_attributes =
        {
            .multisample_state = &reflection_multisample,
            .color_state = &reflection_color,
            .stencil_state = &reflection_stencil,

        },
    .bindings =
        {
            .mvp = &mvp_binding,
            .light_list = &pbr_light_list,
            .probe = NULL,
        },

};

#endif
