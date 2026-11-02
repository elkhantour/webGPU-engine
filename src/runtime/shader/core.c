#include "core.h"

#include <stddef.h>
#include <stdlib.h>
#include <webgpu/webgpu.h>

#include "backend/context.h"
#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "bindgroup.h"
#include "runtime/pipeline/render.h"
#include "string.h"
#include "utils/name.h"

/*

  !! DEPRECATED NEED UDPATE !!
  OVERALL BUILDING PROCESS:
  1. First build layouts for uniforms, textures + samplers
  2. Build pipeline based on those layouts
  3. Actually bind the buffer/textures view and samplers

                  .------------------.
                  |   Add Buffer     |
                  |------------------|
                  | Add Uniforms     |
                  | Add Textures     |
                  | Add Samplers     |
                  '------------------'
                           ||
                  .------------------.
                  |   Build Layout   |
                  |------------------|
                  | Layout Uniforms  |
                  | Layout Textures  |
                  | Layout Samplers  |
                  '------------------'
                           ||
                  .------------------.
                  |  Build Pipeline  |
                  '------------------'
                           ||
                  .------------------.
                  |       Bind       |
                  |------------------|
                  | Bind Uniforms    |
                  | Bind Textures    |
                  | Bind Samplers    |
                  '------------------'

 */

void shader_create(Shader *shader, const ShaderCreateDescriptor *sd) {

  shader->id = reg_register(shader, RegEntryType_Shader);
  name_copy(sd->name, shader->name);

#ifdef VERBOSE_CREATING_PHASE
  logger_add(LoggerFlag_ShaderCreate, "%s", shader->name);
#endif

  shader->pipeline = sd->pipeline;

  // define bind groups count
  shader->bind_groups.count = 0;

  // generate empty bindgroups based on pipeline layout (CPU Side)
  shader_bind_group_create_from_layout(shader, (*shader->pipeline)->shader_pso);

  // create gpu bindgroups from generated layout (GPU side)
  shader_build(shader);
}

void shader_destroy(Shader *shader) {

  // clearing bind groups
  shader_bind_group_clear(shader);
}

const RenderPipeline *shader_pipeline(Shader *shader) {
  return (*shader->pipeline);
}

/**
   Build pipeline based on previously set bind groups.

 */
void shader_build(Shader *shader) {

  // clear pipeline if existing
#ifdef VERBOSE_BINDING_PHASE
  logger_add(LoggerFlag_Print, "\t\t└ Binding Shader: %s", shader->name);
#endif

  // build bind group entries for each individual group index

  // Create Shader GPUBindGroup for each bindgroups
  for (int i = 0; i < shader->bind_groups.count; i++) {

    ShaderBindGroup *group = &shader->bind_groups.entries[i];

    // check if bind group is not already built
    // necessary cause in the wireframe mode we basically already built the
    // wireframe shader a first time for the boundbox but then a second time for
    // the wireframe topology, so we need to make sure it's not already built.
    // If it build a second time it actually accumulate new entries to the bind
    // group (goes from 3 to 6 entries) which lead to an error since the pieline
    // expect 3 entries.
    if (group->bind_group != NULL)
      continue;

#ifdef VERBOSE_BINDING_PHASE
    logger_add(LoggerFlag_Print, "\t\t\t└ Bingroup: %d", i);
#endif
    shader_bind_group_build(group, i, (*shader->pipeline)->handle);
  }

  // TODO: properly release pipeline when deleting mesh
  // shader_pipeline_release_layout(shader);
  // free(bindgroup_layouts);
}
