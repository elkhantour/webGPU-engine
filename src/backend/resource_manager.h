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
#include "utils/frli.h"
#include "utils/hsht.h"
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

// WGPU

// clang-format off

//      Type         |             Registry type           |         Label          |        Pool Capacity
#define REM_WGPU_LIST(_)                                                                                                                                  \
    _(WGPUTexture,             RegEntryType_WGPUObject,             texture,                     128        ) \
    _(WGPUTextureView,         RegEntryType_WGPUObject,             view,                        624        ) \
    _(WGPUBuffer,              RegEntryType_WGPUObject,             buffer,                      624        ) \
    _(WGPUShaderModule,        RegEntryType_WGPUObject,             shader_module,               128        ) \
    _(WGPUSampler,             RegEntryType_WGPUObject,             sampler,                     128        )                        

#define REM_WGPU_ITEM_COUNT 5

#define REM_ENGINE_LIST(_)                                                                                                                                \
    _(Mesh,                   RegEntryType_Mesh,                    mesh,                       1024        ) \
    _(Scene,                  RegEntryType_Scene,                   scene,                         2        ) \
    _(RenderPipeline,         RegEntryType_RenderPipeline,          render_pipeline,              64        ) \
    _(ComputePipeline,        RegEntryType_ComputePipeline,         compute_pipeline,            128        ) \
    _(Shader,                 RegEntryType_Shader,                  shader,                     1024        ) \
    _(Camera,                 RegEntryType_Camera,                  camera,                       32        ) \
    _(Renderer,               RegEntryType_Renderer,                renderer,                      2        ) \
    _(Gui,                    RegEntryType_Gui,                     gui,                           2        ) \
    _(UBOManager,             RegEntryType_Ubo,                     ubo,                           1        ) \
    _(PointLight,             RegEntryType_PointLight,              point_light,                   6        ) \
    _(AmbientLight,           RegEntryType_AmbientLight,            ambient_light,                32        ) \
    _(SpotLight,              RegEntryType_SpotLight,               spot_light,                   32        ) \
    _(SunLight,               RegEntryType_SunLight,                sun_light,                    32        ) \
    _(ProbeReflectionPlane,   RegEntryType_ProbeReflectionPlane,    plane_reflection,             16        ) \
    _(ProbeReflection,        RegEntryType_ProbeReflection,         probe_reflection,             16        ) \
    _(ProbeReflectionGrid,    RegEntryType_ProbeReflectionGrid,     probe_reflection_grid,        16        ) \
    _(JobSystem,              RegEntryType_JobSystem,               job_system,                    2        )

// clang-format on

#define REM_TYPE_COUNT 22
#define REM_HASH_CAPACITY 2048

// === Generates Type Enums ===
#define _(Type, RegistryType, Label, PoolCapacity) REMType_##Type,
typedef enum { REM_WGPU_LIST(_) REM_ENGINE_LIST(_) } REMType;
#undef _

// === Generate Structs ===

typedef struct {
  reg_id_t owner;
  uint32_t key;
  REMType type;
  HashTableBucketState state;
  size_t pool_id;
  void *handle;
} REMBucket;

// === Generate Pools for Engine Objects ===
// We use a pointer-based hashing, so we first need to generate our items in
// respective pools. The we will link each pool pointer to the Resource Bucket
// handle. Note that since WGPU entities are already opaque pointers, we don't
// need to alocate pools for them. Pools a just a way to more easily control
// each resource allocation and a way uniformize the hash system by only using
// pointers as hashing keys.
#define _(Type, RegistryType, Label, PoolCapacity)                             \
  typedef struct {                                                             \
    size_t capacity;                                                           \
    size_t count;                                                              \
    Type *entries;                                                             \
  } REMPool##Type;

REM_ENGINE_LIST(_);
#undef _

/* TODO:
Add the following entities ?
 - sem lists
 - sem

 make sure the remove their respective item->id = ....
 */
typedef struct ResourceManager {

  FREE_LIST(void *) pools[REM_TYPE_COUNT];
  HashTable hash_table;

} ResourceManager;

extern ResourceManager g_rem;

/*
  Hash utils callback

  In order to improve access to our resources O(1), we use a hash system.
  However we use two kinds of hash methods depending on the resource type:
  1. WGPU related resources used Opaque Pointers, as a result we use this
  pointer to generate the hash.
  2. Engine related resources (Mesh, Scene...) have an ID generated by the
  register. As a result we use this ID  to generate the hash.

  In the same way that both WGPU and Engine resources use their respective
  constructor (wgpuDeviceCreate.... vs item_create()) and destructor
  (wgpuResourceRelease() vs item_destroy())
 */
uint32_t rem_generate_hash(const void *);
void rem_bucket_set_bucket_state(const void *, const HashTableBucketState);
HashTableBucketState rem_bucket_get_bucket_state(const void *);
bool rem_bucket_compare(const void *, const void *);
void *rem_bucket_get_key(const void *);

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

REMStatus rem_destroy_texture(WGPUTexture *);
REMStatus rem_destroy_sampler(WGPUSampler *);
REMStatus rem_destroy_view(WGPUTextureView *);
REMStatus rem_destroy_buffer(WGPUBuffer *);
REMStatus rem_destroy_shader_module(WGPUShaderModule *);

// === Generate Engine creator / destructor functions
#define _(Type, RegistryType, Label, PoolCapacity)                             \
  Type *rem_new_##Label();                                                     \
  REMStatus rem_destroy_##Label(Type *);

REM_ENGINE_LIST(_);
#undef _

EXTERN_C_END

#endif
