#include "update.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "backend/compute/mipmap.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/std_texture/core.h"
#include "bindgroup.h"
#include "core.h"
#include "find.h"
#include "runtime/texture/core.h"
#include "utils.h"
#include "utils/dyli.h"
#include "utils/math.h"
#include "webgpu/webgpu.h"

static inline void shader_update_error_null_value(const char *, const char *,
                                                  const bind_group_index,
                                                  const bind_index);

void shader_update_error_null_value(const char *shader_name, const char *type,
                                    const bind_group_index gp_index,
                                    const bind_index index) {
  logger_add(LoggerFlag_Error,
             "Attempting to update shader '%s' with a null value of type '%s', "
             "for bind group "
             "%u, binding %u.",
             shader_name, type, gp_index, index);
}

/*TODO: BATCH UPDATE : like add, take a bunch of entry and ONLY REBUILD at the
 * end of update*/
ShaderBindGroupTextureEntry *
shader_update_texture_view(Shader *shader, const bind_group_index group_index,
                           const bind_index index, WGPUTextureView view,
                           WGPUTextureFormat format,
                           const ShaderUpdateFlag flag) {

  if (!view) {
    shader_update_error_null_value(shader->name, "view", group_index, index);
    return NULL;
  }

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);
  ShaderBindGroupTextureEntry *bound_texture =
      shader_find_texture(shader, group_index, index, NULL);

  if (bound_texture != NULL) {

    // clear the previous texture view if it is NOT a std texture
    if (!is_std_texture_view(bound_texture->texture_view) &&
        (flag & ShaderUpdateFlag_ReleasePrevious)) {
      rem_destroy_view(&bound_texture->texture_view);
      bound_texture->texture_view = NULL;
    }

    // replace the value
    bound_texture->texture_view = view;
    bound_texture->format = format;

    // rebuild group
    shader_bind_group_refresh(bind_group, group_index,
                              shader_pipeline(shader)->handle);

  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound texture view in group: %d, index: %d, make "
        "sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_texture;
}

ShaderBindGroupUniformEntry *
shader_update_uniform_data(Shader *shader, const bind_group_index group_index,
                           const bind_index index, void *data,
                           const ShaderUpdateFlag flag) {

  if (!data) {
    shader_update_error_null_value(shader->name, "buffer data", group_index,
                                   index);
    return NULL;
  }

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);

  ShaderBindGroupUniformEntry *bound_uniform =
      shader_find_uniform(shader, group_index, index, NULL);

  if (bound_uniform != NULL) {

    // since callback/trigger system use an allocated copy of the data we
    // need to make sure to copy the data content and not replace the pointer
    if (bound_uniform->update.callback != NULL)
      memcpy(bound_uniform->data, data, sizeof(bound_uniform->size));
    else
      bound_uniform->data = data;

    rem_write_buffer(bound_uniform->buffer, bound_uniform->offset,
                     bound_uniform->data, bound_uniform->size,
                     REMWriteFlag_None);

    // DELETME
    //   rebuild group (no need for uniforms)
    // shader_bind_group_refresh(bind_group, group_index, shader->device,
    //                          &shader_pipeline(shader)->handle);

  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound uniform in group: %d, index: %d, make sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_uniform;
}

ShaderBindGroupUniformEntry *
shader_update_uniform_buffer(Shader *shader, const bind_group_index group_index,
                             const bind_index index, WGPUBuffer buffer,
                             const size_t offset, const ShaderUpdateFlag flag) {

  if (!buffer) {
    shader_update_error_null_value(shader->name, "buffer", group_index, index);
    return NULL;
  }

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);

  size_t uniform_list_index;
  ShaderBindGroupUniformEntry *bound_uniform =
      shader_find_uniform(shader, group_index, index, &uniform_list_index);

  if (bound_uniform != NULL) {

    if (flag & ShaderUpdateFlag_ReleasePrevious)
      rem_destroy_buffer(&bound_uniform->buffer);

    size_t alignment = shader_device_uniform_alignment();

    bound_uniform->buffer = buffer;

    if (index > SHADER_MAX_OFFSET_CAPACITY)
      logger_add(
          LoggerFlag_Warning,
          "Trying to set a index offset to the shader offset array capacity.");

    if (bound_uniform->dynamic_offset_entry)
      *bound_uniform->dynamic_offset_entry = offset * alignment;

    // TODO SEARCH: why setting the uniform offset equal to the bindgroup offset
    // messes up everything (have an idea why but not exactly)
    bound_uniform->offset = 0;

    shader_bind_group_refresh(bind_group, group_index,
                              shader_pipeline(shader)->handle);

  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound uniform in group: %d, index: %d, make sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_uniform;
}

/**
   Update uniform callback autocheck
 */
ShaderBindGroupUniformEntry *shader_update_uniform_callback(
    Shader *shader, const bind_group_index group_index, const bind_index index,
    const ShaderUniformUpdate *update, const ShaderUpdateFlag flag) {

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);

  size_t uniform_list_index;
  ShaderBindGroupUniformEntry *bound_uniform =
      shader_find_uniform(shader, group_index, index, &uniform_list_index);

  if (bound_uniform != NULL) {

    if (update->callback != NULL) {

      // If callback not set yet
      if (bound_uniform->update.callback == NULL) {

        /* 1. Push pointer to Dynamic Resources Array

           Add uniform pointer to dynamic list. During the shader draw phase the
           resources (uniforms/textures/samplers) are splitted in two kinds:
           Statics and Dynamics. Only Dynamics resources (that have a callback)
           go through a secondary type of check to check depending on their
           callback if their data shall be updated.
         */

        dyli_insert((void *)&bind_group->uniforms_dynamics.entries,
                    &bind_group->uniforms_dynamics.capacity,
                    &bind_group->uniforms_dynamics.count,
                    sizeof(ShaderBindGroupUniformEntry *),
                    (void *)&bound_uniform, 1, "Shader dynamic uniform");

        /* 2. Copy Initial Data for comparison

           In case of a new update callback, we need to make a copy of the data
           since the uniform update is based on a old/new value comparison
           principle to trigger the change. If we reuse the same pointer as the
           original data we will always compare to the latest value and won't
           trigger the update since the uniform data will always be equal to the
           original one.

           Also note that the allocated size is based on the orignial pipeline
           layout. Meaning if my layout expect a vec3 but a mesh is provided it
           will lead to memory corruption.
         */

        if (bound_uniform->data) {
          void *temp_data = bound_uniform->data;
          bound_uniform->data = malloc(bound_uniform->size);
          memcpy(bound_uniform->data, temp_data, bound_uniform->size);
        }
      }

      bound_uniform->update = (ShaderUniformUpdate){
          .callback = update->callback,
          .trigger = update->trigger,
          .data = update->data,
      };
    }
  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound uniform in group: %d, index: %d, make sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_uniform;
}

ShaderBindGroupSamplerEntry *shader_update_sampler(
    Shader *shader, const bind_group_index group_index, const bind_index index,
    const WGPUSamplerDescriptor *sampler, const ShaderUpdateFlag flag) {

  if (!sampler) {
    shader_update_error_null_value(shader->name, "sampler", group_index, index);
    return NULL;
  }

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);
  ShaderBindGroupSamplerEntry *bound_sampler =
      shader_find_sampler(shader, group_index, index, NULL);

  if (bound_sampler != NULL) {

    rem_destroy_sampler(&bound_sampler->sampler);

    // replace the value
    bound_sampler->compare = sampler->compare;
    bound_sampler->addressModeU = sampler->addressModeU;
    bound_sampler->addressModeV = sampler->addressModeV;
    bound_sampler->addressModeW = sampler->addressModeW;
    bound_sampler->minFilter = sampler->minFilter;
    bound_sampler->magFilter = sampler->magFilter;
    bound_sampler->mipmapFilter = sampler->mipmapFilter;

    bound_sampler->sampler = rem_new_sampler(sampler);

    // rebuild group
    shader_bind_group_refresh(bind_group, group_index,
                              shader_pipeline(shader)->handle);

  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound sampler in group: %d, index: %d, make sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_sampler;
}

ShaderBindGroupTextureEntry *shader_update_texture(
    Shader *shader, const bind_group_index group_index, const bind_index index,
    const ShaderUpdateTexture *texture, const ShaderUpdateFlag flag) {

  ShaderBindGroup *bind_group = shader_find_bind_group(shader, group_index);
  ShaderBindGroupTextureEntry *bound_texture =
      shader_find_texture(shader, group_index, index, NULL);

  if (bound_texture != NULL) {

    // generate texture + texture view from data & size
    WGPUTextureView new_view;
    WGPUTexture gpu_texture;

    gpu_texture = rem_new_texture(&(WGPUTextureDescriptor){
        .label = "Anonymous shader bindgroup texture",
        .dimension = WGPUTextureDimension_2D,
        .format = texture->format,
        .size = {texture->width, texture->height, 1},
        .mipLevelCount = 1,
        .sampleCount = 1,
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
    });

    rem_write_texture(gpu_texture, texture->data, texture->size,
                      texture->channels, 0, 0, REMWriteFlag_STBIFreeData);

    new_view = rem_new_view(gpu_texture, NULL);

    shader_update_texture_view(shader, group_index, index, new_view,
                               bound_texture->format, flag);

  } else {
    logger_add(
        LoggerFlag_Warning,
        "Could not find the bound texture in group: %d, index: %d, make sure "
        "the shader is correctly initialised with all bounds (shader: %s)",
        group_index, index, shader->name);
  }

  return bound_texture;
}

ShaderBindGroupTextureEntry *
shader_update_texture_mip(Shader *shader, const bind_group_index group_index,
                          const bind_index index, const TextureMip *textures,
                          const ShaderUpdateFlag flag) {

  ShaderBindGroupTextureEntry *bound_texture =
      shader_find_texture(shader, group_index, index, NULL);

  if (bound_texture == NULL) {
    logger_add(LoggerFlag_Warning,
               "Could not find the bound texture in group: %d, index: %d, "
               "make sure the shader is correctly initialised with all bounds "
               "(shader: %s)",
               group_index, index, shader->name);
    return NULL;
  }

  WGPUTexture gpu_texture = texture_mip_upload(textures);

  // the default view (NULL) covers all mip levels
  WGPUTextureView new_view = rem_new_view(gpu_texture, NULL);

  shader_update_texture_view(shader, group_index, index, new_view,
                             bound_texture->format, flag);

  return bound_texture;
}

ShaderBindGroup *shader_update_bind_group_offset(
    Shader *shader, const bind_group_index group_index, const uint8_t index,
    const size_t offset, const ShaderUpdateFlag flag) {

  ShaderBindGroup *bind_group = &shader->bind_groups.entries[group_index];

  if (group_index > SHADER_MAX_BIND_GROUP) {
    logger_add(LoggerFlag_Warning,
               "Attempting to set shader %s bindgroup %u offset, "
               "which is beyond bindgroup capacity (%d).",
               shader->name, group_index, SHADER_MAX_BIND_GROUP);
    return bind_group;
  }

  if (index > SHADER_MAX_OFFSET_CAPACITY) {
    logger_add(LoggerFlag_Warning,
               "Attempting to set shader %s bindgroup %u offset at index %u, "
               "which is beyond offset capacity (%d).",
               shader->name, group_index, index, SHADER_MAX_OFFSET_CAPACITY);
    return bind_group;
  }

  size_t alignment = shader_device_uniform_alignment();

  ShaderBindGroupUniformEntry *uniform =
      &shader->bind_groups.entries[group_index].uniforms.entries[index];

  *uniform->dynamic_offset_entry = offset * alignment;

  return bind_group;
}
