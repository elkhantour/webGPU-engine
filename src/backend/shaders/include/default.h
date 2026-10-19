#ifndef _PIPELINE_LAYOUT_DEFAULT_H_
#define _PIPELINE_LAYOUT_DEFAULT_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/render.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"
#include "commons.h"
#include <webgpu/webgpu.h>

static const RenderPipelineStateObject layout_default = {
    .label = "Pipeline Bind Groups - Default",
    .shader_path = PATH_SHADER("default/default.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&mvp_layout},
    .bindings = {.mvp = &mvp_binding},
};

#endif
