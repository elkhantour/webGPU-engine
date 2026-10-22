#ifndef _LOADER_GLTF_TEXTURE_H_
#define _LOADER_GLTF_TEXTURE_H_

#include "core.h"

void loader_gltf_bind_mesh_textures(LoaderGLTFContext *, Engine *, Mesh *,
                                    cgltf_material *);

void loader_gltf_preload_textures(LoaderGLTFContext *, Engine *);

#endif
