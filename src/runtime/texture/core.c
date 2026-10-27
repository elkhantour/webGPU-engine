#include "core.h"
#include "backend/compute/mipmap.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"

#include <stdlib.h>

// cache texel size for division are expensive during hot path
float g_texture_resolution_texel_size[TextureResolution_4096 + 1] = {
    [TextureResolution_16] = 1.0f / TextureResolution_16,
    [TextureResolution_32] = 1.0f / TextureResolution_32,
    [TextureResolution_64] = 1.0f / TextureResolution_64,
    [TextureResolution_128] = 1.0f / TextureResolution_128,
    [TextureResolution_256] = 1.0f / TextureResolution_256,
    [TextureResolution_512] = 1.0f / TextureResolution_512,
    [TextureResolution_1024] = 1.0f / TextureResolution_1024,
    [TextureResolution_2048] = 1.0f / TextureResolution_2048,
    [TextureResolution_4096] = 1.0f / TextureResolution_4096,
};

void texture_save(Texture *texture, const char *path) {}
// DELETEME ? Already have destroy in create.h ?
void texture_free(Texture *texture) { free(texture->data); }

WGPUTexture texture_mip_upload(const TextureMip *textures) {

  // validate the mip count
  const uint8_t full = mipmap_count(textures->width, textures->height);
  uint8_t mip_count = textures->mip_count == 0 ? 1 : textures->mip_count;

  if (mip_count > TEXTURE_MAX_MIP_COUNT || mip_count > full) {
    logger_add(LoggerFlag_Warning,
               "Mip count %d is invalid for a %ux%u texture (max %d), "
               "clamping",
               mip_count, textures->width, textures->height,
               full < TEXTURE_MAX_MIP_COUNT ? full : TEXTURE_MAX_MIP_COUNT);
    mip_count = full < TEXTURE_MAX_MIP_COUNT ? full : TEXTURE_MAX_MIP_COUNT;
  }
  
  WGPUTexture gpu_texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Anonymous shader bindgroup texture (mip)",
      .dimension = WGPUTextureDimension_2D,
      .format = textures->format,
      .size = {textures->width, textures->height, 1},
      .mipLevelCount = mip_count,
      .sampleCount = 1,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  // upload every level, sizes are derived from the base size
  for (uint8_t level = 0; level < mip_count; level++) {
    const TextureMipView *mip = &textures->mips[level];
    if (mip->data == NULL)
      continue;

    const uint32_t w = textures->width >> level ? textures->width >> level : 1;
    const uint32_t h =
        textures->height >> level ? textures->height >> level : 1;

    rem_write_texture(gpu_texture, mip->data, mip->size, textures->channels, 0,
                      level, REMWriteFlag_None);
  }

  return gpu_texture;
}
