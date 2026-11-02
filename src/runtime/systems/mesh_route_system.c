#include "mesh_route_system.h"
#include "backend/renderer/core.h"
#include "backend/renderer/mesh_config.h"
#include "backend/render_pass/core.h"
#include "backend/shadow_map/core.h"
#include "backend/shaders/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/draw.h"
#include "runtime/scene/mesh_cache.h"
#include "runtime/systems/scene_system.h"
#include <stdbool.h>
#include <stdint.h>

/**
   Visibility flow:

     mesh_set_flags(Hidden) -> scene cache refresh -> route -> update counts

   Routing is stateless: for every draw mode / pass (pass index == layer bit) /
   layout it asks "should this mesh be in this layout?" and patches the
   layout's drawn_meshes to match. Flags predominate: a Hidden mesh is accepted
   by no layout, whatever its draw mode, layer or pipeline.

 */

// ---------------------------------------------------------------------------
// light / reflection passes (still to port)
// ---------------------------------------------------------------------------

static void enable_in_light_reflection(Scene *scene, Renderer *renderer,
                                       Mesh *mesh) {
  // FIXME
  // render_pass_enable_mesh(&scene->probes.reflection_probe.pass, mesh);
  // render_pass_enable_mesh(&scene->probes.reflection_plane.pass, mesh);
  // render_pass_enable_mesh(&scene->lights.point.shadow.pass, mesh);
  // render_pass_enable_mesh(&scene->lights.spot.shadow.pass, mesh);
}

static void disable_in_light_reflection(Scene *scene, Renderer *renderer,
                                        Mesh *mesh) {
  // FIXME
  // render_pass_disable_mesh(&scene->probes.reflection_probe.pass, mesh);
  // render_pass_disable_mesh(&scene->probes.reflection_plane.pass, mesh);
  // render_pass_disable_mesh(&scene->lights.point.shadow.pass, mesh);
  // render_pass_disable_mesh(&scene->lights.spot.shadow.pass, mesh);
}

// ---------------------------------------------------------------------------
// routing (stateless acceptance rules)
// ---------------------------------------------------------------------------

/**
   Patch every layout so it contains the mesh if and only if it accepts it.
   With remove_all, the mesh is taken out of every layout regardless.
 */
static void route_mesh(Renderer *renderer, Mesh *mesh, const bool remove_all) {

  // Default pipelines
  for (size_t mode = 0; mode < RENDERER_DRAW_MODE_COUNT; mode++) {

    const RendererDrawMode draw_mode = (RendererDrawMode)(1 << mode);
    RenderPassList *pass_list = renderer_mesh_pass_list(renderer, draw_mode);

    for (size_t layer = 0; layer < pass_list->count; layer++) {
      RenderPass *pass = &pass_list->layers[layer];

      for (size_t k = 0; k < pass->layouts.count; k++) {
        RenderPassLayout *layout = &pass->layouts.entries[k];
        bool toggled = render_pass_layout_toggle_mesh(layout, mesh, draw_mode,
                                                      layer, remove_all);
      }
    }
  }

  // Shadow Map / Reflection that use their own render pass
  shadow_map_toggle_mesh(&renderer->shadow_map, mesh, remove_all);
}

// ---------------------------------------------------------------------------
// internals
// ---------------------------------------------------------------------------

static void update_counts(Scene *scene, Renderer *renderer) {
  scene_system_update_vertex_count(scene, renderer);
  scene_system_update_draw_call_count(scene, renderer);
}

static void set_hidden(Mesh *mesh, const bool hidden) {
  const RendererMeshFlag flags = mesh->render_config.flags;
  mesh_set_flags(mesh, hidden ? (flags | RendererMeshFlag_Hidden)
                              : (flags & ~RendererMeshFlag_Hidden));
}

static void apply_hidden_list(Scene *scene, Renderer *renderer,
                              MeshRefList *list, const bool hidden) {

  // 1. change every flag, 2. refresh the cache once and route each mesh
  for (size_t i = 0; i < list->count; i++)
    set_hidden(list->entries[i], hidden);

  mesh_route_system_update_mesh_ref_list(scene, renderer, list);

  for (size_t i = 0; i < list->count; i++) {
    if (hidden)
      disable_in_light_reflection(scene, renderer, list->entries[i]);
    else
      enable_in_light_reflection(scene, renderer, list->entries[i]);
  }
}

// ---------------------------------------------------------------------------
// public
// ---------------------------------------------------------------------------

void mesh_route_system_update_mesh(Scene *scene, Renderer *renderer,
                                   Mesh *mesh) {
  scene_mesh_cache_refresh_all(&scene->mesh_cache);
  route_mesh(renderer, mesh, false);
  update_counts(scene, renderer);
}

void mesh_route_system_update_mesh_ref_list(Scene *scene, Renderer *renderer,
                                            MeshRefList *list) {
  if (list->count == 0)
    return;

  scene_mesh_cache_refresh_all(&scene->mesh_cache); // once for the whole list
  for (size_t i = 0; i < list->count; i++)
    route_mesh(renderer, list->entries[i], false);

  update_counts(scene, renderer);
}

/**
   Rebuild, IN PLACE, every draw packet derived from the mesh. Use it when the
   mesh vertex/index buffers or its shader were recreated (so the packet copies
   are stale). Draw order is kept, unlike a remove + add.

   Only touches layouts that already contain the mesh: it neither adds nor
   removes (see route_mesh for that).
 */
void mesh_route_system_refresh_mesh(Renderer *renderer, Mesh *mesh) {

  for (size_t mode = 0; mode < RENDERER_DRAW_MODE_COUNT; mode++) {

    RenderPassList *pass_list =
        renderer_mesh_pass_list(renderer, (RendererDrawMode)(1 << mode));

    for (size_t layer = 0; layer < pass_list->count; layer++) {

      RenderPass *pass = &pass_list->layers[layer];

      for (size_t k = 0; k < pass->layouts.count; k++) {
        RenderPassLayout *layout = &pass->layouts.entries[k];

        // cheap rejection first: no packet can exist here for this mesh
        if (!render_pass_layout_accepts(mesh, layout, mode, layer))
          continue;

        size_t index;
        MeshDrawPacket *pack = mesh_draw_packet_list_find_mesh(
            &layout->drawn_meshes, mesh, &index);

        if (!pack)
          continue;

        Shader *shader = mesh_shader(mesh, layout->shader);

        // the mesh lost the shader this layout needs: it can't be drawn here
        if (!shader) {
          mesh_draw_packet_list_remove_at_index(&layout->drawn_meshes, index);
          continue;
        }

        mesh_create_draw_packet(layout->topology_callback(mesh), shader, mesh,
                                pack);
      }
    }
  }
}

void mesh_route_system_unroute_mesh(Scene *scene, Renderer *renderer,
                                    Mesh *mesh) {
  route_mesh(renderer, mesh, true);
  update_counts(scene, renderer);
}

void mesh_route_system_show_mesh(Scene *scene, Renderer *renderer, Mesh *mesh) {
  set_hidden(mesh, false);
  mesh_route_system_update_mesh(scene, renderer, mesh);
  enable_in_light_reflection(scene, renderer, mesh);
}

void mesh_route_system_hide_mesh(Scene *scene, Renderer *renderer, Mesh *mesh) {
  set_hidden(mesh, true);
  mesh_route_system_update_mesh(scene, renderer, mesh);
  disable_in_light_reflection(scene, renderer, mesh);
}

RendererStatus mesh_route_system_toggle_mesh(Scene *scene, Renderer *renderer,
                                             Mesh *mesh) {
  if (mesh->render_config.flags & RendererMeshFlag_Hidden) {
    mesh_route_system_show_mesh(scene, renderer, mesh);
    return RendererStatus_MeshVisible;
  }

  mesh_route_system_hide_mesh(scene, renderer, mesh);
  return RendererStatus_MeshHidden;
}

void mesh_route_system_show_mesh_ref_list(Scene *scene, Renderer *renderer,
                                          MeshRefList *list) {
  apply_hidden_list(scene, renderer, list, false);
}

void mesh_route_system_hide_mesh_ref_list(Scene *scene, Renderer *renderer,
                                          MeshRefList *list) {
  apply_hidden_list(scene, renderer, list, true);
}
