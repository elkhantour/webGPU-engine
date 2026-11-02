#ifndef _SCENE_SYSTEM_H_
#define _SCENE_SYSTEM_H_

#include "backend/renderer/core.h"
#include "runtime/gui/core.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/stat.h"

EXTERN_C_BEGIN

void scene_system_set_draw_mode(Scene *, Renderer *, const RendererDrawMode);
Mesh *scene_system_create_grid(Scene *, Renderer *);

static inline void scene_system_set_post_fx_bloom(Scene *scene, Renderer *rd,
                                                  const BloomUniform bloom) {

  // retrieve the pass fx of the last texture pass
  // TODO: make the access more easy; rn not intuitive
  PostFx *texture_pass_fx =
      &render_pass_list_last_layer(
           renderer_mode_mesh_pass_list(rd, RendererDrawMode_Texture))
           ->post_fx;

  PostFxEffectUniform fx_uniform = {.bloom = bloom};
  post_fx_bloom_update_uniform(texture_pass_fx, fx_uniform);
}

static inline void
scene_system_set_post_fx_composite(Scene *scene, Renderer *rd,
                                   const CompositeUniform composite) {
  // retrieve the pass fx of the last texture pass
  PostFx *texture_pass_fx =
      &render_pass_list_last_layer(
           renderer_mode_mesh_pass_list(rd, RendererDrawMode_Texture))
           ->post_fx;

  PostFxEffectUniform fx_uniform = {.composite = composite};
  post_fx_composite_update_uniform(texture_pass_fx, fx_uniform);
}

/**
   Get the number of drawn vertex in the current active scene render pass list.
 */
static inline void scene_system_update_vertex_count(Scene *scene,
                                                    Renderer *rd) {

  int count = 0;

  RenderPassList *active_list = renderer_mesh_pass_list(rd, rd->draw_mode);

  float denom = 1.0f / 3;

  for (size_t i = 0; i < active_list->count; i++)
    for (size_t j = 0; j < active_list->layers[i].layouts.count; j++)
      for (size_t k = 0;
           k < active_list->layers[i].layouts.entries[j].drawn_meshes.count;
           k++)
        count += active_list->layers[i]
                     .layouts.entries[j]
                     .drawn_meshes.entries[k]
                     .count *
                 denom;

  scene_stat_update_vertex_count(scene, count);
}

/**
   Get the number of render and compute pipelines from the registry
 */
static inline void scene_system_update_scene_shader_count(Scene *scene,
                                                          Renderer *rd) {
  int count = 0;

  for (size_t i = 0; i < reg_count(); i++) {
    const RegEntry *entry = reg_lookup(i);
    if (entry->type == RegEntryType_RenderPipeline ||
        entry->type == RegEntryType_ComputePipeline)
      count++;
  }

  scene_stat_update_shader_count(scene, count);
}

/**
   Get the number of textures entities from the registry
 */
static inline void scene_system_update_scene_texture_count(Scene *scene,
                                                           Renderer *rd) {

  int count = 0;

  for (size_t i = 0; i < reg_count(); i++) {
    const RegEntry *entry = reg_lookup(i);
    if (entry->type == RegEntryType_Texture)
      count++;
  }

  scene_stat_update_texture_count(scene, count);
}

/**
   Get the number of drawn meshes in the current active scene render pass list.
 */
static inline void scene_system_update_draw_call_count(Scene *scene,
                                                       Renderer *rd) {

  int count = 0;
  RenderPassList *active_list = renderer_mesh_pass_list(rd, rd->draw_mode);

  for (size_t i = 0; i < active_list->count; i++)
    for (size_t j = 0; j < active_list->layers[i].layouts.count; j++)
      count += active_list->layers[i].layouts.entries[j].drawn_meshes.count;

  scene_stat_update_draw_call_count(scene, count);
}

EXTERN_C_END

#endif
