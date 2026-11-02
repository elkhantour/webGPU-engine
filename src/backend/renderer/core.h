#ifndef _RENDERER_CORE_H_
#define _RENDERER_CORE_H_

#include <emscripten/html5.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "backend/render_pass/render_pass.h"
#include "backend/ao_bake/core.h"
#include "backend/clock.h"
#include "backend/compute/core.h"
#include "backend/profiler.h"
#include "backend/registry.h"
#include "backend/renderer/mesh_config.h"
#include "backend/shaders/core.h"
#include "backend/stat.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/pipeline.h"
#include "runtime/pipeline/render.h"
#include "runtime/shader/core.h"
#include "runtime/texture/core.h"
#include "runtime/texture/texture.h"
#include "utils/hsht.h"
#include "webgpu/webgpu.h"
#include "backend/shadow_map/core.h"

#define RENDERER_MAX_HOOK 6
#define RENDERER_DPI_AUTO 0
#define RENDERER_WIDTH_AUTO 0
#define RENDERER_HEIGHT_AUTO 0

/*
  Renderer has a list of mesh and sublist of mesh pointers that are called
  during certain render pass.
  Nothe that the meshes children also holds pointers to this global list, hence
  it's necessary to take care to handle them accordingly if a mesh is added or
  removed from the global list.

       Global List (Pool)        Pipeline Lists

                                 [Lit/ Physical Meshes]
                                 .----------.
                       .-------> | 0x3948ef |
                      |	         |----------|
       .----------.   |  .-----> | 0x49da39 |
       |  Mesh 1  | --' |        |----------|
       |----------|     |  .-->  | 0xed93fa |
       |  Mesh 3  | ----' |      '----------'
       |----------|       |
       |  Mesh 4  | ------'
       |----------|
       |  Mesh 5  | ------.
       |----------|       |     [Unlit/ Flat Meshes]
       |  Mesh 6  | ----. |     .----------.
       |----------|     |  '--> | 0x48daec |
       |  Mesh 7  | --. |       |----------|
       '----------'   | '-----> | 0x7423bc |
                      |         |----------|
                      '-------> | 0x3e2baf |
                                '----------'

   Render pass and Scene Mesh Lists work hand in hand.
   Meaning by pushing a mesh in a certain Scene Mesh List it will go through a
   predefined Renderer pipeline.

   The render passes are segmented in 2 global classes:
   - Dynamic: Will change depending on Render mode (wireframe/solid/textured).
   - Fixed: Is independant from Render mode.

   Currently the scene offers the following Mesh List depending on requirements:

   .----------.---------------.-----------.----------------.-------------------.
   |   Name   |  Shadow Pass  |  AO Pass  |  Fixed/Dynamic | Common use case   |
   |----------+---------------+-----------+----------------+-------------------|
   |   Lit    |       Y       |     Y     |     Dynamic    | Physical objects  |
   |----------+---------------+-----------+----------------+-------------------|
   |  UnLit   |        -      |     -     |     Dynamic    | Flat objects/ UI  |
   |----------+---------------+-----------+----------------+-------------------|
   |  Fixed   |        -      |     -     |      Fixed     | Gizmo/ Debug      |
   '----------'---------------'-----------'----------------'-------------------'

 */

typedef struct Renderer Renderer;

typedef enum {
  RendererStatus_Success,
  RendererStatus_MeshVisible,
  RendererStatus_MeshHidden,
  RendererStatus_LayoutUnfound,
  RendererStatus_UndefError,
} RendererStatus;

typedef struct {
  WGPUColor background;
  const double dpi;
  const int width;
  const int height;
} RendererCreateDescriptor;

typedef void (*renderer_draw_callback)(void *);

typedef struct {
  renderer_draw_callback callback;
  void *data;
} RendererDrawCallback;

typedef struct {
  RendererDrawCallback entries[RENDERER_MAX_HOOK];
  size_t count;
} RendererDrawCallbackList;

struct Renderer {

  reg_id_t id;
  Profiler profiler;

  double dpi;
  WGPUColor background;
  int width, height;
  RenderPipelineMultisampleCount multisample;

  // cached texture shared throughout parent scene objects
  struct {
    RendererTextureAO ambient_occlusion;
  } texture; // TODO Make a TextureManager

  ShadowMap shadow_map;
  RendererDrawMode draw_mode;

  RendererDrawCallbackList callbacks[RENDERER_DRAW_MODE_COUNT];

  RenderPassList mesh_pass[RENDERER_DRAW_MODE_COUNT];
  RenderPassList plane_reflection_pass;
  RenderPassList probe_reflection_pass;

  ComputePass compute_pass;
};

EXTERN_C_BEGIN

void renderer_create(Renderer *, const RendererCreateDescriptor *);

void renderer_destroy(Renderer *);
void renderer_set_draw_mode(Renderer *, const RendererDrawMode);

void renderer_draw_layout_callback(void *);

void renderer_add_draw_callback(Renderer *, renderer_draw_callback, void *,
                                const int);

renderer_draw_callback renderer_find_draw_callback(Renderer *,
                                                   renderer_draw_callback);

void renderer_draw(Renderer *);

void renderer_update_pass_texture(Renderer *, int, int,
                                  const RenderPipelineMultisampleCount,
                                  const double);

void renderer_create_layouts(Renderer *);

// accessors
static inline const RendererDrawMode renderer_draw_mode(Renderer *renderer) {
  return renderer->draw_mode;
}

static inline RenderPassList *
renderer_active_mesh_pass_list(Renderer *renderer) {
  return &renderer->mesh_pass[__builtin_ctz(renderer->draw_mode)];
}

static inline RenderPassList *
renderer_mode_mesh_pass_list(Renderer *rd, const RendererDrawMode mode) {
  return &rd->mesh_pass[__builtin_ctz(mode)];
}

static inline const int renderer_width(Renderer *rd) { return rd->width; }
static inline const int renderer_height(Renderer *rd) { return rd->height; }
static inline const double renderer_dpi(Renderer *rd) { return rd->dpi; }
static inline const RenderPipelineMultisampleCount
renderer_multisample(Renderer *rd) {
  return rd->multisample;
}

// mutators

static inline void renderer_set_width(Renderer *rd, const int value) {
  rd->width = value;
}

static inline void renderer_set_height(Renderer *rd, const int value) {
  rd->height = value;
}

static inline void renderer_set_dpi(Renderer *rd, const double value) {
  rd->dpi = glm_max(1, value);
}

static inline void
renderer_set_multisample(Renderer *rd,
                         const RenderPipelineMultisampleCount value) {
  rd->multisample = value;

  // rebuild pipelines
  // TODO: dangerous since we update the global pipelines, what if we use
  // multiple render ? Cache the std pipelines with different multisample ? Add
  // it as a batch attribute ? Currently ok cause we only rely on one renderer,
  // but smelly and highly prone to errors.
  standard_render_pipelines_destroy();
  standard_render_pipelines_init(value);

  // update scene render texture
  renderer_update_pass_texture(rd, renderer_width(rd), renderer_height(rd),
                               value, renderer_dpi(rd));

  // update passes relative draw callbacks for each modes
  for (uint8_t i = 0; i < RENDERER_DRAW_MODE_COUNT; i++)
    render_pass_list_update_child_passes_callback(&rd->mesh_pass[i]);
}

static inline RenderPassList *
renderer_mesh_pass_list(Renderer *renderer, const RendererDrawMode mode) {
  return &renderer->mesh_pass[__builtin_ctz(mode)];
}

EXTERN_C_END

#endif
