#ifndef _LOADER_GLTF_GEOMETRY_H_
#define _LOADER_GLTF_GEOMETRY_H_

#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/core.h"
#include "runtime/geometry/vertex/index.h"
#include <cgltf/cgltf.h>

float *get_attributes(const cgltf_accessor *);

void set_attribute_fallback(float *, const float *,
                            const VertexAttributeDimension);
bool attribute_is_empty(const float *, const VertexAttributeDimension);

void primitive_vertex_index(VertexIndex *, cgltf_primitive *);

void primitive_vertex_attribute_create(VertexAttribute *, cgltf_primitive *);
void primitive_vertex_lists_init(VertexAttribute *, size_t);

#endif
