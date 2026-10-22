#include "texture.h"
#include "backend/job_system.h"
#include "backend/logger.h"
#include "backend/resource_manager.h"
#include "backend/std_texture/core.h"
#include "ktx.h"
#include "resources/loader/gltf/core.h"
#include "resources/loader/ktx_loader.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"
#include <stb/stb_image.h>
#include <stb/stb_image_resize2.h>
#include <stdint.h>
#include <string.h>

#define GLTF_FORCED_CHANNELS TextureChannel_RGBA

typedef struct {

  Engine *engine;
  LoaderGLTFContext *context;

  JobGroup *group;
  TextureResolution max_resolution;
  cgltf_texture *gltf_texture;

  ktxTexture2 *ktx_texture; // owner of the mip data (NULL for stbi textures)
  LoaderGLTFTexture *out_texture; // mips (borrowed from context texture list)

} TextureLoadJob;

static void texture_load_worker_start(void *);
static void texture_load_worker_done(void *);

static DynamicListStatus texture_slot_init(LoaderGLTFTexture *, char *,
                                           const size_t);
static void texture_slot_free(LoaderGLTFTexture *);
static DynamicListStatus texture_slot_defer_mesh(LoaderGLTFTexture *,
                                                 LoaderGLTFTextureWaiter *);

/* ---------- extraction helpers ---------- */

/** Single-mip view over a stbi-allocated RGBA8 buffer. */
static void texture_set_single_mip(TextureMip *out, unsigned char *data, int w,
                                   int h) {
  out->format = WGPUTextureFormat_RGBA8Unorm;
  out->width = (uint32_t)w;
  out->height = (uint32_t)h;
  out->mip_count = 1;
  out->mips[0].data = data;
  out->mips[0].size = (size_t)w * h * GLTF_FORCED_CHANNELS;
}

/** Downscale *data (keeps aspect ratio) if it exceeds max_size.
 *  On any failure the original image is kept. */
static void stbi_fit_max_size(unsigned char **data, int *w, int *h,
                              int max_size) {
  if (*w <= max_size && *h <= max_size)
    return;

  int n_w = max_size;
  int n_h = max_size;
  if (*w > *h)
    n_h = (int)((float)*h * max_size / *w);
  else
    n_w = (int)((float)*w * max_size / *h);
  n_w = glm_max(1, n_w);
  n_h = glm_max(1, n_h);

  unsigned char *n_data = malloc((size_t)n_w * n_h * GLTF_FORCED_CHANNELS);
  if (!n_data) {
    logger_add(LoggerFlag_Warning,
               "GLTF Loader couldn't allocate resources for resize texture.");
    return;
  }

  if (!stbir_resize_uint8_srgb(*data, *w, *h, 0, n_data, n_w, n_h, 0,
                               (uint8_t)GLTF_FORCED_CHANNELS)) {
    logger_add(LoggerFlag_Warning, "GLTF Loader STBI resize texture fail.");
    free(n_data);
    return;
  }

  stbi_image_free(*data);
  *data = n_data;
  *w = n_w;
  *h = n_h;
}

/** stbi path: from file (path) or memory (mem), resized to fit max_size. */
static LoaderGLTFStatus load_stbi(const char *path, const unsigned char *mem,
                                  size_t mem_size, int max_size,
                                  TextureMip *out_mip) {
  int w = 0, h = 0, channels = 0;
  unsigned char *data = NULL;

  TIMER("", {
    data = path ? stbi_load(path, &w, &h, &channels, GLTF_FORCED_CHANNELS)
                : stbi_load_from_memory(mem, (int)mem_size, &w, &h, &channels,
                                        GLTF_FORCED_CHANNELS);
  });
  if (!data)
    return LoaderGLTFStatus_LoadError;

  stbi_fit_max_size(&data, &w, &h, max_size);
  texture_set_single_mip(out_mip, data, w, h);
  return LoaderGLTFStatus_TextureFound;
}

#ifdef ENGINE_KTX
/** KTX path, two steps: load the ktxTexture2, then fetch the mips that fit
 *  max_size. On failure nothing is left allocated. */
static LoaderGLTFStatus load_ktx(const char *path, const unsigned char *mem,
                                 size_t mem_size, int max_size,
                                 ktxTexture2 **out_ktx, TextureMip *out_mip) {
  LoaderKTXStatus s = LoaderKTXStatus_FailLoad;

  TIMER("", {
    s = ktx_texture_load(out_ktx, path, mem, mem_size);
    if (s == LoaderKTXStatus_Success)
      s = ktx_texture_get_mips(*out_ktx, max_size, out_mip);
    else
      logger_add(LoggerFlag_Error,
                 "Failed to load ktx texture: %s (Error code: %d)", path, s);
  });

  if (s != LoaderKTXStatus_Success) {
    ktx_texture_destroy(out_ktx);
    memset(out_mip, 0, sizeof *out_mip);
    return LoaderGLTFStatus_LoadError;
  }

  return LoaderGLTFStatus_TextureFound;
}
#endif

/** Image referenced by uri. */
static LoaderGLTFStatus load_from_uri(cgltf_image *image, const char *root_dir,
                                      int max_size, ktxTexture2 **out_ktx,
                                      TextureMip *out_mip) {
  // DELETE ME cgltf_decode_uri(image->uri);

  char path[1024];
  if (root_dir)
    snprintf(path, sizeof(path), "%s/%s", root_dir, image->uri);
  else
    snprintf(path, sizeof(path), "%s", image->uri);

#ifdef ENGINE_KTX
  // Every image was converted: ignore the original extension, use the .ktx2
  path_set_ext(path, sizeof(path), "ktx2");
  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", path);
  return load_ktx(path, NULL, 0, max_size, out_ktx, out_mip);
#else
  (void)out_ktx;
  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", path);
  return load_stbi(path, NULL, 0, max_size, out_mip);
#endif
}

/** Image embedded in a glTF buffer view (GLB). */
static LoaderGLTFStatus load_from_buffer_view(cgltf_image *image, int max_size,
                                              ktxTexture2 **out_ktx,
                                              TextureMip *out_mip) {
  const unsigned char *data =
      (const unsigned char *)image->buffer_view->buffer->data +
      image->buffer_view->offset;
  const size_t size = image->buffer_view->size;

  logger_add(LoggerFlag_Import, "GLTF Texture '%s'", image->name);

#ifdef ENGINE_KTX
  if (is_ktx2_memory(data, size))
    return load_ktx(NULL, data, size, max_size, out_ktx, out_mip);
#else
  (void)out_ktx;
#endif
  return load_stbi(NULL, data, size, max_size, out_mip);
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

static void texture_slot_free(LoaderGLTFTexture *texture) {
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

static void bind_texture(Mesh *mesh, bind_index binding, TextureMip *texture) {

  Shader *texture_shader = mesh_shader(mesh, MeshShader_Texture);
  Shader *reflection_shader = mesh_shader(mesh, MeshShader_Reflection);

  // The mip is shared by every mesh using this texture, so the upload must
  // not free anything and nothing is destroyed here. The slot owns the
  // ktx/stbi data until the context is destroyed.
  ShaderBindGroupTextureEntry *reflection_texture = shader_update_texture_mip(
      texture_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding, texture,
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

  static const uint8_t TEXTURE_COUNT = 5;

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

  for (uint8_t binding = 0; binding < TEXTURE_COUNT; binding++) {

    cgltf_texture_view *view = texture_view_list[binding].gltf_texture;
    const WGPUTextureView fallback =
        texture_view_list[binding].fallback_texture;

    // look up the preloaded slot by cgltf texture index (no string compare)
    LoaderGLTFTexture *slot = NULL;
    if (view->texture != NULL) {
      cgltf_size idx = cgltf_texture_index(ctx->data, view->texture);
      if (idx < ctx->textures.count)
        slot = &ctx->textures.entries[idx];
    }

    if (slot == NULL)
      continue;

    if (slot->state == LoaderGLTFTextureState_Loaded) {
      bind_texture(mesh, binding, &slot->texture);
    } else {
      // referenced but not loaded yet / failed
      printf("texture '%s' not ready (state %d), using fallback\n",
             texture_view_list[binding].label, slot->state);

      // add to the slot queue for later when texture is loaded
      texture_slot_defer_mesh(slot, &(LoaderGLTFTextureWaiter){
                                        .mesh = mesh,
                                        .binding = binding,
                                    });

      // DELETEME reflection_texture = shader_update_texture_view(
      //      texture_shader, SHADER_TEXTURE_BINDGROUP_TEXTURES, binding,
      //      fallback, TEXTURE_FORMAT_OFFSCREEN, ShaderUpdateFlag_None);
    }
  }
}

void loader_gltf_preload_textures(LoaderGLTFContext *ctx, Engine *engine) {

  cgltf_data *data = ctx->data;
  JobGroup *tex_group = calloc(1, sizeof(JobGroup));
  static const uint8_t TEXTURES_LOAD_WORKER_CAP = 4;
  job_group_init(tex_group, TEXTURES_LOAD_WORKER_CAP);

  for (cgltf_size i = 0; i < data->textures_count; i++) {
    cgltf_texture *tex = &data->textures[i];
    LoaderGLTFTexture *slot = &ctx->textures.entries[i];
    static uint8_t TEXTURE_SLOT_WAITER_CAPACITY = 8;
    texture_slot_init(slot, tex->image->uri, TEXTURE_SLOT_WAITER_CAPACITY);

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
  ktxTexture2 **out_ktx = &job->ktx_texture;
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
    load_from_uri(image, root_dir, (int)max_size, out_ktx, out_mip);

  if (image->buffer_view)
    load_from_buffer_view(image, (int)max_size, out_ktx, out_mip);

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

  for (size_t m = 0; m < slot->waiters.count; m++) {
    LoaderGLTFTextureWaiter *waiter = &slot->waiters.entries[m];
    bind_texture(waiter->mesh, waiter->binding, &slot->texture);
  }

  ktx_texture_destroy(&job->ktx_texture);

  if (--ctx->texture_pending == 0)
    free(job->group);

  free(job);
  loader_gltf_attempt_release(ctx);
}
