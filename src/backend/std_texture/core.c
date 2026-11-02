#include "core.h"

#include <stddef.h>
#include <stdint.h>

#include "backend/context.h"
#include "backend/logger.h"
#include "backend/shadow_map/core.h"
#include "backend/resource_manager.h"
#include "runtime/texture/core.h"
#include "webgpu/webgpu.h"

WGPUTextureView g_std_texture_view[STD_TEXTURE_VIEW_COUNT] = {0};
WGPUTexture g_std_texture[STD_TEXTURE_VIEW_COUNT] = {0};

// black color
static const uint8_t pixel[4] = {255, 255, 255, 255};

static inline WGPUTextureView renderer_create_fallback_float(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_float_black(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_float_normal(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_float_2d_array(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_float_cube(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_float_cube_array(WGPUTexture *);

static inline WGPUTextureView renderer_create_fallback_depth(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_depth_cube_array(WGPUTexture *);

static inline WGPUTextureView
renderer_create_fallback_depth_2d_array(WGPUTexture *);

/**
   ▗▄▄▄▖ ▗▄▖ ▗▖   ▗▖   ▗▄▄▖  ▗▄▖  ▗▄▄▖▗▖ ▗▖
   ▐▌   ▐▌ ▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌▗▞▘
   ▐▛▀▀▘▐▛▀▜▌▐▌   ▐▌   ▐▛▀▚▖▐▛▀▜▌▐▌   ▐▛▚▖
   ▐▌   ▐▌ ▐▌▐▙▄▄▖▐▙▄▄▖▐▙▄▞▘▐▌ ▐▌▝▚▄▄▖▐▌ ▐▌

   Create and store the basic fallback texture set as placeholder in more
   complex shader like PBR before being replace by computational intensive
   process like shadow map or AO baking.
 */

static const std_texture_view_create texture_creator[STD_TEXTURE_VIEW_COUNT] = {
    // float
    [TextureViewType_Float] = renderer_create_fallback_float,
    [TextureViewType_FloatBlack] = renderer_create_fallback_float_black,
    [TextureViewType_FloatNormal] = renderer_create_fallback_float_normal,
    [TextureViewType_Float2DArray] = renderer_create_fallback_float_2d_array,
    [TextureViewType_FloatCube] = renderer_create_fallback_float_cube,
    [TextureViewType_FloatCubeArray] =
        renderer_create_fallback_float_cube_array,
    // depth
    [TextureViewType_Depth] = renderer_create_fallback_depth,
    [TextureViewType_DepthCubeArray] =
        renderer_create_fallback_depth_cube_array,
    [TextureViewType_Depth2DArray] = renderer_create_fallback_depth_2d_array,
};

void standard_textures_init() {

  logger_add(LoggerFlag_Process, "Caching fallback textures...");

  for (TextureViewType i = 0; i < STD_TEXTURE_VIEW_COUNT; i++)
    g_std_texture_view[i] = texture_creator[i](&g_std_texture[i]);
}

WGPUTextureView renderer_create_fallback_float(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float 2D",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1,
          },
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  rem_write_texture(*texture, (void *)pixel, 4 * sizeof(uint32_t),
                    TextureChannel_RGBA, 0, 0, REMWriteFlag_None);

  return rem_new_view(*texture, NULL);
}

WGPUTextureView renderer_create_fallback_float_normal(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float 2D Normal",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1,
          },
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  rem_write_texture(*texture, (uint8_t[4]){127, 127, 255, 255},
                    4 * sizeof(uint32_t), TextureChannel_RGBA, 0, 0,
                    REMWriteFlag_None);

  return rem_new_view(*texture, NULL);
}

WGPUTextureView renderer_create_fallback_float_black(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float Black",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1,
          },
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_RenderAttachment |
               WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  return rem_new_view(*texture, NULL);
}

WGPUTextureView renderer_create_fallback_float_2d_array(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float Array",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1,
          },
      .format = WGPUTextureFormat_R8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  rem_write_texture(*texture, (uint8_t[]){255}, sizeof(uint32_t),
                    TextureChannel_R, 0, 0, REMWriteFlag_None);

  return rem_new_view(*texture,
                      &(WGPUTextureViewDescriptor){
                          .label = "Standard View Float Array",
                          .dimension = WGPUTextureViewDimension_2DArray,
                          .format = WGPUTextureFormat_R8Unorm,
                          .mipLevelCount = 1,
                          .arrayLayerCount = 1,
                      });
}

WGPUTextureView renderer_create_fallback_float_cube(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float Cube",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 6,
          },
      .format = WGPUTextureFormat_R8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  rem_write_texture(*texture, (uint8_t[]){255}, sizeof(uint32_t),
                    TextureChannel_R, 0, 0, REMWriteFlag_None);

  return rem_new_view(*texture, &(WGPUTextureViewDescriptor){
                                    .label = "Standard View Float Cube",
                                    .dimension = WGPUTextureViewDimension_Cube,
                                    .format = WGPUTextureFormat_R8Unorm,
                                    .mipLevelCount = 1,
                                    .arrayLayerCount = 6,
                                });
}

WGPUTextureView
renderer_create_fallback_float_cube_array(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Float Cube Array",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 6,
          },
      .format = WGPUTextureFormat_R8Unorm,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
  });

  rem_write_texture(*texture, (uint8_t[]){255}, sizeof(uint32_t),
                    TextureChannel_R, 0, 0, REMWriteFlag_None);

  return rem_new_view(*texture,
                      &(WGPUTextureViewDescriptor){
                          .label = "Standard View Float Cube Array",
                          .dimension = WGPUTextureViewDimension_CubeArray,
                          .format = WGPUTextureFormat_R8Unorm,
                          .mipLevelCount = 1,
                          .arrayLayerCount = 6,
                      });
}

WGPUTextureView renderer_create_fallback_depth(WGPUTexture *texture) {

  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Depth 2D",
      .size =
          {
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1,
          },
      .format = TEXTURE_FORMAT_DEPTH,
      .mipLevelCount = 1,
      .sampleCount = 1,
      .dimension = WGPUTextureDimension_2D,
      .usage =
          WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
  });

  return rem_new_view(*texture, NULL);
}

WGPUTextureView
renderer_create_fallback_depth_cube_array(WGPUTexture *texture) {

  // create texture
  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Depth Cube Array",
      .size =
          (WGPUExtent3D){
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 6,
          },
      .format = SHADOW_DEPTH_FORMAT,
      .usage =
          WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
      .dimension = WGPUTextureDimension_2D,
      .mipLevelCount = 1,
      .sampleCount = 1,
  });

  return rem_new_view(*texture,
                      &(WGPUTextureViewDescriptor){
                          .label = "Standard View Depth Cube Array",
                          .dimension = WGPUTextureViewDimension_CubeArray,
                          .format = SHADOW_DEPTH_FORMAT,
                          .baseMipLevel = 0,
                          .mipLevelCount = 1,
                          .baseArrayLayer = 0,
                          .arrayLayerCount = 6,
                          .aspect = WGPUTextureAspect_DepthOnly,
                      });
}

WGPUTextureView renderer_create_fallback_depth_2d_array(WGPUTexture *texture) {

  // create texture
  *texture = rem_new_texture(&(WGPUTextureDescriptor){
      .label = "Standard Texture Cube Depth 2D Array",
      .size =
          (WGPUExtent3D){
              .width = 1,
              .height = 1,
              .depthOrArrayLayers = 1, // may need to update
          },
      .format = SHADOW_DEPTH_FORMAT,
      .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
      .dimension = WGPUTextureDimension_2D,
      .mipLevelCount = 1,
      .sampleCount = 1,
  });

  return rem_new_view(*texture,
                      &(WGPUTextureViewDescriptor){
                          .label = "Standard View Cube Depth 2D Array",
                          .dimension = WGPUTextureViewDimension_2DArray,
                          .format = SHADOW_DEPTH_FORMAT,
                          .baseMipLevel = 0,
                          .mipLevelCount = 1,
                          .baseArrayLayer = 0,
                          .arrayLayerCount = 1, // or however many layers
                          .aspect = WGPUTextureAspect_DepthOnly,
                      });
}

const WGPUTextureView std_texture_view(const TextureViewType type) {
  return g_std_texture_view[type];
}

/**
   Traverse the std texture view list and compare the argument and returns true
   if the view corressponds to a standrard texture view.

   This function is mostly used during the shader texture updates as to prevent
   to release a global std texture (making it unavailable for the resources
   using it).
 */
bool is_std_texture_view(const WGPUTextureView view) {

  for (size_t i = 0; i < STD_TEXTURE_VIEW_COUNT; i++)
    if (view == std_texture_view(i))
      return true;

  return false;
}
