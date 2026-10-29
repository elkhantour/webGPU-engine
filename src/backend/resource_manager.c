#include "./resource_manager.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/renderer/core.h"
#include "backend/std_texture/core.h"
#include "backend/ubo.h"
#include "runtime/gui/core.h"
#include "runtime/light/core.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/shader/core.h"
#include "stb/stb_image.h"
#include "utils/stli.h"
#include "webgpu/webgpu.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ResourceManager g_rem = {0};

static inline Pool *rem_pool(const REMType type) { return &g_rem.pools[type]; }

/*
   Engine destroy thunks: one per engine type, so shutdown can destroy
   leftover objects through a table indexed by REMType without casting
   function pointers.
 */
#define _(Type, RegistryType, Label, Capacity)                                 \
  static void rem_destroy_thunk_##Label(void *item) {                          \
    Label##_destroy((Type *)item);                                             \
  }
REM_ENGINE_LIST(_)
#undef _

static void (*const rem_destroy_thunks[REMType_Count])(void *) = {
#define _(Type, RegistryType, Label, Capacity)                                 \
  [REMType_##Type] = rem_destroy_thunk_##Label,
    REM_ENGINE_LIST(_)
#undef _
};

REMStatus resource_manager_init() {

  logger_add(LoggerFlag_Process, "Allocating space for resources...");

  REMStatus status = REMStatus_Success;
  size_t total_bytes = 0;

  memset(&g_rem, 0, sizeof g_rem);

  // === init per engine type pools ===
  for (REMType i = REM_WGPU_ITEM_COUNT; i < REMType_Count; i++) {

    const size_t capacity = rem_config[i].capacity;
    const size_t type_size = rem_config[i].type_size;

    // chunk sized to the initial capacity, capped, so singletons stay small
    const uint32_t chunk_slots =
        capacity < REM_MAX_CHUNK_SLOTS ? (uint32_t)capacity
                                       : REM_MAX_CHUNK_SLOTS;

    Pool *pool = rem_pool(i);
    pool_init(pool, (uint32_t)type_size, chunk_slots);

    // Preallocate the initial capacity. Remove this call to make pools fully
    // lazy (they will then allocate a chunk on first use).
    if (pool_reserve(pool, (uint32_t)capacity)) {

      total_bytes += pool_reserved_bytes(pool);

      logger_add(LoggerFlag_Print,
                 "- %s: %lu bytes (type size: %lu, initial capacity: %lu, "
                 "chunk: %u slots)",
                 rem_config[i].label,
                 (unsigned long)pool_reserved_bytes(pool),
                 (unsigned long)type_size, (unsigned long)capacity,
                 pool->chunk_size);
    } else {
      logger_add(LoggerFlag_Error,
                 "- %s: Couldn't allocate memory with an initial capacity "
                 "of %lu (%lu bytes).",
                 rem_config[i].label, (unsigned long)capacity,
                 (unsigned long)(type_size * capacity));
      status = REMStatus_AllocFail;
    }
  }

  logger_add(LoggerFlag_Print, "Total allocation size: %lu bytes",
             (unsigned long)total_bytes);
  return status;
}

void resource_manager_shutdown(void) {

  // reverse order: objects created last usually depend on earlier ones
  for (int t = REMType_Count - 1; t >= REM_WGPU_ITEM_COUNT; t--) {

    Pool *pool = rem_pool((REMType)t);
    const uint32_t live = pool_live_count(pool);

    if (live > 0) {
      logger_add(LoggerFlag_Print,
                 "- %s: destroying %u object(s) still alive at shutdown",
                 rem_config[t].label, live);
    }

    uint32_t gen;
    POOL_FOREACH_ALIVE(pool, i, gen) {
      rem_destroy_thunks[t](pool_slot_ptr(pool, i));
      pool_free(pool, i, gen);
    }

    pool_shutdown(pool);
  }

  for (int t = 0; t < REM_WGPU_ITEM_COUNT; t++) {
    if (g_rem.wgpu_live[t] > 0) {
      logger_add(LoggerFlag_Error, "- %s: %u object(s) leaked at shutdown",
                 rem_config[t].label, g_rem.wgpu_live[t]);
    }
  }
}

uint32_t rem_live_count(const REMType type) {
  if (type < REM_WGPU_ITEM_COUNT)
    return g_rem.wgpu_live[type];
  if (type >= REMType_Count)
    return 0;
  return pool_live_count(rem_pool(type));
}

/*

   ▗▖ ▗▖ ▗▄▄▖▗▄▄▖ ▗▖ ▗▖
   ▐▌ ▐▌▐▌   ▐▌ ▐▌▐▌ ▐▌
   ▐▌ ▐▌▐▌▝▜▌▐▛▀▘ ▐▌ ▐▌
   ▐▙█▟▌▝▚▄▞▘▐▌   ▝▚▄▞▘

   Raw opaque pointers: created by wgpu, returned as-is, only counted.
 */

#define REM_NEW_WGPU_ITEM(Name, FuncName, TypeEnum, DescType, Creator)         \
  Name rem_new_##FuncName(const DescType *desc) {                              \
    Name item = Creator(context_device(), desc);                               \
    if (item != NULL)                                                          \
      g_rem.wgpu_live[TypeEnum]++;                                             \
    return item;                                                               \
  }

REM_NEW_WGPU_ITEM(WGPUTexture, texture, REMType_WGPUTexture,
                  WGPUTextureDescriptor, wgpuDeviceCreateTexture);

REM_NEW_WGPU_ITEM(WGPUSampler, sampler, REMType_WGPUSampler,
                  WGPUSamplerDescriptor, wgpuDeviceCreateSampler);

REM_NEW_WGPU_ITEM(WGPUBuffer, buffer, REMType_WGPUBuffer, WGPUBufferDescriptor,
                  wgpuDeviceCreateBuffer);

WGPUTextureView rem_new_view(const WGPUTexture texture,
                             const WGPUTextureViewDescriptor *desc) {

  WGPUTextureView view = wgpuTextureCreateView(texture, desc);

  if (view != NULL)
    g_rem.wgpu_live[REMType_WGPUTextureView]++;

  return view;
}

WGPUShaderModule rem_new_shader_module(char *code, const char *label,
                                       const REMWriteFlag flag) {

  WGPUShaderModuleWGSLDescriptor wgsl = {
      .chain.sType = WGPUSType_ShaderModuleWGSLDescriptor,
      .code = code,
  };

  wgpuDevicePushErrorScope(context_device(), WGPUErrorFilter_Validation);

  WGPUShaderModule shader = wgpuDeviceCreateShaderModule(
      context_device(), &(WGPUShaderModuleDescriptor){
                            .nextInChain = (WGPUChainedStruct *)(&wgsl),
                            .label = label,
                        });

  wgpuShaderModuleGetCompilationInfo(
      shader, compute_pipeline_compilation_info_callback, shader);

  wgpuDevicePopErrorScope(context_device(),
                          compute_pipeline_handle_validation_error,
                          (void *)shader);

  // the code string is consumed either way, free it if asked to
  if ((flag & REMWriteFlag_FreeData) && code) {
    free(code);
    code = NULL;
  }

  if (shader != NULL)
    g_rem.wgpu_live[REMType_WGPUShaderModule]++;

  return shader;
}

REMStatus rem_write_texture(WGPUTexture texture, void *data, const size_t size,
                            const TextureChannel channels, const uint16_t layer,
                            const uint8_t mip_level, const REMWriteFlag flag) {

  if (texture == NULL) {
    logger_add(LoggerFlag_Error, "Attempting to write in undefined texture.");
    return REMStatus_NullResource;
  }

  if (data == NULL) {
    logger_add(LoggerFlag_Error,
               "Attempting to write in NULL data in texture.");
    return REMStatus_NullResource;
  }

  const uint32_t w =
      glm_max(1, (int)(wgpuTextureGetWidth(texture) >> mip_level));
  const uint32_t h =
      glm_max(1, (int)(wgpuTextureGetHeight(texture) >> mip_level));

  wgpuQueueWriteTexture(context_queue(),
                        &(WGPUImageCopyTexture){
                            .texture = texture,
                            .mipLevel = mip_level,
                            .origin = {0, 0, layer},
                            .aspect = WGPUTextureAspect_All,
                        },
                        data, size,
                        &(WGPUTextureDataLayout){
                            .offset = 0,
                            .bytesPerRow = w * channels,
                            .rowsPerImage = h,
                        },
                        &(WGPUExtent3D){w, h, 1});

  if (flag & REMWriteFlag_FreeData) {
    free(data);
    data = NULL;
  } else if (flag & REMWriteFlag_STBIFreeData) {
    stbi_image_free(data);
    data = NULL;
  }

  return REMStatus_Success;
}

REMStatus rem_write_buffer(WGPUBuffer buffer, const size_t offset, void *data,
                           const size_t size, const REMWriteFlag flag) {

  if (buffer == NULL) {
    logger_add(LoggerFlag_Error, "Attempting to write in undefined buffer.");
    return REMStatus_NullResource;
  }

  wgpuQueueWriteBuffer(context_queue(), buffer, offset, data, size);

  if (flag & REMWriteFlag_FreeData) {
    free(data);
    data = NULL;
  }

  return REMStatus_Success;
}

// Release a raw WGPU object, decrement its counter, NULL the caller's handle.
#define REM_DESTROY_WGPU_ITEM(FuncName, HandleType, TypeEnum, Releaser)        \
  REMStatus rem_destroy_##FuncName(HandleType *handle) {                       \
                                                                               \
    if (handle == NULL || *handle == NULL)                                     \
      return REMStatus_NullResource;                                           \
                                                                               \
    Releaser(*handle);                                                         \
                                                                               \
    if (g_rem.wgpu_live[TypeEnum] > 0)                                         \
      g_rem.wgpu_live[TypeEnum]--;                                             \
                                                                               \
    *handle = NULL;                                                            \
    return REMStatus_Success;                                                  \
  }

REM_DESTROY_WGPU_ITEM(texture, WGPUTexture, REMType_WGPUTexture,
                      wgpuTextureRelease);

REM_DESTROY_WGPU_ITEM(buffer, WGPUBuffer, REMType_WGPUBuffer,
                      wgpuBufferRelease);

REM_DESTROY_WGPU_ITEM(sampler, WGPUSampler, REMType_WGPUSampler,
                      wgpuSamplerRelease);

REM_DESTROY_WGPU_ITEM(shader_module, WGPUShaderModule,
                      REMType_WGPUShaderModule, wgpuShaderModuleRelease);

// Views need the std-texture protection check first.
REMStatus rem_destroy_view(WGPUTextureView *handle) {

  if (handle == NULL || *handle == NULL)
    return REMStatus_NullResource;

  if (is_std_texture_view(*handle))
    return REMStatus_ProtectedResource;

  wgpuTextureViewRelease(*handle);

  if (g_rem.wgpu_live[REMType_WGPUTextureView] > 0)
    g_rem.wgpu_live[REMType_WGPUTextureView]--;

  *handle = NULL;
  return REMStatus_Success;
}

/*

   ▗▄▄▄▖▗▖  ▗▖ ▗▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖
   ▐▌   ▐▛▚▖▐▌▐▌     █  ▐▛▚▖▐▌▐▌
   ▐▛▀▀▘▐▌ ▝▜▌▐▌▝▜▌  █  ▐▌ ▝▜▌▐▛▀▀▘
   ▐▙▄▄▖▐▌  ▐▌▝▚▄▞▘▗▄█▄▖▐▌  ▐▌▐▙▄▄▖

   Pooled objects. Pointers stay valid until rem_destroy_*().
 */

// === NEW ENGINE ITEM ===
#define _(Type, RegistryType, Label, Capacity)                                 \
  Type *rem_new_##Label() {                                                    \
                                                                               \
    Pool *pool = rem_pool(REMType_##Type);                                     \
                                                                               \
    uint32_t gen;                                                              \
    const uint32_t index = pool_alloc(pool, &gen);                             \
                                                                               \
    if (index == POOL_INVALID) {                                               \
      logger_add(LoggerFlag_Error, "- %s: out of memory (live: %u).",          \
                 rem_config[REMType_##Type].label, pool_live_count(pool));     \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    return (Type *)pool_slot_ptr(pool, index);                                 \
  }

REM_ENGINE_LIST(_)
#undef _

// === DESTROY ENGINE ITEM ===
// Lookup first: a pointer that isn't a live slot of this pool is rejected
// without calling the destructor.
#define _(Type, RegistryType, Label, Capacity)                                 \
  REMStatus rem_destroy_##Label(Type *handle) {                                \
                                                                               \
    if (handle == NULL)                                                        \
      return REMStatus_NullResource;                                           \
                                                                               \
    Pool *pool = rem_pool(REMType_##Type);                                     \
                                                                               \
    uint32_t index, gen;                                                       \
    if (!pool_index_of(pool, handle, &index, &gen))                            \
      return REMStatus_UnfoundResource;                                        \
                                                                               \
    Label##_destroy(handle); /* releases contents only */                      \
                                                                               \
    return pool_free(pool, index, gen) ? REMStatus_Success                     \
                                       : REMStatus_AllocFail;                  \
  }

REM_ENGINE_LIST(_)
#undef _
