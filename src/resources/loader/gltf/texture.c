#include "texture.h"
#include "backend/job_system.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/std_texture/core.h"
#include "cgltf/cgltf.h"
#include "ktx.h"
#include "resources/loader/gltf/core.h"
#include "resources/loader/ktx_loader.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "runtime/texture/create.h"
#include "runtime/texture/processing.h"
#include "utils/dyli.h"
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>
#include <stdint.h>
#include <string.h>

typedef struct {

  Engine *engine;
  LoaderGLTFContext *context;

  JobGroup *group;
  TextureResolution max_resolution;
  cgltf_texture *gltf_texture;

  LoaderGLTFTexture *out_texture;

} TextureLoadJob;

typedef cgltf_texture_view (*texture_getter_callback)(cgltf_material material);

static cgltf_texture_view material_get_albedo(cgltf_material material) {
  return material.pbr_metallic_roughness.base_color_texture;
}

static cgltf_texture_view
material_get_metallic_roughness(cgltf_material material) {
  return material.pbr_metallic_roughness.metallic_roughness_texture;
}

static cgltf_texture_view material_get_normal(cgltf_material material) {
  return material.normal_texture;
}

static cgltf_texture_view material_get_emissive(cgltf_material material) {
  return material.emissive_texture;
}

static cgltf_texture_view material_get_occlusion(cgltf_material material) {
  return material.occlusion_texture;
}

static const uint8_t TEXTURE_COUNT = 5;

static const struct TextureRoute {
  const char *label;
  texture_getter_callback texture_callback;
  const TextureViewType fallback_texture;
  const WGPUTextureFormat format;
} TEXTURE_ROUTE[] = {
    {
        "Base",
        material_get_albedo,
        TextureViewType_Float,
        WGPUTextureFormat_RGBA8UnormSrgb,
    },
    {
        "Metallic Roughness",
        material_get_metallic_roughness,
        TextureViewType_Float,
        WGPUTextureFormat_RGBA8Unorm,
    },
    {
        "Normal",
        material_get_normal,
        TextureViewType_FloatNormal,
        WGPUTextureFormat_RGBA8Unorm,
    },
    {
        "Emissive",
        material_get_emissive,
        TextureViewType_Float,
        WGPUTextureFormat_RGBA8UnormSrgb,
    },
    {
        "Occlusion",
        material_get_occlusion,
        TextureViewType_Float,
        WGPUTextureFormat_RGBA8Unorm,
    },
};

static void texture_load_worker_start(void *);
static void texture_load_worker_done(void *);

static DynamicListStatus texture_slot_init(LoaderGLTFTexture *, char *,
                                           const size_t);

static DynamicListStatus texture_slot_defer_mesh(LoaderGLTFTexture *,
                                                 LoaderGLTFTextureWaiter *);

/** Image referenced by uri. */
static LoaderGLTFStatus load_from_uri(cgltf_image *image, const char *root_dir,
                                      int max_size, TextureMip *out_mip) {
  // DELETE ME cgltf_decode_uri(image->uri);

  char path[1024];
  if (root_dir)
    snprintf(path, sizeof(path), "%s/%s", root_dir, image->uri);
  else
    snprintf(path, sizeof(path), "%s", image->uri);

  texture_mip_create_from_file(out_mip, &(TextureCreateFileDescriptor){
                                            // TODO set max-size AUTO
                                            .width = max_size,
                                            .height = max_size,
                                            .path = path,
                                        });

  return LoaderGLTFStatus_Success;
}

/** Image embedded in a glTF buffer view (GLB). */
static LoaderGLTFStatus load_from_buffer_view(cgltf_image *image, int max_size,
                                              TextureMip *out_mip) {
  const unsigned char *data =
      (const unsigned char *)image->buffer_view->buffer->data +
      image->buffer_view->offset;
  const size_t size = image->buffer_view->size;

  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", image->name);

  texture_mip_create_from_memory(out_mip, &(TextureCreateMemoryDescriptor){
                                              .data = data,
                                              .size = size,
                                              .width = max_size,
                                              .height = max_size,
                                          });

  return LoaderGLTFStatus_Success;
}

static char *texture_slot_name(cgltf_texture *texture) {
  return texture->image->uri;
}

static DynamicListStatus texture_slot_init(LoaderGLTFTexture *texture,
                                           char *name, const size_t capacity) {
  *texture = (LoaderGLTFTexture){0};
  texture->state = LoaderGLTFTextureState_Pending;
  texture->name = name;
  return dyli_create((void *)&texture->waiters.entries,
                     &texture->waiters.capacity, &texture->waiters.count,
                     sizeof(LoaderGLTFTextureWaiter), capacity,
                     "Loader Gltf Texture Waiter");
}

void texture_slot_free(LoaderGLTFTexture *texture) {
  dyli_free((void *)&texture->waiters.entries, &texture->waiters.capacity,
            &texture->waiters.count);
  *texture = (LoaderGLTFTexture){0};
}

static DynamicListStatus
texture_slot_defer_mesh(LoaderGLTFTexture *texture,
                        LoaderGLTFTextureWaiter *waiter) {
  return dyli_insert((void *)&texture->waiters.entries,
                     &texture->waiters.capacity, &texture->waiters.count,
                     sizeof(LoaderGLTFTextureWaiter), waiter, 1,
                     "Loader Gltf Texture Waiter");
}

/**
 * @brief Extract a glTF texture (uri or buffer_view).
 *
 * KTX source:   *out_ktx is the owner, out_mip borrows from it.
 *               Release with ktx_texture_destroy(out_ktx).
 * Other source: *out_ktx == NULL, out_mip->mips[0].data is a stbi buffer
 *               (stbi_image_free, or upload with REMWriteFlag_STBIFreeData).
 */

static void bind_texture(Mesh *mesh, bind_index binding, WGPUTextureView view,
                         WGPUTextureFormat format) {

  Shader *texture_shader = mesh_shader(mesh, MeshShader_Texture);
  Shader *reflection_shader = mesh_shader(mesh, MeshShader_Reflection);

  // The mip is shared by every mesh using this texture, so the upload must
  // not free anything and nothing is destroyed here. The slot owns the
  // ktx/stbi data until the context is destroyed.
  ShaderBindGroupTextureEntry *reflection_texture = shader_update_texture_view(
      texture_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding, view, format,
      ShaderUpdateFlag_None);

  // share the view with the reflection shader (see ownership note in
  // original)
  shader_update_texture_view(
      reflection_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
      reflection_texture->texture_view, reflection_texture->format,
      ShaderUpdateFlag_ReleasePrevious);
}

/* ---------- job system ---------- */
void loader_gltf_bind_mesh_textures(LoaderGLTFContext *ctx, Engine *engine,
                                    Mesh *mesh, cgltf_material *material) {

  for (uint8_t texture = 0; texture < TEXTURE_COUNT; texture++) {

    cgltf_texture_view view =
        TEXTURE_ROUTE[texture].texture_callback(*material);
    const WGPUTextureView fallback =
        std_texture_view(TEXTURE_ROUTE[texture].fallback_texture);

    // look up the preloaded slot by cgltf texture index (no string compare)
    LoaderGLTFTexture *slot = NULL;
    if (view.texture != NULL) {
      cgltf_size idx = cgltf_texture_index(ctx->data, view.texture);
      if (idx < ctx->textures.count)
        slot = &ctx->textures.entries[idx];
    }

    if (slot == NULL)
      continue;

    if (slot->state == LoaderGLTFTextureState_Loaded) {
      bind_texture(mesh, texture, slot->view, slot->format);
    } else {
      // referenced but not loaded yet / failed
      printf("texture '%s' not ready (state %d), using fallback\n",
             TEXTURE_ROUTE[texture].label, slot->state);

      // add to the slot queue for later when texture is loaded
      texture_slot_defer_mesh(slot, &(LoaderGLTFTextureWaiter){
                                        .mesh = mesh,
                                        .binding = texture,
                                    });

      // DELETEME reflection_texture = shader_update_texture_view(
      //      texture_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
      //      fallback, TEXTURE_FORMAT_OFFSCREEN, ShaderUpdateFlag_None);
    }
  }
}

/**
   clgtf_texture doesn't allow to retrieve the format. So we need to traverse
   the materials to see the usage of the texture.
 */
static WGPUTextureFormat get_format(cgltf_data *data, const char *name) {

  for (uint8_t t = 0; t < TEXTURE_COUNT; t++) {

    const struct TextureRoute *route = &TEXTURE_ROUTE[t];

    for (cgltf_size m = 0; m < data->materials_count; ++m) {
      cgltf_texture_view view =
          TEXTURE_ROUTE[t].texture_callback(data->materials[m]);

      if (view.texture && texture_slot_name(view.texture) == name) {

        // DEBUG
        printf("[mat] %s | %s | %d\n", view.texture->image->uri, route->label,
               route->format);

        return route->format;
      }
    }
  }

  return WGPUTextureFormat_RGBA8UnormSrgb;
}

void loader_gltf_preload_textures(LoaderGLTFContext *ctx, Engine *engine) {

  cgltf_data *data = ctx->data;
  JobGroup *tex_group = calloc(1, sizeof(JobGroup));
  static const uint8_t TEXTURES_LOAD_WORKER_CAP = 1;
  job_group_init(tex_group, TEXTURES_LOAD_WORKER_CAP);

  for (cgltf_size i = 0; i < data->textures_count; i++) {
    cgltf_texture *tex = &data->textures[i];
    LoaderGLTFTexture *slot = &ctx->textures.entries[i];
    static uint8_t TEXTURE_SLOT_WAITER_CAPACITY = 8;
    texture_slot_init(slot, texture_slot_name(tex),
                      TEXTURE_SLOT_WAITER_CAPACITY);

    TextureLoadJob *job = calloc(1, sizeof(TextureLoadJob));
    job->group = tex_group;
    job->engine = engine;
    job->gltf_texture = tex;
    job->out_texture = slot;
    job->context = ctx;
    job->max_resolution = ctx->options.max_texture_size;
    job_system_submit(engine->job_system, tex_group, texture_load_worker_start,
                      texture_load_worker_done, job);

    ctx->texture_pending++;
  }
}

/** Worker thread: CPU decode only, no GPU calls. */
void texture_load_worker_start(void *data) {

  TextureLoadJob *job = (TextureLoadJob *)data;

  TextureResolution max_size = job->max_resolution;
  cgltf_texture *gltf_texture = job->gltf_texture;
  char *root_dir = job->context->root_dir;
  TextureMip *out_mip = &job->out_texture->texture;

  memset(out_mip, 0, sizeof *out_mip);

  if (!gltf_texture) {
    logger_add(LoggerFlag_Print,
               "Loader GLTF: Couldn't find texture '%s', loading default "
               "texture",
               "TODO: replace label");
    return;
  }

  cgltf_image *image = gltf_texture->image;
  if (!image)
    return;

  if (image->uri)
    load_from_uri(image, root_dir, (int)max_size, out_mip);

  if (image->buffer_view)
    load_from_buffer_view(image, (int)max_size, out_mip);

  logger_add(LoggerFlag_Print,
             "Loader GLTF: Texture found but couldn't be loaded, loading "
             "default texture");

  return;
}

/** Main thread: upload to the GPU, then release the CPU data. */
void texture_load_worker_done(void *data) {

  TextureLoadJob *job = (TextureLoadJob *)data;
  LoaderGLTFContext *ctx = job->context;

  LoaderGLTFTexture *slot = job->out_texture;
  slot->state = LoaderGLTFTextureState_Loaded;
  slot->format = get_format(ctx->data, slot->name);
  slot->texture.format = slot->format;

  {
    // DEBUG
    printf("Textures --------------- \n");
    for (size_t i = 0; i < ctx->textures.count; i++) {
      printf("[%lu] %s | %d (RGBA: %d, UNORM: %d)\n", i,
             ctx->textures.entries[i].name, ctx->textures.entries[i].format,
             WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_RGBA8Unorm);
    }
    printf("----------------------- \n");
  }

  // DEBUG
  printf("Uploading with: %d\n", slot->texture.format);
  WGPUTexture gpu_texture = texture_mip_upload(&slot->texture);
  texture_mip_destroy(&slot->texture);

  slot->view = rem_new_view(gpu_texture, NULL);

  for (size_t m = 0; m < slot->waiters.count; m++) {
    LoaderGLTFTextureWaiter *waiter = &slot->waiters.entries[m];
    bind_texture(waiter->mesh, waiter->binding, slot->view, slot->format);
  }

  if (--ctx->texture_pending == 0)
    free(job->group);

  free(job);
  loader_gltf_attempt_release(ctx);
}
