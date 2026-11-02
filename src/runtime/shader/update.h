#ifndef _SHADER_UPDATE_H_
#define _SHADER_UPDATE_H_

#include <stddef.h>
#include <stdint.h>

#include "bindgroup.h"
#include "core.h"
#include "runtime/texture/core.h"
#include "runtime/texture/texture.h"
#include "webgpu/webgpu.h"

typedef enum {
  ShaderUpdateFlag_None = 0,
  ShaderUpdateFlag_KeepPrevious = 1 << 0, // DELETEME
  ShaderUpdateFlag_ReleasePrevious = 1 << 1,
} ShaderUpdateFlag;

typedef struct {
  TextureResolution width;
  TextureResolution height;
  TextureChannel channels;
  unsigned char *data;
  size_t size;
  WGPUTextureViewDimension dimension;
  WGPUTextureFormat format;
} ShaderUpdateTexture;

EXTERN_C_BEGIN

ShaderBindGroupTextureEntry *
shader_update_texture(Shader *, const bind_group_index, const bind_index,
                      const ShaderUpdateTexture *, const ShaderUpdateFlag);

ShaderBindGroupTextureEntry *
shader_update_texture_mip(Shader *shader, const bind_group_index group_index,
                          const bind_index index, const TextureMip *texture,
                          const ShaderUpdateFlag flag);

ShaderBindGroupTextureEntry *
shader_update_texture_view(Shader *, const bind_group_index, const bind_index,
                           WGPUTextureView, WGPUTextureFormat,
                           const ShaderUpdateFlag);

ShaderBindGroupUniformEntry *
shader_update_uniform_data(Shader *, const bind_group_index, const bind_index,
                           void *, const ShaderUpdateFlag);

ShaderBindGroupUniformEntry *
shader_update_uniform_buffer(Shader *, const bind_group_index, const bind_index,
                             WGPUBuffer, const size_t, const ShaderUpdateFlag);


// TODO: since we use 'WGPU{Type}' for all the update function, why still use
// the WGPU{Type}Descriptor for this one? Replace to WPGUSampler
ShaderBindGroupSamplerEntry *
shader_update_sampler(Shader *, const bind_group_index, const bind_index,
                      const WGPUSamplerDescriptor *, const ShaderUpdateFlag);

ShaderBindGroup *shader_update_bind_group_offset(Shader *,
                                                 const bind_group_index,
                                                 const uint8_t, const size_t,
                                                 const ShaderUpdateFlag);

EXTERN_C_END

#endif
