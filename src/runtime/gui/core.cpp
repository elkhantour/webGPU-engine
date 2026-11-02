#include "core.h"
#include "./draw.h"
#include "./imgui_style/style.carbon.hpp"
#include "./io.hpp"
#include "backend/clock.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/profiler.h"
#include "backend/registry.h"
#include "backend/renderer/core.h"
#include "backend/render_pass/core.h"
#include "backend/shaders/core.h"
#include "backend/theme/core.h"
#include "backend/ubo.h"
#include "emscripten/html5.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_wgpu.h"
#include "runtime/gui/windows/browser.hpp"
#include "runtime/gui/windows/display.hpp"
#include "runtime/gui/windows/gizmo.hpp"
#include "runtime/gui/windows/inspector/inspector.hpp"
#include "runtime/gui/windows/inspector/inspector.information.hpp"
#include "runtime/gui/windows/log.hpp"
#include "runtime/gui/windows/monitor.hpp"
#include "runtime/gui/windows/registry.hpp"
#include "runtime/gui/windows/render_mode.hpp"
#include "runtime/gui/windows/texture_viewer.hpp"
#include "runtime/gui/windows/tree.hpp"
#include "runtime/gui/windows/vertex_viewer.hpp"
#include "runtime/html_event/add.h"
#include "runtime/html_event/core.h"
#include "runtime/input/core.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/core.h"
#include "runtime/texture/core.h"
#include "runtime/viewport/core.h"
#include "stdio.h"
#include <cstring>

static ImGuiContext *imgui_context;

// layouts
static inline void gui_create_top_bar(Gui *);
static inline void gui_create_left_panel(Gui *);
static inline void gui_create_right_panel(Gui *);
static inline void gui_create_bottom_panel(Gui *);

bool keydown_callback(int eventType, const EmscriptenKeyboardEvent *e,
                      void *userData) {
  ImGuiIO &io = ImGui::GetIO();
  ImGuiKey key = (ImGuiKey)(ImGuiKey_NamedKey_BEGIN + e->keyCode);

  if (e->which > 0 && e->which < 0x10000)
    io.AddInputCharacter((unsigned int)e->which);

  return EM_TRUE;
}

bool wheel_callback(int eventType, const EmscriptenWheelEvent *e,
                    void *userData) {
  ImGuiIO &io = ImGui::GetIO();

  io.AddMouseWheelEvent(input_wheel_x() * input_wheel_sensitivity(),
                        input_wheel_y() * input_wheel_sensitivity());
  return EM_FALSE;
}

GuiStatus gui_init(Gui *gui, const GuiDescriptor *desc) {

  logger_add(LoggerFlag_Process, "Intitializing Editor UI");

  {
    gui->id = reg_register(gui, RegEntryType_Gui);
    gui->dpi = desc->dpi;
    gui->theme = desc->theme;
    gui->active_scene = desc->active_scene;
    gui->renderer = desc->renderer;

    gui_create_depth_texture((uint32_t)gui_scale(gui, context_width()),
                             (uint32_t)gui_scale(gui, context_height()),
                             &gui->depth_texture, &gui->depth_view);

    gui_tree_create(&gui->tree, GUI_TREE_CAPACITY);

    HTMLEventKey keydown_desc = {
        .callback = keydown_callback,
        .owner = gui->id,
    };
    html_event_add_key_down(&keydown_desc);

    HTMLEventWheel wheel_desc = {
        .callback = wheel_callback,
        .owner = gui->id,
    };
    html_event_add_wheel(&wheel_desc);
  }

  {
    imgui_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context);
    gui_style_carbon(gui);

    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(PATH_FONT("GolosText_Regular.ttf"), 14.0f);

    ImGui_ImplWGPU_InitInfo info;

    info.Device = context_device();
    info.RenderTargetFormat = TEXTURE_FORMAT_ONSCREEN;
    info.DepthStencilFormat = TEXTURE_FORMAT_DEPTH;
    info.NumFramesInFlight = 3;

    ImGui_ImplWGPU_Init(&info);
  }

  return GuiStatus_Success;
}

void gui_draw_callback(void *data) {

  Gui *gui = (Gui *)data;

  profiler_latency_end(&gui->renderer->profiler, ProfilerLatencyType_UIPass);
  profiler_latency_start(&gui->renderer->profiler, ProfilerLatencyType_UIPass);

  gui_command_begin(gui);
  gui_draw_swapchain_begin(gui);
  gui_draw_update_io(gui);
  {
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();
    {

      if (UI::Display::state & UI::DisplayState_Layout) {
        gui_create_top_bar(gui);
        gui_create_right_panel(gui);
        gui_create_bottom_panel(gui);
        UI::Gizmo(gui, "Gizmo").draw();
      }

      if (UI::Display::state & UI::DisplayState_Activity)
        UI::Monitor(gui, "Monitor").draw();

      UI::Registry(gui, "Registry").draw();
      UI::VertexViewer(gui, "Vertex Viewer").draw();
      UI::TextureViewer(gui, "Texture Viewer").draw();
      UI::Display(gui, "Display").draw();
    }
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), gui->pass_encoder);
  }
  gui_draw_swapchain_end(gui);
  gui_command_end(gui);
}

void gui_destroy(Gui *gui) {}

void gui_create_color_texture(const uint32_t width, const uint32_t height,
                              WGPUTexture *texture, WGPUTextureView *view) {

  WGPUTextureDescriptor tex_desc = {
      .label = "UI Color Texture",
      .dimension = WGPUTextureDimension_2D,
      .size =
          {
              .width = width,
              .height = height,
              .depthOrArrayLayers = 1,
          },
      .mipLevelCount = 1,
      .sampleCount = 1,
      .format = TEXTURE_FORMAT_ONSCREEN,
      .usage = WGPUTextureUsage_RenderAttachment,
  };

  *texture = wgpuDeviceCreateTexture(context_device(), &tex_desc);

  WGPUTextureViewDescriptor view_desc = {
      .label = "UI Color View",
      .format = TEXTURE_FORMAT_ONSCREEN,
      .dimension = WGPUTextureViewDimension_2D,
      .aspect = WGPUTextureAspect_All,
      .baseMipLevel = 0,
      .mipLevelCount = 1,
      .baseArrayLayer = 0,
      .arrayLayerCount = 1,
  };

  *view = wgpuTextureCreateView(*texture, &view_desc);
}

void gui_create_depth_texture(const uint32_t width, const uint32_t height,
                              WGPUTexture *texture, WGPUTextureView *view) {

  WGPUTextureDescriptor tex_desc = {
      .label = "Scene UI Depth Texture",
      .dimension = WGPUTextureDimension_2D,
      .size =
          {
              .width = width,
              .height = height,
              .depthOrArrayLayers = 1,
          },
      .mipLevelCount = 1,
      .sampleCount = 1,
      .format = TEXTURE_FORMAT_DEPTH,
      .usage = WGPUTextureUsage_RenderAttachment,
  };

  *texture = wgpuDeviceCreateTexture(context_device(), &tex_desc);

  WGPUTextureViewDescriptor view_desc = {
      .label = "Scene UI Depth View",
      .format = TEXTURE_FORMAT_DEPTH,
      .dimension = WGPUTextureViewDimension_2D,
      .aspect = WGPUTextureAspect_DepthOnly,
      .baseMipLevel = 0,
      .mipLevelCount = 1,
      .baseArrayLayer = 0,
      .arrayLayerCount = 1,
  };

  *view = wgpuTextureCreateView(*texture, &view_desc);
}

void gui_create_right_panel(Gui *gui) {

  ImGui::SetNextWindowPos(
      ImVec2(theme_scale_size(gui->theme, context_width()) -
                 theme_size(gui->theme, ThemeSize_RightPanel_Width),
             0));

  ImGui::SetNextWindowSize(
      ImVec2(theme_size(gui->theme, ThemeSize_RightPanel_Width),
             theme_scale_size(gui->theme, context_height())));

  ImGui::Begin("Right Panel", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
  {
    UI::Tree(gui, "Hierarchy").draw();
    UI::Inspector(gui, "Inspector").draw();
  }

  ImGui::End();
}

void gui_create_bottom_panel(Gui *gui) {

  ImGui::SetNextWindowPos(
      ImVec2(0, theme_scale_size(gui->theme, context_height()) -
                    theme_size(gui->theme, ThemeSize_BottomPanel_Height)));

  ImGui::SetNextWindowSize(
      ImVec2(theme_scale_size(gui->theme, context_width()) -
                 theme_size(gui->theme, ThemeSize_RightPanel_Width),
             theme_size(gui->theme, ThemeSize_BottomPanel_Height)));

  ImGui::Begin("Bottom Panel", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar);
  {
    UI::Log(gui, "Log").draw();
    // ImGui::SameLine();
    //  UI::Browser(gui, "Browser").draw();
  }

  ImGui::End();
}

void gui_create_top_bar(Gui *gui) {

  const int top_bar_width = theme_scale_size(gui->theme, context_width()) -
                            theme_size(gui->theme, ThemeSize_RightPanel_Width);

  UI::RenderMode render_mode_buttons = UI::RenderMode(gui, "Render mode");

  ImGui::SetNextWindowPos(
      ImVec2(top_bar_width -
                 (render_mode_buttons.count + 0.8) *
                     theme_size(gui->theme, ThemeSize_Button_RenderModeSize),
             0));

  render_mode_buttons.draw();
}
