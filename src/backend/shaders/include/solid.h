#ifndef _PIPELINE_LAYOUT_SOLID_H_
#define _PIPELINE_LAYOUT_SOLID_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/viewport/viewport.h"

#include "commons.h"
#include <webgpu/webgpu.h>

static const RenderPipelineStateObject layout_solid = {
    .label = "Pipeline Bind Groups - Solid",
    .shader_path = PATH_SHADER("solid/solid.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mvp_layout},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes =
        {
            .primitive_state = &primitive_double_sided,
        },
};

#endif
