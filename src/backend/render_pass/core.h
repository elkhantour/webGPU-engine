#ifndef _RENDER_PASS_CORE_H_
#define _RENDER_PASS_CORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#include "backend/postfx/core.h"
#include "backend/renderer/mesh_config.h"
#include "backend/shaders/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/draw.h"
#include "runtime/mesh/mesh.h"
#include "runtime/pipeline/render.h"
#include "utils/stli.h"

#define RENDER_PASS_MAX_DRAW_LIST 32
#define RENDER_PASS_COUNT 2
#define RENDER_PASS_VIEW_UNDEFINED 0
#define RENDER_PASS_VIEW_OVERRIDE_NONE 0
#define RENDER_PASS_VIEW_CAPACITY 256

typedef enum {
  RenderPassType_OnScreen,
  RenderPassType_OffScreen,
} RenderPassType;

typedef struct RenderPass RenderPass;
typedef struct RenderPassList RenderPassList;
typedef struct RenderPassDrawOptions RenderPassDrawOptions;

typedef void (*render_pass_draw_callback)(RenderPass *);
typedef void (*render_pass_list_draw_callback)(RenderPassList *);

typedef enum {
  RenderPassStatus_Success,
  RenderPassStatus_OutOfBoundDrawIndex,
  RenderPassStatus_LayoutUnfound,
  RenderPassStatus_DrawListUpdateError,
  RenderPassStatus_MeshAlreadyEnabled,
  RenderPassStatus_UndefError,
} RenderPassStatus;

typedef enum {
  RenderPassLayoutAcceptStatus_Accepted,
  RenderPassLayoutAcceptStatus_Hidden,
  RenderPassLayoutAcceptStatus_Unmatch_DrawMode,
  RenderPassLayoutAcceptStatus_Unmatch_Layer,
  RenderPassLayoutAcceptStatus_Unmatch_Pipeline,
  RenderPassLayoutAcceptStatus_Undefined_Shader,
  RenderPassLayoutAcceptStatus_Outline,
} RenderPassLayoutAcceptStatus;

/**
   Since the priority for hot path (draw loop) is cache hit, we reorganise the
   data read the draw loop in a compact way (reducing spatial proximity).
   We will define "Packet" as a keyword for every struct that serve this
   purpose.

        COLD PATH                                         HOT PATH
            |------------------------------------------------|
       Base Struct                                        Packet
   .--------------------.                          .-------------------.
   | Mesh               |                          | Mesh Draw Packet  |
   |  + …               |                          |  + v attr         |
   |  + …               |                          |  + i attr         |
   |  + v attr          |   ====[ FILTER ]====>    |  + bindgroup      |
   |  + i attr          |                          |  + pipeline       |
   |  + …               |                          |  --- extras ---   |
   |  + Shader          |                          |  + shader name    |
   |     + …            |                          |  + mesh id        |
   |     + bindgroup    |                          '-------------------'
   |     + pipeline     |
   |     + …            |
   |  +…                |
   |                    |
   '--------------------'

   Structured for human                            Structured for cache
   readibility.                                    efficiency.

 */

typedef struct {
  MeshShader shader;
  RenderPipelineType pipeline;
  mesh_get_topology_callback topology_callback;
  MeshDrawPacketList
      drawn_meshes; // Compact and cache friendly data from mesh and shader
} RenderPassLayout;

typedef STATIC_LIST(RenderPassLayout,
                    RENDER_PASS_MAX_DRAW_LIST) RenderPassLayoutList;

typedef struct {
  MeshShader shader;
  RenderPipelineType pipeline;
  mesh_get_topology_callback topology_callback;
} RenderPassLayoutDescriptor;

typedef struct {
  RenderPassLayoutDescriptor entries[RENDER_PASS_MAX_DRAW_LIST];
  size_t count;
} RenderPassLayoutListDescriptor;

// Descriptor
typedef struct {
  WGPUTexture texture;
  WGPUTexture resolve_texture;
  WGPUTextureView resolve_view;
  WGPUTextureView views[RENDER_PASS_VIEW_CAPACITY];
  size_t views_count;
  WGPURenderPassColorAttachment attachment;
} RenderPassColor;

typedef struct {
  WGPUTexture texture;
  WGPUTextureView views[RENDER_PASS_VIEW_CAPACITY];
  size_t views_count;
  WGPURenderPassDepthStencilAttachment attachment;
} RenderPassDepth;

/**
   Basically a RenderPass is just a config with a color, depth attachment and a
   builtin encoder to render layouts from the render layout.
*/
struct RenderPass {
  const char *label;
  RenderPassType type;
  RenderPassColor color;
  RenderPassDepth depth;
  RenderPipelineMultisampleCount multisample;

  RenderPassLayoutList layouts;

  render_pass_draw_callback draw_callback;
  WGPUCommandEncoder command_encoder;
  PostFx post_fx;
};

struct RenderPassList {
  struct {

    struct {
      WGPUTexture texture;
      WGPUTextureView view;
    } color;

    struct {
      WGPUTexture texture;
      WGPUTextureView view;
    } depth;

  } shared;

  RenderPass layers[RENDER_PASS_MAX_DRAW_LIST];
  size_t count;
  render_pass_list_draw_callback draw_callback;
};

typedef struct {
  const int width;
  const int height;
  const RenderPipelineMultisampleCount multisample;
  const WGPUTextureFormat format;
} RenderPassTextureDescriptor;

typedef struct {
  WGPUTexture texture;
  WGPUTextureFormat format;
  WGPURenderPassDepthStencilAttachment attachment;
} RenderPassDepthAttachment;

typedef struct {
  WGPUTexture texture;
  WGPUTextureFormat format;
  WGPURenderPassColorAttachment attachment;
} RenderPassColorAttachment;

typedef struct {
  const RenderPassType type;
  const char *label;
  RenderPassColorAttachment *color;
  RenderPassDepthAttachment *depth;
  int width;
  int height;
  RenderPipelineMultisampleCount multisample;
  const RenderPassLayoutListDescriptor *draw_list;
} RenderPassCreateDescriptor;

struct RenderPassDrawOptions {
  WGPUTextureView color, depth;
};

EXTERN_C_BEGIN

/* === Pass List === */
void render_pass_list_create(RenderPassList *);

RenderPass *render_pass_list_insert_pass(RenderPassList *,
                                         const RenderPassCreateDescriptor *);

void render_pass_list_update_child_passes_callback(RenderPassList *);

static inline RenderPass *render_pass_list_last_layer(RenderPassList *list) {
  return &list->layers[list->count - 1];
}

/* === Pass === */

void render_pass_draw_list_copy(const RenderPassLayoutListDescriptor *,
                                RenderPassLayoutList *);

void render_pass_create(RenderPass *, const RenderPassCreateDescriptor *);
void render_pass_destroy(RenderPass *);

StaticListStatus render_pass_view_color_insert(RenderPass *, WGPUTextureView);
StaticListStatus render_pass_view_depth_insert(RenderPass *, WGPUTextureView);

StaticListStatus render_pass_view_color_remove(RenderPass *, WGPUTextureView);
StaticListStatus render_pass_view_depth_remove(RenderPass *, WGPUTextureView);

WGPUTextureView render_pass_view_color(RenderPass *, size_t);
WGPUTextureView render_pass_view_depth(RenderPass *, size_t);

void render_pass_sync_drawn_layouts(RenderPass *);

/* === Layout === */

RenderPassLayoutAcceptStatus
render_pass_layout_accepts(Mesh *mesh, const RenderPassLayout *layout,
                           const RendererDrawMode mode, const size_t layer);

bool render_pass_layout_add_mesh(RenderPassLayout *layout, Mesh *mesh);

bool render_pass_layout_toggle_mesh(RenderPassLayout *layout, Mesh *mesh,
                                    RendererDrawMode mode, RendererLayer layer,
                                    bool remove_all);

EXTERN_C_END

#endif
