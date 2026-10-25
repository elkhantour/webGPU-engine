#include "create.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/compute/core.h"
#include "backend/compute/mipmap.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "core.h"
#include "ktx.h"
#include "resources/loader/ktx_loader.h"
#include "runtime/texture/processing.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize2.h"
#include "stb/stb_image_write.h"
#include "utils/math.h"
#include "utils/path.h"
#include "webgpu/webgpu.h"
#include "write.h"

#define FORCED_CHANNELS TextureChannel_RGBA

/** Single-mip view over a stbi-allocated RGBA8 buffer. */
static void texture_set_single_mip(TextureMip *out, unsigned char *data, int w,
                                   int h) {
  out->format = WGPUTextureFormat_RGBA8Unorm;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->mip_count = 1;
  out->mips[0].data = data;
  out->mips[0].size = (size_t)w * h * FORCED_CHANNELS;
}

/** stbi path: from file (path) or memory (mem), resized to fit max_size. */
static TextureStatus load_stbi(const char *path, const unsigned char *mem,
                               size_t mem_size, int max_size, bool flip,
                               TextureMip *out_mip) {
  int w = 0, h = 0, channels = 0;
  unsigned char *data = NULL;

  // thread-local flip flag: the global one is not safe from worker threads
  stbi_set_flip_vertically_on_load_thread(flip);

  data = path ? stbi_load(path, &w, &h, &channels, FORCED_CHANNELS)
              : stbi_load_from_memory(mem, (int)mem_size, &w, &h, &channels,
                                      FORCED_CHANNELS);

  if (!data)
    return TextureStatus_FileError;

  // Downscale a local Texture, then read the result back from it: the
  // dimensions / buffer may change (and the old buffer may be freed).
  Texture tex = {
      .channels = FORCED_CHANNELS,
      .data = data,
      .width = w,
      .height = h,
      .size = (size_t)w * h * FORCED_CHANNELS,
  };

  if (max_size > TextureResolution_Undefined)
    texture_downscale(&tex, max_size, max_size);

  texture_set_single_mip(out_mip, tex.data, tex.width, tex.height);
  return TextureStatus_Success;
}

#ifdef ENGINE_KTX
/** KTX path, two steps: load the ktxTexture2, then fetch the mips that fit
 *  max_size. On failure nothing is left allocated. */
static TextureStatus load_ktx(const char *path, const unsigned char *mem,
                              size_t mem_size, int max_size,
                              TextureMip *out_mip) {
  const char *name = path ? path : "<memory>";
  ktxTexture2 *ktx = NULL;

  LoaderKTXStatus s = ktx_texture_load(&ktx, path, mem, mem_size);
  if (s != LoaderKTXStatus_Success) {
    logger_add(LoggerFlag_Error,
               "Failed to load ktx texture: %s (Error code: %d)", name, s);
    memset(out_mip, 0, sizeof *out_mip);
    return TextureStatus_FileError;
  }

  s = ktx_texture_get_mips(ktx, max_size, out_mip, LoaderKTXMemory_CopyData);
  ktx_texture_destroy(&ktx); // always release, mips were copied

  if (s != LoaderKTXStatus_Success) {
    logger_add(LoggerFlag_Error, "Failed to read ktx mips: %s (Error code: %d)",
               name, s);
    memset(out_mip, 0, sizeof *out_mip);
    return TextureStatus_FileError;
  }

  return TextureStatus_Success;
}
#endif

TextureStatus texture_create(Texture *texture, const TextureCreateDescriptor *desc) {

  // if (texture->data)
  // texture_free(texture);

  texture->width = desc->width;
  texture->height = desc->height;
  texture->channels = desc->channels;
  texture->size = texture->width * texture->height * texture->channels;
  texture->data =
      (void *)calloc(texture->width * texture->height, texture->channels);

  if (texture->data == NULL) {
    logger_add(LoggerFlag_Error, "Could not create texture.");
    return TextureStatus_AllocFail;
  }

  if (desc->value != NULL)
    texture_write_fill(texture, desc->value);

  return TextureStatus_Success;
}

TextureStatus texture_destroy(Texture *texture) {
  stbi_image_free(texture->data);
  *texture = (Texture){0};

  return TextureStatus_Success;
}

TextureStatus
texture_mip_create_from_file(TextureMip *texture,
                             const TextureCreateFileDescriptor *desc) {

  const int max_size = MAX(desc->height, desc->width);

#ifdef ENGINE_KTX
  char path[1024];
  snprintf(path, sizeof(path), "%s", desc->path);
  // Every image was converted: ignore the original extension, use the .ktx2
  path_set_ext(path, sizeof(path), "ktx2");
  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", path);
  return load_ktx(path, NULL, 0, max_size, texture);
#else
  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", desc->path);
  return load_stbi(desc->path, NULL, 0, max_size, desc->flip, texture);
#endif
}

TextureStatus
texture_mip_create_from_memory(TextureMip *texture,
                               const TextureCreateMemoryDescriptor *desc) {

  const int max_size = MAX(desc->height, desc->width);

#ifdef ENGINE_KTX
  if (is_ktx2_memory(desc->data, desc->size))
    return load_ktx(NULL, desc->data, desc->size, max_size, texture);
#endif
  return load_stbi(NULL, desc->data, desc->size, max_size, desc->flip, texture);
}

/**
   load picture from file
 */
TextureStatus
texture_create_from_file(Texture *texture,
                         const TextureCreateFileDescriptor *desc) {

  logger_add(LoggerFlag_Import, "texture: %s", desc->path);

  if (desc->channels == TextureChannel_Undefined)
    texture->channels = FORCED_CHANNELS;
  else
    texture->channels = desc->channels;

  int width = 0, height = 0, channels = 0;
  texture_data data = NULL;

  if (path_has_ext(desc->path, "ktx2")) {
#ifdef ENGINE_KTX
    // KTX2 path always yields RGBA8
    texture->channels = FORCED_CHANNELS;
    data = load_ktx2_rgba8(desc->path, &width, &height);
#else
    logger_add(LoggerFlag_Error, "KTX2 support disabled (ENGINE_KTX off).");
#endif
  } else {
    // flip vertically so match wgpu coordinates
    // thread-local variant: safe to call from worker threads
    stbi_set_flip_vertically_on_load_thread(desc->flip);
    data = stbi_load(desc->path, &width, &height, &channels, texture->channels);
  }

  if (data == NULL) {
    logger_add(LoggerFlag_Error, "Couldn't load texture from file.");
    texture->data = NULL;
    texture->width = 0;
    texture->height = 0;
    texture->channels = 0;
    texture->size = 0;
    return TextureStatus_FileError;
  }

  texture->width = width;
  texture->height = height;
  texture->size = texture->width * texture->height * texture->channels;
  texture->data = data;

  // resize if width and height are provided in the descriptor
  if (desc->width > TextureResolution_Undefined ||
      desc->height > TextureResolution_Undefined)
    texture_downscale(texture, desc->width, desc->height);

  return TextureStatus_Success;
}

TextureStatus
texture_create_cubemap_from_file(Texture texture[TEXTURE_CUBE_LAYER],
                                 const TextureCreateCubeMapDescriptor *desc) {

  // put path in order
  const char *path_sort[TEXTURE_CUBE_LAYER] = {
      desc->path->right,  // +X
      desc->path->left,   // -X
      desc->path->top,    // +Y
      desc->path->bottom, // -Y
      desc->path->front,  // +Z
      desc->path->back,   // -Z
  };

  // load image to layer textures
  for (size_t i = 0; i < TEXTURE_CUBE_LAYER; i++) {
    const char *path = path_sort[i];

    if (texture_create_from_file(&texture[i],
                                 &(TextureCreateFileDescriptor){
                                     .channels = TextureChannel_Undefined,
                                     .height = desc->resolution,
                                     .width = desc->resolution,
                                     .flip = false,
                                     .path = path,
                                 }) != TextureStatus_Success) {
      logger_add(LoggerFlag_Error, "Couldn't read cubemap texture.");
      return TextureStatus_FileError;
    }
  }

  return TextureStatus_Success;
}

TextureStatus texture_create_mip(Texture *src, WGPUTexture *out_tex,
                                 WGPUTextureView *out_view) {

  static const char *label = "Mip Texture";
  const mip_t mip_count = mipmap_count(src->width, src->height);
  *out_tex = rem_new_texture(&(WGPUTextureDescriptor){
      .label = label,
      .mipLevelCount = mip_count,
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding   // read texturen in shader
               | WGPUTextureUsage_StorageBinding // write texture in shader
               | WGPUTextureUsage_CopyDst,       // upload the input data,
      .sampleCount = 1,
      .size =
          {
              .width = src->width,
              .height = src->height,
              .depthOrArrayLayers = 1,
          },
  });

  rem_write_texture(*out_tex, src->data, src->size, src->channels, 1, 0,
                    REMWriteFlag_None);

  {
    ComputePass mipmap;
    compute_pass_mipmap_create(&mipmap, &(ComputePassDescriptor){
                                            .label = label,
                                            .source_texture = *out_tex,
                                        });

    compute_pass_mipmap_draw(&mipmap);
    compute_pass_destroy(&mipmap);
  }

  if (out_view)
    *out_view =
        rem_new_view(*out_tex, &(WGPUTextureViewDescriptor){
                                   .dimension = WGPUTextureViewDimension_2D,
                                   .format = TEXTURE_FORMAT_OFFSCREEN,
                                   .arrayLayerCount = 1,
                                   .baseArrayLayer = 0,
                                   .mipLevelCount = 1,
                                   .baseMipLevel = 0,
                               });

  return TextureStatus_Success;
}
