#ifndef _RENDER_PASS_TEXTURE_H_
#define _RENDER_PASS_TEXTURE_H_

#include <webgpu/webgpu.h>

#include "core.h"

typedef enum {
  RenderPassTextureFlag_None = 1 << 0,
  RenderPassTextureFlag_ReleasePrevious = 1 << 1,
  RenderPassTextureFlag_AssignChildPasses = 1 << 2,
} RenderPassTextureFlag;

void render_pass_texture_create_multisample(WGPUTexture *, WGPUTextureView *,
                                            const RenderPassTextureDescriptor *,
                                            const RenderPassTextureFlag);

void render_pass_texture_create_monosample(WGPUTexture *, WGPUTextureView *,
                                           const RenderPassTextureDescriptor *,
                                           const RenderPassTextureFlag);

void render_pass_texture_create_color(RenderPass *,
                                      const RenderPassTextureDescriptor *,
                                      const RenderPassTextureFlag);

void render_pass_texture_create_depth(RenderPass *,
                                      const RenderPassTextureDescriptor *,
                                      const RenderPassTextureFlag);

void render_pass_texture_destroy_color(RenderPass *);
void render_pass_texture_destroy_depth(RenderPass *);

void render_pass_list_texture_create_shared_color(
    RenderPassList *, const RenderPassTextureDescriptor *, WGPUTexture *,
    WGPUTextureView *, const RenderPassTextureFlag);

void render_pass_list_texture_create_shared_depth(
    RenderPassList *, const RenderPassTextureDescriptor *, WGPUTexture *,
    WGPUTextureView *, const RenderPassTextureFlag);

void render_pass_list_destroy_shared_texture_color(RenderPassList *);
void render_pass_list_destroy_shared_texture_depth(RenderPassList *);

#endif
