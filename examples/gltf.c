#include "gltf.h"

#include "../include/loader.h"
#include "../include/mesh.h"
#include "../include/systems.h"

void example_gltf(Engine *engine) {
  loader_gltf_load(
      &(GLTFLoadDescriptor){
          .engine = engine,
          .path = PATH_GLTF("cube.gltf"),
          .cgltf_options = &(cgltf_options){0},
          .options =
              &(LoaderGLTFOptions){
                  .max_texture_size = TextureResolution_512,
              },
      },
      NULL);
}

void example_gltf_spa(Engine *engine) {

  //  engine_scene_add_point_light(engine,
  //                        &(PointLightDescriptor){
  //                            .color = {0.4f, 0.0f, 1.0f, 1.0f},
  //                            .intensity = 7.0f,
  //                            .cutoff = 20.0f,
  //                            .inner_cutoff = 50.0f,
  //                            .near = 0.1,
  //                            .far = 20.0f,
  //                            .position = {0.0f, 3.5f, 0.0f},
  //                        },
  //                        LightCreateFlag_None, NULL);

  engine_scene_add_sun_light(
      engine,
      &(SunLightDescriptor){
          .position = {10.8f, 12.0f, -15.0f},
          .color = {241.0f / 255.0f, 120.0f / 255.0f, 82.0f / 255.0f, 1.0f},
          .intensity = 3.2f,
          .size = 10.0f,
      },
      LightCreateFlag_Shadow, NULL);

  engine_scene_add_ambient_light(engine,
                                 &(AmbientLightDescriptor){
                                     .color = {0.0f, 0.4f, 1.0f, 1.0f},
                                     .intensity = 0.2f,
                                     .position = {-2.0f, 3.0f, 3.3f},
                                 },
                                 NULL);

  loader_gltf_load(
      &(GLTFLoadDescriptor){
          .engine = engine,
          .path = PATH_GLTF("spa.gltf"),
          .cgltf_options = &(cgltf_options){0},
          .options =
              &(LoaderGLTFOptions){
                  .max_texture_size = TextureResolution_512,
              },
      },
      NULL);

  // adjust Post FX
  scene_system_set_post_fx_bloom(engine_get_active_scene(engine),
                                 engine_get_renderer(engine),
                                 (BloomUniform){
                                     .blur = 2,
                                     .downscale = 3,
                                     .knee = 0.450,
                                     .threshold = 0.300,
                                 });

  scene_system_set_post_fx_composite(engine_get_active_scene(engine),
                                     engine_get_renderer(engine),
                                     (CompositeUniform){
                                         .exposure = 1.020,
                                         .bloom_intensity = 0.320f,
                                         .gamma = 0.920,
                                         .vignette_feather = 1.0f,
                                         .vignette_strength = 0.0f,
                                     });
}

void example_gltf_podium(Engine *engine) {

  LoaderGLTFResult gltf_result;
  LoaderGLTFStatus status = loader_gltf_load(
      &(GLTFLoadDescriptor){
          .engine = engine,
          .path = PATH_GLTF("podium.gltf"),
          .cgltf_options = &(cgltf_options){0},
          .options =
              &(LoaderGLTFOptions){
                  .max_texture_size = TextureResolution_512,
              },
      },
      &gltf_result);

  // create a new planar reflection for the podium
  ProbeReflectionPlane *plane;
  SceneEditorMeshList *plane_probe = engine_scene_add_probe_reflection_plane(
      engine,
      &(ProbeReflectionPlaneDescriptor){
          .far = 100.0f,
          .near = 0.1f,
          .normal = {0.0f, 1.0f, 0.0f},
          .position = {0.0f, 1.0f, 0.0f},
          .scale = {10.0f, 10.0f, 10.0f},
          .distance = 1.0f,
          .camera = engine_get_active_scene(engine)->active_camera,
      },
      &plane);

  const char *podium_name = "podium";
  Mesh *podium = mesh_ref_list_find_by_name(
      &(MeshRefList){
          .capacity = gltf_result.meshes.count,
          .count = gltf_result.meshes.count,
          .entries = gltf_result.meshes.entries,
      },
      podium_name);

  if (podium)
    mesh_shader_texture_bind_probe(podium, plane,
                                   engine_get_active_scene(engine)->ubo);
}
