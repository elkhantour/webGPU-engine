#include "ktx_loader.h"

#include "backend/logger.h"
#include <ktx.h>
#include <strings.h>

bool is_ktx2_memory(const unsigned char *d, size_t n) {
  static const unsigned char magic[12] = {0xAB, 'K',  'T',  'X', ' ',  '2',
                                          '0',  0xBB, '\r', '\n', 0x1A, '\n'};
  return d && n >= sizeof(magic) && memcmp(d, magic, sizeof(magic)) == 0;
}

/** Load KTX2 (from file or memory), transcode to RGBA8, pick the smallest mip
 *  that fits max_size. Output is malloc'd (free with free()/stbi_image_free). */
unsigned char *load_ktx2(const char *path, const unsigned char *mem,
                                size_t mem_size, int *width, int *height,
                                int *channels, int max_size) {
  ktxTexture2 *tex = NULL;
  KTX_error_code r =
      path ? ktxTexture2_CreateFromNamedFile(
                 path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex)
           : ktxTexture2_CreateFromMemory(
                 mem, mem_size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
  if (r != KTX_SUCCESS) {
    logger_add(LoggerFlag_Warning, "KTX2 load failed: %s", ktxErrorString(r));
    return NULL;
  }

  if (ktxTexture2_NeedsTranscoding(tex)) {
    r = ktxTexture2_TranscodeBasis(tex, KTX_TTF_RGBA32, 0);
    if (r != KTX_SUCCESS) {
      logger_add(LoggerFlag_Warning, "KTX2 transcode failed: %s",
                 ktxErrorString(r));
      ktxTexture_Destroy(ktxTexture(tex));
      return NULL;
    }
  } else if (tex->vkFormat != 37 /*VK_FORMAT_R8G8B8A8_UNORM*/ &&
             tex->vkFormat != 43 /*VK_FORMAT_R8G8B8A8_SRGB*/) {
    logger_add(LoggerFlag_Warning, "KTX2 unsupported raw format %u",
               tex->vkFormat);
    ktxTexture_Destroy(ktxTexture(tex));
    return NULL;
  }

  // choose mip level replacing stbir resize
  uint32_t level = 0;
  while (level + 1 < tex->numLevels &&
         ((int)(tex->baseWidth >> level) > max_size ||
          (int)(tex->baseHeight >> level) > max_size))
    level++;

  int w = (int)glm_max(1, (int)(tex->baseWidth >> level));
  int h = (int)glm_max(1, (int)(tex->baseHeight >> level));

  ktx_size_t offset = 0;
  ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &offset);
  size_t level_size = ktxTexture_GetImageSize(ktxTexture(tex), level);

  unsigned char *out = malloc(level_size);
  if (out) {
    memcpy(out, ktxTexture_GetData(ktxTexture(tex)) + offset, level_size);
    *width = w;
    *height = h;
    *channels = 4;
  }

  ktxTexture_Destroy(ktxTexture(tex));
  return out;
}


/**
   Load a .ktx2 file as tightly packed RGBA8 (level 0 only).
   Returns a malloc'd buffer (free-able with stbi_image_free / free), or NULL.
 */
unsigned char *load_ktx2_rgba8(const char *path, int *width,
                                      int *height) {
  ktxTexture2 *kt = NULL;
  KTX_error_code err = ktxTexture2_CreateFromNamedFile(
      path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kt);
  if (err != KTX_SUCCESS) {
    logger_add(LoggerFlag_Error, "KTX2 open failed: %s",
               ktxErrorString(err));
    return NULL;
  }

  // Basis Universal (ETC1S/UASTC) files must be transcoded before use.
  // RGBA32 gives plain RGBA8; swap for BC7/ASTC/ETC2 later if you want
  // GPU-compressed textures.
  if (ktxTexture2_NeedsTranscoding(kt)) {
    err = ktxTexture2_TranscodeBasis(kt, KTX_TTF_RGBA32, 0);
    if (err != KTX_SUCCESS) {
      logger_add(LoggerFlag_Error, "KTX2 transcode failed: %s",
                 ktxErrorString(err));
      ktxTexture_Destroy(ktxTexture(kt));
      return NULL;
    }
  } else if (kt->vkFormat != 37 /* R8G8B8A8_UNORM */ &&
             kt->vkFormat != 43 /* R8G8B8A8_SRGB  */) {
    logger_add(LoggerFlag_Error, "KTX2 format %u not supported (need RGBA8).",
               kt->vkFormat);
    ktxTexture_Destroy(ktxTexture(kt));
    return NULL;
  }

  size_t size = ktxTexture_GetImageSize(ktxTexture(kt), 0);
  ktx_size_t offset = 0;
  ktxTexture_GetImageOffset(ktxTexture(kt), 0, 0, 0, &offset);

  // copy out: ktxTexture_Destroy frees the library-owned pixel data
  unsigned char *out = malloc(size);
  if (out != NULL) {
    memcpy(out, ktxTexture_GetData(ktxTexture(kt)) + offset, size);
    *width = (int)kt->baseWidth;
    *height = (int)kt->baseHeight;
  }

  ktxTexture_Destroy(ktxTexture(kt));
  return out;
}
