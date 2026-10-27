#include "ktx_loader.h"

#include "backend/logger.h"
#include <ktx.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

/* ---------- shared statics ---------- */

#define KTX_VK_FORMAT_RGBA8_UNORM 37
#define KTX_VK_FORMAT_RGBA8_SRGB 43

/** Create the ktxTexture2 from a file path or a memory blob. */
static LoaderKTXStatus ktx_open(const char *path, const unsigned char *mem,
                                size_t mem_size, ktxTexture2 **out) {
  ktxTexture2 *tex = NULL;
  KTX_error_code r =
      path ? ktxTexture2_CreateFromNamedFile(
                 path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex)
           : ktxTexture2_CreateFromMemory(
                 mem, mem_size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &tex);
  if (r != KTX_SUCCESS) {
    logger_add(LoggerFlag_Warning, "KTX2 load failed: %s", ktxErrorString(r));
    return LoaderKTXStatus_FailLoad;
  }
  *out = tex;
  return LoaderKTXStatus_Success;
}

/** Transcode if needed, then make sure the result is RGBA8 (UNORM or SRGB).
 *  Does NOT destroy tex on failure, the caller owns it. */
static LoaderKTXStatus ktx_ensure_rgba8(ktxTexture2 *tex) {
  if (ktxTexture2_NeedsTranscoding(tex)) {
    KTX_error_code r = ktxTexture2_TranscodeBasis(tex, KTX_TTF_RGBA32, 0);
    if (r != KTX_SUCCESS) {
      logger_add(LoggerFlag_Warning, "KTX2 transcode failed: %s",
                 ktxErrorString(r));
      return LoaderKTXStatus_FailTranscode;
    }
  }

  // after transcoding, vkFormat reflects the real format
  if (tex->vkFormat != KTX_VK_FORMAT_RGBA8_UNORM &&
      tex->vkFormat != KTX_VK_FORMAT_RGBA8_SRGB) {
    logger_add(LoggerFlag_Warning, "KTX2 unsupported raw format %u",
               tex->vkFormat);
    return LoaderKTXStatus_UnsupportedFormat;
  }
  return LoaderKTXStatus_Success;
}

/** open + ensure_rgba8, destroys the texture itself on failure. */
static LoaderKTXStatus ktx_open_rgba8(const char *path,
                                      const unsigned char *mem, size_t mem_size,
                                      ktxTexture2 **out) {
  ktxTexture2 *tex = NULL;
  LoaderKTXStatus s = ktx_open(path, mem, mem_size, &tex);
  if (s != LoaderKTXStatus_Success)
    return s;

  s = ktx_ensure_rgba8(tex);
  if (s != LoaderKTXStatus_Success) {
    ktxTexture_Destroy(ktxTexture(tex));
    return s;
  }
  *out = tex;
  return s;
}

/** Index of the first mip level that fits inside max_size. */
static uint32_t ktx_first_level_fitting(const ktxTexture2 *tex, int max_size) {
  uint32_t level = 0;
  while (level + 1 < tex->numLevels &&
         ((int)(tex->baseWidth >> level) > max_size ||
          (int)(tex->baseHeight >> level) > max_size))
    level++;
  return level;
}

/** Size of a given level (clamped to 1). */
static void ktx_level_extent(const ktxTexture2 *tex, uint32_t level,
                             uint32_t *w, uint32_t *h) {
  *w = glm_max(1, (int)(tex->baseWidth >> level));
  *h = glm_max(1, (int)(tex->baseHeight >> level));
}

/** Borrowed pointer + byte size of one level (layer 0, face 0). */
static const uint8_t *ktx_level_data(ktxTexture2 *tex, uint32_t level,
                                     size_t *size) {
  ktx_size_t offset = 0;
  ktxTexture_GetImageOffset(ktxTexture(tex), level, 0, 0, &offset);
  *size = ktxTexture_GetImageSize(ktxTexture(tex), level);
  return (const uint8_t *)ktxTexture_GetData(ktxTexture(tex)) + offset;
}

static WGPUTextureFormat ktx_wgpu_format(const ktxTexture2 *tex) {
  return tex->vkFormat == KTX_VK_FORMAT_RGBA8_SRGB
             ? WGPUTextureFormat_RGBA8UnormSrgb
             : WGPUTextureFormat_RGBA8Unorm;
}

/* ---------- public functions ---------- */

bool is_ktx2_memory(const unsigned char *d, size_t n) {
  static const unsigned char magic[12] = {0xAB, 'K',  'T',  'X',  ' ',  '2',
                                          '0',  0xBB, '\r', '\n', 0x1A, '\n'};
  return d && n >= sizeof(magic) && memcmp(d, magic, sizeof(magic)) == 0;
}

/** Release a ktxTexture2 and set the pointer to NULL. Safe on NULL. */
LoaderKTXStatus ktx_texture_destroy(ktxTexture2 **tex) {
  if (tex && *tex) {
    ktxTexture_Destroy(ktxTexture(*tex));
    *tex = NULL;
  }
  return LoaderKTXStatus_Success;
}

/**
 * @brief Load a KTX2 file (path, or mem + mem_size) and make sure it is RGBA8
 *        (Basis files are transcoded). Step 1 of 2, see ktx_texture_get_mips().
 *
 * @param[out] out       Owning texture, release with ktx_texture_destroy().
 *                       Set to NULL on failure.
 * @param[in]  path      Path to a .ktx2 file, or NULL to load from memory.
 * @param[in]  mem       Complete .ktx2 file in memory. Not retained.
 * @param[in]  mem_size  Size of @p mem in bytes.
 *
 * @return LoaderKTXStatus_Success, or FailLoad / FailTranscode /
 *         UnsupportedFormat.
 */
LoaderKTXStatus ktx_texture_load(ktxTexture2 **out, const char *path,
                                 const unsigned char *mem, size_t mem_size) {
  *out = NULL;
  return ktx_open_rgba8(path, mem, mem_size, out);
}

/**
 * @brief Fill @p out with zero-copy views of the mip levels that fit
 *        @p max_size. Step 2 of 2.
 *
 * The data points into @p tex, so it is valid until ktx_texture_destroy().
 * Never free it or upload it with REMWriteFlag_STBIFreeData.
 *
 * @param[in]  tex       Texture from ktx_texture_load().
 * @param[in]  max_size  Leading mips larger than this (width or height) are
 *                       skipped; mips[0] is the first level that fits.
 * @param[out] out       Zeroed, then filled. mip_count is capped at
 *                       TEXTURE_MAX_MIP_COUNT.
 *
 * @note Only layer 0 / face 0 is exposed.
 */
LoaderKTXStatus ktx_texture_get_mips(ktxTexture2 *tex, int max_size,
                                     TextureMip *out,
                                     const LoaderKTXMemory memory) {
  memset(out, 0, sizeof *out);
  if (!tex)
    return LoaderKTXStatus_FailLoad;

  const uint32_t first = ktx_first_level_fitting(tex, max_size);

  uint32_t count = tex->numLevels - first;
  if (count > TEXTURE_MAX_MIP_COUNT)
    count = TEXTURE_MAX_MIP_COUNT;

  for (uint32_t i = 0; i < count; i++) {

    const unsigned char *mip_data =
        ktx_level_data(tex, first + i, &out->mips[i].size);
    if (memory == LoaderKTXMemory_ShareData) {
      out->mips[i].data = mip_data;
    } else {
      const size_t mip_size = out->mips[i].size;
      out->mips[i].data = malloc(mip_size);
      if (out->mips[i].data) {
        memcpy(out->mips[i].data, mip_data, mip_size);
      } else {
        logger_add(LoggerFlag_Error,
                   "[KTX Loader] Unable to allocate data for mip %d.", i);
        return LoaderKTXStatus_FailAlloc;
      }
    }
  }

  out->mip_count = count;
  out->channels = TextureChannel_RGBA;
  ktx_level_extent(tex, first, &out->width, &out->height);
  out->format = ktx_wgpu_format(tex);

  return LoaderKTXStatus_Success;
}

/** Load KTX2, transcode to RGBA8, pick the smallest mip that fits max_size.
 *  Output is malloc'd (free with free()/stbi_image_free). */
unsigned char *load_ktx2(const char *path, const unsigned char *mem,
                         size_t mem_size, int *width, int *height,
                         int *channels, int max_size) {
  ktxTexture2 *tex = NULL;
  if (ktx_texture_load(&tex, path, mem, mem_size) != LoaderKTXStatus_Success)
    return NULL;

  TextureMip mip;
  unsigned char *out = NULL;

  if (ktx_texture_get_mips(tex, max_size, &mip, LoaderKTXMemory_ShareData) ==
      LoaderKTXStatus_Success) {
    out = malloc(mip.mips[0].size);
    if (out) {
      memcpy(out, mip.mips[0].data, mip.mips[0].size);
      *width = (int)mip.width;
      *height = (int)mip.height;
      *channels = 4;
    }
  }

  ktx_texture_destroy(&tex);
  return out;
}
/**
   Load a .ktx2 file as tightly packed RGBA8 (level 0 only).
   Returns a malloc'd buffer (free-able with stbi_image_free / free), or NULL.
 */
unsigned char *load_ktx2_rgba8(const char *path, int *width, int *height) {
  ktxTexture2 *kt = NULL;
  KTX_error_code err = ktxTexture2_CreateFromNamedFile(
      path, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kt);
  if (err != KTX_SUCCESS) {
    logger_add(LoggerFlag_Error, "KTX2 open failed: %s", ktxErrorString(err));
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
