#include "atlas.h"

#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "runtime/texture/core.h"
#include "webgpu/webgpu.h"
#include <stdint.h>

TextureStatus texture_atlas_create(TextureAtlas *atlas,
                                   const TextureAtlasDescriptor *desc) {

  atlas->label = desc->label;

  uint8_t layer_count =
      (uint8_t)fminf(desc->layers.count, TEXTURE_ATLAS_MAX_LAYER);

  if (desc->layers.count > TEXTURE_ATLAS_MAX_LAYER)
    logger_add(LoggerFlag_Warning,
               "Attempting to access a layer index (%u) superior to the max "
               "layer count (%u) of texture atlas %s",
               layer_count, TEXTURE_ATLAS_MAX_LAYER, atlas->label);

  atlas->texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Module Atlas Texture",
      .dimension = WGPUTextureDimension_2D,
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .size =
          {
              desc->resolution,
              desc->resolution,
              layer_count,
          },
      .usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding,
  });

  for (uint8_t i = 0; i < layer_count; i++) {

    Texture texture;
    texture_create_from_file(&texture, &(TextureCreateFileDescriptor){
                                           .path = desc->layers.paths[i],
                                           .channels = TextureChannel_RGBA,
                                           .flip = false,
                                           .width = desc->resolution,
                                           .height = desc->resolution,
                                       });

    rem_write_texture(atlas->texture, texture.data, texture.size,
                      texture.channels, i, 0, REMWriteFlag_STBIFreeData);

    atlas->view[i] = rem_new_view(atlas->texture,
                                  &(WGPUTextureViewDescriptor){
                                      .label = "Module Atlas Texture View",
                                      .arrayLayerCount = 1,
                                      .baseArrayLayer = i,
                                      .mipLevelCount = 1,
                                      .aspect = WGPUTextureAspect_All,
                                      .dimension = WGPUTextureViewDimension_2D,
                                  });
  }

  return TextureStatus_Success;
}

TextureStatus texture_atlas_destroy(TextureAtlas *atlas) {

  for (uint8_t i = 0; i < wgpuTextureGetDepthOrArrayLayers(atlas->texture); i++)
    rem_destroy_view(&atlas->view[i]);

  rem_destroy_texture(&atlas->texture);

  return TextureStatus_Success;
}
