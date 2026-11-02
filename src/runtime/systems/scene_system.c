#include "scene_system.h"
#include "backend/reflection/draw.h"
#include "backend/render_pass/texture.h"
#include "backend/renderer/core.h"
#include "backend/resource_manager.h"
#include "backend/shadow_map/draw.h"
#include "runtime/geometry/line/core.h"
#include "runtime/light/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/mesh/shader/texture.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/editor_mesh/grid/grid.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/scene/stat.h"
#include "utils/color.h"

Mesh *scene_system_create_grid(Scene *scene, Renderer *renderer) {

  Mesh *grid = rem_new_mesh();
  sem_grid_create(grid, &(GridUniform){
                            .size = 100.0f,
                            .cell_size = 100.0f,
                            .thickness = 44.0f,
                            .color = {0.5f, 0.5f, 0.5f, 1.0f},
                        });

  scene->grid = grid;

  return grid;
}

void scene_system_set_draw_mode(Scene *scene, Renderer *renderer,
                                const RendererDrawMode mode) {

  if (mode == renderer->draw_mode)
    return;

  profiler_latency_clear_all(&renderer->profiler);

  // update light / reflections
  if (mode == RendererDrawMode_Texture) {

    const MeshRefList *shadow_meshes;
    scene_mesh_query_with_flags(scene, RendererMeshFlag_Shadow, &shadow_meshes);

    shadow_map_draw_all(&renderer->shadow_map);
  }

  // update renderer drawn render pass configuration
  renderer_set_draw_mode(renderer, mode);
}
