#ifndef _TEXTURE_CREATE_H_
#define _TEXTURE_CREATE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "backend/job_system.h"
#include "core.h"
#include "utils/defines.h"
#include "webgpu/webgpu.h"

#define TEXTURE_CUBE_LAYER 6

typedef struct {
  const int width;
  const int height;
  const uint8_t channels;
  const uint8_t *value;
} TextureCreateDescriptor;

typedef struct {
  const TextureResolution width;
  const TextureResolution height;
  const uint8_t channels;
  const char *path;
  bool flip;
} TextureCreateFileDescriptor;

typedef struct {
  const TextureResolution width;
  const TextureResolution height;
  const uint8_t channels;
  const unsigned char *data;
  const size_t size;
  bool flip;
} TextureCreateMemoryDescriptor;

typedef struct {
  const char *right;
  const char *left;
  const char *top;
  const char *bottom;
  const char *front;
  const char *back;
} CubeMapPath;

typedef struct {
  const TextureResolution resolution;
  const WGPUTextureFormat format;
  const CubeMapPath *path;
} TextureCreateCubeMapDescriptor;

EXTERN_C_BEGIN

TextureStatus texture_create(Texture *, const TextureCreateDescriptor *);

TextureStatus texture_destroy(Texture *);
TextureStatus texture_mip_destroy(TextureMip *);
TextureStatus texture_create_from_file(Texture *,
                                       const TextureCreateFileDescriptor *);

TextureStatus texture_mip_create_from_file(TextureMip *,
                                           const TextureCreateFileDescriptor *);

TextureStatus
texture_mip_create_from_memory(TextureMip *,
                               const TextureCreateMemoryDescriptor *);

TextureStatus
texture_create_cubemap_from_file(Texture[TEXTURE_CUBE_LAYER],
                                 const TextureCreateCubeMapDescriptor *);

TextureStatus texture_create_mip(Texture *, WGPUTexture *, WGPUTextureView *);

EXTERN_C_END

#endif
