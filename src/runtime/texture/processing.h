#ifndef _TEXTURE_PROCESSING_H_
#define _TEXTURE_PROCESSING_H_

#include "core.h"

EXTERN_C_BEGIN

TextureStatus texture_blur(Texture *, int, float);
TextureStatus texture_contrast(Texture *, float);
TextureStatus texture_remap(Texture *, int, int);
TextureStatus texture_downscale(Texture *, const TextureResolution,  const TextureResolution);

EXTERN_C_END

#endif
