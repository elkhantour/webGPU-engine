#include "processing.h"

#include <float.h>
#include <math.h>

#include "backend/logger.h"
#include "runtime/texture/core.h"
#include "utils/math.h"
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>

/**
   Blur algorithm
   https://stackoverflow.com/questions/21418892/understanding-super-fast-blur-algorithm
   https://developer.apple.com/documentation/accelerate/blurring-an-image
 */
TextureStatus texture_blur(Texture *src, int kernel_size, float sigma) {

  // texture attributes
  int w = src->width;
  int h = src->height;
  int channels = src->channels;
  unsigned char *in = src->data;
  unsigned char *out = src->data;

  // blur attributes
  int k = kernel_size / 2;
  float kernel[kernel_size][kernel_size];
  float kernel_sum = 0.0f;

  // calculate gaussian kernel
  for (int i = -k; i <= k; i++) {
    for (int j = -k; j <= k; j++) {
      kernel[i + k][j + k] = exp(-(i * i + j * j) / (2.0f * sigma * sigma));
      kernel_sum += kernel[i + k][j + k];
    }
  }

  // normalize kernel
  for (int i = 0; i < kernel_size; i++) {
    for (int j = 0; j < kernel_size; j++) {
      kernel[i][j] /= kernel_sum;
    }
  }

  // apply blur to each channels
  for (int y = k; y < h - k; y++) {
    for (int x = k; x < w - k; x++) {
      for (int c = 0; c < channels; c++) {
        float blurred_pixel = 0.0f;
        for (int i = -k; i <= k; i++) {
          for (int j = -k; j <= k; j++) {
            int ny = y + i; // y neighbors
            int nx = x + j; // x neighbors
            if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
              int in_index = (ny * w + nx) * channels + c;
              blurred_pixel += (float)in[in_index] * kernel[i + k][j + k];
            }
          }
        }
        int out_index = (y * w + x) * channels + c;
        out[out_index] = (unsigned char)(blurred_pixel + 0.5f);
      }
    }
  }

  // handle boundaries
  for (int y = 0; y < k; y++) {
    for (int x = 0; x < w; x++) {
      for (int c = 0; c < channels; c++) {
        out[(y * w + x) * channels + c] = in[(y * w + x) * channels + c];
      }
    }
  }

  for (int y = h - k; y < h; y++) {
    for (int x = 0; x < w; x++) {
      for (int c = 0; c < channels; c++) {
        out[(y * w + x) * channels + c] = in[(y * w + x) * channels + c];
      }
    }
  }

  for (int y = k; y < h - k; y++) {
    for (int x = 0; x < k; x++) {
      for (int c = 0; c < channels; c++) {
        out[(y * w + x) * channels + c] = in[(y * w + x) * channels + c];
      }
    }
  }

  for (int y = k; y < h - k; y++) {
    for (int x = w - k; x < w; x++) {
      for (int c = 0; c < channels; c++) {
        out[(y * w + x) * channels + c] = in[(y * w + x) * channels + c];
      }
    }
  }

  return TextureStatus_Success;
}

TextureStatus texture_contrast(Texture *source, float contrast) {

  unsigned int w = source->width;
  unsigned int h = source->height;
  unsigned int channels = source->channels;
  texture_data dest = source->data;

  for (int i = 0; i < w * h; ++i) {
    texture_data pixel = dest + i * channels;

    for (int c = 0; c < MIN(channels, 3); c++) {
      float v = pixel[c] / 255.0f;
      // v = (v - 0.5f) * contrast + 0.5f;
      // v = fminf(fmaxf(v, 0.0f), 1.0f);
      v = 1.0f / (1.0f + expf(-contrast * (v - 0.5f)));
      pixel[c] = (unsigned char)(v * 255.0f);
    }
  }

  return TextureStatus_Success;
}
/**
 */
TextureStatus texture_remap(Texture *source, int min, int max) {

  unsigned int w = source->width;
  unsigned int h = source->height;
  unsigned int channels = source->channels;
  texture_data dest = source->data;

  float old_min = FLT_MAX;
  float old_max = -FLT_MAX;

  // get min / max value
  for (int i = 0; i < w * h; ++i) {
    texture_data pixel = source->data + i * channels;
    for (int c = 0; c < MIN(channels, 3); c++) {
      float v = pixel[c] / 255.0f;
      old_min = MIN(old_min, v);
      old_max = MAX(old_max, v);
    }
  }

  // solid color, no need for remap
  if (old_max == old_min)
    return TextureStatus_Success;

  float old_range = old_max - old_min;

  if (old_range == 0.0f)
    old_range = 1.0f;

  float new_range = max - min;

  // remap
  for (int i = 0; i < w * h; ++i) {
    texture_data source_pixel = source->data + i * channels;
    texture_data dst_pixel = dest + i * channels;

    for (int c = 0; c < MIN(channels, 3); ++c) {
      float v = source_pixel[c] / 255.0f;
      float old_v = v;
      v = ((v - old_min) / old_range) * new_range + min;
      v = fminf(fmaxf(v, 0.0f), 1.0f);
      dst_pixel[c] = (unsigned char)(v * 255.0f);
    }
  }

  return TextureStatus_Success;
}

TextureStatus texture_downscale(Texture *texture, const TextureResolution width,
                                const TextureResolution height) {

  int w = texture->width;
  int h = texture->height;
  texture_data dest = texture->data;

  if (w <= width && h <= height)
    return TextureStatus_Success;

  int n_w = width;
  int n_h = height;
  if (w > h)
    n_h = (int)((float)h * height / w);
  else
    n_w = (int)((float)w * width / h);
  n_w = glm_max(1, n_w);
  n_h = glm_max(1, n_h);

  unsigned char *n_data = malloc((size_t)n_w * n_h * texture->channels);
  if (!n_data) {
    logger_add(LoggerFlag_Warning,
               "GLTF Loader couldn't allocate resources for resize texture.");
    return TextureStatus_AllocFail;
  }

  if (!stbir_resize_uint8_srgb(texture->data, texture->width, texture->height,
                               0, n_data, n_w, n_h, 0,
                               (uint8_t)texture->channels)) {
    logger_add(LoggerFlag_Warning, "GLTF Loader STBI resize texture fail.");
    free(n_data);
    return TextureStatus_UndefError;
  }

  stbi_image_free(texture->data);
  texture->data = n_data;
  texture->width = n_w;
  texture->height = n_h;
  texture->size = texture->width * texture->height * texture->channels;

  return TextureStatus_Success;
}
