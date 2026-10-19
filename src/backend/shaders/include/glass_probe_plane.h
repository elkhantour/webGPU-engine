#ifndef _PIPELINE_LAYOUT_GLASS_PROBE_PLANE_H_
#define _PIPELINE_LAYOUT_GLASS_PROBE_PLANE_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/core.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/scene/environment/core.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include <webgpu/webgpu.h>


static const WGPUBindGroupLayoutDescriptor glass_probe_plane_bind_group = {
    .label = "Group 1 (Glass + Environment + Probes)",
    .entryCount = 6,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // uGlass
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(GlassUniform),
                    },
            },
            {
                .binding = 1, // uEnvironment
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(SceneEnvironmentUniform),
                    },
            },
            {
                .binding = 2, // uProbes
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(ProbeListUniform),
                    },
            },
            {
                .binding = 3, // probe_reflection_maps
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2DArray,
                        .multisampled = false,
                    },
            },
            {
                .binding = 4, // skybox_map
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_Cube,
                        .multisampled = false,
                    },
            },
            {
                .binding = 5, // linear_sampler
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
        },
};

static const PipelineBindingProbe glass_probe_plane = {
    .group = 1,
    .list = 2,
    .reflection_plane_texture = 3,
    .reflection_grid_texture = PIPELINE_BINDING_UNDEFINED,
    .irradiance_texture = PIPELINE_BINDING_UNDEFINED,
    .skybox_texture = 4,
    .sampler = 5,
};

static const PipelineBindingEnvironment glass_env_plane = {
    .group = 1,
    .environment = 1,
};

static const RenderPipelineStateObject layout_glass_probe_plane = {
    .label = "Pipeline Bind Groups - Glass Probe Plane",
    .shader_path = PATH_SHADER("glass_probe_plane/glass_probe_plane.wgsl"),
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
