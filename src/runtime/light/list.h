#ifndef _LIGHT_LIST_H_
#define _LIGHT_LIST_H_
#include <stddef.h>
#include <stdint.h>

#include "backend/render_pass/render_pass.h"
#include "backend/ubo.h"
#include "core.h"
#include "runtime/pipeline/render.h"
#include "utils/defines.h"
#include "utils/mem.h"
#include "utils/stli.h"
#include "webgpu/webgpu.h"

typedef enum {
  LightListStatus_Success,
  LightListStatus_AlloFail,
  LightListStatus_Error,
} LightListStatus;

typedef enum {
  LightCreateFlag_None = 0,
  LightCreateFlag_Shadow = 1 << 0,
} LightCreateFlag;

// light list

typedef STATIC_LIST(PointLight, LIGHT_MAX_CAPACITY) PointLightList;
typedef STATIC_LIST(SpotLight, LIGHT_MAX_CAPACITY) SpotLightList;
typedef STATIC_LIST(AmbientLight, LIGHT_MAX_CAPACITY) AmbientLightList;
typedef STATIC_LIST(SunLight, LIGHT_MAX_CAPACITY) SunLightList;

typedef DYNAMIC_LIST(PointLight *) PointLightRefList;
typedef DYNAMIC_LIST(SpotLight *) SpotLightRefList;
typedef DYNAMIC_LIST(AmbientLight *) AmbientLightRefList;
typedef DYNAMIC_LIST(SunLight *) SunLightRefList;

typedef struct {

  PointLightList point;
  SunLightList sun;
  SpotLightList spot;
  AmbientLightList ambient;

  UBOSlot ubo_slot;
} LightList;

#define LIGHT_LIST_ENTRIES_CAPACITY 16

typedef struct {
  AmbientLightUniform ambient_light[LIGHT_LIST_ENTRIES_CAPACITY];
  PointLightUniform point_light[LIGHT_LIST_ENTRIES_CAPACITY];
  SpotLightUniform spot_light[LIGHT_LIST_ENTRIES_CAPACITY];
  SunLightUniform sun_light[LIGHT_LIST_ENTRIES_CAPACITY];

  uint32_t ambient_count;
  uint32_t point_count;
  uint32_t spot_count;
  uint32_t sun_count;

  float _pad[STRUCT_PAD(
      16, sizeof(AmbientLightUniform) * LIGHT_LIST_ENTRIES_CAPACITY +
              sizeof(PointLightUniform) * LIGHT_LIST_ENTRIES_CAPACITY +
              sizeof(SpotLightUniform) * LIGHT_LIST_ENTRIES_CAPACITY +
              sizeof(SunLightUniform) * LIGHT_LIST_ENTRIES_CAPACITY +
              sizeof(uint32_t) * 4)];
} __attribute__((aligned(16))) LightListUniform;

EXTERN_C_BEGIN

StaticListStatus light_list_create(LightList *, size_t);

PointLight *light_list_new_point_light(PointLightList *);
AmbientLight *light_list_new_ambient_light(AmbientLightList *);
SpotLight *light_list_new_spot_light(SpotLightList *);
SunLight *light_list_new_sun_light(SunLightList *);

StaticListStatus light_list_point_insert(PointLightList *, PointLight *);
StaticListStatus light_list_point_remove(PointLightList *, PointLight *);

StaticListStatus light_list_sun_insert(SunLightList *, SunLight *);
StaticListStatus light_list_sun_remove(SunLightList *, SunLight *);

StaticListStatus light_list_spot_insert(SpotLightList *, SpotLight *);
StaticListStatus light_list_spot_remove(SpotLightList *, SpotLight *);

/**
   Return the offset (in bytes) of the light slot within the light list uniform.
   This allows to update the buffer only where the light got updated instead of
   the whole Light List UBO each change.
 */
static inline size_t light_list_uniform_offset(const ubo_id_t id,
                                               const LightType type) {

  static const size_t light_uniform_size[] = {
      sizeof(AmbientLightUniform),
      sizeof(PointLightUniform),
      sizeof(SpotLightUniform),
      sizeof(SunLightUniform),
  };

  size_t offset = 0;
  for (uint8_t i = 0; i < type; i++)
    offset += light_uniform_size[i] * LIGHT_LIST_ENTRIES_CAPACITY;

  return offset + id * sizeof(light_uniform_size[type]);
}

LightListSlot light_list_uniform_new_entry(LightListUniform *, const LightType);

void light_list_uniform_update(LightList *);

EXTERN_C_END
#endif
