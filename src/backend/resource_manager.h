#ifndef _RESOURCE_MANAGER_H_
#define _RESOURCE_MANAGER_H_

#include "backend/job_system.h"
#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/renderer/core.h"
#include "backend/ubo.h"
#include "runtime/camera/core.h"
#include "runtime/gui/core.h"
#include "runtime/light/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/compute.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/grid.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/probe/reflection/probe.h"
#include "runtime/scene/core.h"
#include "runtime/shader/core.h"
#include "runtime/texture/core.h"
#include "utils/defines.h"
#include "utils/pool.h"
#include "webgpu/webgpu.h"
#include <stddef.h>

typedef enum {
  REMStatus_Success,
  REMStatus_MaxCapacity,
  REMStatus_AllocFail,
  REMStatus_NullResource,
  REMStatus_UnfoundResource,
  REMStatus_ProtectedResource,
  REMStatus_UndefError,
} REMStatus;

// clang-format off

// WGPU objects are opaque pointers: they are NOT pooled and NOT wrapped. They
// only get a live counter (leak checking). The capacity column is unused.
//
//      Type         |             Registry type           |         Label          |        Pool Capacity
#define REM_WGPU_LIST(_)                                                                                                                                  \
    _(WGPUTexture,             RegEntryType_WGPUObject,             texture,                     128        ) \
    _(WGPUTextureView,         RegEntryType_WGPUObject,             view,                        624        ) \
    _(WGPUBuffer,              RegEntryType_WGPUObject,             buffer,                      624        ) \
    _(WGPUShaderModule,        RegEntryType_WGPUObject,             shader_module,               128        ) \
    _(WGPUSampler,             RegEntryType_WGPUObject,             sampler,                     128        )

#define REM_WGPU_ITEM_COUNT 5

// Engine objects: one chunked Pool each. The capacity column is the INITIAL
// capacity (preallocated at init); pools keep growing past it if needed.
#define REM_ENGINE_LIST(_)                                                                                                                                \
    _(Mesh,                   RegEntryType_Mesh,                    mesh,                       1024        ) \
    _(Scene,                  RegEntryType_Scene,                   scene,                         1        ) \
    _(RenderPipeline,         RegEntryType_RenderPipeline,          render_pipeline,              64        ) \
    _(ComputePipeline,        RegEntryType_ComputePipeline,         compute_pipeline,            128        ) \
    _(Shader,                 RegEntryType_Shader,                  shader,                     1024        ) \
    _(Camera,                 RegEntryType_Camera,                  camera,                       32        ) \
    _(Renderer,               RegEntryType_Renderer,                renderer,                      1        ) \
    _(Gui,                    RegEntryType_Gui,                     gui,                           2        ) \
    _(UBOManager,             RegEntryType_Ubo,                     ubo,                           1        ) \
    _(PointLight,             RegEntryType_PointLight,              point_light,                   6        ) \
    _(AmbientLight,           RegEntryType_AmbientLight,            ambient_light,                32        ) \
    _(SpotLight,              RegEntryType_SpotLight,               spot_light,                   32        ) \
    _(SunLight,               RegEntryType_SunLight,                sun_light,                    32        ) \
    _(ProbeReflectionPlane,   RegEntryType_ProbeReflectionPlane,    plane_reflection,             16        ) \
    _(ProbeReflection,        RegEntryType_ProbeReflection,         probe_reflection,             16        ) \
    _(ProbeReflectionGrid,    RegEntryType_ProbeReflectionGrid,     probe_reflection_grid,        16        ) \
    _(JobSystem,              RegEntryType_JobSystem,               job_system,                    1        )

// clang-format on

// Upper bound on slots per chunk. Pools with a smaller initial capacity use a
// chunk sized to that capacity, so singletons don't allocate 256 slots.
#define REM_MAX_CHUNK_SLOTS 256

// === Generates Type Enums ===
#define _(Type, RegistryType, Label, PoolCapacity) REMType_##Type,
typedef enum { REM_WGPU_LIST(_) REM_ENGINE_LIST(_) REMType_Count } REMType;
#undef _

#define REM_TYPE_COUNT REMType_Count

// WGPU types come first in the enum, engine types follow.
_Static_assert(REMType_Mesh == REM_WGPU_ITEM_COUNT,
               "REM_WGPU_ITEM_COUNT doesn't match REM_WGPU_LIST");

typedef struct ResourceManager {

  // Indexed by REMType. Only engine types (>= REM_WGPU_ITEM_COUNT) are used.
  Pool pools[REM_TYPE_COUNT];

  // Live counters for raw WGPU objects, indexed by REMType.
  uint32_t wgpu_live[REM_WGPU_ITEM_COUNT];

} ResourceManager;

extern ResourceManager g_rem;

// === Define Config ===
static const struct {
  const char *label;
  const size_t capacity;
  const size_t type_size;
} rem_config[] = {
#define _(Type, RegistryType, Label, PoolCapacity)                             \
  [REMType_##Type] = {                                                         \
      .label = #Type,                                                          \
      .capacity = PoolCapacity,                                                \
      .type_size = sizeof(Type),                                               \
  },
    REM_WGPU_LIST(_) REM_ENGINE_LIST(_)
#undef _
};

typedef enum {
  REMWriteFlag_None = 0,
  REMWriteFlag_FreeData = 1 << 0,
  REMWriteFlag_STBIFreeData = 1 << 1,
} REMWriteFlag;

EXTERN_C_BEGIN

REMStatus resource_manager_init();

// Destroys every engine object still alive (reverse type order), frees all
// pools and reports leaked WGPU objects.
void resource_manager_shutdown(void);

// Number of live objects of a given type (engine or WGPU).
uint32_t rem_live_count(const REMType);

// === WGPU (raw opaque pointers, not pooled) ===
WGPUTexture rem_new_texture(const WGPUTextureDescriptor *);

WGPUTextureView rem_new_view(const WGPUTexture,
                             const WGPUTextureViewDescriptor *);

WGPUBuffer rem_new_buffer(const WGPUBufferDescriptor *);

WGPUSampler rem_new_sampler(const WGPUSamplerDescriptor *);

WGPUShaderModule rem_new_shader_module(char *code, const char *label,
                                       const REMWriteFlag);

REMStatus rem_write_buffer(WGPUBuffer, const size_t, void *, const size_t,
                           const REMWriteFlag);
REMStatus rem_write_texture(WGPUTexture, void *, const size_t,
                            const TextureChannel, const uint16_t, const uint8_t,
                            const REMWriteFlag);

// Release the object and set *handle to NULL.
REMStatus rem_destroy_texture(WGPUTexture *);
REMStatus rem_destroy_sampler(WGPUSampler *);
REMStatus rem_destroy_view(WGPUTextureView *);
REMStatus rem_destroy_buffer(WGPUBuffer *);
REMStatus rem_destroy_shader_module(WGPUShaderModule *);

// === Engine objects (pooled, pointer-stable) ===
// rem_new_<label>() returns a zeroed object, or NULL on out-of-memory.
// rem_destroy_<label>() calls <label>_destroy() (contents only), then
// returns the slot to the pool. The pointer is invalid afterwards.
#define _(Type, RegistryType, Label, PoolCapacity)                             \
  Type *rem_new_##Label();                                                     \
  REMStatus rem_destroy_##Label(Type *);

REM_ENGINE_LIST(_)
#undef _

EXTERN_C_END

#endif
