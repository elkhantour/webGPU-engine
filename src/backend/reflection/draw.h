#ifndef _RENDERER_PROBE_DRAW_H_
#define _RENDERER_PROBE_DRAW_H_

#include "backend/compute/kawase.h"
#include "backend/logger.h"
#include "backend/profiler.h"
#include "backend/render_pass/core.h"
#include "backend/render_pass/draw.h"
#include "backend/resource_manager.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/scene/core.h"
#include "webgpu/webgpu.h"

typedef struct {
  Renderer *renderer;
  ProbeReflectionPlaneList *list;
} RendererDrawPlaneRelfectionData;

EXTERN_C_BEGIN

static inline void renderer_draw_plane_reflection(void *data) {

  // temp
  ProbeReflectionListDebug *debug = NULL;
  RendererDrawPlaneRelfectionData *payload =
      (RendererDrawPlaneRelfectionData *)data;

  Renderer *renderer = payload->renderer;
  ProbeReflectionPlaneList *list = payload->list;

  WGPUTextureView cached_view_color = list->pass.color.attachment.view;
  WGPUTextureView cached_view_depth = list->pass.depth.attachment.view;

  // then update probe list texture cube array based on each probes views
  profiler_latency_start(&renderer->profiler,
                         ProfilerLatencyType_ReflectionPass);
  render_pass_im_begin(&list->pass);
  {
    for (size_t i = 0; i < list->count; i++) {

      ProbeReflectionPlane *probe = &list->entries[i];

      // prevent self reflection by disabling probe meshes from the render pass
      // FIXME render_pass_disable_mesh_ref_list(&list->pass,
      // &probe->excluded_meshes);

      // define target layer
      WGPUTextureViewDescriptor target_color_desc = {
          .label = "Probe Reflection Plane Target Color View",
          .arrayLayerCount = 1,
          .baseArrayLayer = probe->texture_layer,
          .dimension = WGPUTextureViewDimension_2D,
          .baseMipLevel = 0,
          .mipLevelCount = 1,
      };
      WGPUTextureView target_color =
          rem_new_view(list->pass.color.texture, &target_color_desc);

      WGPUTextureViewDescriptor target_depth_desc = {
          .label = "Probe Reflection Plane Target Depth View",
          .arrayLayerCount = 1,
          .baseArrayLayer = probe->texture_layer,
          .dimension = WGPUTextureViewDimension_2D,
          .baseMipLevel = 0,
          .mipLevelCount = 1,
      };
      WGPUTextureView target_depth =
          rem_new_view(list->pass.depth.texture, &target_depth_desc);

      // update each mesh views/projections matrix
      ProbeReflectionListPreprocessorData preprocessor_data = {
          .camera_offset = probe->ubo_camera.id,
      };
      // FIXME render_pass_update_all_preprocessor_data(&list->pass, &preprocessor_data);

      // draw pass
      RenderPassDrawOptions target_views = {
          .color = target_color,
          .depth = target_depth,
      };
      render_pass_im_set_views(&list->pass, &target_views);
      render_pass_im_draw(&list->pass);

      if (debug && probe->texture_layer < debug->max_views)
        scene_debug_view_create(debug->scene_debug, target_color);
      else
        rem_destroy_view(&target_color);

      rem_destroy_view(&target_depth);

      // re-enable all meshes for next draw (dirty......)
      // FIXME render_pass_enable_all_mesh(&list->pass);
    }
  }
  render_pass_im_end(&list->pass);
  profiler_latency_end(&renderer->profiler, ProfilerLatencyType_ReflectionPass);

  // Kawase pass
  profiler_latency_start(&renderer->profiler, ProfilerLatencyType_KawasePass);
  {
    RenderPassDrawOptions src_views = {
        .color = cached_view_color,
        .depth = cached_view_depth,
    };
    render_pass_im_set_views(&list->pass, &src_views);

    compute_pass_kawase_draw(&renderer->compute_pass, 3);
  }
  profiler_latency_end(&renderer->profiler, ProfilerLatencyType_KawasePass);
}

static inline void probe_reflection_grid_list_draw(Renderer *renderer,
                                                   void *data) {

  // then update probe list texture cube array based on each probes views
  size_t layer = 0;

  ProbeReflectionListDebug *debug = NULL;
  ProbeReflectionGridList *list = (ProbeReflectionGridList *)data;

  render_pass_im_begin(&list->pass);
  {
    for (size_t i = 0; i < list->count; i++) {

      ProbeReflectionGrid *grid = list->entries[i];

      logger_add(LoggerFlag_Process, "Rendering Probe Reflection Grid %lu/%lu",
                 i + 1, list->count);

      for (size_t j = 0; j < grid->probes.count; j++) {

        ProbeReflection *probe = grid->probes.entries[j];

        for (uint8_t k = 0; k < PROBE_REFLECTION_VIEW_COUNT; k++) {

          WGPUTextureViewDescriptor color_view_desc = {
              .label = "Probe Reflection Target Color View",
              .arrayLayerCount = 1,
              .baseArrayLayer = layer,
              .dimension = WGPUTextureViewDimension_2D,
              .baseMipLevel = 0,
              .mipLevelCount = 1,
          };

          // define target layer
          WGPUTextureView target_color =
              rem_new_view(list->pass.color.texture, &color_view_desc);

          WGPUTextureViewDescriptor depth_view_desc = {
              .label = "Probe Reflection Target Depth View",
              .arrayLayerCount = 1,
              .baseArrayLayer = layer,
              .dimension = WGPUTextureViewDimension_2D,
              .baseMipLevel = 0,
              .mipLevelCount = 1,
          };

          WGPUTextureView target_depth =
              rem_new_view(list->pass.depth.texture, &depth_view_desc);

          ProbeReflectionListPreprocessorData preprocessor_data = {
              .camera_offset = probe->ubo_camera[k].id};

          // update each mesh views/projections matrix
          // FIXMErender_pass_update_all_preprocessor_data(&list->pass,
          //                                         &preprocessor_data);

          // draw pass
          RenderPassDrawOptions options = {
              .color = target_color,
              .depth = target_depth,
          };

          render_pass_im_set_views(&list->pass, &options);
          render_pass_im_draw(&list->pass);

          if (debug && layer < debug->max_views)
            scene_debug_view_create(debug->scene_debug, target_color);
          else
            rem_destroy_view(&target_color);

          rem_destroy_view(&target_depth);
          layer++;
        }
      }
    }
  }
  render_pass_im_end(&list->pass);
}

EXTERN_C_END

#endif
