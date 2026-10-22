#include "skybox.h"

#include "backend/compute/core.h"
#include "backend/compute/mipmap.h"
#include "backend/context.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "runtime/engine/add.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/primitive/core.h"
#include "runtime/primitive/cube.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/environment/core.h"
#include "runtime/shader/core.h"
#include "runtime/shader/update.h"
#include "runtime/systems/scene_system.h"
#include "runtime/texture/core.h"
#include "stb/stb_image.h"
#include "webgpu/webgpu.h"
#include <stdint.h>

static inline WGPUTexture prefab_skybox_texture(const size_t);

static inline void prefab_skybox_upload_layer(const WGPUTexture,
                                              const Texture *, const size_t,
                                              const bool);

static inline void prefab_skybox_create_from_texture(Engine *,
                                                     const WGPUTexture,
                                                     WGPUTextureView *,
                                                     const size_t, const mip_t);

/**
   Upload the skybox side to the gpu (write to texture at given layer index)
 */
static inline void prefab_skybox_upload_layer(const WGPUTexture texture,
                                              const Texture *layer_texture,
                                              const size_t layer_index,
                                              const bool free) {

  rem_write_texture(texture, layer_texture->data, layer_texture->size,
                    layer_texture->channels, layer_index, 0,
                    REMWriteFlag_STBIFreeData);
}

/**
  Create texture & global texture view
 */
WGPUTexture prefab_skybox_texture(const size_t resolution) {

  return rem_new_texture(&(WGPUTextureDescriptor){
      .dimension = WGPUTextureDimension_2D,
      .format = TEXTURE_FORMAT_OFFSCREEN,
      .usage = WGPUTextureUsage_TextureBinding   // read texturen in shader
               | WGPUTextureUsage_StorageBinding // write texture in shader
               | WGPUTextureUsage_CopyDst,       // upload the input data
      .sampleCount = 1,
      .mipLevelCount = mipmap_count(resolution, resolution),
      .size =
          (WGPUExtent3D){
              .width = resolution,
              .height = resolution,
              .depthOrArrayLayers = TEXTURE_CUBE_LAYER,
          },
  });
}

/**
   Create global view from previously generated cubemap texture and create mesh
 */
void prefab_skybox_create_from_texture(Engine *engine,
                                       const WGPUTexture texture,
                                       WGPUTextureView *view,
                                       const size_t resolution,
                                       const mip_t blur) {

  Renderer *renderer = engine_get_renderer(engine);
  Scene *scene = engine_get_active_scene(engine);

  ComputePass mipmap;
  compute_pass_mipmap_create(&mipmap, &(ComputePassDescriptor){
                                          .label = "Skybox Mipmap",
                                          .source_texture = texture,
                                      });

  // mipmap generated texture
  compute_pass_mipmap_draw(&mipmap);
  compute_pass_destroy(&mipmap);

  // create global texture view
  const mip_t mip_count = mipmap_count(resolution, resolution);

  if (blur > mip_count)
    logger_add(LoggerFlag_Warning,
               "Attempting to set a skybox blur factor (%u) superior to "
               "the available Mip count (%u)",
               blur, mip_count);

  /* TODO:

  Looks like the textureSampleLevel is broken on Intel Mac:
   - https://issues.chromium.org/issues/372283570
   - https://github.com/gpuweb/gpuweb/issues/4818

  So we basically only make 1 mipmap view available to the shader.
  Maybe check in the future if this solution has been resolved.

   */

  *view = rem_new_view(texture, &(WGPUTextureViewDescriptor){
                                    .dimension = WGPUTextureViewDimension_Cube,
                                    .format = TEXTURE_FORMAT_OFFSCREEN,
                                    .arrayLayerCount = TEXTURE_CUBE_LAYER,
                                    .baseArrayLayer = 0,
                                    .mipLevelCount = 1,
                                    .baseMipLevel = glm_min(blur, mip_count),
                                });

  /* TODO OPTI:
     Currently use default box primitive which include
     normal/uv/color, but we actually only need position for the skybox, so
     maybe can use a "position-only" version to save a bit of memory
   */
  Primitive box_primitive = primitive_cube();
  Mesh *skybox_mesh = rem_new_mesh();
  mesh_create_primitive(skybox_mesh, &(MeshCreatePrimitiveDescriptor){
                                         .name = "skybox mesh",
                                         .primitive = &box_primitive,
                                     });

  // assign shader
  mesh_shader_create(skybox_mesh, &(ShaderCreateDescriptor){
                                      .name = "skybox shader",
                                      .pipeline = std_render_pipeline(
                                          RenderPipelineType_Skybox),
                                  });

  // update texture and sampler
  Shader *shader = mesh_shader(skybox_mesh, MeshShader_Texture);
  shader_update_texture_view(shader, 1, 0, *view, TEXTURE_FORMAT_OFFSCREEN,
                             ShaderUpdateFlag_ReleasePrevious);

  // add blur uniform
  shader_update_uniform_data(shader, 1, 2, (void *)&blur,
                             ShaderUpdateFlag_None);

  // alter pipeline (no depth test)
  const RenderPipeline *pipeline = shader_pipeline(shader);

  engine_scene_add_mesh(engine, skybox_mesh, NULL,
                        EngineAddFlag_Unselectable | EngineAddFlag_TreeHide);
}

/**
   Create a skybox from a list of 6 textures.
 */
void prefab_skybox_create(Engine *engine,
                          const PrefabSkyboxCreateDescriptor *desc) {

  Scene *scene = engine_get_active_scene(engine);
  Renderer *renderer = engine_get_renderer(engine);

  // create global texture
  SceneEnvironmentSkybox *scene_skybox =
      scene_environment_skybox(&scene->environment);

  WGPUTexture *skybox_texture = &scene_skybox->texture;
  WGPUTextureView *skybox_cubemap_view = &scene_skybox->view;

  *skybox_texture = prefab_skybox_texture(desc->resolution);

  Texture skybox_sides[TEXTURE_CUBE_LAYER];
  texture_create_cubemap_from_file(skybox_sides,
                                   &(TextureCreateCubeMapDescriptor){
                                       .path = &desc->path,
                                       .format = TEXTURE_FORMAT_OFFSCREEN,
                                       .resolution = desc->resolution,
                                   });

  for (uint8_t i = 0; i < TEXTURE_CUBE_LAYER; i++)
    prefab_skybox_upload_layer(*skybox_texture, &skybox_sides[i], i, false);

  prefab_skybox_create_from_texture(engine, *skybox_texture,
                                    skybox_cubemap_view, desc->resolution,
                                    desc->blur);
}

/**
   Create a gradient skybox from a list of gradient stops.
 */
void prefab_skybox_gradient_create(
    Engine *engine, const PrefabSkyboxGradientCreateDescriptor *desc) {

  SceneEnvironmentSkybox *scene_skybox =
      scene_environment_skybox(&engine_get_active_scene(engine)->environment);

  WGPUTexture *skybox_texture = &scene_skybox->texture;
  WGPUTextureView *skybox_cubemap_view = &scene_skybox->view;

  // create global texture
  *skybox_texture = prefab_skybox_texture(desc->resolution);

  // define stops start and end (i.e. top and bottom color)
  const TextureGradient *grad = &desc->stops;
  TextureGradientStop *start = &grad->entries[0];
  TextureGradientStop *end = &grad->entries[desc->stops.count - 1];

  // define end/ start based on stops position
  for (size_t i = 0; i < grad->count; i++) {
    TextureGradientStop *stop = &grad->entries[0];
    if (start->position < stop->position)
      start = stop;

    if (end->position > stop->position)
      end = stop;
  }

  // precompute gradient sides to be reused for each cube sides
  Texture gradient_texture;
  texture_create(&gradient_texture, &(TextureCreateDescriptor){
                                        .channels = 4,
                                        .height = desc->resolution,
                                        .width = desc->resolution,
                                        .value = end->color,
                                    });

  // create gradient
  texture_write_gradient(&gradient_texture, &desc->stops,
                         TextureWriteMethod_Replace);

  /* create layers, order:
     0 - right
     1 - left
     2 - top
     3 - bottom
     4 - front
     5 - back
   */
  for (size_t i = 0; i < TEXTURE_CUBE_LAYER; i++) {

    Texture layer_texture;
    Texture *final_texture;
    bool free_texture = true;

    switch (i) {

    // top
    case 2:
      texture_create(&layer_texture, &(TextureCreateDescriptor){
                                         .channels = 4,
                                         .height = desc->resolution,
                                         .width = desc->resolution,
                                         .value = start->color,
                                     });
      final_texture = &layer_texture;
      break;

    // bottom
    case 3:
      texture_create(&layer_texture, &(TextureCreateDescriptor){
                                         .channels = 4,
                                         .height = desc->resolution,
                                         .width = desc->resolution,
                                         .value = end->color,
                                     });
      final_texture = &layer_texture;
      break;

    // sides
    default:
      final_texture = &gradient_texture;
      free_texture = false;
    }

    // upload texture
    prefab_skybox_upload_layer(*skybox_texture, final_texture, i, free_texture);
  }

  // free gradient texture
  stbi_image_free(gradient_texture.data);
  gradient_texture.data = NULL;

  prefab_skybox_create_from_texture(
      engine, *skybox_texture, skybox_cubemap_view, desc->resolution, 0.0f);
}
