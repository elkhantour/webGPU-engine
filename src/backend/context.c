#include "context.h"
#include "./resource_manager.h"
#include "backend/clock.h"
#include "backend/logger.h"
#include "backend/shaders/core.h"
#include "backend/std_texture/core.h"
#include "backend/theme/configs/default/default.icon.h"
#include "backend/theme/configs/default/default.size.h"
#include "backend/theme/configs/default/default.theme.h"
#include "backend/theme/core.h"
#include "emscripten/em_types.h"
#include "emscripten/emscripten.h"
#include "emscripten/html5_webgpu.h"
#include "runtime/html_event/core.h"
#include "runtime/input/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/texture/core.h"
#include "runtime/texture/sprite_sheet.h"
#include "string.h"
#include "webgpu/webgpu.h"

Context g_context = {0};

static inline WGPUSwapChain context_create_swapchain();
static void context_req_adapter_callback(WGPURequestAdapterStatus status,
                                         WGPUAdapter adapter,
                                         char const *message, void *userdata) {
  Context *ctxt = (Context *)userdata;
  ctxt->adapter = adapter;
  wgpuAdapterGetInfo(ctxt->adapter, &ctxt->adapter_info);
}

static bool context_update_size(int event_type,
                                const EmscriptenUiEvent *ui_event,
                                void *user_data);

ContextStatus context_init(const ContextDescriptor *desc) {

  g_context.html_target = strdup(desc->html_target);
  g_context.instance = wgpuCreateInstance(NULL);
  g_context.device = emscripten_webgpu_get_device();
  g_context.queue = wgpuDeviceGetQueue(g_context.device);
  g_context.dpi = emscripten_get_device_pixel_ratio();

  wgpuInstanceRequestAdapter(g_context.instance, NULL,
                             context_req_adapter_callback, (void *)&g_context);

  context_update_size(0, NULL, (void *)&g_context);

  wgpuDeviceSetUncapturedErrorCallback(g_context.device,
                                       context_handle_uncaptured_error, NULL);

  // === Debug & Error Handling ===

  // === Global Input & Event polling ===

  TIMER("Resource Manager", { resource_manager_init(); });

  TIMER("Fallback Textures", { standard_textures_init(); });

  TIMER("Default Theme", {
    theme_init(&g_theme, &(ThemeDescriptor){
                             .label = "Default Theme",
                             .colors = theme_default_color,
                             .sizes = theme_default_size,
                             .dpi = g_context.dpi,
                         });

    theme_create_icon_atlas(&g_theme,
                            &(TextureSpriteSheetDescriptor){
                                .cell_count = {16, 16},
                                .cell_size = {128, 128},
                                .format = TEXTURE_FORMAT_OFFSCREEN,
                                .label = "Scene UI Icon Atlas",
                                .path = PATH_TEXTURE("ui/icon_atlas.png"),
                            });

    theme_set_icons_coordinates(&g_theme, theme_default_icon);
  });

  TIMER("Clock", { clock_init(&g_clock); });

  TIMER("Standard Shaders", {
    standard_render_pipelines_init(PipelineMultisampleCount_1x);
    standard_compute_pipelines_init();
  });

  // init global HTML event manager
  html_event_init(g_context.html_target);

  // init resize event
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW,
                                 (void *)&g_context, false,
                                 context_update_size);

  // poll global input
  input_init(&(InputDescriptor){
      .mouse_sensitivity = 1.0f,
      .wheel_sensitivity = 1.0f,
  });

  return ContextStatus_Success;
}

WGPUSwapChain context_create_swapchain() {
  WGPUSurface surface = wgpuInstanceCreateSurface(
      g_context.instance,
      &(WGPUSurfaceDescriptor){
          .nextInChain = (WGPUChainedStruct *)(&(
              WGPUSurfaceDescriptorFromCanvasHTMLSelector){
              .chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector,
              .selector = g_context.html_target,
          }),
      });

  return wgpuDeviceCreateSwapChain(
      g_context.device, surface,
      &(WGPUSwapChainDescriptor){
          .usage = WGPUTextureUsage_RenderAttachment,
          .format = TEXTURE_FORMAT_ONSCREEN,
          .width = g_context.width * g_context.dpi,
          .height = g_context.height * g_context.dpi,
          .presentMode = WGPUPresentMode_Fifo,
      });
}

bool context_update_size(int event_type, const EmscriptenUiEvent *ui_event,
                         void *user_data) {

  Context *context = (Context *)user_data;
  double w, h;

  // retrieve canvas dimension
  emscripten_get_element_css_size(context->html_target, &w, &h);

  // define render resolution
  context->width = (int)w;
  context->height = (int)h;

  // set canvas size
  emscripten_set_element_css_size(context->html_target, w, h);

  if (context->swapchain) {
    // wgpuSwapChainRelease(renderer->wgpu.swapchain);
    // renderer->wgpu.swapchain = NULL;
  }

  context->swapchain = context_create_swapchain();

  return EM_FALSE;
}

ContextStatus context_close() {

  wgpuSwapChainRelease(g_context.swapchain);
  wgpuQueueRelease(g_context.queue);
  wgpuDeviceRelease(g_context.device);
  wgpuInstanceRelease(g_context.instance);

  return ContextStatus_Success;
}

void context_handle_device_error(WGPUErrorType type, const char *message,
                                 void *userdata) {
  printf("WebGPU error (%d): %s\n", type, message);
}

void handle_device_lost(WGPUDeviceLostReason reason, const char *message,
                        void *userdata) {
  printf("Device lost: %s\n", message);
}

void context_handle_uncaptured_error(WGPUErrorType type, char const *message, void* userdata){
  printf("Uncaptured Error: %s\n", message);
}
