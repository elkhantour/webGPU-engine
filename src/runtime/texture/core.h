#ifndef _TEXTURE_CORE_H_
#define _TEXTURE_CORE_H_

#include <cglm/cglm.h>
#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#include "utils/vector/vector.h"

#define TEXTURE_HEIGHT_AUTO 0
#define TEXTURE_WIDTH_AUTO 0
#define TEXTURE_FORMAT_OFFSCREEN WGPUTextureFormat_RGBA8Unorm
#define TEXTURE_FORMAT_ONSCREEN WGPUTextureFormat_BGRA8Unorm
#define TEXTURE_FORMAT_DEPTH WGPUTextureFormat_Depth24Plus
#define TEXTURE_FORMAT_DEPTH_STENCIL WGPUTextureFormat_Depth24PlusStencil8
#define TEXTURE_MAX_MIP_COUNT 16 /* 16 levels covers up to 32768px */

typedef enum {
  TextureChannel_Undefined = 0,
  TextureChannel_R = 1,
  TextureChannel_RGB = 3,
  TextureChannel_RGBA = 4,
} TextureChannel;

typedef enum {
  TextureResolution_Undefined = 0,
  TextureResolution_16 = 16,
  TextureResolution_32 = 32,
  TextureResolution_64 = 64,
  TextureResolution_128 = 128,
  TextureResolution_256 = 256,
  TextureResolution_512 = 512,
  TextureResolution_1024 = 1024,
  TextureResolution_2048 = 2048,
  TextureResolution_4096 = 4096,
} TextureResolution;

typedef enum {
  TextureCubeMapSide_Right,  // +X
  TextureCubeMapSide_Left,   // -X
  TextureCubeMapSide_Top,    // +Y
  TextureCubeMapSide_Bottom, // -Y
  TextureCubeMapSide_Front,  // +z
  TextureCubeMapSide_Back,   // -Z
} TextureCubeMapSide;

typedef TextureResolution texture_size_t[2];

typedef enum {
  TextureStatus_Success,
  TextureStatus_AllocFail,
  TextureStatus_FileError,
  TextureStatus_Pending,
  TextureStatus_CellOutOfBound,
  TextureStatus_UndefError,
} TextureStatus;

typedef struct {
  vec2 offset;
  vec2 scale;
  vec4 color;
} TextureCoordinatesUniform;

typedef unsigned char *texture_data;

typedef struct {
  TextureResolution width;
  TextureResolution height;
  size_t size;
  texture_data data;
  TextureChannel channels;
  uint8_t value; // TODO: check what this means ???
} Texture;

typedef struct {
  uint8_t *data; // borrowed, points into tex's data
  size_t size;
} TextureMipView;

typedef struct {
  WGPUTextureFormat format;
  uint32_t width, height; // size of mips[0] (after skipping)
  TextureChannel channels;
  uint32_t mip_count;
  TextureMipView mips[TEXTURE_MAX_MIP_COUNT];
} TextureMip;

// cached textel sizes since they use expensive divide operation
extern float g_texture_resolution_texel_size[TextureResolution_4096 + 1];

EXTERN_C_BEGIN

void texture_save(Texture *, const char *);
void texture_free(Texture *);

static inline float texture_size_texel(const TextureResolution size) {

  if (g_texture_resolution_texel_size[size] == 0 &&
      size <= TextureResolution_4096)
    g_texture_resolution_texel_size[size] = 1.0f / size;

  return g_texture_resolution_texel_size[size];
}

EXTERN_C_END

#endif
