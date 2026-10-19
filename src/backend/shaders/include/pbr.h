#ifndef _PIPELINE_LAYOUT_PBR_H_
#define _PIPELINE_LAYOUT_PBR_H_

#include "runtime/camera/camera.h"
#include "runtime/light/light.h"
#include "runtime/light/list.h"
#include "runtime/light/uniform.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/core.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/probe/reflection/probe.h"
#include "runtime/scene/environment/core.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"

#include "commons.h"
#include <webgpu/webgpu.h>

typedef struct {
  vec4 base_color_factor;
  vec3 emissive_factor;
  float metallic_factor;
  float roughness_factor;
  float specular_factor;
  float normal_scale;
  float occlusion_strength;
  float alpha_threshold;
  float _pad[3];
} PBRMaterialUniform;

static const WGPUBindGroupLayoutDescriptor pbr_material_bind_group = {
    .label = "Group 1 (PBR Material)",
    .entryCount = 7,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // diffuse_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 1, // metallic_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 2, // normal_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 3, // emissive_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                .binding = 4, // occlusion_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
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
            {
                .binding = 6, // uPBR
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(PBRMaterialUniform),
                    },
            },
        },
};

static const WGPUBindGroupLayoutDescriptor pbr_lighting_bind_group = {
    .label = "Group 2 (Environment + Lights + Probes)",
    .entryCount = 9,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                .binding = 0, // uEnvironment
                .visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(SceneEnvironmentUniform),
                    },
            },
            {
                .binding = 1, // uLights
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = false,
                        .minBindingSize = sizeof(LightListUniform),
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
                .binding = 3, // point_shadow_maps
                .visibility = WGPUShaderStage_Fragment,
#ifdef RENDER_SHADOW_AS_COLOR
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
#else
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Depth,
#endif
                        .viewDimension = WGPUTextureViewDimension_CubeArray,
                        .multisampled = false,
                    },
            },
            {
                .binding = 4, // directional_shadow_maps
                .visibility = WGPUShaderStage_Fragment,
#ifdef RENDER_SHADOW_AS_COLOR
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
#else
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Depth,
#endif
                        .viewDimension = WGPUTextureViewDimension_2DArray,
                        .multisampled = false,
                    },
            },
#ifndef RENDER_SHADOW_AS_COLOR
            {
                .binding = 5, // shadow_sampler
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Comparison,
                    },
            },
#endif
            {
                .binding = 6, // probe_reflection_plane_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2DArray,
                        .multisampled = false,
                    },
            },
            {
                .binding = 7, // probe_reflection_grid_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_CubeArray,
                        .multisampled = false,
                    },
            },
            {
                .binding = 8, // skybox_texture
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_Cube,
                        .multisampled = false,
                    },
            },
        },
};

static const PipelineBindingLightList pbr_light_list = {
    .group = 2,
    .list = 1,
    .point_texture = 3,
    .directional_texture = 4,
};

static const PipelineBindingProbe pbr_probe = {
    .group = 2,
    .reflection_plane_texture = 6,
    .reflection_grid_texture = 7,
    .irradiance_texture = PIPELINE_BINDING_UNDEFINED,
    .skybox_texture = 8,
    .sampler = PIPELINE_BINDING_UNDEFINED,
};

static const PipelineBindingEnvironment pbr_env = {
    .group = 2,
    .environment = 0,
};

static const WGPUDepthStencilState pbr_alpha_stencil = {
    .format = TEXTURE_FORMAT_DEPTH_STENCIL,
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_GreaterEqual,
};

static const RenderPipelineStateObject layout_pbr = {
    .label = "Pipeline Bind Groups - PBR",
    .shader_path = PATH_SHADER("pbr/pbr.wgsl"),
    .bind_groups_count = 3,
    .bind_groups =
        {
            &mvp_layout,
            &pbr_material_bind_group,
            &pbr_lighting_bind_group,
        },
    .bindings =
        {
            .mvp = &mvp_binding,
            .light_list = &pbr_light_list,
            .probe = &pbr_probe,
            .environment = &pbr_env,
        },
};

static const RenderPipelineStateObject layout_pbr_double_sided = {
    .label = "Pipeline Bind Groups - PBR Double Sided",
    .shader_path = PATH_SHADER("pbr/pbr.wgsl"),
    .bind_groups_count = 3,
    .bind_groups =
        {
            &mvp_layout,
            &pbr_material_bind_group,
            &pbr_lighting_bind_group,
        },
    .bindings =
        {
            .mvp = &mvp_binding,
            .light_list = &pbr_light_list,
            .probe = &pbr_probe,
            .environment = &pbr_env,
        },
    .pipeline_attributes =
        {
            .primitive_state = &primitive_double_sided,
        },
};

static const RenderPipelineStateObject layout_pbr_alpha = {
    .label = "Pipeline Bind Groups - PBR Alpha",
    .shader_path = PATH_SHADER("pbr/pbr.wgsl"),
    .bind_groups_count = 3,
    .bind_groups =
        {
            &mvp_layout,
            &pbr_material_bind_group,
            &pbr_lighting_bind_group,
        },
    .bindings =
        {
            .mvp = &mvp_binding,
            .light_list = &pbr_light_list,
            .probe = &pbr_probe,
            .environment = &pbr_env,
        },
    .pipeline_attributes =
        {
            .primitive_state = &primitive_double_sided,
            .blend_state = &blend_alpha,
            .stencil_state = &pbr_alpha_stencil,
        },
};

#endif
