#include "utils.h"

#include <cglm/mat4.h>
#include <cglm/types.h>
#include <cglm/util.h>
#include <cglm/vec2.h>
#include <cglm/vec3.h>
#include <stdint.h>

#include "core.h"
#include "runtime/geometry/triangle/core.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/core.h"
#include "runtime/geometry/vertex/index.h"
#include "runtime/mesh/core.h"
#include "runtime/scene/debug/ray.h"
#include "runtime/texture/core.h"
#include "runtime/texture/processing.h"
#include "runtime/texture/write.h"
#include "utils/point.h"

#ifdef AO_BAKE_HIT_COUNT
int g_debug_ao_bake_hit_count = 0;
#endif

float ao_bake_vertex(const AOBakeVertexDescriptor *desc) {

  int vertex_hit = 0;
  vec3 rays[AO_LOCAL_RAY_MAX_AMOUNT];
  uint16_t ray_count =
      glm_min(desc->settings->sample_amount, AO_LOCAL_RAY_MAX_AMOUNT);

  vec3 ray_color = {0.0f, 0.0f, 1.0f};

  // Generate random ray in an hemisphere oriented on vertex normal
  hemisphere_random_points(desc->vertex->normal, ray_count, rays);

  vec3 world_position;
  glm_mat4_mulv3(desc->mesh->model, desc->vertex->position, 1.0f,
                 world_position);

  for (int ray = 0; ray < ray_count; ray++) {

    vec3 ray_direction;
    glm_vec3_scale(rays[ray], desc->settings->max_distance, ray_direction);
    glm_vec3_add(world_position, ray_direction, ray_direction);

    // traverse mesh triangles
    for (size_t t = 0; t < desc->mesh->topology.base.index.count; t += 3) {
      Triangle triangle;
      ao_bake_mesh_triangle(&triangle, desc->mesh, t);
      vec3 hit;
      triangle_raycast(&triangle, world_position, ray_direction,
                       desc->settings->max_distance, hit);

      // is occluded
      if (hit[0] || hit[1] || hit[2]) {
        vertex_hit++;
        glm_vec3_copy((vec3){1.0f, 0.0f, 0.0f}, ray_color);
      }

      if (desc->debug.line && ray < desc->debug.max_ray)
        scene_debug_ray_add_point(desc->debug.line, world_position,
                                  ray_direction, ray_color);
    }
  }

  // accumulated AO (actually unused for now)
  return 1 - ((float)vertex_hit / ray_count);
}

/**
   Raycast from the source surface towards a certain direction an check if the
   ray traverse a triangle of the compared mesh
 */
bool ao_bake_raycast(const AOBakeRaycastDescriptor *desc) {

  bool ray_hit = false;

  // Raycast from ray origin (source surface) towards each compare mesh
  // triangles
  for (size_t i = 0; i < desc->compare_mesh->topology.base.index.count;
       i += 3) {
    Triangle compare_triangle;
    ao_bake_mesh_triangle(&compare_triangle, desc->compare_mesh, i);
    vec3 hit;

    // is occluded
    // transpose hit point to triangle UV space
    // 1. retrieve hit position and translate it to uv space
    // 2. scale to the texture coordinates
    // 3.write pixel to texture
    if (triangle_raycast(&compare_triangle, *desc->ray_origin,
                         *desc->ray_direction, desc->max_distance,
                         hit) == TriangleStatus_RaycastSuccess) {
#ifdef AO_BAKE_HIT_COUNT
      g_debug_ao_bake_hit_count++;
#endif

      vec2 compare_uv, source_uv;
      triangle_point_to_uv(desc->source_triangle, *desc->ray_origin, source_uv);
      glm_vec2_scale(source_uv, desc->texture_size, source_uv);
      texture_write_pixel(desc->source_texture, 0, source_uv,
                          TextureWriteMethod_Replace);

      // do the same for compare mesh
      if (desc->compare_texture) {
        triangle_point_to_uv(&compare_triangle, hit, compare_uv);
        glm_vec2_scale(compare_uv, desc->texture_size, compare_uv);
        texture_write_pixel(desc->compare_texture, 0, compare_uv,
                            TextureWriteMethod_Replace);
      }

      ray_hit = true;
    }
  }

  return ray_hit;
}

/**
   Return a triangle of a mesh starting at a certain index
 */
void ao_bake_mesh_triangle(Triangle *triangle, Mesh *mesh, size_t index) {

  vattr_t *base_attribute = mesh->topology.base.attribute.entries;
  vindex_t *base_index = mesh->topology.base.index.entries;

  Vertex *src_vert[3] = {
      &triangle->a,
      &triangle->b,
      &triangle->c,
  };

  for (uint8_t i = 0; i < 3; i++) {

    *src_vert[i] = vertex_from_array(
        &base_attribute[base_index[index + i] * VERTEX_STRIDE]);

    // put vertex to worldspace
    glm_mat4_mulv3(mesh->model, src_vert[i]->position, 1.0f,
                   src_vert[i]->position);
  }
}

void ao_bake_process_texture(Texture *texture) {
  texture_remap(texture, 0, 1);

  // 1st pass blur
  texture_blur(texture, 3, 1.0f);
  // 2nd pass blur
  texture_blur(texture, 3, 1.0f);
}
