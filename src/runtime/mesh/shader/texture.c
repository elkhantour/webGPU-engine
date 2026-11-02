#include "texture.h"

#include <stddef.h>
#include <stdint.h>

#include "backend/logger.h"
#include "backend/ubo.h"
#include "core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/uniform.h"
#include "runtime/pipeline/render.h"
#include "runtime/probe/reflection/core.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/shader/core.h"
#include "backend/shadow_map/core.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "webgpu/webgpu.h"

/**
   Clear the texture shader bind groups of mesh
 */
void mesh_shader_texture_clear_bindings(Mesh *mesh) {
  shader_bind_group_clear(mesh_shader(mesh, MeshShader_Texture));
}

/**
   Init light list uniforms
   due to WGSL array uniforms necessity to have constant size, uniforms lists
   are already set at <light_list>[12], init them all to 0
   by default we will upload all the lights (point, ambient, spot)
   within a defined group
  */
void mesh_shader_texture_update_lights(Mesh *mesh, const MeshShader shader_type,
                                       UBOManager *ubo) {

  Shader *shader = mesh_shader(mesh, shader_type);
  shader_update_uniform_buffer(shader,
                               shader_pipeline(shader)->bindings.light_list->group,
                               shader_pipeline(shader)->bindings.light_list->list,
                               ubo_buffer_handle(ubo, UBOType_LightList), 0,
                               ShaderUpdateFlag_ReleasePrevious);
}

/**
   Bind the shadow maps and sampler to the default shader (called during shader
   creation)
 */
void mesh_shader_texture_update_shadow_maps(Mesh *mesh,
                                            WGPUTextureView point_texture_view,
                                            WGPUTextureView spot_texture_view) {

  Shader *shader = mesh_shader(mesh, MeshShader_Texture);
  const RenderPipelineBinding *bindings = &shader_pipeline(shader)->bindings;

  // create texture views
#ifdef RENDER_SHADOW_AS_COLOR
  const WGPUTextureFormat texture_format = SHADOW_COLOR_FORMAT;
  const WGPUTextureSampleType texture_sample_type = WGPUTextureSampleType_Float;
  const WGPUSamplerBindingType sample_type = WGPUSamplerBindingType_Filtering;
  const WGPUCompareFunction sample_compare = WGPUCompareFunction_Undefined;
#else
  const WGPUTextureFormat texture_format = SHADOW_DEPTH_FORMAT;
  const WGPUTextureSampleType texture_sample_type = WGPUTextureSampleType_Depth;
  const WGPUSamplerBindingType sample_type = WGPUSamplerBindingType_Comparison;
  const WGPUCompareFunction sample_compare = WGPUCompareFunction_Less;
#endif

  // add multi-layered texture to default shader
  shader_update_texture_view(
      shader, bindings->light_list->group, bindings->light_list->point_texture,
      point_texture_view, texture_format, ShaderUpdateFlag_KeepPrevious);

  shader_update_texture_view(shader, bindings->light_list->group,
                             bindings->light_list->directional_texture,
                             spot_texture_view, texture_format,
                             ShaderUpdateFlag_KeepPrevious);
}

void mesh_shader_texture_update_probes(Mesh *mesh,
                                       WGPUTextureView plane_texture,
                                       WGPUTextureView grid_texture,
                                       UBOManager *ubo) {

  Shader *shader = mesh_shader(mesh, MeshShader_Texture);
  const RenderPipelineBinding *bindings = &shader_pipeline(shader)->bindings;

  // update plane texture
  if (bindings->probe->reflection_plane_texture != PIPELINE_BINDING_UNDEFINED)
    shader_update_texture_view(shader, bindings->probe->group,
                               bindings->probe->reflection_plane_texture,
                               plane_texture, TEXTURE_FORMAT_OFFSCREEN,
                               ShaderUpdateFlag_ReleasePrevious);

  // update grid texture
  if (bindings->probe->reflection_grid_texture != PIPELINE_BINDING_UNDEFINED)
    shader_update_texture_view(shader, bindings->probe->group,
                               bindings->probe->reflection_grid_texture,
                               grid_texture, TEXTURE_FORMAT_OFFSCREEN,
                               ShaderUpdateFlag_ReleasePrevious);
}

void mesh_shader_texture_update_environment(Mesh *mesh,
                                            WGPUTextureView skybox_texture,
                                            SceneEnvironmentUniform *scene_env,
                                            UBOManager *ubo) {

  Shader *shader = mesh_shader(mesh, MeshShader_Texture);
  const RenderPipelineBinding *bindings = &shader_pipeline(shader)->bindings;

  // update skybox texture
  if (bindings->probe->skybox_texture != PIPELINE_BINDING_UNDEFINED)
    shader_update_texture_view(shader, bindings->probe->group,
                               bindings->probe->skybox_texture, skybox_texture,
                               TEXTURE_FORMAT_OFFSCREEN,
                               ShaderUpdateFlag_ReleasePrevious);

  // udpate scene environment (fog...)
  if (bindings->environment->environment != PIPELINE_BINDING_UNDEFINED)
    shader_update_uniform_data(shader, bindings->environment->group,
                               bindings->environment->environment,
                               (void *)scene_env, ShaderUpdateFlag_None);
}

/**
   Link one mesh texture to a reflection probe.
 */
void mesh_shader_texture_bind_probe(Mesh *mesh, ProbeReflectionPlane *plane,
                                    UBOManager *ubo) {

  Shader *shader = mesh_shader(mesh, MeshShader_Texture);
  const RenderPipeline *pipeline = shader_pipeline(shader);

  shader_update_uniform_buffer(shader, pipeline->bindings.probe->group,
                               pipeline->bindings.probe->list,
                               ubo_buffer_handle(ubo, UBOType_ProbeList), 0,
                               ShaderUpdateFlag_ReleasePrevious);

  {
    MeshUniform *uniform = mesh_uniform(mesh);
    uniform->probe_reflection_plane_count = plane->ubo_uniform.id;
    ubo_update_queue_insert(ubo, UBOType_Mesh, mesh->ubo_slot.id);
  }

  // prevent self reflection by removing the mesh from the plane draw list
  probe_reflection_plane_disable_mesh(plane, mesh);
}
