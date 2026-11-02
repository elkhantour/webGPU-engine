#ifndef _PROBE_REFLECTION_CORE_H_
#define _PROBE_REFLECTION_CORE_H_

#include <stddef.h>
#include <stdint.h>

#include "backend/render_pass/core.h"
#include "runtime/scene/debug/core.h"
#include "utils/dyli.h"
#include "webgpu/webgpu.h"

#define PROBE_REFLECTION_UBO_SLOT_COUNT 2
#define PROBE_REFLECTION_NEAR 0.1f
#define PROBE_REFLECTION_FAR 100.0f

typedef enum {
  ProbeReflectionUBOField_List,
  ProbeReflectionUBOField_Camera,
} ProbeReflectionUBOField;

typedef struct {
  SceneDebug *scene_debug;
  const uint8_t max_views;
} ProbeReflectionListDebug;

typedef struct {
  ubo_id_t camera_offset;
} ProbeReflectionListPreprocessorData;

typedef struct {
  void **entries;
  size_t *capacity;
  size_t *count;
  const size_t num;
  const size_t type_size;
  RenderPass *pass;
  const char *label;
} ProbeReflectionCreateList;

typedef struct {
  float roughness;
  float frost_scale;
  float frost_strength;
  float _pad;
  color color;
  vec4 _pad1;
} __attribute__((aligned(16))) GlassUniform;


#endif
