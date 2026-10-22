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
#include "utils/dyli.h"
#include "utils/frli.h"
#include "utils/hsht.h"
#include "utils/stli.h"
#include "webgpu/webgpu.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

ResourceManager g_rem = {0};

static inline const size_t rem_type_size(const REMType type) {
  return rem_config[type].type_size;
}

static inline const char *rem_type_label(const REMType type) {
  return rem_config[type].label;
}

static inline const size_t rem_type_init_capacity(const REMType type) {
  return rem_config[type].capacity;
}

static inline const size_t rem_type_count(const REMType type) {
  return g_rem.pools[type].count;
}

REMStatus resource_manager_init() {

  logger_add(LoggerFlag_Process, "Allocating space for resources...");

  REMStatus status = REMStatus_Success;
  size_t total_bytes = 0;

  // === init global hash table ===
  hsht_create(&g_rem.hash_table,
              &(HashTableDescriptor){
                  .bucket_size = sizeof(REMBucket),
                  .label = "Resource Manager Hash Table",
                  .capacity = REM_HASH_CAPACITY,
                  .generator_callback = rem_generate_hash,
                  .comparator_callback = rem_bucket_compare,
                  .get_bucket_state_callback = rem_bucket_get_bucket_state,
                  .set_bucket_state_callback = rem_bucket_set_bucket_state,
                  .get_key_callback = rem_bucket_get_key,
              });

  // === init per engine type pools ===
  for (REMType i = 0; i < REM_TYPE_COUNT; i++) {

    g_rem.pools[i].type_size = rem_type_size(i);
    g_rem.pools[i].label = rem_type_label(i);

    const size_t capacity = rem_type_init_capacity(i);

    if (frli_create((void **)&g_rem.pools[i].entries, &g_rem.pools[i].used,
                    &g_rem.pools[i].capacity, &g_rem.pools[i].count,
                    g_rem.pools[i].type_size, capacity,
                    g_rem.pools[i].label) == FreeListStatus_Success) {

      total_bytes += rem_config[i].type_size * rem_config[i].capacity;

      logger_add(
          LoggerFlag_Print, "- %s: %lu bytes (type size: %lu, capacity: %lu)",
          rem_config[i].label, rem_config[i].type_size * rem_config[i].capacity,
          rem_config[i].type_size, rem_config[i].capacity);
    } else {
      logger_add(LoggerFlag_Error,
                 "- %s: Couldn't allocate memory with a "
                 "capacity of %lu (%lu bytes).",
                 rem_config[i].label, rem_config[i].capacity,
                 rem_config[i].type_size * rem_config[i].capacity);
      status = REMStatus_AllocFail;
    }
  }

  // log result
  logger_add(LoggerFlag_Print, "Total allocation size: %lu bytes", total_bytes);
  return status;
}

/*

   ▗▖ ▗▖ ▗▄▖  ▗▄▄▖▗▖ ▗▖
   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌ ▐▌
   ▐▛▀▜▌▐▛▀▜▌ ▝▀▚▖▐▛▀▜▌
   ▐▌ ▐▌▐▌ ▐▌▗▄▄▞▘▐▌ ▐▌

   ▗▖ ▗▖▗▄▄▄▖▗▄▄▄▖▗▖    ▗▄▄▖
   ▐▌ ▐▌  █    █  ▐▌   ▐▌
   ▐▌ ▐▌  █    █  ▐▌    ▝▀▚▖
   ▝▚▄▞▘  █  ▗▄█▄▖▐▙▄▄▖▗▄▄▞▘


 */

uint32_t rem_generate_hash(const void *ptr) { return hsht_hash_ptr(ptr); }

HashTableBucketState rem_bucket_get_bucket_state(const void *obj) {
  return (bool)(((REMBucket *)obj)->state);
}

void rem_bucket_set_bucket_state(const void *bucket,
                                 const HashTableBucketState state) {
  ((REMBucket *)bucket)->state = state;
}

bool rem_bucket_compare(const void *key, const void *bucket) {
  return key == ((REMBucket *)bucket)->handle;
}

void *rem_bucket_get_key(const void *bucket) {
  return ((REMBucket *)bucket)->handle;
}

/*



   ▗▖ ▗▖ ▗▄▄▖▗▄▄▖ ▗▖ ▗▖
   ▐▌ ▐▌▐▌   ▐▌ ▐▌▐▌ ▐▌
   ▐▌ ▐▌▐▌▝▜▌▐▛▀▘ ▐▌ ▐▌
   ▐▙█▟▌▝▚▄▞▘▐▌   ▝▚▄▞▘



 */

WGPUTexture rem_new_texture(const WGPUTextureDescriptor *desc) {

  const REMType type = REMType_WGPUTexture;

  // first generate hash source
  WGPUTexture texture = wgpuDeviceCreateTexture(context_device(), desc);

  REMBucket *entry =
      hsht_new_entry(&g_rem.hash_table, texture, HashTableNewFlag_None);

  if (entry == NULL) {
    rem_destroy_texture(&texture);
    return NULL;
  }

  entry->owner = 0;
  entry->key = 0;
  entry->pool_id = DYLI_INVALID_INDEX;
  entry->type = type;
  entry->handle = texture;

  return texture;
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

#define REM_NEW_WGPU_ITEM(Name, FuncName, Type, REMName, DescType, Creator)    \
  Name rem_new_##FuncName(const DescType *desc) {                              \
                                                                               \
    const REMType type = Type;                                                 \
                                                                               \
    Name item = Creator(context_device(), desc);                               \
                                                                               \
    REMBucket *entry =                                                         \
        hsht_new_entry(&g_rem.hash_table, item, HashTableNewFlag_None);        \
                                                                               \
    if (entry == NULL) {                                                       \
      rem_destroy_##FuncName(&item);                                           \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    entry->owner = 0;                                                          \
    entry->key = 0;                                                            \
    entry->pool_id = DYLI_INVALID_INDEX;                                       \
    entry->type = type;                                                        \
    entry->handle = item;                                                      \
                                                                               \
    return item;                                                               \
  }

WGPUTextureView rem_new_view(const WGPUTexture texture,
                             const WGPUTextureViewDescriptor *desc) {

  const REMType type = REMType_WGPUTextureView;

  WGPUTextureView view = wgpuTextureCreateView(texture, desc);

  REMBucket *entry =
      hsht_new_entry(&g_rem.hash_table, view, HashTableNewFlag_None);

  if (entry == NULL) {
    rem_destroy_view(&view);
    return NULL;
  }

  entry->pool_id = DYLI_INVALID_INDEX;
  entry->owner = 0;
  entry->key = 0;
  entry->type = type;
  entry->handle = view;

  return view;
}

REM_NEW_WGPU_ITEM(WGPUSampler, sampler, REMType_WGPUSampler, REMWGPUSampler,
                  WGPUSamplerDescriptor, wgpuDeviceCreateSampler);

REM_NEW_WGPU_ITEM(WGPUBuffer, buffer, REMType_WGPUBuffer, REMWGPUBuffer,
                  WGPUBufferDescriptor, wgpuDeviceCreateBuffer);

REMStatus rem_write_buffer(WGPUBuffer buffer, const size_t offset, void *data,
                           const size_t size, const REMWriteFlag flag) {

  if (buffer == NULL) {
    logger_add(LoggerFlag_Error, "Attempting to write in undefined buffer.");
    return REMStatus_NullResource;
  }

  // if (data == NULL) {
  //   logger_add(LoggerFlag_Error, "Attempting to write in NULL data in
  //   buffer."); return REMStatus_NullResource;
  // }

  wgpuQueueWriteBuffer(context_queue(), buffer, offset, data, size);

  if (flag & REMWriteFlag_FreeData) {
    free(data);
    data = NULL;
  }

  return REMStatus_Success;
}

WGPUShaderModule rem_new_shader_module(char *code, const char *label,
                                       const REMWriteFlag flag) {

  const REMType type = REMType_WGPUShaderModule;

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

  REMBucket *entry =
      hsht_new_entry(&g_rem.hash_table, shader, HashTableNewFlag_None);

  if (entry == NULL) {
    rem_destroy_shader_module(&shader);
    return NULL;
  }

  if ((flag & REMWriteFlag_FreeData) && code) {
    free(code);
    code = NULL;
  }

  entry->pool_id = DYLI_INVALID_INDEX;
  entry->owner = 0;
  entry->key = 0;
  entry->type = REMType_Shader;
  entry->handle = shader;

  return shader;
}

// Destroy item based on its handle pointer (wgpu Opaque Pointer objects)
#define REM_DESTROY_WGPU_ITEM(FuncName, HandleType, REMType, REMItem,          \
                              Destructor)                                      \
  REMStatus rem_destroy_##FuncName(HandleType *handle) {                       \
                                                                               \
    if (*handle == NULL)                                                       \
      return REMStatus_NullResource;                                           \
                                                                               \
    Destructor;                                                                \
                                                                               \
    HashTableStatus remove =                                                   \
        hsht_remove_entry(&g_rem.hash_table, (void *)*handle);                 \
                                                                               \
    *handle = NULL;                                                            \
                                                                               \
    return REMStatus_UnfoundResource;                                          \
  }

REM_DESTROY_WGPU_ITEM(texture, WGPUTexture, REMType_WGPUTexture, REMTexture,
                      wgpuTextureRelease(*handle));

REM_DESTROY_WGPU_ITEM(view, WGPUTextureView, REMType_WGPUTextureView, REMView, {
  if (is_std_texture_view(*handle))
    return REMStatus_ProtectedResource;

  wgpuTextureViewRelease(*handle);
});

REM_DESTROY_WGPU_ITEM(buffer, WGPUBuffer, REMType_WGPUBuffer, REMBuffer,
                      wgpuBufferRelease(*handle));

REM_DESTROY_WGPU_ITEM(sampler, WGPUSampler, REMType_WGPUSampler, REMSampler,
                      wgpuSamplerRelease(*handle));

REM_DESTROY_WGPU_ITEM(shader_module, WGPUShaderModule, REMType_WGPUShaderModule,
                      REMShaderModule, wgpuShaderModuleRelease(*handle));

/*


   ▗▄▄▄▖▗▖  ▗▖ ▗▄▄▖▗▄▄▄▖▗▖  ▗▖▗▄▄▄▖
   ▐▌   ▐▛▚▖▐▌▐▌     █  ▐▛▚▖▐▌▐▌
   ▐▛▀▀▘▐▌ ▝▜▌▐▌▝▜▌  █  ▐▌ ▝▜▌▐▛▀▀▘
   ▐▙▄▄▖▐▌  ▐▌▝▚▄▞▘▗▄█▄▖▐▌  ▐▌▐▙▄▄▖


 */

// === NEW ENGINE ITEM===
#define _(Type, RegistryType, Label, Capacity)                                 \
  Type *rem_new_##Label() {                                                    \
                                                                               \
    const REMType type = REMType_##Type;                                       \
                                                                               \
    size_t index = 0;                                                          \
    void *new_item = frli_new_entry(                                           \
        (void **)&g_rem.pools[type].entries, g_rem.pools[type].used,           \
        &g_rem.pools[type].capacity, &g_rem.pools[type].count,                 \
        g_rem.pools[type].type_size, &index, g_rem.pools[type].label);         \
                                                                               \
    if (new_item == NULL)                                                      \
      return NULL;                                                             \
                                                                               \
    REMBucket *new_bucket = hsht_new_entry(                                    \
        &g_rem.hash_table, (void *)new_item, HashTableNewFlag_None);           \
                                                                               \
    if (new_bucket == NULL)                                                    \
      return NULL;                                                             \
                                                                               \
    new_bucket->handle = new_item;                                             \
    new_bucket->pool_id = index;                                               \
    new_bucket->owner = 0;                                                     \
    new_bucket->key = 0;                                                       \
    new_bucket->type = REMType_##Type;                                         \
                                                                               \
    return new_bucket->handle;                                                 \
  }

REM_ENGINE_LIST(_);
#undef _

// Destroy item based on its handle id (engine objects)
#define _(Type, RegistryType, Label, Capacity)                                 \
  REMStatus rem_destroy_##Label(Type *handle) {                                \
                                                                               \
    const REMType type = REMType_##Type;                                       \
                                                                               \
    if (handle == NULL)                                                        \
      return REMStatus_NullResource;                                           \
                                                                               \
    Label##_destroy(handle);                                                   \
                                                                               \
    REMBucket *bucket = hsht_find(&g_rem.hash_table, handle, NULL);            \
                                                                               \
    if (!bucket)                                                               \
      return REMStatus_UnfoundResource;                                        \
                                                                               \
    FreeListStatus remove_pool = frli_remove_at_index(                         \
        (void *)g_rem.pools[type].entries, g_rem.pools[type].used,             \
        g_rem.pools[type].capacity, &g_rem.pools[type].count,                  \
        g_rem.pools[type].type_size, bucket->pool_id,                          \
        g_rem.pools[type].label);                                              \
                                                                               \
    HashTableStatus remove_hash =                                              \
        hsht_remove_entry(&g_rem.hash_table, handle);                          \
                                                                               \
    if (remove_hash != HashTableStatus_Success)                                \
      return REMStatus_UnfoundResource;                                        \
                                                                               \
    handle = NULL;                                                             \
                                                                               \
    return REMStatus_Success;                                                  \
  }

REM_ENGINE_LIST(_);
#undef _
