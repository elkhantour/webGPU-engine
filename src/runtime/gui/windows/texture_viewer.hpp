#ifndef _GUI_WINDOW_TEXTURE_VIEWER_H_
#define _GUI_WINDOW_TEXTURE_VIEWER_H_

#include "backend/depth_preview.h"
#include "utils/dyli.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_wgpu.h"
#include "runtime/gui/windows/core.hpp"

namespace UI {

class TextureViewer : public Window {

public:
  TextureViewer(Gui *gui, const char *label) : Window(gui, label) {}
  ~TextureViewer();
  void draw() override;
  static bool open;

private:
  void refresh();
  void drawGrid();
  void drawInspector();
  void ensureDerived(WGPUTexture tex, WGPUTextureFormat fmt, int mip,
                     int layer);
  void releaseDerived();
  WGPUTextureView viewAt(size_t i) const;

  // list of WGPUTextureView (filled by rem_get_texture_views)
  DynamicList views_ = {};

  // grid
  float thumb_ = 128.0f;

  // inspector
  WGPUTextureView selected_ = nullptr;
  bool inspectorOpen_ = false;
  int mip_ = 0;
  int layer_ = 0;
  bool channel_[3] = {true, true, true}; // R, G, B
  float zoom_ = 1.0f;
  float offsetX_ = 0.0f, offsetY_ = 0.0f; // image top-left relative to canvas
  bool fit_ = true;

  // depth textures (shadow maps...): drawn into an RGBA8 target first
  DepthPreview depth_ = {};
  DepthPreviewParams depthParams_ = DEPTH_PREVIEW_PARAMS_DEFAULT;

  // single-mip / single-layer 2D view we create for inspection
  WGPUTexture derivedTex_ = nullptr;
  WGPUTextureView derived_ = nullptr;
  int derivedMip_ = -1;
  int derivedLayer_ = -1;
};

} // namespace UI

#endif
