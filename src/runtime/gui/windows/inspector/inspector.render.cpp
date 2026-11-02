#include "inspector.render.hpp"
#include "backend/postfx/core.h"
#include "backend/renderer/core.h"
#include "backend/render_pass/core.h"
#include "backend/shaders/core.h"
#include "imgui/imgui.h"
#include "runtime/gui/components/input.hpp"
#include "runtime/gui/components/tree_item.hpp"
#include "runtime/gui/core.h"
#include "runtime/mesh/draw.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/systems/scene_system.h"
#include <stdint.h>

void UI::RenderTab::draw() {

  const InputStyle style = {
      .direction = InputDirection_Horizontal,
      .label_width = gui_scale(gui, 85),
  };

  ImGui::BeginChild("##render_properties", ImVec2(0, 0), true,
                    ImGuiWindowFlags_NoScrollWithMouse);

  ImGui::PushItemWidth(-1);
  // resolution / multisample
  if (UI::TreeItem(gui, "Resolution").draw()) {

    {
      width = renderer_width(renderer);
      if (UI::DragInt(gui, "Width", &style, &width, 1.0f, 1, 10000).draw()) {
        // update renderer
        renderer_set_width(renderer, width);
        // update viewport + uniform
        {
          viewport_set_width(&scene->viewport, width);
          viewport_uniform_update(&scene->viewport);
          ubo_update_queue_insert(scene->ubo, UBOType_Viewport,
                                  ubo_slot_id(&scene->viewport.ubo_slot));
        }
        // update scene render texture
        renderer_update_pass_texture(renderer, width, height, multisample, dpi);
      }
    }

    {
      height = renderer_height(renderer);
      if (UI::DragInt(gui, "Height", &style, &height, 1.0f, 1, 10000).draw()) {
        // update renderer
        renderer_set_height(renderer, height);
        // update viewport + uniform
        {
          viewport_set_height(&scene->viewport, height);
          viewport_uniform_update(&scene->viewport);
          ubo_update_queue_insert(scene->ubo, UBOType_Viewport,
                                  ubo_slot_id(&scene->viewport.ubo_slot));
        }
        // update scene render texture
        renderer_update_pass_texture(renderer, width, height, multisample, dpi);
      }
    }

    {

      char default_value[12];
      snprintf(default_value, 12, "x%d", renderer_multisample(renderer));

      UI::Combobox combobox = UI::Combobox(gui, "MSAA", &style, default_value);

      multisample = renderer_multisample(renderer);
      if (combobox.draw()) {
        for (int i = 0; i < IM_ARRAYSIZE(multisample_count); ++i) {

          const bool is_selected = (multisample_count[i] == multisample);

          char value[12];
          snprintf(value, 12, "x%d", multisample_count[i]);
          if (ImGui::Selectable(value, is_selected)) {
            RenderPipelineMultisampleCount count = multisample_count[i];
            renderer_set_multisample(renderer, count);

            // Set the initial focus when opening the combo (for keyboard
            // navigation)
            if (is_selected)
              ImGui::SetItemDefaultFocus();
          }
        }

        combobox.end();
      }
    }

    {
      dpi = (float)renderer_dpi(renderer);
      if (UI::DragFloat(gui, "DPI", &style, &dpi, 0.01f, 1, 4).draw()) {
        // update renderer
        renderer_set_dpi(renderer, dpi);
        // update scene render texture
        renderer_update_pass_texture(renderer, width, height, multisample, dpi);
      }
    }
    ImGui::TreePop();
  }

  if (UI::TreeItem(gui, "View").draw()) {

    {
      fov = viewport_fov(&scene->viewport);
      if (UI::DragFloat(gui, "FOV", &style, &fov, 1.0f, 10.0f, 179.9f).draw()) {
        viewport_set_fov(&scene->viewport, fov);
        viewport_uniform_update(&scene->viewport);
        ubo_update_queue_insert(scene->ubo, UBOType_Viewport,
                                ubo_slot_id(&scene->viewport.ubo_slot));
      }
    }

    {
      near_clip = viewport_near_clip(&scene->viewport);
      if (UI::DragFloat(gui, "Near Clip", &style, &near_clip, 0.01f, 0.01f,
                        1000.0f)
              .draw()) {
        viewport_set_near_clip(&scene->viewport, near_clip);
        viewport_uniform_update(&scene->viewport);
        ubo_update_queue_insert(scene->ubo, UBOType_Viewport,
                                ubo_slot_id(&scene->viewport.ubo_slot));
      }
    }

    {
      far_clip = viewport_far_clip(&scene->viewport);
      if (UI::DragFloat(gui, "Far Clip", &style, &far_clip, 0.01f, 0.01f,
                        1000.0f)
              .draw()) {

        viewport_set_far_clip(&scene->viewport, far_clip);
        viewport_uniform_update(&scene->viewport);
        ubo_update_queue_insert(scene->ubo, UBOType_Viewport,
                                ubo_slot_id(&scene->viewport.ubo_slot));
      }
    }
    ImGui::TreePop();
  }

  PostFx *texture_pass_fx =
      &render_pass_list_last_layer(
           renderer_mode_mesh_pass_list(renderer, RendererDrawMode_Texture))
           ->post_fx;

  PostFxEffect *bloom = post_fx_effect(texture_pass_fx, PostFxType_Bloom);
  PostFxEffect *composite =
      post_fx_effect(texture_pass_fx, PostFxType_Composite);

  if (UI::TreeItem(gui, "Tone Mapping").draw()) {

    if (UI::DragFloat(gui, "Exposure", &style,
                      &composite->uniform.composite.exposure, 0.01f, 0.0f,
                      10.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Composite,
                                    composite->uniform);

    if (UI::DragFloat(gui, "Gamma", &style, &composite->uniform.composite.gamma,
                      0.01f, 0.0f, 10.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Composite,
                                    composite->uniform);

    ImGui::TreePop();
  }

  if (UI::TreeItem(gui, "Bloom").draw()) {

    ImGui::Text("Enable");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x);
    if (ImGui::Checkbox("##enablebloom", NULL))
      post_fx_toggle_effect(texture_pass_fx, PostFxType_Bloom);

    if (UI::DragFloat(gui, "Threshold", &style, &bloom->uniform.bloom.threshold,
                      0.01f, 0.0f, 1.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Bloom,
                                    bloom->uniform);

    if (UI::DragFloat(gui, "Knee", &style, &bloom->uniform.bloom.knee, 0.01f,
                      0.0f, 1.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Bloom,
                                    bloom->uniform);

    if (UI::DragInt(gui, "Blur", &style, (int *)&bloom->uniform.bloom.blur,
                    1.0f, 0, 10)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Bloom,
                                    bloom->uniform);

    if (UI::DragFloat(gui, "Intensity", &style,
                      &composite->uniform.composite.bloom_intensity, 0.01f,
                      0.0f, 10.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Composite,
                                    composite->uniform);

    if (UI::DragInt(gui, "Downscale", &style,
                    (int *)&bloom->uniform.bloom.downscale, 1, 1, 6)
            .draw())
      post_fx_bloom_update_texture_resolution(texture_pass_fx, width, height);

    ImGui::TreePop();
  }

  if (UI::TreeItem(gui, "Vignette").draw()) {

    if (UI::DragFloat(gui, "Strength", &style,
                      &composite->uniform.composite.vignette_strength, 0.01f,
                      0.0f, 1.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Composite,
                                    composite->uniform);

    if (UI::DragFloat(gui, "Feather", &style,
                      &composite->uniform.composite.vignette_feather, 0.01f,
                      0.0f, 1.0f)
            .draw())
      post_fx_update_effect_uniform(texture_pass_fx, PostFxType_Composite,
                                    composite->uniform);

    ImGui::TreePop();
  }

  ImGui::PopItemWidth();
  ImGui::EndChild();
}
