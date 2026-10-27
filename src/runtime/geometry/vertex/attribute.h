#ifndef _VERTEX_ATTRIBUTE_H_
#define _VERTEX_ATTRIBUTE_H_
#include <stddef.h>
#include <webgpu/webgpu.h>

#include "utils/defines.h"

#include "core.h"

typedef float vattr_t;

typedef struct {
  vattr_t *entries;
  size_t count;
  size_t capacity;
  WGPUBuffer buffer;
} VertexAttribute;

EXTERN_C_BEGIN

void vertex_attribute_find_equal_attr(Vertex *, VertexAttribute *,
                                      VertexAttributeType, VertexAttribute *);
void vertex_attribute_print(VertexAttribute *);

void vertex_attribute_set_position(VertexAttribute *, const vertex_position);
void vertex_attribute_set_normal(VertexAttribute *, const vertex_normal);
void vertex_attribute_set_color(VertexAttribute *, const vertex_color);
void vertex_attribute_set_uv(VertexAttribute *, const vertex_uv);

void vertex_attribute_set_position_at_index(VertexAttribute *,
                                            const vertex_position, const int);

void vertex_attribute_set_normal_at_index(VertexAttribute *,
                                          const vertex_normal, const int);

void vertex_attribute_set_color_at_index(VertexAttribute *, const vertex_color,
                                         const int);

void vertex_attribute_set_uv_at_index(VertexAttribute *, const vertex_uv,
                                      const int);

void vertex_attribute_set_position_add(VertexAttribute *,
                                       const vertex_position);
void vertex_attribute_set_normal_add(VertexAttribute *, const vertex_normal);
void vertex_attribute_set_color_add(VertexAttribute *, const vertex_color);
void vertex_attribute_set_uv_add(VertexAttribute *, const vertex_uv);

VertexStatus vertex_attribute_copy(VertexAttribute *, VertexAttribute *);
void vertex_attribute_destroy(VertexAttribute *);

/**
   Only free the CPU data, do not remove GPU buffer, to clear the buffer use
   vertex_attribute_destroy()
 */
static inline void
vertex_attribute_free_data(VertexAttribute *vertex_attribute) {
  if (vertex_attribute->entries != NULL)
    return;

  free(vertex_attribute->entries);
  vertex_attribute->entries = NULL;
  vertex_attribute->capacity = 0;
  vertex_attribute->count = 0;
}

EXTERN_C_END
#endif
