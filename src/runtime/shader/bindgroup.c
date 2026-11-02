#include "bindgroup.h"

#include <stdint.h>
#include <stdlib.h>

#include "./utils.h"
#include "add.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/std_texture/core.h"
#include "core.h"
#include "runtime/pipeline/render.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include "webgpu/webgpu.h"

static inline void shader_convert_uniforms(ShaderBindGroup *,
                                           WGPUBindGroupEntry *, bind_index *);
static inline void shader_convert_textures(ShaderBindGroup *,
                                           WGPUBindGroupEntry *, bind_index *);
static inline void shader_convert_samplers(ShaderBindGroup *,
                                           WGPUBindGroupEntry *, bind_index *);

static inline void shader_layout_print(bind_group_index group, bind_index index,
                                       const char *type);

#ifdef VERBOSE_BINDING_PHASE
void shader_layout_print(bind_group_index group, bind_index index,
                         const char *type) {
  logger_add(LoggerFlag_Print, "\t\t\t└ group: %u | binding: %u | '%s'", group,
             index, type);
}
#endif

/**
   Initialise shader bind group static and dynamic lists and eventually
   free/reset the existing ones if already existing.

    Max stack allocation easily reached with static allocation for
    Uniforms, Texture and Sampler arrays, so need to allocate them on the heap.
 */
void shader_bind_group_create(Shader *shader, bind_group_index index) {

  if (index > SHADER_MAX_BIND_GROUP) {
    logger_add(LoggerFlag_Warning, "Cannot initialize a group index > %d.",
               SHADER_MAX_BIND_GROUP);
    return;
  }

  ShaderBindGroupUniforms *uniform_group =
      &shader->bind_groups.entries[index].uniforms;

  ShaderBindGroupTextures *texture_group =
      &shader->bind_groups.entries[index].textures;

  ShaderBindGroupSamplers *sampler_group =
      &shader->bind_groups.entries[index].samplers;

  // init Uniforms array
  dyli_create((void *)&uniform_group->entries, &uniform_group->capacity,
              &uniform_group->count, sizeof(ShaderBindGroupUniformEntry),
              SHADER_UNIFORMS_DEFAULT_CAPACITY, "Shader uniform list");

  // init Texture array
  dyli_create((void *)&texture_group->entries, &texture_group->capacity,
              &texture_group->count, sizeof(ShaderBindGroupTextureEntry),
              SHADER_UNIFORMS_DEFAULT_CAPACITY, "Shader texture list");

  // init Sampler array
  dyli_create((void *)&sampler_group->entries, &sampler_group->capacity,
              &sampler_group->count, sizeof(ShaderBindGroupSamplerEntry),
              SHADER_UNIFORMS_DEFAULT_CAPACITY, "Shader sampler list");

  shader->bind_groups.count++;
}

/**
   Freeing all shader's bind groups allocation and reseting the count
 */
void shader_bind_group_clear(Shader *shader) {

  // traverse static/dynamic entries

  for (size_t b = 0; b < shader->bind_groups.count; b++) {
    ShaderBindGroup *current_group = &shader->bind_groups.entries[b];

    // reseting uniforms
    dyli_free((void *)&current_group->uniforms.entries,
              &current_group->uniforms.capacity,
              &current_group->uniforms.count);

    // reseting textures
    dyli_free((void *)&current_group->textures.entries,
              &current_group->textures.capacity,
              &current_group->textures.count);

    // reseting samplers
    dyli_free((void *)&current_group->samplers.entries,
              &current_group->samplers.capacity,
              &current_group->samplers.count);

    if (current_group->bind_group) {
      wgpuBindGroupRelease(current_group->bind_group);
      current_group->bind_group = NULL;
    }
  }

  shader->bind_groups.count = 0;
}

void shader_convert_uniforms(ShaderBindGroup *bindgroup,
                             WGPUBindGroupEntry *entries, bind_index *index) {

  // map shader bind group entry to WGPU bind group entry
  // (basically the same just without data and callback attributes)

  for (int j = 0; j < bindgroup->uniforms.count; j++) {
    ShaderBindGroupUniformEntry *current_entry =
        &bindgroup->uniforms.entries[j];
    entries[(*index)++] = (WGPUBindGroupEntry){
        .binding = current_entry->binding,
        .buffer = current_entry->buffer,
        .offset = current_entry->offset,
        .size = current_entry->size,
    };
  }
}

void shader_convert_textures(ShaderBindGroup *bindgroup,
                             WGPUBindGroupEntry *entries, bind_index *index) {

  // map shader bind group entry to WGPU bind group entry
  // (basically the same just without data and callback attributes)

  for (int j = 0; j < bindgroup->textures.count; j++) {

    ShaderBindGroupTextureEntry *current_entry =
        &bindgroup->textures.entries[j];
    entries[(*index)++] = (WGPUBindGroupEntry){
        .binding = current_entry->binding,
        .textureView = current_entry->texture_view,
    };
  }
}

void shader_convert_samplers(ShaderBindGroup *bindgroup,
                             WGPUBindGroupEntry *entries, bind_index *index) {

  for (int j = 0; j < bindgroup->samplers.count; j++) {
    ShaderBindGroupSamplerEntry *current_entry =
        &bindgroup->samplers.entries[j];
    entries[(*index)++] = (WGPUBindGroupEntry){
        .binding = current_entry->binding,
        .sampler = current_entry->sampler,
    };
  }
}

/**
   Check if a bind group in the shader isn't already registered
   if not, it creates a new bind group entry to the list
 */
ShaderBindGroup *shader_get_bind_group(Shader *shader,
                                       bind_group_index group_index) {

  // check if group within acceptable range
  if (group_index >= SHADER_MAX_BIND_GROUP) {
    logger_add(LoggerFlag_Error,
               "WebGPU is unable to create more than 4 bind groups.");
    return NULL;
  }

  // check if group index already exists
  if (shader->bind_groups.entries[group_index].textures.entries == NULL) {
    // create new bind group
    shader_bind_group_create(shader, group_index);
  }

  return &shader->bind_groups.entries[group_index];
}

/**
   Convert the ShaderBindGroup into WGPUBindGroup.
   The converted entries are then realized.
 */
WGPUBindGroupEntry *shader_bind_group_convert(ShaderBindGroup *group) {

  uint16_t total_count = shader_bind_group_entries_count(group);

  WGPUBindGroupEntry *converted_entries =
      malloc(total_count * sizeof(WGPUBindGroupEntry));

  uint16_t count = 0;
  // bind uniforms
  shader_convert_uniforms(group, converted_entries, &count);

  // bind textures
  shader_convert_textures(group, converted_entries, &count);

  // bind samplers
  shader_convert_samplers(group, converted_entries, &count);

  return converted_entries;
}

void shader_bind_group_release(ShaderBindGroup *shader_bind_group) {
  if (shader_bind_group->bind_group != NULL) {
    wgpuBindGroupRelease(shader_bind_group->bind_group);
    shader_bind_group->bind_group = NULL;
  }
}

/**
   III. Last phase of the shader build process.

   In the previous steps we:
   1. Organise the layouts.
   2. Apply the layouts in the pipeline.

   Howerver the pipeline only own a layout/ blueprint an not the content itself.
   Meaning we still have to upload the actual bindgroup content to use it in the
   shader. That's the purpose of this latest phase.

   Overall flow:
   1. Traverse all uniforms/textures and samplers from a bind group
   2. Convert entries (tex/smpl/unfrm) to generic "WGPUBindGroupEntry"
   3. Store those converted entries in an array or entry.
   4. Create bind group based on the converted entries.
 */
void shader_bind_group_build(ShaderBindGroup *group,
                             bind_group_index group_index,
                             const WGPURenderPipeline pipeline) {

  uint16_t total_count = shader_bind_group_entries_count(group);

#ifdef VERBOSE_BINDING_PHASE
  logger_add(LoggerFlag_Print,
             "\t\t\t\t└ Uniforms: %lu\n\t\t\t\t└ Textures: "
             "%lu\n\t\t\t\t└ Samplers: %lu",
             group->uniforms.count, group->textures.count,
             group->samplers.count);
#endif

  // convert shader bind group to WGPU bind group
  WGPUBindGroupEntry *converted_entries = shader_bind_group_convert(group);

  // realize bind group
  group->bind_group = wgpuDeviceCreateBindGroup(
      context_device(),
      &(WGPUBindGroupDescriptor){
          .layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, group_index),
          .entryCount = total_count,
          .entries = converted_entries,
      });

  // release layouts
  free(converted_entries);
  converted_entries = NULL;
  // TODO: Clear layouts on mesh destruction
  // WGPUBindGroupLayout *current_layout = &layouts[i];
  // wgpuBindGroupLayoutRelease(*current_layout);
}

/**
   Basically unmount and remount the shader bind group.
   Function mostly used during the uniforms/texture/sampler updates.
 */
void shader_bind_group_refresh(ShaderBindGroup *group,
                               bind_group_index group_index,
                               const WGPURenderPipeline pipeline) {
#ifdef VERBOSE_BINDING_PHASE
  logger_add(LoggerFlag_Print, "\t\t\t(refresh)");
#endif

  shader_bind_group_release(group);
  shader_bind_group_build(group, group_index, pipeline);
}

/**
   Create empty bind groups for the shader depending on its pipeline layout.
   The function matches the shader pieline from the std pipelines and generate
   the bindgroups with empty values but with the right size.

   The function is called at the shader creation step, ensuring the right
   bindgroups are created. Once the shader is created uniforms and textures can
   be updated via update functions.
 */
void shader_bind_group_create_from_layout(
    Shader *shader, const RenderPipelineStateObject *layout) {

#ifdef VERBOSE_BINDING_PHASE
  logger_add(LoggerFlag_Print,
             "\t\t└ Initialize bindgroups from PSO with default values:");
#endif

  // traverse group
  for (size_t i = 0; i < layout->bind_groups_count; i++) {

    for (size_t j = 0; j < layout->bind_groups[i]->entryCount; j++) {
      const WGPUBindGroupLayoutEntry *entry =
          &layout->bind_groups[i]->entries[j];

      // Use discriminator to define entry type

      // generate uniform/ storage
      if (entry->buffer.type != WGPUBufferBindingType_Undefined) {
#ifdef VERBOSE_BINDING_PHASE
        shader_layout_print(i, entry->binding, "uniform");
#endif

        shader_add_uniform(
            shader,
            &(ShaderCreateUniformDescriptor){
                .entry_count = 1,
                .visibility = entry->visibility,
                .group_index = i,
                .entries =
                    (ShaderBindGroupUniformEntryDescriptor[]){
                        {
                            .hasDynamicOffset = entry->buffer.hasDynamicOffset,
                            .usage =
                                (entry->buffer.type ==
                                         WGPUBufferBindingType_ReadOnlyStorage
                                     ? WGPUBufferUsage_Storage
                                     : WGPUBufferUsage_Uniform) |
                                WGPUBufferUsage_CopyDst,
                            .binding = entry->binding,
                            .size = entry->buffer.minBindingSize,
                            .offset = 0,
                            .data = (void *)0, // empty data
                        },
                    },
            });
      }
      // generate float texture
      if (entry->texture.sampleType == WGPUTextureSampleType_Float) {

        // 2D Texture
        if (entry->texture.viewDimension == WGPUTextureViewDimension_2D) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "2D texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = WGPUTextureFormat_R8Unorm,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_Float),
                                  },
                              },
                      });
        }

        // 2D Array
        if (entry->texture.viewDimension == WGPUTextureViewDimension_2DArray) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "2D texture array");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = WGPUTextureFormat_R8Unorm,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_Float2DArray),
                                  },
                              },
                      });
        }

        // Cube
        if (entry->texture.viewDimension == WGPUTextureViewDimension_Cube) {
#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "2D float cube texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = WGPUTextureFormat_R8Unorm,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_FloatCube),
                                  },
                              },
                      });
        }

        // Cube Array
        if (entry->texture.viewDimension ==
            WGPUTextureViewDimension_CubeArray) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "2D float cube array texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = WGPUTextureFormat_R8Unorm,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_FloatCubeArray),
                                  },
                              },
                      });
        }
      }

      // generate depth texture
      if (entry->texture.sampleType == WGPUTextureSampleType_Depth) {

        // Depth 2D
        if (entry->texture.viewDimension == WGPUTextureViewDimension_2D) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "2d depth texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = TEXTURE_FORMAT_DEPTH,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_Depth),
                                  },
                              },
                      });
        }

        // Depth Cube
        if (entry->texture.viewDimension ==
            WGPUTextureViewDimension_CubeArray) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "cube depth texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = TEXTURE_FORMAT_DEPTH,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_DepthCubeArray),
                                  },
                              },
                      });
        }

        // Depth Array
        if (entry->texture.viewDimension == WGPUTextureViewDimension_2DArray) {

#ifdef VERBOSE_BINDING_PHASE
          shader_layout_print(i, entry->binding, "array depth texture");
#endif
          shader_add_texture_view(
              shader, &(ShaderCreateTextureViewDescriptor){
                          .entry_count = 1,
                          .visibility = entry->visibility,
                          .group_index = i,
                          .entries =
                              (ShaderBindGroupTextureViewEntryDescriptor[]){
                                  {
                                      .binding = entry->binding,
                                      .dimension = entry->texture.viewDimension,
                                      .sample_type = entry->texture.sampleType,
                                      .format = TEXTURE_FORMAT_DEPTH,
                                      // use fallback texture as  placeholder
                                      .texture_view = std_texture_view(
                                          TextureViewType_Depth2DArray),
                                  },
                              },
                      });
        }
      }

      // generate filtering sampler
      if (entry->sampler.type == WGPUSamplerBindingType_Filtering) {
#ifdef VERBOSE_BINDING_PHASE
        shader_layout_print(i, entry->binding, "filtering sampler");
#endif
        shader_add_sampler(
            shader, &(ShaderCreateSamplerDescriptor){
                        .entry_count = 1,
                        .visibility = entry->visibility,
                        .group_index = i,
                        .entries =
                            (ShaderBindGroupSamplerEntryDescriptor[]){
                                {
                                    .binding = entry->binding,
                                    .type = entry->sampler.type,
                                    .compare = WGPUCompareFunction_Undefined,
                                    .minFilter = WGPUFilterMode_Linear,
                                    .magFilter = WGPUFilterMode_Linear,
                                    .mipMapFilter = WGPUMipmapFilterMode_Linear,
                                    .addressModeU = WGPUAddressMode_Repeat,
                                    .addressModeV = WGPUAddressMode_Repeat,
                                    .addressModeW = WGPUAddressMode_Repeat,
                                },
                            },
                    });
      }

      // generate comparison sampler
      if (entry->sampler.type == WGPUSamplerBindingType_Comparison) {
#ifdef VERBOSE_BINDING_PHASE
        shader_layout_print(i, entry->binding, "filtering sampler");
#endif
        shader_add_sampler(
            shader, &(ShaderCreateSamplerDescriptor){
                        .entry_count = 1,
                        .visibility = entry->visibility,
                        .group_index = i,
                        .entries =
                            (ShaderBindGroupSamplerEntryDescriptor[]){
                                {
                                    .binding = entry->binding,
                                    .type = entry->sampler.type,
                                    // default compare function
                                    .compare = WGPUCompareFunction_LessEqual,
                                },
                            },
                    });
      }
    }
  }
}
