#include "ubo.h"

#include <stdbool.h>
#include <stdint.h>

#include "backend/context.h"
#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "runtime/camera/core.h"
#include "runtime/light/list.h"
#include "runtime/light/uniform.h"
#include "runtime/mesh/core.h"
#include "runtime/probe/core.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/probe/reflection/probe.h"
#include "runtime/scene/environment/core.h"
#include "runtime/shader/utils.h"
#include "runtime/viewport/core.h"
#include "utils/projection.h"
#include "utils/stli.h"
#include "webgpu/webgpu.h"

static const struct {
  const size_t size;
  const char *label;
} ubo_type[UBO_TYPE_COUNT] = {
#define _(Name, Uniform) [UBOType_##Name] = {sizeof(Uniform), #Name},
    UBO_LIST(_)
#undef _
};

void ubo_init(UBOManager *manager) {

  logger_add(LoggerFlag_Process, "Initializing UBO Manager");

  manager->id = reg_register(manager, RegEntryType_Ubo);

  static const uint16_t ALIGNMENT = 16;
  static const uint16_t MIN_SIZE = 256;

  for (UBOType i = 0; i < UBO_TYPE_COUNT; i++) {

    if (ubo_type[i].size % ALIGNMENT != 0)
      logger_add(LoggerFlag_Warning,
                 "Attempting to set a UBO buffer (%d) not aligned with %hu "
                 "bytes (%lu). UBO Buffers require 256 alignment.",
                 i, ALIGNMENT, ubo_type[i].size);

    if (ubo_type[i].size < MIN_SIZE)
      logger_add(
          LoggerFlag_Warning,
          "Attempting to set a buffer (%d) not with a type size inferior to %hu"
          "bytes (%lu).",
          i, MIN_SIZE, ubo_type[i].size);

    if (ubo_type[i].size > UBO_MAX_TYPE_SIZE)
      logger_add(LoggerFlag_Warning,
                 "Attempting to set a buffer (%d) with a type size "
                 "superior to the maximum allowed size (%hu)"
                 ", type size bytes %lu.",
                 i, UBO_MAX_TYPE_SIZE, ubo_type[i].size);

    UBOBuffer *buffer = &manager->buffers[i];
    buffer->type_size = ubo_type[i].size;
    buffer->count = 0;
    buffer->capacity = UBO_CAPACITY * UBO_MAX_TYPE_SIZE;
    buffer->update_queue.capacity = UBO_UPDATE_QUEUE_CAPACITY;
    buffer->handle = rem_new_buffer(&(WGPUBufferDescriptor){
        .size = buffer->capacity,
        .mappedAtCreation = false,
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform,
        .label = ubo_type[i].label,
    });
  }
}

void ubo_destroy(UBOManager *manager) {}

UBOStatus ubo_update_entry(UBOManager *manager, const UBOType type,
                           const UBOSlot *slot) {

  if (slot->id >= UBO_CAPACITY) {
    logger_add(LoggerFlag_Warning,
               "Attempting to write into UBO out of bound index (%lu) max UBO "
               "capacity is currently set to %d.",
               slot->id, UBO_CAPACITY);
    return UBOStatus_OutOfBound;
  }

  UBOBuffer *ubo = &manager->buffers[type];
  memcpy((char *)ubo->entries + slot->id * ubo->type_size, slot->uniform,
         ubo->type_size);

  // TODO improve index incrementation (currently very unsafe)
  manager->buffers[type].count = slot->id + 1;

  return UBOStatus_Success;
}

UBOStatus ubo_upload_entry(UBOManager *manager, const UBOType type,
                           const UBOSlot *slot) {

  // update ubo buffer at index
  UBOBuffer *ubo = &manager->buffers[type];
  size_t offset = slot->id * ubo->type_size;

  rem_write_buffer(ubo->handle, offset, (uint8_t *)ubo->entries + offset,
                   ubo->type_size, REMWriteFlag_None);

  return UBOStatus_Success;
}

void ubo_upload(UBOManager *manager, const UBOType type) {

  UBOBuffer *ubo = &manager->buffers[type];
  rem_write_buffer(ubo->handle, 0, ubo->entries, ubo->type_size * UBO_CAPACITY,
                   REMWriteFlag_None);
}

WGPUBuffer ubo_buffer_handle(UBOManager *manager, const UBOType type) {
  return manager->buffers[type].handle;
}

UBOSlot ubo_new_entry(UBOManager *manager, const UBOType type) {
  UBOBuffer *ubo = &manager->buffers[type];

  return (UBOSlot){
      .uniform = stli_new_entry((void *)ubo->entries, ubo->capacity,
                                &ubo->count, ubo->type_size, "UBO Manager"),
      .id = ubo->count,
  };
}

StaticListStatus ubo_remove_entry(UBOManager *manager, const UBOType type,
                                  ubo_id_t index) {
  UBOBuffer *ubo = &manager->buffers[type];
  return stli_remove((void *)ubo->entries, &ubo->count, ubo->type_size,
                     &ubo->entries[index], "UBO Manager");
}

size_t ubo_count(UBOManager *ubo, const UBOType type) {
  return ubo->buffers[type].count;
}

void *ubo_entry(UBOManager *ubo, const UBOType type, ubo_id_t index) {
  return (void *)((uint8_t *)ubo->buffers[type].entries +
                  index * ubo->buffers[type].type_size);
}

size_t ubo_find_index(UBOManager *manager, const UBOType type, void *data) {
  for (size_t i = 0; i < ubo_count(manager, UBOType_Mesh); i++)
    if (ubo_entry(manager, UBOType_Mesh, i) == data)
      return i;

  return UBO_INDEX_UNFOUND;
}

/**
   Transfer the slot data into the UBO buffer and update the slot id.
 */
UBOStatus ubo_insert_entry(UBOManager *manager, const UBOType type,
                           UBOSlot *slot) {
  ubo_new_entry(manager, type);
  ubo_update_entry(manager, type, slot);
  return ubo_upload_entry(manager, type, slot);
}

/**
   Insert and id of a specific Buffer type (view, mesh...) into the buffer
   update queue. Each queue is then read during the ubo_draw to write at the
   corresponding index.
   Using a queue prevent having "bool" last minutes flags.
 */
StaticListStatus ubo_update_queue_insert(UBOManager *manager,
                                         const UBOType type,
                                         const ubo_id_t id) {

  UBOBufferUpdateQueue *queue = &manager->buffers[type].update_queue;

  return stli_insert((void *)queue->entries, queue->capacity, &queue->count,
                     sizeof(ubo_id_t), (void *)&id, "UBO Update Queue");
}

StaticListStatus ubo_update_queue_shift(UBOManager *manager,
                                        const UBOType type) {
  UBOBufferUpdateQueue *queue = &manager->buffers[type].update_queue;
  return stli_shift((void *)queue->entries, &queue->count, sizeof(ubo_id_t),
                    "UBO Update Queue");
}
