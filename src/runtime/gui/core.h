#ifndef _GUI_CORE_H_
#define _GUI_CORE_H_

#include "backend/clock.h"
#include "backend/registry.h"
#include "backend/renderer/core.h"
#include "backend/theme/core.h"
#include "emscripten/html5.h"
#include "runtime/gui/tree.h"
#include "runtime/scene/core.h"
#include "runtime/texture/sprite_sheet.h"
#include <webgpu/webgpu.h> 

#define _GUI_MAX_SCENE_COUNT 1

typedef enum {
  GuiStatus_Success,
  GuiStatus_UndefError,
} GuiStatus;

typedef struct {

  reg_id_t id;
  double dpi;
  WGPURenderPassEncoder pass_encoder;
  WGPUCommandEncoder command_encoder;

  WGPUTexture depth_texture;
  WGPUTextureView depth_view;
  WGPUTextureView swapchain_view;

  GUITree tree;
  Scene *active_scene;
  Renderer *renderer; // TODO make a gui_system instead of passing scene and
                      // renderer in it
  Theme *theme;

} Gui;

typedef struct {
  double dpi;
  Theme *theme;
  Scene *active_scene;
  Renderer *renderer;
} GuiDescriptor;

// prevent c++ mangling
EXTERN_C_BEGIN

GuiStatus gui_init(Gui *, const GuiDescriptor *);
void gui_destroy(Gui *);

// TODO, separate this into the editor, I think the GUI object should be more
// flexible, cause it's sutruct is already versatile, but chat if we want
// another draw_callback. It feels like the current callback ties it too much to
// the scene editor. The approach should rather be: Scene Editor is a KIND of
// GUI. Same for the components, they should be under the scene editor
// directory, not gui
void gui_draw_callback(void *);

void gui_create_color_texture(const uint32_t, const uint32_t, WGPUTexture *,
                              WGPUTextureView *);

void gui_create_depth_texture(const uint32_t, const uint32_t, WGPUTexture *,
                              WGPUTextureView *);

static inline int gui_scale(const Gui *ui, const int size) {
  return size * ui->dpi;
}

static inline void gui_scale_vec2(const Gui *ui, const vec2 src, vec2 dest) {
  dest[0] = gui_scale(ui, src[0]);
  dest[1] = gui_scale(ui, src[1]);
}

bool keydown_callback(int, const EmscriptenKeyboardEvent *, void *);
bool wheel_callback(int, const EmscriptenWheelEvent *, void *);

EXTERN_C_END

#endif
