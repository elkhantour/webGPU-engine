#ifndef _PIPELINE_LAYOUT_DEPTH_VIS_H_
#define _PIPELINE_LAYOUT_DEPTH_VIS_H_

#include "commons.h"
#include "runtime/pipeline/render.h"
#include "utils/path.h"

#include <stdint.h>
#include <webgpu/webgpu.h>

#define DEPTH_VIS_FLAG_LINEARIZE (1u << 0)
#define DEPTH_VIS_FLAG_REVERSE_Z (1u << 1)

/* CPU mirror of `struct Params` in the shader (32 bytes) */
typedef struct DepthVisParams {
  float near_z;
  float far_z;
  float lo;
  float hi;
  uint32_t flags;
  uint32_t p0;
  uint32_t p1;
  uint32_t p2;
} DepthVisParams;

_Static_assert(sizeof(DepthVisParams) == 32, "DepthVisParams must be 32 bytes");

/* group(0): binding 0 = depth texture array, binding 1 = params UBO */
static const WGPUBindGroupLayoutEntry depth_vis_entries[] = {
    {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture =
            {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false,
            },
    },
    {
        .binding = 1,
        .visibility = WGPUShaderStage_Fragment,
        .buffer =
            {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(DepthVisParams),
            },
    },
};

static const WGPUBindGroupLayoutDescriptor depth_vis_bgl = {
    .label = "Bind Group Layout - Depth Vis",
    .entryCount = sizeof(depth_vis_entries) / sizeof(depth_vis_entries[0]),
    .entries = depth_vis_entries,
};

static const WGPUColorTargetState depth_vis_target = {
    .format = WGPUTextureFormat_RGBA8Unorm,
    .writeMask = WGPUColorWriteMask_All,
};

static const WGPUMultisampleState depth_vis_multisample = {
    .count = PipelineMultisampleCount_1x,
    .mask = 0xFFFFFFFF,
    .alphaToCoverageEnabled = false,
};

static const WGPUPrimitiveState depth_vis_primitive = {
    .topology = WGPUPrimitiveTopology_TriangleList,
    .frontFace = WGPUFrontFace_CCW,
    .cullMode = WGPUCullMode_None,
};

static const WGPUDepthStencilState depth_vis_stencil = {
    .format = WGPUTextureFormat_Undefined,
};

static const WGPUVertexState depth_vis_vertex = {
    .bufferCount = 0,
};

static const RenderPipelineStateObject layout_depth_vis = {
    .label = "Pipeline Bind Groups - Depth Vis",
    .shader_path = PATH_SHADER("depthvis/depthvis.wgsl"),
    .bind_groups_count = 1,
    .bind_groups = {&depth_vis_bgl},
    .pipeline_attributes =
        {
            .multisample_state = &depth_vis_multisample,
            .primitive_state = &depth_vis_primitive,
            .stencil_state = &depth_vis_stencil,
            .color_state = &depth_vis_target,
            .vertex_state = &depth_vis_vertex,
        },
};

#endif
