#ifndef _PIPELINE_LAYOUT_GLASS_PROBE_GRID_H_
#define _PIPELINE_LAYOUT_GLASS_PROBE_GRID_H_

#include "glass_probe_plane.h"
#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/grid.h"
#include "runtime/scene/environment/core.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"

#include <webgpu/webgpu.h>

static const RenderPipelineStateObject layout_glass_probe_grid = {
    .label = "Pipeline Bind Groups - Glass Probe Grid",
    .shader_path = PATH_SHADER("glass_probe_grid/glass_probe_grid.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&mvp_layout, &glass_probe_plane_bind_group},
    .bindings =
        {
            .mvp = &mvp_binding,
            .probe = &glass_probe_plane,
            .environment = &glass_env_plane,
        },
};

#endif
