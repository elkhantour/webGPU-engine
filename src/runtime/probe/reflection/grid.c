#include "grid.h"

#include <cglm/ivec3.h>
#include <cglm/util.h>
#include <cglm/vec3.h>
#include <stdint.h>

#include "backend/logger.h"
#include "backend/registry.h"
#include "backend/render_pass/core.h"
#include "backend/render_pass/draw.h"
#include "backend/resource_manager.h"
#include "backend/std_texture/core.h"
#include "core.h"
#include "probe.h"
#include "runtime/scene/debug/view.h"
#include "utils/dyli.h"
#include "webgpu/webgpu.h"

static inline float probe_reflection_point(size_t x, uint16_t count,
                                           float size);

float probe_reflection_point(size_t x, uint16_t count, float size) {
  return count > 1 ? ((float)x * 2.0f * size / (count - 1)) - size : 0.0f;
}

void probe_reflection_grid_create(ProbeReflectionGrid *grid,
                                  ProbeReflectionGridDescriptor *desc) {

  grid->id = reg_register(grid, RegEntryType_ProbeReflectionGrid);
  probe_reflection_grid_set_name(grid, desc->name == 0 ? "Probe Reflection Grid"
                                                       : desc->name);

  glm_vec3_copy(desc->scale, grid->scale);
  glm_vec3_copy(desc->position, grid->position);

  int clamp_count[PROBE_REFLECTION_GRID_DIMENSION]; // prevent overflow

  for (size_t i = 0; i < PROBE_REFLECTION_GRID_DIMENSION; i++)
    clamp_count[i] = glm_min(desc->count[i], PROBE_REFLECTION_GRID_MAX_COUNT);

  glm_ivec3_copy(clamp_count, grid->count);

  size_t count = clamp_count[0] * clamp_count[1] * clamp_count[2];
  probe_reflection_list_create(&grid->probes, count);

  for (size_t x = 0; x < clamp_count[0]; x++) {
    float x_pos = probe_reflection_point(x, clamp_count[0], desc->scale[0]);

    for (size_t y = 0; y < clamp_count[1]; y++) {
      float y_pos = probe_reflection_point(y, clamp_count[1], desc->scale[1]);

      for (size_t z = 0; z < clamp_count[2]; z++) {
        float z_pos = probe_reflection_point(z, clamp_count[2], desc->scale[2]);

        ProbeReflection *probe = probe_reflection_list_new_entry(&grid->probes);

        if (probe)
          probe_reflection_create(probe, (vec3){x_pos, y_pos, z_pos});
      }
    }
  }

  grid->view = std_texture_view(TextureViewType_FloatCubeArray);
  probe_reflection_grid_update_boundbox(grid);
}

void probe_reflection_grid_destroy(ProbeReflectionGrid *grid) {

  rem_destroy_view(&grid->view);
  rem_destroy_texture(&grid->texture);

  glm_ivec3_zero(grid->count);
  glm_vec3_zero(grid->scale);

  probe_reflection_list_destroy(&grid->probes);
};

/*

 ▗▄▄▖▗▄▄▖ ▗▄▄▄▖▗▄▄▄     ▗▖   ▗▄▄▄▖ ▗▄▄▖▗▄▄▄▖
▐▌   ▐▌ ▐▌  █  ▐▌  █    ▐▌     █  ▐▌     █
▐▌▝▜▌▐▛▀▚▖  █  ▐▌  █    ▐▌     █   ▝▀▚▖  █
▝▚▄▞▘▐▌ ▐▌▗▄█▄▖▐▙▄▄▀    ▐▙▄▄▖▗▄█▄▖▗▄▄▞▘  █

 */

DynamicListStatus
probe_reflection_grid_list_create(ProbeReflectionGridList *list,
                                  const size_t capacity) {

  return dyli_create((void *)&list->entries, &list->capacity, &list->count,
                     sizeof(ProbeReflectionGrid), capacity,
                     "Probe Reflection Grid list");
}

DynamicListStatus
probe_reflection_grid_list_insert(ProbeReflectionGridList *list,
                                  ProbeReflectionGrid *entry) {

  // temporary (shader only accept static array for now)
  if (list->count == PROBE_REFLECTION_GRID_LIST_CAPACITY)
    return DynamicListStatus_UndefError;

  return dyli_insert((void *)&list->entries, &list->capacity, &list->count,
                     sizeof(ProbeReflectionGrid *), (void *)&entry, 1,
                     "Probe Reflection Grid list");
}

ProbeReflectionGrid *
probe_reflection_grid_list_new_entry(ProbeReflectionGridList *list) {

  // temporary (shader only accept static array for now)
  if (list->count == PROBE_REFLECTION_GRID_LIST_CAPACITY)
    return NULL;

  ProbeReflectionGrid *grid = rem_new_probe_reflection_grid();

  if (grid == NULL) {
    logger_add(
        LoggerFlag_Error,
        "Couldn't create new probe reflection grid. Max capacity reached.");
    return NULL;
  }

  DynamicListStatus insert = probe_reflection_grid_list_insert(list, grid);

  if (insert != DynamicListStatus_Success) {
    logger_add(LoggerFlag_Error,
               "Couldn't insert new probe reflection grid. Error code: %d.",
               insert);
    return NULL;
  }

  return grid;
}

DynamicListStatus
probe_reflection_grid_list_remove(ProbeReflectionGridList *list,
                                  ProbeReflectionGrid *entry) {
  return dyli_remove((void *)list->entries, &list->count,
                     sizeof(ProbeReflectionGrid *), (void *)entry,
                     "Probe Reflection Grid list");
}

DynamicListStatus
probe_reflection_grid_list_destroy(ProbeReflectionGridList *list) {
  return dyli_free((void *)list->entries, &list->capacity, &list->count);
}

size_t
probe_reflection_grid_list_probe_count(ProbeReflectionGridList *grid_list) {

  size_t count = 0;
  for (size_t i = 0; i < grid_list->count; i++)
    count += grid_list->entries[i]->probes.count;

  return count;
}

void probe_reflection_grid_update_boundbox(ProbeReflectionGrid *grid) {

  vec3 half;
  glm_vec3_scale(grid->scale, 0.5f, half);

  glm_vec3_add(grid->position, half, grid->boundbox.max);
  glm_vec3_sub(grid->position, half, grid->boundbox.min);
}
