#ifndef _RENDER_PASS_DRAW_H_
#define _RENDER_PASS_DRAW_H_

#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "core.h"
#include "webgpu/webgpu.h"

#include <stddef.h>
#include <string.h>

#include "backend/context.h"
#include "backend/postfx/core.h"
#include "backend/render_pass/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/draw.h"
#include "runtime/mesh/shader/core.h"
#include "webgpu/webgpu.h"

/**

  ▗▄▄▄ ▗▄▄▖  ▗▄▖ ▗▖ ▗▖
  ▐▌  █▐▌ ▐▌▐▌ ▐▌▐▌ ▐▌
  ▐▌  █▐▛▀▚▖▐▛▀▜▌▐▌ ▐▌
  ▐▙▄▄▀▐▌ ▐▌▐▌ ▐▌▐▙█▟▌

   Used in hot path so we static inline the calls

 */

/**
   Traverse a specific pass draw list and draw meshes onto the given render pass
   encoder
 */

// Pass
static inline void render_pass_draw(RenderPass *);
static inline void render_pass_draw_callback_resolve_multisample(RenderPass *);
static inline void render_pass_draw_callback_resolve_monosample(RenderPass *);

// Pass Immediate mode functions
static inline WGPUCommandEncoder render_pass_im_begin(RenderPass *);
static inline void render_pass_im_draw(RenderPass *);
static inline void render_pass_im_draw_common_pipeline(RenderPass *);
static inline void render_pass_im_end(RenderPass *);
static inline void render_pass_im_set_views(RenderPass *,
                                            const RenderPassDrawOptions *);
// Pass List
static inline void render_pass_list_draw(RenderPassList *);

/**
   ▗▄▄▖  ▗▄▖  ▗▄▄▖ ▗▄▄▖    ▗▖   ▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖
   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌       ▐▌     █  ▐▌     █
   ▐▛▀▘ ▐▛▀▜▌ ▝▀▚▖ ▝▀▚▖    ▐▌     █   ▝▀▚▖  █
   ▐▌   ▐▌ ▐▌▗▄▄▞▘▗▄▄▞▘    ▐▙▄▄▖▗▄█▄▖▗▄▄▞▘  █

   Draw the meshes of a given list along with a target topology and shader.
   The function will be call with attributes coming from the renderer
   draw_callbacks array.

   Using topology and shader callbacks allow greater flexibility when it comes
   to the different display modes.

   For each multisample pass we render to the scene renderer shared color and
   depth textures (accumulate).
   And after all passes we make a final one (resolve pass) where we send the
   shared texture to the swapchain.

      Render Pass 1     Render Pass 2       Resolve Pass
      +-----------+     +-----------+      +-----------+
      |           |     |           |      |           |
      |  Texture  |  +  |  Texture  |  =>  | Swapchain |
      |           |     |           |      |           |
      +-----------+     +-----------+      +-----------+

  For monosample we directly render to the swapchain as it is 1x sampled

      Render Pass 1     Render Pass 2
      +-----------+     +-----------+
      |           |     |           |
      | Swapchain |  +  | Swapchain |
      |           |     |           |
      +-----------+     +-----------+

 */

void render_pass_list_draw(RenderPassList *list) {

  /*
    Create 1 command encoder for all the render passes.
    Encoder records GPU operations such as:
    - Texture upload
    - Buffer upload
    - Render passes
    - Compute passes
   */

  WGPUCommandEncoder command_encoder =
      wgpuDeviceCreateCommandEncoder(context_device(), NULL);

  // get swapchain view to be resolved
  WGPUTextureView swapchain_view =
      wgpuSwapChainGetCurrentTextureView(context_swapchain());

  // Go through and draw each mode render pass
  for (size_t i = 0; i < list->count; i++) {
    RenderPass *pass = &list->layers[i];
    pass->command_encoder = command_encoder;
    render_pass_draw(pass);
  }

  {
    // create command buffer
    WGPUCommandBuffer render_buffer =
        wgpuCommandEncoderFinish(command_encoder, NULL);
    wgpuQueueSubmit(context_queue(), 1, &render_buffer);

    // release command encoder
    wgpuCommandEncoderRelease(command_encoder);
    wgpuCommandBufferRelease(render_buffer);
    rem_destroy_view(&swapchain_view);
  }
}

/**
    ▗▄▄▖ ▗▄▖ ▗▖   ▗▖   ▗▄▄▖  ▗▄▖  ▗▄▄▖▗▖ ▗▖ ▗▄▄▖
   ▐▌   ▐▌ ▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌▗▞▘▐▌
   ▐▌   ▐▛▀▜▌▐▌   ▐▌   ▐▛▀▚▖▐▛▀▜▌▐▌   ▐▛▚▖  ▝▀▚▖
   ▝▚▄▄▖▐▌ ▐▌▐▙▄▄▖▐▙▄▄▖▐▙▄▞▘▐▌ ▐▌▝▚▄▄▖▐▌ ▐▌▗▄▄▞▘

   Look up pass color draw based on msaa type

   Depending on mono sampling or multi sampling, the pass color attachment of
   renderer will be different (i.e. no resolveTarget for monosampling). As to
   a  void branching during the draw function, we set those callback as
   parameters before calling the draw.

   MSAA Texture (Nx) ===> resolved ===> Swapchain texture (1x)
 */

/*
  Only for On Screen last render pass in a pass list
  Resolve msaa => revolve view => blit => swapchain
 */
void render_pass_draw_callback_resolve_multisample(RenderPass *pass) {
  // Transfert cached resolve view (sampled 1x) to pass resolve
  pass->color.attachment.resolveTarget = pass->color.resolve_view;
  render_pass_im_draw(pass);
  post_fx_draw(&pass->post_fx, pass->command_encoder);
}

/*
  Basically the same as the resolve_multisample, except we don't need to assign
  a resolve target since the view is already monosampled.
 */
void render_pass_draw_callback_resolve_monosample(RenderPass *pass) {
  render_pass_im_draw(pass);
  post_fx_draw(&pass->post_fx, pass->command_encoder);
}

void render_pass_draw(RenderPass *pass) { pass->draw_callback(pass); }

/*
   ▗▄▄▄▖▗▖  ▗▖▗▖  ▗▖▗▄▄▄▖▗▄▄▄ ▗▄▄▄▖ ▗▄▖▗▄▄▄▖▗▄▄▄▖
     █  ▐▛▚▞▜▌▐▛▚▞▜▌▐▌   ▐▌  █  █  ▐▌ ▐▌ █  ▐▌
     █  ▐▌  ▐▌▐▌  ▐▌▐▛▀▀▘▐▌  █  █  ▐▛▀▜▌ █  ▐▛▀▀▘
   ▗▄█▄▖▐▌  ▐▌▐▌  ▐▌▐▙▄▄▖▐▙▄▄▀▗▄█▄▖▐▌ ▐▌ █  ▐▙▄▄▖

               ▗▖  ▗▖ ▗▄▖ ▗▄▄▄ ▗▄▄▄▖
               ▐▛▚▞▜▌▐▌ ▐▌▐▌  █▐▌
               ▐▌  ▐▌▐▌ ▐▌▐▌  █▐▛▀▀▘
               ▐▌  ▐▌▝▚▄▞▘▐▙▄▄▀▐▙▄▄▖


  Immediate mode provide modular functions to build up the render pass drawing.
  Can be usefull if in between draw we need to reassign attachment view
  dynamically between passes etc.

 */

WGPUCommandEncoder render_pass_im_begin(RenderPass *pass) {
  pass->command_encoder =
      wgpuDeviceCreateCommandEncoder(context_device(), NULL);
  return pass->command_encoder;
}

void render_pass_im_set_views(RenderPass *pass,
                              const RenderPassDrawOptions *overrides) {
  if (overrides && overrides->color)
    pass->color.attachment.view = overrides->color;

  if (overrides && overrides->depth)
    pass->depth.attachment.view = overrides->depth;
}

void render_pass_im_end(RenderPass *pass) {

  // create command buffer
  WGPUCommandBuffer render_buffer = wgpuCommandEncoderFinish(
      pass->command_encoder, NULL); // after 'end render pass'

  // submit commands
  wgpuQueueSubmit(context_queue(), 1, &render_buffer);

  // release command encoder
  wgpuCommandEncoderRelease(pass->command_encoder);
  pass->command_encoder = NULL;

  wgpuCommandBufferRelease(render_buffer);
}

void render_pass_im_draw(RenderPass *pass) {

  const WGPURenderPassDescriptor pass_desc = {
      .label = pass->label,
      .colorAttachmentCount = 1,
      .colorAttachments = &pass->color.attachment,
      .depthStencilAttachment = &pass->depth.attachment,
  };

  WGPURenderPassEncoder pass_encoder =
      wgpuCommandEncoderBeginRenderPass(pass->command_encoder, &pass_desc);

  // Go through and draw each mode render pass
  // Draw meshes
  // loop through mesh lists and draw meshes
  for (size_t j = 0; j < pass->layouts.count; j++) {

    // retrieve layout
    RenderPassLayout *layout = &pass->layouts.entries[j];
    MeshDrawPacketList *packets = &layout->drawn_meshes;

    // set 1 pipeline per layout
    WGPURenderPipeline pipeline =
        (*std_render_pipeline(layout->pipeline))->handle;

    wgpuRenderPassEncoderSetPipeline(pass_encoder, pipeline);

    // draw mesh with layout callbacks
    for (size_t k = 0; k < packets->count; k++) {
      MeshDrawPacket *pack = &packets->entries[k];
      shader_draw(pack->bindgroup_list, pack->shader_name, pass_encoder);
      mesh_draw(pack, pass_encoder);
    }
  }
  wgpuRenderPassEncoderEnd(pass_encoder);
  wgpuRenderPassEncoderRelease(pass_encoder);
}

#endif
