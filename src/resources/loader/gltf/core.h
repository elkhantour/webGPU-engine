#ifndef _LOADER_GLTF_CORE_H_
#define _LOADER_GLTF_CORE_H_

#include <stddef.h>
#include <webgpu/webgpu.h>

#include "cgltf/cgltf.h"
#include "runtime/engine/core.h"
#include "runtime/mesh/core.h"
#include "runtime/scene/core.h"
#include "runtime/scene/scene.h"
#include "runtime/shader/bindgroup.h"
#include "runtime/texture/core.h"
#include "utils/dyli.h"

typedef enum {
  LoaderGLTFStatus_Success,
  LoaderGLTFStatus_UndefError,
  LoaderGLTFStatus_TextureFound,
  LoaderGLTFStatus_TextureUnfound,
  LoaderGLTFStatus_LoadError,
  LoaderGLTFStatus_OutOfBoundMemory,
  LoaderGLTFStatus_FileUnfound,
  LoaderGLTFStatus_JSONInvalid,
} LoaderGLTFStatus;

typedef enum {
  LoaderGLTFTextureState_Default,
  LoaderGLTFTextureState_Pending,
  LoaderGLTFTextureState_Loaded,
} LoaderGLTFTextureState;

typedef struct {
  Mesh *mesh;
  bind_index binding;
} LoaderGLTFTextureWaiter;

typedef struct {
  LoaderGLTFTextureState state;
  char *name;
  TextureMip texture;
  DYNAMIC_LIST(LoaderGLTFTextureWaiter)
  waiters; // mesh requiring the texture when it wasn't loaded
} LoaderGLTFTexture;

typedef struct {
  TextureResolution max_texture_size;
} LoaderGLTFOptions;

typedef struct {
  cgltf_data *data;
  char root_dir[1024];
  int mesh_pending;
  int texture_pending;
  LoaderGLTFOptions options;
  DYNAMIC_LIST(LoaderGLTFTexture) textures;
} LoaderGLTFContext;

typedef struct {
  cgltf_texture_view diffuse;
  cgltf_texture_view metallic;
  cgltf_texture_view normal;
  cgltf_texture_view occlusion;
  cgltf_texture_view emissive;
} GLTFTextureViews;

#define LOADER_GLTF_RESULT_MESH_COUNT 128

typedef struct {

  struct {
    size_t vertex_count;
    size_t mesh_count;
  } stats;

  struct {
    Mesh *entries[LOADER_GLTF_RESULT_MESH_COUNT];
    size_t count;
  } meshes;

} LoaderGLTFResult;

typedef struct {
  Engine *engine;
  const char *path;
  const LoaderGLTFOptions *options;
  const cgltf_options *cgltf_options;
} GLTFLoadDescriptor;

LoaderGLTFStatus loader_gltf_load(const GLTFLoadDescriptor *,
                                  LoaderGLTFResult *);

void loader_gltf_attempt_release(LoaderGLTFContext *);
#endif
