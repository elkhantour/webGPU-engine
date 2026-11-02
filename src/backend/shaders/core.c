#include "core.h"

#include <stddef.h>
#include <stdio.h>

#include "./include/bgra2rgba.h"
#include "./include/billboard.h"
#include "./include/blit.h"
#include "./include/bloom.h"
#include "./include/colormap.h"
#include "./include/composite.h"
#include "./include/default.h"
#include "./include/glass_probe_grid.h"
#include "./include/glass_probe_plane.h"
#include "./include/grid.h"
#include "./include/kawase.h"
#include "./include/line.h"
#include "./include/mipmap.h"
#include "./include/outline.h"
#include "./include/pbr.h"
#include "./include/reflection.h"
#include "./include/screen.h"
#include "./include/shadow.h"
#include "./include/skybox.h"
#include "./include/solid.h"
#include "./include/stencil.h"
#include "./include/unlit.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/shaders/include/depthvis.h"
#include "runtime/pipeline/render.h"
#include "runtime/pipeline/set.h"
#include "webgpu/webgpu.h"

// Global definitions
RenderPipeline *g_std_render_pipelines[RenderPipelineType_COUNT] = {0};
ComputePipeline *g_std_compute_pipelines[ComputePipelineType_COUNT] = {0};

static inline WGPUPipelineLayout shader_pipeline_state_object_create(
    const WGPUBindGroupLayoutDescriptor *const *, const size_t,
    WGPUBindGroupLayout *);

static const RenderPipelineStateObject
    *standard_render_layouts[RenderPipelineType_COUNT] = {
        // clang-format off
        [ RenderPipelineType_Billboard       ]   =   &layout_billboard,
        [ RenderPipelineType_Default         ]   =   &layout_default,
        [ RenderPipelineType_Line            ]   =   &layout_line,
	[ RenderPipelineType_Unlit_Stencil   ]   =   &layout_unlit_stencil,
        [ RenderPipelineType_Unlit           ]   =   &layout_unlit,
        [ RenderPipelineType_Grid            ]   =   &layout_grid,
        [ RenderPipelineType_PBR             ]   =   &layout_pbr,
        [ RenderPipelineType_PBR_DoubleSided ]   =   &layout_pbr_double_sided,
        [ RenderPipelineType_PBR_Alpha       ]   =   &layout_pbr_alpha,
        [ RenderPipelineType_Screen          ]   =   &layout_screen,
        [ RenderPipelineType_Shadow          ]   =   &layout_shadow,
        [ RenderPipelineType_Skybox          ]   =   &layout_skybox,
        [ RenderPipelineType_Solid           ]   =   &layout_solid,
        [ RenderPipelineType_GlassProbeGrid  ]   =   &layout_glass_probe_grid,
        [ RenderPipelineType_GlassProbePlane ]   =   &layout_glass_probe_plane,
        [ RenderPipelineType_Reflection      ]   =   &layout_reflection,
        [ RenderPipelineType_Blit            ]   =   &layout_blit,
        [ RenderPipelineType_Outline         ]   =   &layout_outline,
        [ RenderPipelineType_Stencil         ]   =   &layout_stencil,
        [ RenderPipelineType_Bloom           ]   =   &layout_bloom,
        [ RenderPipelineType_Composite       ]   =   &layout_composite,
        [ RenderPipelineType_DepthVis        ]   =   &layout_depth_vis,
        // clang-format on
};

static const ComputePipelineStateObject
    *standard_compute_layouts[ComputePipelineType_COUNT] = {
        // clang-format off
        [ ComputePipelineType_Mipmap         ]   =   &layout_mipmap,
        [ ComputePipelineType_Kawase         ]   =   &layout_kawase,
	[ ComputePipelineType_BGRA2RGBA      ]   =   &layout_bgra2rgba,
	[ ComputePipelineType_Colormap       ]   =   &layout_colormap,
        // clang-format on
};

/**
   Initialize standards shaders and build pipelines layout for each of them.

                 [ PSO ] ===> [ WGPUPipeline ]

 */
void standard_render_pipelines_init(
    const RenderPipelineMultisampleCount multisample) {

  logger_add(LoggerFlag_Process, "Initializing Standard Render Pipelines...");

  for (RenderPipelineType i = 0; i < RenderPipelineType_COUNT; i++) {

    if (standard_render_layouts[i] == NULL) {
      logger_add(
          LoggerFlag_Error,
          "Attempting to initialized an undefined pipeline (%d), make sure "
          "the PSO list is rightly configured and match the actual "
          "amount of Standard Render Pipelines",
          i);
      continue;
    }

    const RenderPipelineStateObject *layout = standard_render_layouts[i];

    g_std_render_pipelines[i] = rem_new_render_pipeline();

    RenderPipeline *cached_pipeline = g_std_render_pipelines[i];

    // create pipeline
    render_pipeline_create(cached_pipeline, &(RenderPipelineCreateDescriptor){
                                                .label = layout->label,
                                                .path = layout->shader_path,
                                                .pso = layout,
                                            });

    {
      /* ===  CHECK CUSTOM ATTRIBUTES === */
      // vertex state
      if (layout->pipeline_attributes.vertex_state)
        render_pipeline_set_vertex(cached_pipeline,
                                   layout->pipeline_attributes.vertex_state);

      // fragment state
      if (layout->pipeline_attributes.fragment_state)
        render_pipeline_set_fragment(
            cached_pipeline, layout->pipeline_attributes.fragment_state);

      // primitive state
      if (layout->pipeline_attributes.primitive_state)
        render_pipeline_set_primitive(
            cached_pipeline, layout->pipeline_attributes.primitive_state);

      // stencil state
      if (layout->pipeline_attributes.stencil_state)
        render_pipeline_set_stencil(cached_pipeline,
                                    layout->pipeline_attributes.stencil_state);

      // color state
      if (layout->pipeline_attributes.color_state)
        render_pipeline_set_color(cached_pipeline,
                                  layout->pipeline_attributes.color_state);

      // blend state
      if (layout->pipeline_attributes.blend_state)
        render_pipeline_set_blend(cached_pipeline,
                                  layout->pipeline_attributes.blend_state);

      // if sampling set in custom attbutes, apply the config one
      if (layout->pipeline_attributes.multisample_state) {
        render_pipeline_set_multisample(
            cached_pipeline, layout->pipeline_attributes.multisample_state);
      } else {
        // else use the renderer one
        render_pipeline_set_sampling(cached_pipeline, multisample);
      }
    }

    // build layout based on bindgroup description
    WGPUPipelineLayout temp_layout = shader_pipeline_state_object_create(
        layout->bind_groups, layout->bind_groups_count, NULL);

    render_pipeline_build(cached_pipeline, &temp_layout);
  }
}

void standard_compute_pipelines_init() {

  logger_add(LoggerFlag_Process, "Initializing Standard Compute Pipelines...");

  for (ComputePipelineType i = 0; i < ComputePipelineType_COUNT; i++) {

    const ComputePipelineStateObject *layout = standard_compute_layouts[i];
    g_std_compute_pipelines[i] = rem_new_compute_pipeline();
    ComputePipeline *cached_pipeline = g_std_compute_pipelines[i];

    // create pipeline
    compute_pipeline_create(cached_pipeline, &(ComputePipelineCreateDescriptor){
                                                 .label = layout->label,
                                                 .path = layout->shader_path,
                                                 .pso = layout,
                                             });

    // build layout based on bindgroup description
    WGPUPipelineLayout temp_layout = shader_pipeline_state_object_create(
        layout->bind_groups, layout->bind_groups_count, NULL);

    compute_pipeline_build(cached_pipeline, &temp_layout);
  }
}

/**
   Transforms bindgroups into pipeline layouts and returns the generated
   pipeline descriptor
 */
WGPUPipelineLayout shader_pipeline_state_object_create(
    const WGPUBindGroupLayoutDescriptor *const *bind_groups, const size_t count,
    WGPUBindGroupLayout *outLayout) {

  WGPUBindGroupLayout *layouts = NULL;
  outLayout = NULL;

  if (count > 0) {
    const size_t layout_size = sizeof(WGPUBindGroupLayout) * count;
    layouts = malloc(layout_size);

    for (size_t i = 0; i < count; i++)
      layouts[i] =
          wgpuDeviceCreateBindGroupLayout(context_device(), bind_groups[i]);

    if (outLayout != NULL)
      memcpy(outLayout, layouts, layout_size);
  }

  WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
      context_device(), &(WGPUPipelineLayoutDescriptor){
                            .bindGroupLayoutCount = count,
                            .bindGroupLayouts = layouts,
                        });

  free(layouts);
  layouts = NULL;

  return pipeline_layout;
}

void standard_render_pipelines_destroy() {
  for (RenderPipelineType i = 0; i < RenderPipelineType_COUNT; i++)
    rem_destroy_render_pipeline(g_std_render_pipelines[i]);
}

const char *std_render_pipeline_label(const RenderPipelineType type) {

  static const char *labels[] = {
#define _(Pipeline) #Pipeline,
      STD_RENDER_PIPELINES(_)
#undef _
  };

  return labels[type];
}
const char *std_compute_pipeline_label(const ComputePipelineType type) {
  static const char *labels[] = {
#define _(Pipeline) #Pipeline,
      STD_COMPUTE_PIPELINES(_)
#undef _
  };

  return labels[type];
}
