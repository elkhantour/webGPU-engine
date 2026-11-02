#include "list.h"

#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/shadow_map/core.h"
#include "backend/ubo.h"
#include "core.h"
#include "utils/stli.h"
#include <stddef.h>

StaticListStatus light_list_create(LightList *list, size_t capacity) {

  const struct {
    size_t *capacity;
    size_t *count;
    const char *label;
  } LIGHT_STATIC_LIST[] = {
      {
          .capacity = &list->point.capacity,
          .count = &list->point.count,
          .label = "Point Light List Base",
      },
      {
          .capacity = &list->spot.capacity,
          .count = &list->spot.count,
          .label = "Spot Light List Base",
      },
      {
          .capacity = &list->sun.capacity,
          .count = &list->sun.count,
          .label = "Sun Light List Base",
      },
      {
          .capacity = &list->ambient.capacity,
          .count = &list->ambient.count,
          .label = "Ambient Light List Base",
      },
  };

  static const size_t LIGHT_STATIC_COUNT =
      sizeof(LIGHT_STATIC_LIST) / sizeof(LIGHT_STATIC_LIST[0]);

  for (size_t i = 0; i < LIGHT_STATIC_COUNT; i++)
    stli_create(LIGHT_STATIC_LIST[i].capacity, LIGHT_STATIC_LIST[i].count,
                capacity, LIGHT_STATIC_LIST[i].label);

  return StaticListStatus_Success;
}

#define LIGHT_LIST_NEW(Type, FuncName, List, Label)                            \
  Type *light_list_new_##FuncName(List *list) {                                \
                                                                               \
    Type *light = rem_new_##FuncName();                                        \
                                                                               \
    if (light == NULL)                                                         \
      return NULL;                                                             \
                                                                               \
    if (stli_insert((void *)list->entries, list->capacity, &list->count,       \
                    sizeof(Type *), (void *)&light,                            \
                    Label) != StaticListStatus_Success)                        \
      return NULL;                                                             \
                                                                               \
    return light;                                                              \
  }

#define LIGHT_LIST_INSERT(List, FuncName, Type, Label)                         \
  StaticListStatus light_list_##FuncName##_insert(List *list, Type *light) {   \
                                                                               \
    return stli_insert((void *)list->entries, list->capacity, &list->count,    \
                       sizeof(Type *), (void *)&light, Label);                 \
  }

#define LIGHT_LIST_REMOVE(List, FuncName, Type, Label)                         \
  StaticListStatus light_list_##FuncName##_remove(List *list, Type *light) {   \
    return stli_remove((void *)list->entries, &list->count, sizeof(Type *),    \
                       (void *)light, Label);                                  \
  }

// clang-format off
LIGHT_LIST_NEW(PointLight,   point_light,   PointLightList,   "Point Light"   );
LIGHT_LIST_NEW(AmbientLight, ambient_light, AmbientLightList, "Ambient Light" );
LIGHT_LIST_NEW(SpotLight,    spot_light,    SpotLightList,    "Spot Light"    );
LIGHT_LIST_NEW(SunLight,     sun_light,     SunLightList,     "Sun Light"     );

LIGHT_LIST_INSERT(PointLightList, point, PointLight, "Point Light Shadow List" );
LIGHT_LIST_REMOVE(PointLightList, point, PointLight, "Point Light Shadow List" );
LIGHT_LIST_INSERT(SpotLightList,  spot,  SpotLight,  "Spot Light Shadow List"  );
LIGHT_LIST_REMOVE(SpotLightList,  spot,  SpotLight,  "Spot Light Shadow List"  );
LIGHT_LIST_INSERT(SunLightList,   sun,   SunLight,   "Sun Light Shadow List"   );
LIGHT_LIST_REMOVE(SunLightList,   sun,   SunLight,   "Sun Light Shadow List"   );
// clang-format on

LightListSlot light_list_uniform_new_entry(LightListUniform *list,
                                           const LightType type) {

  switch (type) {

  case LightType_Ambient:

    if (list->ambient_count < LIGHT_LIST_ENTRIES_CAPACITY)
      return (LightListSlot){
          .uniform = {.ambient = &list->ambient_light[list->ambient_count]},
          .offset =
              light_list_uniform_offset(list->ambient_count, LightType_Ambient),
          .id = list->ambient_count++,
      };
    break;

  case LightType_Point:
    if (list->point_count < LIGHT_LIST_ENTRIES_CAPACITY)
      return (LightListSlot){
          .uniform = {.point = &list->point_light[list->point_count]},
          .offset =
              light_list_uniform_offset(list->point_count, LightType_Point),
          .id = list->point_count++,
      };
    break;

  case LightType_Spot:
    if (list->spot_count < LIGHT_LIST_ENTRIES_CAPACITY)
      return (LightListSlot){
          .uniform = {.spot = &list->spot_light[list->spot_count]},
          .offset = light_list_uniform_offset(list->spot_count, LightType_Spot),
          .id = list->spot_count++,
      };
    break;

  case LightType_Sun:
    if (list->sun_count < LIGHT_LIST_ENTRIES_CAPACITY)
      return (LightListSlot){
          .uniform = {.sun = &list->sun_light[list->sun_count]},
          .offset = light_list_uniform_offset(list->sun_count, LightType_Sun),
          .id = list->sun_count++,
      };
    break;
  }

  logger_add(LoggerFlag_Error, "Couldn't create new "
                               "light uniform slot. "
                               "Light "
                               "list reached max "
                               "capacity.");

  return (LightListSlot){.uniform = 0, .id = UBO_INDEX_UNFOUND, .offset = 0};
}
