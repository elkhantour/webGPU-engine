#include "loader.gltf.h"

#include "backend/job_system.h"
#include "backend/resource_manager.h"
#include "runtime/engine/add.h"
#include "runtime/systems/scene_system.h"
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
#include "webgpu/webgpu.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

typedef struct {
  cgltf_data *data;
  char root_dir[1024];
  int pending;
} LoaderGLTFContext;

typedef struct {
  uint16_t mesh_index;

  cgltf_mesh *gltf_mesh;
  cgltf_node *gltf_node;
  cgltf_primitive gltf_primitive;
  cgltf_material *gltf_material;

  const LoaderGLTFOptions *options;
  LoaderGLTFResult *result;
  LoaderGLTFContext *context;

  Engine *engine;
  Mesh *parent;

  Mesh *out_mesh;
  VertexAttribute out_vertex_attribute;
  VertexIndex out_vertex_index;
} MeshLoadJob;

// gltf utils
static float *get_attributes(const cgltf_accessor *);

static inline void set_attribute_fallback(float *, const float *,
                                          const VertexAttributeDimension);
static inline bool attribute_is_empty(const float *,
                                      const VertexAttributeDimension);

// context
static void ctx_release(LoaderGLTFContext *ctx) {
  if (--ctx->pending > 0)
    return;

  cgltf_free(ctx->data);
  free(ctx);
}

// vertex buffer utils
static void primitive_vertex_index(VertexIndex *, cgltf_primitive *);

static inline void primitive_vertex_attribute_create(VertexAttribute *,
                                                     cgltf_primitive *);
static inline void primitive_vertex_lists_init(VertexAttribute *, size_t);

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
static inline void bind_textures(Mesh *, cgltf_material *, const char *,
                                 const LoaderGLTFOptions *);

static inline void bind_uniforms(Mesh *, cgltf_material *,
                                 const LoaderGLTFOptions *);

static LoaderGLTFStatus extract_texture(cgltf_texture_view *, void **, size_t *,
                                        int *, int *, int *, TextureResolution,
                                        const char *, const char *);

LoaderGLTFStatus loader_gltf_load(const GLTFLoadDescriptor *desc,
                                  LoaderGLTFResult *dest) {

  logger_add(LoggerFlag_Import, "GLTF file: %s", desc->path);

  LoaderGLTFContext *ctx = calloc(1, sizeof(LoaderGLTFContext));
  file_dir(desc->path, ctx->root_dir, sizeof(ctx->root_dir));
  
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

void set_attribute_fallback(float *attr, const float *fallback,
                            const VertexAttributeDimension count) {
  for (VertexAttributeDimension i = 0; i < count; i++)
    attr[i] = fallback[i];
}

bool attribute_is_empty(const float *attr,
                        const VertexAttributeDimension count) {

  for (VertexAttributeDimension i = 0; i < count; i++)
    if (attr[i])
      return false;

  return true;
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
  bind_textures(mesh, material, ctx->root_dir, options); // TODO: MOVE TO ASYNC
  bind_uniforms(mesh, material, options);

  engine_scene_add_mesh(engine, mesh, NULL, EngineAddFlag_None);

  free(data);
  ctx_release(ctx);
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

float *get_attributes(const cgltf_accessor *accessor) {

  cgltf_buffer_view *buffer_view = accessor->buffer_view;

  // combine accessor and buffer view offset
  size_t offset = buffer_view->offset + accessor->offset;
  return (float *)((uint8_t *)buffer_view->buffer->data + offset);
}

void primitive_vertex_lists_init(VertexAttribute *attributes, size_t count) {
  attributes->count = count;
  attributes->capacity = attributes->count;
  attributes->entries =
      (vattr_t *)calloc(attributes->capacity, sizeof(vattr_t));
}

/**
   Access vertex data from the file and interleave them within the referenced
   vertex attribute. Note that initially in GLTF format, all the vertex
   attributes are packed by attributes, meaning we have: [p1, p2, p3, p4, p5]
   [n1, n1, n3, n4, n5]
   etc.
 */
static inline void
primitive_vertex_attribute_create(VertexAttribute *vert_attr,
                                  cgltf_primitive *primitive) {

  const struct {
    const cgltf_attribute_type gltf_attribute;
    const VertexAttributeType type;
    const VertexAttributeDimension dimension;
    const VertexAttributeOffset offset;
    const vattr_t (*fallback)[4];
  } type_vertex_map[VERTEX_ATTRIBUTE_COUNT] = {
      {
          .gltf_attribute = cgltf_attribute_type_position,
          .type = VertexAttributeType_Position,
          .dimension = VertexAttributeDimension_Position,
          .offset = VertexAttributeOffset_Position,
      },
      {
          .gltf_attribute = cgltf_attribute_type_normal,
          .type = VertexAttributeType_Normal,
          .dimension = VertexAttributeDimension_Normal,
          .offset = VertexAttributeOffset_Normal,
          .fallback = &(vattr_t[4]){0.0f, 1.0f, 0.0f},
      },
      {
          .gltf_attribute = cgltf_attribute_type_tangent,
          .type = VertexAttributeType_Tangent,
          .dimension = VertexAttributeDimension_Tangent,
          .offset = VertexAttributeOffset_Tangent,
          .fallback = &(vattr_t[4]){-1.0f, 0.0f, 0.0f, -1.0f},
      },
      {
          .gltf_attribute = cgltf_attribute_type_color,
          .type = VertexAttributeType_Color,
          .dimension = VertexAttributeDimension_Color,
          .offset = VertexAttributeOffset_Color,
      },
      {
          .gltf_attribute = cgltf_attribute_type_texcoord,
          .type = VertexAttributeType_Uv,
          .dimension = VertexAttributeDimension_Uv,
          .offset = VertexAttributeOffset_Uv,
      },
  };

  // traverse and check if gltf attribute match
  for (size_t j = 0; j < primitive->attributes_count; j++) {

    const cgltf_attribute *attribute = &primitive->attributes[j];
    const cgltf_accessor *accessor = attribute->data;
    const cgltf_attribute_type gltf_type = attribute->type;

    float *data = get_attributes(accessor);

    for (size_t i = 0; i < VERTEX_ATTRIBUTE_COUNT; i++) {

      size_t index = 0;
      const VertexAttributeDimension dimension = type_vertex_map[i].dimension;
      const VertexAttributeOffset offset = type_vertex_map[i].offset;
      const cgltf_attribute_type gltf_attr = type_vertex_map[i].gltf_attribute;

      if (primitive->attributes[j].type == gltf_attr) {

        for (size_t k = 0; k < accessor->count * dimension; k += dimension) {
          memcpy(&vert_attr->entries[index + offset], &data[k],
                 sizeof(vattr_t) * dimension);

          index += VERTEX_STRIDE;
        }
      }
    }
  }
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

void primitive_vertex_index(VertexIndex *vert_index, cgltf_primitive *source) {

  // index
  cgltf_accessor *index_accessor = source->indices;
  cgltf_buffer_view *index_buffer_view = index_accessor->buffer_view;
  size_t index_offset = index_buffer_view->offset + index_accessor->offset;
  size_t index_count = index_accessor->count;
  uint16_t *raw_index_data =
      (uint16_t *)((uint8_t *)index_buffer_view->buffer->data + index_offset);

  // gltf provide index a uint16, however the engine sur uint32, need to
  // manually cast it
  // alloc on heap cause VLA throws stack overflow
  vindex_t *index_data = malloc(sizeof(vindex_t) * index_count);
  for (size_t i = 0; i < index_count; i++)
    index_data[i] = (vindex_t)raw_index_data[i];

  *vert_index = (VertexIndex){
      .entries = index_data,
      .capacity = index_count,
      .count = index_count,
  };
}

LoaderGLTFStatus create_mesh(LoaderGLTFContext *ctx, Engine *engine,
                             cgltf_node *gl_node, Mesh *parent,
                             const LoaderGLTFOptions *options,
                             LoaderGLTFResult *result) {

  cgltf_mesh *gl_mesh = gl_node->mesh;
  for (size_t p = 0; p < gl_mesh->primitives_count; p++) {

    cgltf_primitive current_primitive = gl_mesh->primitives[p];

    MeshLoadJob *job = calloc(1, sizeof(*job));
    {
      job->gltf_primitive = current_primitive;
      job->gltf_material = current_primitive.material;
      job->gltf_mesh = gl_mesh;
      job->gltf_node = gl_node;
      job->engine = engine;
      job->options = options;
      job->context = ctx;
      job->result = result;
      job->parent = parent;
      job->out_mesh = rem_new_mesh();
      job->mesh_index = p;
    }

    ctx->pending++;
    job_system_submit(engine->job_system, mesh_load_worker_start,
                      mesh_load_worker_done, job);
  }

  for (size_t i = 0; i < gl_node->children_count; i++)
    create_mesh(ctx, engine, gl_node->children[i], parent, options, result);

  return LoaderGLTFStatus_Success;
}

/**
  Bind PBR textures
  store the texture_views (hold pointer to actual texture + other data)
 */
void bind_textures(Mesh *mesh, cgltf_material *material, const char *root_dir,
                   const LoaderGLTFOptions *options) {

  const uint8_t texture_count = 5;

  // TODO: check how to handle if object already has a AO Texture imported ?
  // overwrite ?
  //&material->occlusion_texture : baked separately in the AO pass,
  const struct {
    const char *label;
    cgltf_texture_view *gltf_texture;
    const WGPUTextureView fallback_texture;
  } texture_view_list[] = {
      {
          "Base",
          &material->pbr_metallic_roughness.base_color_texture,
          std_texture_view(TextureViewType_Float),
      },
      {
          "Metallic Roughness",
          &material->pbr_metallic_roughness.metallic_roughness_texture,
          std_texture_view(TextureViewType_Float),
      },
      {
          "Normal",
          &material->normal_texture,
          std_texture_view(TextureViewType_FloatNormal),
      },
      {
          "Emissive",
          &material->emissive_texture,
          std_texture_view(TextureViewType_Float),
      },
      {
          "Occlusion",
          &material->occlusion_texture,
          std_texture_view(TextureViewType_Float),
      },
  };

  uint8_t binding = 0;

  // get the cached fallback texture view from renderer
  const WGPUTextureView fallback_texture =
      std_texture_view(TextureViewType_Float);

  for (int t = 0; t < texture_count; t++) {

    void *data;
    size_t size;
    int width, height, channels;

    // If find texture, upload new texture to GPU and bind to shader
    //(before freeing it)
    ShaderBindGroupTextureEntry *reflection_texture;

    const LoaderGLTFStatus extracted_texture =
        extract_texture(texture_view_list[t].gltf_texture, &data, &size, &width,
                        &height, &channels, options->max_texture_size, root_dir,
                        texture_view_list[t].label);

    if (extracted_texture == LoaderGLTFStatus_TextureFound) {

      // send texture + sampler to shader
      reflection_texture =
          shader_update_texture(mesh_shader(mesh, MeshShader_Texture),
                                SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
                                &(ShaderUpdateTexture){
                                    .data = data,
                                    .size = size,
                                    .width = width,
                                    .height = height,
                                    .dimension = WGPUTextureViewDimension_2D,
                                    .format = TEXTURE_FORMAT_OFFSCREEN,
                                    .channels = TextureChannel_RGBA,
                                },
                                ShaderUpdateFlag_None);
    } else {

      reflection_texture = shader_update_texture_view(
          mesh_shader(mesh, MeshShader_Texture),
          SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
          texture_view_list[t].fallback_texture, TEXTURE_FORMAT_OFFSCREEN,
          ShaderUpdateFlag_None);
    }

    // transfert texture view to reflection shader (reuse resource), however
    // need to be careful with shared ownership. Here it shouldn't be to
    // much trouble since reflection and texture shader lifetime are mostly
    // linked.
    Shader *reflection_shader = mesh_shader(mesh, MeshShader_Reflection);
    shader_update_texture_view(
        reflection_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
        reflection_texture->texture_view, reflection_texture->format,
        ShaderUpdateFlag_ReleasePrevious);

    binding++;
  }
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

/**
    Extract textures from texture_view
    1. if uri => load image
    2. if buffer_view => store buffer & size
 */
LoaderGLTFStatus extract_texture(cgltf_texture_view *texture_view, void **data,
                                 size_t *size, int *width, int *height,
                                 int *channels, TextureResolution max_size,
                                 const char *root_dir, const char *label) {

  const TextureChannel forced_channel = TextureChannel_RGBA;

  if (texture_view->texture) {

    // TODO: check why cgltf buffer->size return smaller size that w * h *
    // channels

    cgltf_image *image = texture_view->texture->image;

    if (image->uri) {
      cgltf_decode_uri(image->uri);

      char concat_path[1024];
      if (root_dir)
        snprintf(concat_path, sizeof(concat_path), "%s/%s", root_dir,
                 image->uri);

      char *final_path = root_dir ? concat_path : image->uri;

      logger_add(LoggerFlag_Import, "GLTF Texture '%s'", final_path);
      TIMER("", {
        *data = stbi_load(final_path, width, height, channels, forced_channel);
      });

    } else if (image->buffer_view) {

      unsigned char *gltf_data =
          (unsigned char *)image->buffer_view->buffer->data +
          image->buffer_view->offset;

      // use stbi to convert gltf image from RGB(A) to RGBA, ensuring 4
      // channels
      // TODO: more flexible texture upload (RGB/RGBA, large texture
      // handling...)
      logger_add(LoggerFlag_Import, "GLTF Texture '%s'", image->name);
      TIMER("", {
        *data =
            stbi_load_from_memory(gltf_data, image->buffer_view->buffer->size,
                                  width, height, channels, forced_channel);
      });

    } else {
      logger_add(LoggerFlag_Print,
                 "Loader GLTF: Texture found but couldn't be loaded, loading "
                 "default texture");
      return LoaderGLTFStatus_LoadError;
    }

    if (*data != NULL) {

      if (*width > max_size || *height > max_size) {

        int n_w = max_size;
        int n_h = max_size;

        if (*width > *height)
          n_h = (int)((float)*height * max_size / *width);
        else
          n_w = (int)((float)*width * max_size / *height);

        n_w = glm_max(1, n_w);
        n_h = glm_max(1, n_h);

        unsigned char *n_data = malloc(n_w * n_h * forced_channel);

        if (n_data == NULL) {
          logger_add(
              LoggerFlag_Warning,
              "GLTF Loader couldn't allocate resources for resize texture.");
        } else if (stbir_resize_uint8_srgb(*data, *width, *height, 0, n_data,
                                           n_w, n_h, 0,
                                           (uint8_t)forced_channel) == NULL) {
          logger_add(LoggerFlag_Warning,
                     "GLTF Loader STBI resize texture fail.");
        } else {

          *width = n_w;
          *height = n_h;

          // free old texture
          stbi_image_free(*data);

          *data = n_data;
        }
      }

      *size = (*width) * (*height) * forced_channel;
      return LoaderGLTFStatus_TextureFound;
    } else {
      return LoaderGLTFStatus_LoadError;
    }

  } else {
    logger_add(
        LoggerFlag_Print,
        "Loader GLTF: Couldn't find texture '%s', loading default texture",
        label);
    return LoaderGLTFStatus_TextureUnfound;
  }

  return LoaderGLTFStatus_UndefError;
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
