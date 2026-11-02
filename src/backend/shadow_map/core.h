#ifndef _SHADOW_MAP_CORE_H_
#define _SHADOW_MAP_CORE_H_

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>

#include "backend/logger.h"
#include "backend/render_pass/core.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/scene/debug/core.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include "utils/stli.h"

#define SHADOW_DEPTH_FORMAT WGPUTextureFormat_Depth32Float
#define SHADOW_COLOR_FORMAT TEXTURE_FORMAT_ONSCREEN
#define SHADOW_MAP_SIZE 512
#define SHADOW_MAP_LAYER_CAPACITY 8
static const size_t SHADOW_MAP_LIGHT_UNFOUND = SIZE_MAX;

typedef struct {
  SceneDebug *scene_debug;
  uint16_t max_views;
} ShadowMapDebug;

typedef enum {
  ShadowMapStatus_Success,
  ShadowMapStatus_Skip,
  ShadowMapStatus_UndefError,
} ShadowMapStatus;

typedef struct {
  LightList *lights;
  Profiler *profiler;
  ShadowMapDebug debug;
} ShadowMapInitDescriptor;

typedef struct {
  WGPUTexture color_texture;
  WGPUTexture depth_texture;
  uint32_t layer;
} ShadowPassFallbackToTextureDescriptor;

typedef struct {
  const int width;
  const int height;
  const size_t layer_count;
  const WGPUTextureViewDimension dimension;
  WGPUTexture *color_texture, *depth_texture;
  WGPUTextureView *color_view, *depth_view;
} ShadowPassTextureDescriptor;

typedef struct {

  RenderPass render_pass;

  struct {
    WGPUTexture color, depth;
  } texture;

  struct {
    WGPUTextureView color, depth; // global/all layers

    struct { // cached individual layers view (1 layer <=> 1 light)
      WGPUTextureView *color, *depth;
      size_t count;
      size_t capacity;
    } layers;

  } view;

  union {
    PointLightRefList point;
    struct {
      SpotLightRefList spot;
      SunLightRefList sun;
    } directional;
  } caster;

} ShadowMapLight;

typedef enum {
  ShadowMapLightType_Point,       // Cubemap
  ShadowMapLightType_Directional, // 2D [...Spots, ...Sun]
  ShadowMapLightType_COUNT,
} ShadowMapLightType;

typedef struct {
  ShadowMapLight lights[ShadowMapLightType_COUNT];
  Profiler *profiler;
  ShadowMapDebug debug;
} ShadowMap;

EXTERN_C_BEGIN

ShadowMapStatus shadow_map_init(ShadowMap *, const ShadowMapInitDescriptor *);

ShadowMapStatus shadow_map_destroy(ShadowMap *);

ShadowMapStatus shadow_map_texture_create(const ShadowPassTextureDescriptor *);

ShadowMapStatus
shadow_map_update_light_resolution(ShadowMapLight *, const TextureResolution,
                                   const WGPUTextureViewDimension);

ShadowMapStatus shadow_map_toggle_mesh(ShadowMap *, Mesh *, bool);

static inline DynamicListStatus
shadow_map_register_point_light(ShadowMap *sm, PointLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Point];
  PointLightRefList *list = &group->caster.point;

  DynamicListStatus insert = dyli_insert(
      (void **)&list->entries, &list->capacity, &list->count,
      sizeof(PointLight *), (void *)&light, 1, "Shadow Map Point Light");

  return insert;
}

static inline DynamicListStatus
shadow_map_register_spot_light(ShadowMap *sm, SpotLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SpotLightRefList *list = &group->caster.directional.spot;

  DynamicListStatus insert = dyli_insert(
      (void **)&list->entries, &list->capacity, &list->count,
      sizeof(SpotLight *), (void *)&light, 1, "Shadow Map Spot Light");

  return insert;
}

static inline DynamicListStatus shadow_map_register_sun_light(ShadowMap *sm,
                                                              SunLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SunLightRefList *list = &group->caster.directional.sun;

  DynamicListStatus insert = dyli_insert(
      (void **)&list->entries, &list->capacity, &list->count,
      sizeof(SunLight *), (void *)&light, 1, "Shadow Map Sun Light");

  return insert;
}

static inline DynamicListStatus
shadow_map_unregister_point_light(ShadowMap *sm, PointLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Point];
  PointLightRefList *list = &group->caster.point;

  return dyli_remove((void **)&list->entries, &list->count,
                     sizeof(PointLight *), (void *)&light,
                     "Shadow Map Point Light");
}

static inline DynamicListStatus
shadow_map_unregister_spot_light(ShadowMap *sm, SpotLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SpotLightRefList *list = &group->caster.directional.spot;

  return dyli_remove((void **)&list->entries, &list->count, sizeof(SpotLight *),
                     (void *)&light, "Shadow Map Spot Light");
}

static inline DynamicListStatus
shadow_map_unregister_sun_light(ShadowMap *sm, SunLight *light) {

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SunLightRefList *list = &group->caster.directional.sun;

  return dyli_remove((void **)&list->entries, &list->count, sizeof(SunLight *),
                     (void *)&light, "Shadow Map Sun Light");
}

static inline size_t shadow_map_sun_layer(ShadowMap *sm, size_t index) {
  return sm->lights[ShadowMapLightType_Directional]
             .caster.directional.spot.count +
         index;
}

static inline size_t shadow_map_point_light_index(ShadowMap *sm,
                                                  PointLight *light) {

  size_t index = SHADOW_MAP_LIGHT_UNFOUND;

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Point];
  PointLightRefList *list = &group->caster.point;

  dyli_find_index((const void *)list->entries, list->count,
                  sizeof(PointLight *), (void *)&light, &index);

  if (index == SHADOW_MAP_LIGHT_UNFOUND)
    logger_add(LoggerFlag_Error, "Unable for find point light (%p)", light);

  return index;
}

static inline size_t shadow_map_sun_light_index(ShadowMap *sm,
                                                SunLight *light) {

  size_t index = SHADOW_MAP_LIGHT_UNFOUND;

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SunLightRefList *list = &group->caster.directional.sun;

  dyli_find_index((const void *)list->entries, list->count, sizeof(SunLight *),
                  (void *)&light, &index);

  if (index == SHADOW_MAP_LIGHT_UNFOUND)
    logger_add(LoggerFlag_Error, "Unable for find sun light (%p)", light);

  return index;
}

static inline size_t shadow_map_spot_light_index(ShadowMap *sm,
                                                 SpotLight *light) {

  size_t index = SHADOW_MAP_LIGHT_UNFOUND;

  ShadowMapLight *group = &sm->lights[ShadowMapLightType_Directional];
  SpotLightRefList *list = &group->caster.directional.spot;

  dyli_find_index((const void *)list->entries, list->count, sizeof(SpotLight *),
                  (void *)&light, &index);

  if (index == SHADOW_MAP_LIGHT_UNFOUND)
    logger_add(LoggerFlag_Error, "Unable for find spot light (%p)", light);

  return index;
}

static inline WGPUTextureView
shadow_map_depth_view(ShadowMap *sm, const ShadowMapLightType type) {
  return sm->lights[type].view.depth;
}

static inline WGPUTextureView
shadow_map_color_view(ShadowMap *sm, const ShadowMapLightType type) {
  return sm->lights[type].view.color;
}

EXTERN_C_END

#endif
