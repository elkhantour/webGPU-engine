#include "texture_viewer.hpp"
#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "utils/math.h"

bool UI::TextureViewer::open = false;

// Parent texture of a registered view (nullptr if unknown).
static WGPUTexture lookup_texture(WGPUTextureView view) {
  const REMViewRecord *vr = rem_get_view_record(view);
  return vr ? vr->texture : nullptr;
}

// ImGui's image pipeline binds textures as texture_2d<f32> (filterable).
// Anything else (array/cube/3D views, depth, integer, 32-bit float,
// multisampled) fails bind group validation every frame, so it must never be
// passed to ImGui as an image.
static bool format_previewable(WGPUTextureFormat f) {
  switch (f) {
  case WGPUTextureFormat_R8Unorm:
  case WGPUTextureFormat_RG8Unorm:
  case WGPUTextureFormat_RGBA8Unorm:
  case WGPUTextureFormat_RGBA8UnormSrgb:
  case WGPUTextureFormat_BGRA8Unorm:
  case WGPUTextureFormat_BGRA8UnormSrgb:
  case WGPUTextureFormat_R16Float:
  case WGPUTextureFormat_RG16Float:
  case WGPUTextureFormat_RGBA16Float:
    return true;
  default:
    return false;
  }
}

// Can a 2D view of this texture be shown by ImGui?
static bool texture_sampleable(WGPUTexture t) {
  if (!t)
    return false;
  return wgpuTextureGetDimension(t) == WGPUTextureDimension_2D &&
         wgpuTextureGetSampleCount(t) == 1 &&
         (wgpuTextureGetUsage(t) & WGPUTextureUsage_TextureBinding) &&
         format_previewable(wgpuTextureGetFormat(t));
}

static const char *view_dimension_name(WGPUTextureViewDimension d) {
  switch (d) {
  case WGPUTextureViewDimension_1D:
    return "1D";
  case WGPUTextureViewDimension_2D:
    return "2D";
  case WGPUTextureViewDimension_2DArray:
    return "2D Array";
  case WGPUTextureViewDimension_Cube:
    return "Cube";
  case WGPUTextureViewDimension_CubeArray:
    return "Cube Array";
  case WGPUTextureViewDimension_3D:
    return "3D";
  default:
    return "?";
  }
}

static ImTextureID tid(WGPUTextureView v) { return (ImTextureID)v; }

static const char *format_name(WGPUTextureFormat f) {
  static char buf[16];
  switch (f) {
  case WGPUTextureFormat_R8Unorm:
    return "R8Unorm";
  case WGPUTextureFormat_RG8Unorm:
    return "RG8Unorm";
  case WGPUTextureFormat_RGBA8Unorm:
    return "RGBA8Unorm";
  case WGPUTextureFormat_RGBA8UnormSrgb:
    return "RGBA8UnormSrgb";
  case WGPUTextureFormat_BGRA8Unorm:
    return "BGRA8Unorm";
  case WGPUTextureFormat_BGRA8UnormSrgb:
    return "BGRA8UnormSrgb";
  case WGPUTextureFormat_R16Float:
    return "R16Float";
  case WGPUTextureFormat_RGBA16Float:
    return "RGBA16Float";
  case WGPUTextureFormat_R32Float:
    return "R32Float";
  case WGPUTextureFormat_RGBA32Float:
    return "RGBA32Float";
  case WGPUTextureFormat_Depth16Unorm:
    return "Depth16Unorm";
  case WGPUTextureFormat_Depth24Plus:
    return "Depth24Plus";
  case WGPUTextureFormat_Depth24PlusStencil8:
    return "Depth24PlusStencil8";
  case WGPUTextureFormat_Depth32Float:
    return "Depth32Float";
  default:
    snprintf(buf, sizeof(buf), "0x%X", (unsigned)f);
    return buf;
  }
}

static const char *dimension_name(WGPUTextureDimension d) {
  switch (d) {
  case WGPUTextureDimension_1D:
    return "1D";
  case WGPUTextureDimension_2D:
    return "2D";
  case WGPUTextureDimension_3D:
    return "3D";
  default:
    return "?";
  }
}

// ---------------------------------------------------------------------------

UI::TextureViewer::~TextureViewer() {
  releaseDerived();
  depth_preview_destroy(&depth_);
  if (views_.entries) {
    dyli_free((void **)&views_.entries, &views_.capacity, &views_.count);
  }
}

WGPUTextureView UI::TextureViewer::viewAt(size_t i) const {
  return ((WGPUTextureView *)views_.entries)[i];
}

void UI::TextureViewer::draw() {
  if (!open) {
    releaseDerived();
    return;
  }

  refresh();

  ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Textures", &open, ImGuiWindowFlags_NoCollapse)) {
    drawGrid();
  }
  ImGui::End(); // always call End(), even if Begin() returned false

  drawInspector();
}

void UI::TextureViewer::refresh() {
  // 1) ask how many views exist
  size_t count = 0;
  rem_get_texture_views(nullptr, &count);

  // 2) make sure the list can hold them (free + create, no expand needed)
  if (count > views_.capacity) {
    if (views_.entries) {
      dyli_free((void **)&views_.entries, &views_.capacity, &views_.count);
    }
    DynamicListStatus st =
        dyli_create((void **)&views_.entries, &views_.count, &views_.capacity,
                    sizeof(WGPUTextureView), count, "texture_views");
    if (st != DynamicListStatus_Success) {
      views_.count = 0;
      selected_ = nullptr;
      inspectorOpen_ = false;
      releaseDerived();
      return;
    }
  }

  // 3) fill it
  if (count > 0) {
    rem_get_texture_views((WGPUTextureView *)views_.entries, &count);
  }
  views_.count = count;

  // Drop the selection if the view no longer exists.
  if (selected_) {
    bool found = false;
    for (size_t i = 0; i < views_.count; i++) {
      if (viewAt(i) == selected_) {
        found = true;
        break;
      }
    }
    if (!found) {
      selected_ = nullptr;
      inspectorOpen_ = false;
      releaseDerived();
    }
  }
}

void UI::TextureViewer::drawGrid() {
  ImGui::SetNextItemWidth(160);
  ImGui::SliderFloat("Thumbnail", &thumb_, 48.0f, 256.0f, "%.0f px");
  ImGui::SameLine();
  ImGui::Text("%zu textures", views_.count);
  ImGui::Separator();

  if (!ImGui::BeginChild("TextureGrid")) {
    ImGui::EndChild();
    return;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  const float pad = style.FramePadding.x;
  const float cellW = thumb_ + pad * 2.0f + style.ItemSpacing.x;
  const float cellH = thumb_ + pad * 2.0f + style.ItemSpacing.y;
  const int cols = fmax(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
  const ImVec2 start = ImGui::GetCursorPos();

  for (int i = 0; i < (int)views_.count; i++) {
    WGPUTextureView view = viewAt((size_t)i);
    const REMViewRecord *vr = rem_get_view_record(view);
    const bool known = vr != nullptr;
    WGPUTexture texture = known ? vr->texture : nullptr;
    const bool direct = known && vr->dimension == WGPUTextureViewDimension_2D &&
                        texture_sampleable(texture);

    // Fit the thumbnail into a square cell, preserving aspect ratio.
    float w = thumb_, h = thumb_;
    if (texture) {
      float tw = (float)wgpuTextureGetWidth(texture);
      float th = (float)wgpuTextureGetHeight(texture);
      if (tw > 0 && th > 0) {
        float aspect = tw / th;
        if (aspect >= 1.0f)
          h = thumb_ / aspect;
        else
          w = thumb_ * aspect;
      }
    }

    int col = i % cols;
    int row = i / cols;
    ImGui::SetCursorPos(ImVec2(start.x + col * cellW + (thumb_ - w) * 0.5f,
                               start.y + row * cellH + (thumb_ - h) * 0.5f));

    ImGui::PushID(i);
    ImVec4 bg = (view == selected_) ? ImVec4(0.20f, 0.50f, 1.00f, 0.60f)
                                    : ImVec4(0, 0, 0, 0);
    bool clicked;
    if (direct) {
      clicked = ImGui::ImageButton("##tex", tid(view), ImVec2(w, h),
                                   ImVec2(0, 0), ImVec2(1, 1), bg);
    } else {
      // Not something ImGui can sample: placeholder, the inspector can still
      // show it through a derived 2D view when the texture allows it.
      char label[48];
      snprintf(label, sizeof(label), "%s\n%s",
               known ? view_dimension_name(vr->dimension) : "?",
               texture ? format_name(wgpuTextureGetFormat(texture)) : "");
      const bool sel = (view == selected_);
      if (sel)
        ImGui::PushStyleColor(ImGuiCol_Button, bg);
      clicked = ImGui::Button(label, ImVec2(w, h));
      if (sel)
        ImGui::PopStyleColor();
    }
    if (clicked) {
      selected_ = view;
      inspectorOpen_ = true;
      mip_ = known ? (int)vr->base_mip : 0;
      layer_ = known ? (int)vr->base_layer : 0;
      fit_ = true;
      releaseDerived();
    }
    if (ImGui::IsItemHovered()) {
      if (texture) {
        ImGui::SetTooltip("#%d  %ux%u  %s", i, wgpuTextureGetWidth(texture),
                          wgpuTextureGetHeight(texture),
                          format_name(wgpuTextureGetFormat(texture)));
      } else {
        ImGui::SetTooltip("#%d  %p", i, (void *)view);
      }
    }
    ImGui::PopID();
  }

  // Extend the scroll area (required after SetCursorPos).
  int rows = ((int)views_.count + cols - 1) / cols;
  ImGui::SetCursorPos(ImVec2(start.x, start.y + rows * cellH));
  ImGui::Dummy(ImVec2(0, 0));

  ImGui::EndChild();
}

void UI::TextureViewer::drawInspector() {

  if (!inspectorOpen_ || !selected_) {
    releaseDerived();
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(700, 560), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Texture Inspector", &inspectorOpen_)) {

    WGPUTexture tex = lookup_texture(selected_);
    WGPUTextureView shown = nullptr; // only ever a 2D view we created
    uint32_t w = 256, h = 256;       // fallback when the texture is unknown

    if (tex) {
      const uint32_t baseW = wgpuTextureGetWidth(tex);
      const uint32_t baseH = wgpuTextureGetHeight(tex);
      const uint32_t depthOrLayers = wgpuTextureGetDepthOrArrayLayers(tex);
      const uint32_t mips = wgpuTextureGetMipLevelCount(tex);
      const uint32_t samples = wgpuTextureGetSampleCount(tex);
      const WGPUTextureDimension dim = wgpuTextureGetDimension(tex);
      const WGPUTextureFormat fmt = wgpuTextureGetFormat(tex);

      ImGui::Text("Size:    %u x %u x %u (%s)", baseW, baseH, depthOrLayers,
                  dimension_name(dim));
      ImGui::Text("Format:  %s", format_name(fmt));
      ImGui::Text("Mips:    %u    Samples: %u", mips, samples);

      mip_ = clampf(mip_, 0, (int)mips - 1);
      layer_ = clampf(layer_, 0, (int)depthOrLayers - 1);

      const bool is2D = (dim == WGPUTextureDimension_2D);
      const bool sampleable = texture_sampleable(tex);

      ImGui::BeginDisabled(mips <= 1);
      ImGui::SetNextItemWidth(200);
      ImGui::SliderInt("Mip", &mip_, 0, (int)mips - 1);
      ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::BeginDisabled(!(is2D && depthOrLayers > 1));
      ImGui::SetNextItemWidth(200);
      ImGui::SliderInt("Layer", &layer_, 0, (int)depthOrLayers - 1);
      ImGui::EndDisabled();

      w = fmax(1u, baseW >> mip_);
      h = fmax(1u, baseH >> mip_);
      ImGui::Text("Showing: %u x %u", w, h);

      if (sampleable) {
        ensureDerived(tex, fmt, mip_, layer_);
        if (derived_)
          shown = derived_;
      } else if (depth_preview_supports(tex)) {
        DepthPreviewParams &dpp = depthParams_;
        bool lin = dpp.linearize != 0, rev = dpp.reversed_z != 0;
        ImGui::Checkbox("Linearize", &lin);
        ImGui::SameLine();
        ImGui::Checkbox("Reversed Z", &rev);
        dpp.linearize = lin;
        dpp.reversed_z = rev;
        if (lin) {
          ImGui::SameLine();
          ImGui::SetNextItemWidth(90);
          ImGui::DragFloat("Near", &dpp.near_z, 0.01f, 0.0001f, 1.0e6f, "%.3f");
          ImGui::SameLine();
          ImGui::SetNextItemWidth(90);
          ImGui::DragFloat("Far", &dpp.far_z, 0.1f, 0.001f, 1.0e7f, "%.2f");
          dpp.far_z = fmax(dpp.far_z, dpp.near_z + 0.0001f);
        }
        ImGui::SetNextItemWidth(260);
        ImGui::DragFloatRange2("Range", &dpp.range_min, &dpp.range_max, 0.001f,
                               0.0f, 1.0f, "min %.3f", "max %.3f");
        shown = depth_preview_render(&depth_, tex, mip_, layer_, w, h, &dpp);
      } else {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                           "Not previewable: needs a single-sampled 2D texture "
                           "with TextureBinding usage and an 8-bit unorm, "
                           "16-bit float or depth format.");
      }
    } else {
      ImGui::TextDisabled("Parent texture unknown: metadata, mip and layer "
                          "select unavailable.");
    }

    // Channel toggles (tint multiply). Alpha can't be ignored with this
    // method, so only R/G/B are offered.
    ImGui::Checkbox("R", &channel_[0]);
    ImGui::SameLine();
    ImGui::Checkbox("G", &channel_[1]);
    ImGui::SameLine();
    ImGui::Checkbox("B", &channel_[2]);
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
      fit_ = true;
    ImGui::SameLine();
    if (ImGui::Button("100%")) {
      zoom_ = 1.0f;
    }
    ImGui::SameLine();
    ImGui::Text("Zoom: %.0f%%", zoom_ * 100.0f);

    // ---- Zoom / pan canvas ----
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = fmax(avail.x, 64.0f);
    avail.y = fmax(avail.y, 64.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + avail.x, p0.y + avail.y);

    ImGui::InvisibleButton("canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // keep wheel for zoom
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO &io = ImGui::GetIO();

    if (fit_) {
      zoom_ = fmin(avail.x / (float)w, avail.y / (float)h);
      offsetX_ = (avail.x - w * zoom_) * 0.5f;
      offsetY_ = (avail.y - h * zoom_) * 0.5f;
      fit_ = false;
    }

    if (hovered && io.MouseWheel != 0.0f) {
      float newZoom = clampf(zoom_ * pow(1.15f, io.MouseWheel), 0.05f, 64.0f);
      // keep the image point under the cursor fixed
      float mx = io.MousePos.x - p0.x, my = io.MousePos.y - p0.y;
      float ix = (mx - offsetX_) / zoom_, iy = (my - offsetY_) / zoom_;
      zoom_ = newZoom;
      offsetX_ = mx - ix * zoom_;
      offsetY_ = my - iy * zoom_;
    }

    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
      offsetX_ += io.MouseDelta.x;
      offsetY_ += io.MouseDelta.y;
    }

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(24, 24, 24, 255));
    dl->PushClipRect(p0, p1, true);

    const ImVec2 tl(p0.x + offsetX_, p0.y + offsetY_);
    const ImVec2 br(tl.x + w * zoom_, tl.y + h * zoom_);
    const ImU32 tint = ImGui::ColorConvertFloat4ToU32(
        ImVec4(channel_[0] ? 1.0f : 0.0f, channel_[1] ? 1.0f : 0.0f,
               channel_[2] ? 1.0f : 0.0f, 1.0f));
    if (shown) {
      dl->AddImage(tid(shown), tl, br, ImVec2(0, 0), ImVec2(1, 1), tint);
      dl->AddRect(tl, br, IM_COL32(255, 255, 255, 60));
    } else {
      dl->AddText(ImVec2(p0.x + 10, p0.y + 10), IM_COL32(200, 200, 200, 255),
                  "No preview available for this texture.");
    }

    if (hovered) {
      int px = (int)floor((io.MousePos.x - tl.x) / zoom_);
      int py = (int)floor((io.MousePos.y - tl.y) / zoom_);
      if (px >= 0 && py >= 0 && px < (int)w && py < (int)h) {
        char buf[32];
        snprintf(buf, sizeof(buf), "(%d, %d)", px, py);
        dl->AddText(ImVec2(p0.x + 6, p1.y - 20), IM_COL32(255, 255, 255, 200),
                    buf);
      }
    }
    dl->PopClipRect();
  }
  ImGui::End();
}

void UI::TextureViewer::ensureDerived(WGPUTexture tex, WGPUTextureFormat fmt,
                                      int mip, int layer) {
  
  if (derived_ && derivedTex_ == tex && derivedMip_ == mip &&
      derivedLayer_ == layer)
    return;

  releaseDerived();

  WGPUTextureViewDescriptor d{};
  d.format = fmt;
  d.dimension = WGPUTextureViewDimension_2D;
  d.baseMipLevel = (uint32_t)mip;
  d.mipLevelCount = 1;
  d.baseArrayLayer = (uint32_t)layer;
  d.arrayLayerCount = 1;
  d.aspect = WGPUTextureAspect_All;

  derived_ = wgpuTextureCreateView(tex, &d);
  derivedTex_ = tex;
  derivedMip_ = mip;
  derivedLayer_ = layer;
}

void UI::TextureViewer::releaseDerived() {
  if (derived_) {
    wgpuTextureViewRelease(derived_);
    derived_ = nullptr;
  }
  derivedTex_ = nullptr;
  derivedMip_ = -1;
  derivedLayer_ = -1;
  depth_preview_release_source(&depth_);
}
