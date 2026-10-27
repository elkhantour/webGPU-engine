#ifndef _SHADER_UTILS_H_
#define _SHADER_UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "backend/context.h"
#include "core.h"
#include "runtime/texture/core.h"
#include "webgpu/webgpu.h"

#define SHADER_BIND_VALID 0
#define SHADER_BIND_UNVALID 1

uint16_t shader_bind_group_entries_count(const ShaderBindGroup *);

bool shader_validate_binding(Shader *);

static inline size_t shader_device_storage_alignment() {
  WGPUSupportedLimits limits;
  wgpuDeviceGetLimits(context_device(), &limits);
  return limits.limits.minStorageBufferOffsetAlignment;
}

static inline size_t shader_device_uniform_alignment() {
  WGPUSupportedLimits limits;
  wgpuDeviceGetLimits(context_device(), &limits);
  return limits.limits.minUniformBufferOffsetAlignment;
}

static inline size_t shader_device_max_uniform_size() {
  WGPUSupportedLimits limits;
  wgpuDeviceGetLimits(context_device(), &limits);
  return limits.limits.maxUniformBufferBindingSize;
}


#endif
