#include "inspector.information.hpp"
#include "backend/context.h"
#include "backend/theme/core.h"
#include "imgui/imgui.h"
#include "runtime/gui/components/spacing.hpp"
#include "runtime/gui/components/tree_item.hpp"
#include "runtime/gui/windows/display.hpp"
#include "runtime/gui/windows/registry.hpp"
#include "runtime/gui/windows/texture_viewer.hpp"

void UI::InfoTab::draw() {

  uint8_t i;

  UI::Spacing(gui, ThemeSize_Space_Medium).draw_y();
  ImGui::Text("Registry:");
  ImGui::SameLine();
  if (ImGui::Button("Open table"))
    UI::Registry::open = true;

  ImGui::Text("Textures:");
  ImGui::SameLine();
  if (ImGui::Button("Open mosaic"))
    UI::TextureViewer::open = true;

  // === Scene Stats Info ===
  const struct {
    const char *label;
    const StatCount value;
  } stats_items[] = {
      {"Vertex Count", StatCount_Vertex},
      {"Texture Count", StatCount_Texture},
      {"Shader Count", StatCount_Shader},
      {"Draw Call Count", StatCount_DrawCall},
  };

  static const int scene_item_count =
      sizeof(stats_items) / sizeof(stats_items[0]);

  UI::Spacing(gui, ThemeSize_Space_Medium).draw_y();
  if (UI::TreeItem(gui, "Scene").draw()) {
    for (i = 0; i < scene_item_count; i++) {
      ImGui::Spacing();
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x);
      ImGui::Text("%s: %d", stats_items[i].label,
                  stat_get_count(&scene->stats, stats_items[i].value));
      ImGui::PopTextWrapPos();
      ImGui::Separator();
    }
    ImGui::TreePop();
  }

  // === Device Info ===
  const struct {
    const char *label;
    const char *value;
  } device_items[] = {
      {"Vendor", g_context.adapter_info.vendor},
      {"Architecture", g_context.adapter_info.architecture},
      {"Device", g_context.adapter_info.device},
      {"Description", g_context.adapter_info.description},
      {"Backend", backend_label[g_context.adapter_info.backendType]},
      {"Adapter Type", adapter_type_label[g_context.adapter_info.adapterType]},
  };

  static const int device_item_count =
      sizeof(device_items) / sizeof(device_items[0]);

  UI::Spacing(gui, ThemeSize_Space_Small).draw_y();
  if (UI::TreeItem(gui, "Device").draw()) {
    for (i = 0; i < device_item_count; i++) {
      ImGui::Spacing();
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                             ImGui::GetContentRegionAvail().x);
      ImGui::Text("%s: %s", device_items[i].label, device_items[i].value);
      ImGui::PopTextWrapPos();
      ImGui::Separator();
    }
    ImGui::TreePop();
  }
}
