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
   WGPU tracking: every live raw WGPU object gets a slot in the pool of its
   type. The public API still hands out the raw handle.
     - most types: the slot holds just the handle
     - views:      the slot holds a REMViewRecord
   The handle is always the first thing in the slot, so find/untrack work
   the same for every type.
 */

// Allocate a zeroed tracking slot, NULL if out of memory.
static void *rem_track_slot(const REMType type) {

  Pool *pool = rem_pool(type);

  uint32_t gen;
  const uint32_t index = pool_alloc(pool, &gen);

  if (index == POOL_INVALID) {
    logger_add(LoggerFlag_Error,
               "- %s: out of memory while tracking object (live: %u).",
               rem_config[type].label, pool_live_count(pool));
    return NULL;
  }

  return pool_slot_ptr(pool, index);
}

// The handle stored at the start of a tracking slot.
static inline void *rem_slot_handle(const void *slot) {
  void *handle;
  memcpy(&handle, slot, sizeof handle);
  return handle;
}

// Linear search by handle. Returns the slot index (and its generation) or
// POOL_INVALID.
static uint32_t rem_wgpu_find(const REMType type, const void *handle,
                              uint32_t *out_gen) {

  const Pool *pool = rem_pool(type);

  uint32_t gen;
  POOL_FOREACH_ALIVE(pool, i, gen) {
    if (rem_slot_handle(pool_slot_ptr(pool, i)) == handle) {
      *out_gen = gen;
      return i;
    }
  }

  return POOL_INVALID;
}

// Remove the tracking slot of a WGPU object. Returns 0 if it wasn't tracked.
static int rem_untrack(const REMType type, const void *handle) {

  uint32_t gen;
  const uint32_t index = rem_wgpu_find(type, handle, &gen);

  if (index == POOL_INVALID)
    return 0;

  pool_free(rem_pool(type), index, gen);
  return 1;
}

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

  // === init one pool per type (WGPU tracking pools + engine pools) ===
  for (REMType i = 0; i < REMType_Count; i++) {

    const size_t capacity = rem_config[i].capacity;

    // WGPU pools store the handle itself (sizeof(Type)), except views which
    // store a REMViewRecord
    const size_t type_size = (i == REMType_WGPUTextureView)
                                 ? sizeof(REMViewRecord)
                                 : rem_config[i].type_size;

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

  // WGPU pools are shut down last: engine destructors above may still
  // release WGPU objects through rem_destroy_*.
  for (int t = 0; t < REM_WGPU_ITEM_COUNT; t++) {

    Pool *pool = rem_pool((REMType)t);
    const uint32_t live = pool_live_count(pool);

    if (live > 0) {
      logger_add(LoggerFlag_Error, "- %s: %u object(s) leaked at shutdown",
                 rem_config[t].label, live);
    }

    pool_shutdown(pool);
  }
}

uint32_t rem_live_count(const REMType type) {
  if (type >= REMType_Count)
    return 0;
  return pool_live_count(rem_pool(type));
}

/*

   ▗▖ ▗▖ ▗▄▄▖▗▄▄▖ ▗▖ ▗▖
   ▐▌ ▐▌▐▌   ▐▌ ▐▌▐▌ ▐▌
   ▐▌ ▐▌▐▌▝▜▌▐▛▀▘ ▐▌ ▐▌
   ▐▙█▟▌▝▚▄▞▘▐▌   ▝▚▄▞▘

   Raw opaque pointers: created by wgpu, returned as-is, tracked in a pool.
 */

#define REM_NEW_WGPU_ITEM(Name, FuncName, TypeEnum, DescType, Creator)         \
  Name rem_new_##FuncName(const DescType *desc) {                              \
    Name item = Creator(context_device(), desc);                               \
    if (item != NULL) {                                                        \
      Name *slot = (Name *)rem_track_slot(TypeEnum);                           \
      if (slot)                                                                \
        *slot = item;                                                          \
    }                                                                          \
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

  if (view == NULL)
    return view;

  REMViewRecord *rec =
      (REMViewRecord *)rem_track_slot(REMType_WGPUTextureView);

  if (rec) {
    const uint32_t base_mip = desc ? desc->baseMipLevel : 0;
    const uint32_t base_layer = desc ? desc->baseArrayLayer : 0;

    WGPUTextureViewDimension dim =
        desc ? desc->dimension : WGPUTextureViewDimension_Undefined;

    // resolve "Undefined" the same way WebGPU does
    if (dim == WGPUTextureViewDimension_Undefined) {
      const WGPUTextureDimension tdim = wgpuTextureGetDimension(texture);
      if (tdim == WGPUTextureDimension_1D) {
        dim = WGPUTextureViewDimension_1D;
      } else if (tdim == WGPUTextureDimension_3D) {
        dim = WGPUTextureViewDimension_3D;
      } else {
        const uint32_t layers =
            (desc && desc->arrayLayerCount != 0 &&
             desc->arrayLayerCount != WGPU_ARRAY_LAYER_COUNT_UNDEFINED)
                ? desc->arrayLayerCount
                : wgpuTextureGetDepthOrArrayLayers(texture) - base_layer;
        dim = layers > 1 ? WGPUTextureViewDimension_2DArray
                         : WGPUTextureViewDimension_2D;
      }
    }

    rec->handle = view;
    rec->texture = texture;
    rec->dimension = dim;
    rec->base_mip = base_mip;
    rec->base_layer = base_layer;
  }

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

  if (shader != NULL) {
    WGPUShaderModule *slot =
        (WGPUShaderModule *)rem_track_slot(REMType_WGPUShaderModule);
    if (slot)
      *slot = shader;
  }

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

// Untrack + release a raw WGPU object and NULL the caller's handle.
// An object that isn't tracked is rejected WITHOUT being released (catches
// double destroys and objects that were not created through rem_new_*).
#define REM_DESTROY_WGPU_ITEM(FuncName, HandleType, TypeEnum, Releaser)        \
  REMStatus rem_destroy_##FuncName(HandleType *handle) {                       \
                                                                               \
    if (handle == NULL || *handle == NULL)                                     \
      return REMStatus_NullResource;                                           \
                                                                               \
    if (!rem_untrack(TypeEnum, *handle))                                       \
      return REMStatus_UnfoundResource;                                        \
                                                                               \
    Releaser(*handle);                                                         \
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

  if (!rem_untrack(REMType_WGPUTextureView, *handle))
    return REMStatus_UnfoundResource;

  wgpuTextureViewRelease(*handle);

  *handle = NULL;
  return REMStatus_Success;
}

/*
   Enumeration of live WGPU objects.

   void rem_get_all_<label>(Type *entries, size_t *count)
     entries == NULL : *count = live objects
     entries != NULL : copies up to *count handles, *count = written
 */
#define _(Type, RegistryType, Label, Capacity)                                 \
  void rem_get_all_##Label(Type *entries, size_t *count) {                     \
                                                                               \
    if (count == NULL)                                                         \
      return;                                                                  \
                                                                               \
    const Pool *pool = rem_pool(REMType_##Type);                               \
    const size_t cap = entries ? *count : 0;                                   \
    size_t n = 0;                                                              \
                                                                               \
    uint32_t gen;                                                              \
    POOL_FOREACH_ALIVE(pool, i, gen) {                                         \
      if (entries) {                                                           \
        if (n >= cap)                                                          \
          break;                                                               \
        entries[n] = *(const Type *)pool_slot_ptr(pool, i);                                             \
      }                                                                        \
      n++;                                                                     \
    }                                                                          \
                                                                               \
    *count = n;                                                                \
  }

REM_WGPU_LIST(_)
#undef _

const REMViewRecord *rem_get_view_record(WGPUTextureView view) {

  uint32_t gen;
  const uint32_t index = rem_wgpu_find(REMType_WGPUTextureView, view, &gen);

  if (index == POOL_INVALID)
    return NULL;

  return (const REMViewRecord *)pool_slot_ptr(
      rem_pool(REMType_WGPUTextureView), index);
}

WGPUTexture rem_get_view_texture(WGPUTextureView view) {
  const REMViewRecord *rec = rem_get_view_record(view);
  return rec ? rec->texture : NULL;
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

// === ENUMERATE ENGINE ITEMS ===
// void rem_get_all_<label>(Type **entries, size_t *count): same semantics as
// the WGPU getters, entries are pool-stable pointers.
#define _(Type, RegistryType, Label, Capacity)                                 \
  void rem_get_all_##Label(Type **entries, size_t *count) {                    \
                                                                               \
    if (count == NULL)                                                         \
      return;                                                                  \
                                                                               \
    const Pool *pool = rem_pool(REMType_##Type);                               \
    const size_t cap = entries ? *count : 0;                                   \
    size_t n = 0;                                                              \
                                                                               \
    uint32_t gen;                                                              \
    POOL_FOREACH_ALIVE(pool, i, gen) {                                         \
      if (entries) {                                                           \
        if (n >= cap)                                                          \
          break;                                                               \
        entries[n] = (Type *)pool_slot_ptr(pool, i);                           \
      }                                                                        \
      n++;                                                                     \
    }                                                                          \
                                                                               \
    *count = n;                                                                \
  }

REM_ENGINE_LIST(_)
#undef _
