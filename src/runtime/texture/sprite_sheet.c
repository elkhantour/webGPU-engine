#include "sprite_sheet.h"
#include "backend/resource_manager.h"
#include "runtime/texture/core.h"

#include "backend/logger.h"
#include "runtime/texture/create.h"
#include "webgpu/webgpu.h"

TextureStatus
texture_sprite_sheet_create(TextureSpriteSheet *atlas,
                            const TextureSpriteSheetDescriptor *desc) {

  if (desc->cell_count[0] > TEXTURE_SPRITE_SHEET_MAX_ROW ||
      desc->cell_count[1] > TEXTURE_SPRITE_SHEET_MAX_COL) {
    logger_add(LoggerFlag_Error,
               "Attempting to set a cell count out of maximum allowed cell "
               "count: [%d,%d], trying to set [%d, %d].",
               TEXTURE_SPRITE_SHEET_MAX_ROW, TEXTURE_SPRITE_SHEET_MAX_COL,
               desc->cell_count[0], desc->cell_count[1]);
    return TextureStatus_CellOutOfBound;
  }

  Texture texture;
  TextureStatus create =
      texture_create_from_file(&texture, &(TextureCreateFileDescriptor){
                                             .channels = TextureChannel_RGBA,
                                             .flip = false,
                                             .height = TEXTURE_HEIGHT_AUTO,
                                             .width = TEXTURE_WIDTH_AUTO,
                                             .path = desc->path,
                                         });

  if (create != TextureStatus_Success) {
    logger_add(LoggerFlag_Error, "Couldn't create texture atlas %s.",
               desc->label);
    return create;
  }

  if (desc->label)
    atlas->label = strdup(desc->label);

  atlas->texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Atlas Texture",
      .dimension = WGPUTextureDimension_2D,
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
      .size = {texture.width, texture.height, 1},
  });

  rem_write_texture(atlas->texture, texture.data, texture.size,
                    texture.channels, 0, 0, REMWriteFlag_STBIFreeData);

  atlas->view = rem_new_view(atlas->texture, NULL);

  atlas->width = texture.width;
  atlas->height = texture.height;
  glm_ivec2_copy((int *)desc->cell_count, atlas->cell_count);
  glm_ivec2_copy((int *)desc->cell_size, atlas->cell_size);

  return TextureStatus_Success;
}

TextureStatus texture_sprite_sheet_cell_uv(TextureSpriteSheet *atlas,
                                           ivec2 cell, vec2 uv0, vec2 uv1) {

  if (cell[0] > atlas->cell_count[0] || cell[1] > atlas->cell_count[1]) {
    logger_add(
        LoggerFlag_Warning,
        "Attempting to reach a cell out of texture atlas %s. Maximum cell "
        "count: [%d,%d], trying to reach [%d, %d].",
        atlas->label, atlas->cell_count[0], atlas->cell_count[1], cell[0],
        cell[1]);
    return TextureStatus_CellOutOfBound;
  }

  ivec2 dimension = {atlas->width, atlas->height};

  for (int i = 0; i < 2; i++) {
    uv0[i] = (float)cell[i] * atlas->cell_size[i] / dimension[i];
    uv1[i] = (float)(cell[i] + 1) * atlas->cell_size[i] / dimension[i];
  }

  return TextureStatus_Success;
}

TextureStatus texture_sprite_sheet_destroy(TextureSpriteSheet *atlas) {

  if (atlas->label) {
    free(atlas->label);
    atlas->label = NULL;
  }

  if (atlas->sampler)
    rem_destroy_sampler(&atlas->sampler);

  if (atlas->texture)
    rem_destroy_texture(&atlas->texture);

  if (atlas->view)
    rem_destroy_view(&atlas->view);

  return TextureStatus_Success;
}
