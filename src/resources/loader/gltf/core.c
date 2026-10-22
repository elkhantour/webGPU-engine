#include "core.h"

#include "backend/job_system.h"
#include "backend/resource_manager.h"
#include "runtime/engine/add.h"
#include "runtime/engine/core.h"
#include "utils/path.h"
#include "utils/system.h"
#include <cglm/types.h>
#include <cglm/util.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend/logger.h"
#include "backend/shaders/core.h"
#include "backend/shaders/include/pbr.h"
#include "backend/std_texture/core.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/core.h"
#include "runtime/geometry/vertex/index.h"
#include "runtime/geometry/vertex/list.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/mesh/topology/base.h"
#include "runtime/mesh/transform.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/shader/core.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include "utils/name.h"
#include "utils/path.h"
#include "webgpu/webgpu.h"

#include "geometry.h"
#include "texture.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

typedef struct {
  uint16_t mesh_index;

  cgltf_mesh *gltf_mesh;
  cgltf_node *gltf_node;
  cgltf_primitive gltf_primitive;
  cgltf_material *gltf_material;

  const LoaderGLTFOptions *options;
  LoaderGLTFResult *result;
  LoaderGLTFContext *context;
  JobGroup *group;

  Engine *engine;
  Mesh *parent;

  Mesh *out_mesh;
  VertexAttribute out_vertex_attribute;
  VertexIndex out_vertex_index;
} MeshLoadJob;

// context
void loader_gltf_attempt_release(LoaderGLTFContext *ctx) {
  if (ctx->mesh_pending > 0 || ctx->texture_pending > 0)
    return;

  for (int i = 0; i < ctx->textures.count; i++) {
    dyli_free((void *)&ctx->textures.entries[i].waiters.entries,
              &ctx->textures.entries[i].waiters.capacity,
              &ctx->textures.entries[i].waiters.count);
  }

  dyli_free((void *)&ctx->textures.entries, &ctx->textures.capacity,
            &ctx->textures.count);

  cgltf_free(ctx->data);
  free(ctx);
}

static void ctx_init(LoaderGLTFContext *ctx, const char *root_path,
                     const int texture_count,
                     const LoaderGLTFOptions *options) {

  ctx->options = *options;

  file_dir(root_path, ctx->root_dir, sizeof(ctx->root_dir));

  dyli_create((void *)&ctx->textures.entries, &ctx->textures.capacity,
              &ctx->textures.count, sizeof(LoaderGLTFTexture), texture_count,
              "Loader Gltf Texture List");
  ctx->textures.count = texture_count;
}

// mesh utils
static inline LoaderGLTFStatus traverse_nodes(LoaderGLTFContext *, Engine *,
                                              const LoaderGLTFOptions *,
                                              LoaderGLTFResult *);

static inline LoaderGLTFStatus create_mesh(LoaderGLTFContext *, Engine *,
                                           cgltf_node *, Mesh *,
                                           const LoaderGLTFOptions *,
                                           LoaderGLTFResult *);

static inline void mesh_position(cgltf_node *, Mesh *);

static void mesh_load_worker_start(void *);
static void mesh_load_worker_done(void *);

// shader utils

static inline void bind_uniforms(Mesh *, cgltf_material *,
                                 const LoaderGLTFOptions *);

LoaderGLTFStatus loader_gltf_load(const GLTFLoadDescriptor *desc,
                                  LoaderGLTFResult *dest) {

  logger_add(LoggerFlag_Import, "GLTF file: %s", desc->path);

  LoaderGLTFContext *ctx = calloc(1, sizeof(LoaderGLTFContext));

  cgltf_result result;

  // load json structure
  TIMER("GLTF Parse file", {
    result = cgltf_parse_file(desc->cgltf_options, desc->path, &ctx->data);
  });

  if (ctx->data == NULL) {
    logger_add(
        LoggerFlag_Error,
        "GLTF file data unabled to be passed, make sure the file exists: %s.",
        desc->path);
    return LoaderGLTFStatus_FileUnfound;
  }

  // load actual gltf buffer data
  TIMER("GLTF Load Buffer", {
    result = cgltf_load_buffers(desc->cgltf_options, ctx->data, desc->path);
  });

  if (dest)
    *dest = (LoaderGLTFResult){0};

  switch (result) {

  case cgltf_result_invalid_json:
    logger_add(LoggerFlag_Error, "Invalid GLTF JSON.");
    return LoaderGLTFStatus_JSONInvalid;
    break;

  case cgltf_result_success:
    ctx_init(ctx, desc->path, ctx->data->images_count, desc->options);
    loader_gltf_preload_textures(ctx, desc->engine);
    return traverse_nodes(ctx, desc->engine, desc->options, dest);
    break;

  case cgltf_result_file_not_found:
    logger_add(LoggerFlag_Error, "GLTF file not found.");
    return LoaderGLTFStatus_FileUnfound;

  case cgltf_result_out_of_memory:
    logger_add(LoggerFlag_Error, "GLTF loading aborted, out of memory.");
    return LoaderGLTFStatus_OutOfBoundMemory;

  default:
    logger_add(LoggerFlag_Error, "GLTF loading aborted, unhanded error.");
    return LoaderGLTFStatus_UndefError;
  }

  return LoaderGLTFStatus_Success;
}

void mesh_load_worker_start(void *data) {

  MeshLoadJob *job = (MeshLoadJob *)data;

  cgltf_primitive primitive = job->gltf_primitive;
  cgltf_material *material = job->gltf_material;
  cgltf_mesh *gltf_mesh = job->gltf_mesh;
  cgltf_node *gltf_node = job->gltf_node;

  Mesh *mesh = job->out_mesh;
  Mesh *parent = job->parent;
  Engine *engine = job->engine;
  uint16_t mesh_index = job->mesh_index;

  VertexAttribute *vert_attr = &job->out_vertex_attribute;
  VertexIndex *vert_index = &job->out_vertex_index;

  const LoaderGLTFOptions *options = job->options;
  LoaderGLTFResult *result = job->result;

  // get accessors to decode buffers into typed data (vertex, indices...)
  // load vertex attributes

  /*
    GLTF PRIMITIVES
    primitives are vertices that belong to a same mesh but have different
    material/shader.
    In the current case we separate the primitives into mesh
    children maybe in the future we will need to create a dedicated array.

    Hierarchy:
    .-----------------------------------.
    | (parent mesh)                     |
    |  .-----------------------------.  |
    |  | primitive[0]                |  |
    |  |  .-----------------------.  |  |
    |  |  |  primitive[n > 0]     |  |  |
    |  |  '-----------------------'  |  |
    |  '-----------------------------'  |
    '-----------------------------------'

    TODO :
    For now we include primitives as child along with the other child meshes.
    But maybe in the future it could be useful to put them under a primitive
    array.
  */

  // Initialize vertex lists with 0.0:
  // need fallback values in case no color or uv coordinates
  // ensure to maintain correct standaridzed structure for shaders
  {
    const size_t vertex_count =
        primitive.attributes[0].data->count * VERTEX_STRIDE;

    primitive_vertex_lists_init(vert_attr, vertex_count);
    primitive_vertex_attribute_create(vert_attr, &primitive);
    primitive_vertex_index(vert_index, &primitive);
  }

  name_t mesh_name;
  name_copy(gltf_mesh->name, mesh_name);

  if (mesh_index > 0)
    name_compose(mesh_name, "%s.%lu", gltf_mesh->name, mesh_index);

  mesh_create(mesh, &(MeshCreateDescriptor){
                        .name = mesh_name,
                        .vertex = (VertexAttribute){0},
                        .index = (VertexIndex){0},
                    });

  mesh_topology_base_create(&mesh->topology.base, &job->out_vertex_attribute,
                            &job->out_vertex_index);

  if (parent)
    mesh_child_add(parent, mesh);

  mesh_position(gltf_node, mesh);
  job->out_mesh = mesh;

  // ==== UPDATE STATS ===
  {
    if (result) {
      result->stats.mesh_count++;
      result->stats.vertex_count +=
          mesh->topology.base.attribute.count / VERTEX_STRIDE;

      if (result->meshes.count < LOADER_GLTF_RESULT_MESH_COUNT)
        result->meshes.entries[result->meshes.count++] = mesh;
    }
  }
}

/**
 * On done, handle the GPU part (bind textures...) and add to engine scene
 */
void mesh_load_worker_done(void *data) {

  MeshLoadJob *job = (MeshLoadJob *)data;

  Mesh *mesh = job->out_mesh;
  VertexAttribute *vert_attr = &job->out_vertex_attribute;
  VertexIndex *vert_index = &job->out_vertex_index;

  Engine *engine = job->engine;

  LoaderGLTFContext *ctx = job->context;
  const LoaderGLTFOptions *options = job->options;
  cgltf_material *material = job->gltf_material;

  // define mesh vertex attribute
  mesh_topology_base_upload(&mesh->topology.base);

  // Use default pbr shader as default
  // TODO: Add a custom path for different shader in loader configuration

  RenderPipelineType pipeline_type = RenderPipelineType_PBR;

  if (material->double_sided)
    pipeline_type = RenderPipelineType_PBR_DoubleSided;

  if (material->alpha_mode == cgltf_alpha_mode_blend)
    pipeline_type = RenderPipelineType_PBR_Alpha;

  mesh_shader_create(mesh, &(ShaderCreateDescriptor){
                               .pipeline = std_render_pipeline(pipeline_type),
                               .name = material->name,
                           });

  // load and bind gltf textures
  loader_gltf_bind_mesh_textures(ctx, engine, mesh, material);
  bind_uniforms(mesh, material, options);

  engine_scene_add_mesh(engine, mesh, NULL, EngineAddFlag_None);

  if (--ctx->mesh_pending == 0)
    free(job->group);

  free(job);
  loader_gltf_attempt_release(ctx);
}

LoaderGLTFStatus traverse_nodes(LoaderGLTFContext *ctx, Engine *engine,
                                const LoaderGLTFOptions *options,
                                LoaderGLTFResult *result) {

  for (size_t i = 0; i < ctx->data->nodes_count; i++) {
    cgltf_node *node = &ctx->data->nodes[i];
    if (node->mesh)
      create_mesh(ctx, engine, node, NULL, options, result);
  }

  return LoaderGLTFStatus_Success;
}

static void
loader_gltf_accessor_to_array(const cgltf_accessor *accessor,
                              float *destination,
                              const VertexAttributeDimension dimension) {

  float *attributes = get_attributes(accessor);
  size_t index = 0;
  for (size_t a = 0; a < accessor->count; a++)
    for (uint8_t u = 0; u < dimension; u++)
      destination[index++] = attributes[a * dimension + u];
}

LoaderGLTFStatus create_mesh(LoaderGLTFContext *ctx, Engine *engine,
                             cgltf_node *gl_node, Mesh *parent,
                             const LoaderGLTFOptions *options,
                             LoaderGLTFResult *result) {

  cgltf_mesh *gl_mesh = gl_node->mesh;

  JobGroup *mesh_group = calloc(1, sizeof(JobGroup));
  job_group_init(mesh_group, 0);

  for (size_t p = 0; p < gl_mesh->primitives_count; p++) {

    cgltf_primitive current_primitive = gl_mesh->primitives[p];

    MeshLoadJob *job = calloc(1, sizeof(*job));
    {
      job->gltf_primitive = current_primitive;
      job->gltf_material = current_primitive.material;
      job->gltf_mesh = gl_mesh;
      job->gltf_node = gl_node;
      job->engine = engine;
      job->group = mesh_group;
      job->options = options;
      job->context = ctx;
      job->result = result;
      job->parent = parent;
      job->out_mesh = rem_new_mesh();
      job->mesh_index = p;
    }

    ctx->mesh_pending++;
    job_system_submit(engine->job_system, mesh_group, mesh_load_worker_start,
                      mesh_load_worker_done, job);
  }

  for (size_t i = 0; i < gl_node->children_count; i++)
    create_mesh(ctx, engine, gl_node->children[i], parent, options, result);

  return LoaderGLTFStatus_Success;
}

/**
   Read material factors and load it in the PBRUniform
 */
void bind_uniforms(Mesh *mesh, cgltf_material *material,
                   const LoaderGLTFOptions *options) {

  Shader *texture_shader = mesh_shader(mesh, MeshShader_Texture);
  Shader *reflection_shader = mesh_shader(mesh, MeshShader_Reflection);

  PBRMaterialUniform pbr = {0};

  pbr.metallic_factor = material->pbr_metallic_roughness.metallic_factor;
  pbr.roughness_factor = material->pbr_metallic_roughness.roughness_factor;
  pbr.specular_factor = material->specular.specular_factor;
  pbr.normal_scale = material->normal_texture.scale || 1.0f;
  pbr.occlusion_strength = material->occlusion_texture.scale;
  pbr.alpha_threshold = 0.9f;
  glm_vec3_copy(material->emissive_factor, pbr.emissive_factor);
  glm_vec4_copy(material->pbr_metallic_roughness.base_color_factor,
                pbr.base_color_factor);

  static const bind_index pbr_uniform_binding = 6;
  shader_update_uniform_data(texture_shader, 1, pbr_uniform_binding, &pbr,
                             ShaderUpdateFlag_None);
  shader_update_uniform_data(reflection_shader, 1, pbr_uniform_binding, &pbr,
                             ShaderUpdateFlag_None);

  {
    // DELETE ME DEBUG
    // printf("[[%s]]\n", material->name);
    // printf("\tmetallic: %f\n", pbr.metallic_factor);
    // printf("\troughness: %f\n", pbr.roughness_factor);
    // printf("\tocclusion: %f\n", pbr.occlusion_strength);
    // printf("\tspecular: %f\n", pbr.roughness_factor);
    // printf("\tnormal: %f\n", pbr.normal_scale);
    // printf("\temissive:");
    // print_vec3(pbr.emissive_factor);
    // printf("\tbase color:");
    // print_vec4(pbr.base_color_factor);
  }
}

void mesh_position(cgltf_node *node, Mesh *mesh) {

  // set translation
  if (node->has_translation)
    mesh_set_position(mesh, (vec3){
                                node->translation[0],
                                node->translation[1],
                                node->translation[2],
                            });

  // set scale
  if (node->has_scale)
    mesh_set_scale(mesh, (vec3){
                             node->scale[0],
                             node->scale[1],
                             node->scale[2],
                         });
  // set rotation
  if (node->has_rotation)
    mesh_set_rotation_quat(mesh, node->rotation);
}
