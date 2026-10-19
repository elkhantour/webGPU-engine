#ifndef _PIPELINE_LAYOUT_SCREEN_H_
#define _PIPELINE_LAYOUT_SCREEN_H_

#include "runtime/camera/camera.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/viewport/viewport.h"
#include "utils/path.h"
#include "commons.h"
#include <cglm/cglm.h>
#include <webgpu/webgpu.h>


static const WGPUBindGroupLayoutDescriptor screen_layout_mesh = {
    // Group 0: Mesh
    .label = "Group 0 - Mesh",
    .entryCount = 1,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                // uMesh
                .binding = 0,
                .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .hasDynamicOffset = true,
                        .minBindingSize = sizeof(MeshUniform),
                    },
            },
        },
};

static const WGPUBindGroupLayoutDescriptor screen_layout_texture = {
    // Group 1: Texture + Sampler
    .label = "Group 1 - Screen Texture",
    .entryCount = 3,
    .entries =
        (WGPUBindGroupLayoutEntry[]){
            {
                // texture
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .texture =
                    (WGPUTextureBindingLayout){
                        .sampleType = WGPUTextureSampleType_Float,
                        .viewDimension = WGPUTextureViewDimension_2D,
                        .multisampled = false,
                    },
            },
            {
                // texture_sampler
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .sampler =
                    (WGPUSamplerBindingLayout){
                        .type = WGPUSamplerBindingType_Filtering,
                    },
            },
            {
                // texture_coordinates
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .buffer =
                    (WGPUBufferBindingLayout){
                        .type = WGPUBufferBindingType_Uniform,
                        .minBindingSize = sizeof(TextureCoordinatesUniform),
			.hasDynamicOffset = false,
                    },
            },
        },
};

static const WGPUDepthStencilState screen_depth_stencil = {
    .format = TEXTURE_FORMAT_DEPTH, // FOR UI (DEFAULT IS
                                    // TEXTURE_FORMAT_DEPTH_STENCIL), MAYBE NEED
                                    // TO CREATE ANOTHER PIPELINE "SCREEN_UI"
    .depthWriteEnabled = false,
    .depthCompare = WGPUCompareFunction_Undefined,
};

static const RenderPipelineStateObject layout_screen = {
    .label = "Pipeline Bind Groups - Screen",
    .shader_path = PATH_SHADER("screen/screen.wgsl"),
    .bind_groups_count = 2,
    .bind_groups = {&screen_layout_mesh, &screen_layout_texture},
    .bindings = {.mvp = &mvp_binding},
    .pipeline_attributes = {.stencil_state = &screen_depth_stencil},
};

#endif
