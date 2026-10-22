#include "geometry.h"
#include <string.h>

void set_attribute_fallback(float *attr, const float *fallback,
                            const VertexAttributeDimension count) {
  for (VertexAttributeDimension i = 0; i < count; i++)
    attr[i] = fallback[i];
}

bool attribute_is_empty(const float *attr,
                        const VertexAttributeDimension count) {

  for (VertexAttributeDimension i = 0; i < count; i++)
    if (attr[i])
      return false;

  return true;
}

float *get_attributes(const cgltf_accessor *accessor) {

  cgltf_buffer_view *buffer_view = accessor->buffer_view;

  // combine accessor and buffer view offset
  size_t offset = buffer_view->offset + accessor->offset;
  return (float *)((uint8_t *)buffer_view->buffer->data + offset);
}

void primitive_vertex_lists_init(VertexAttribute *attributes, size_t count) {
  attributes->count = count;
  attributes->capacity = attributes->count;
  attributes->entries =
      (vattr_t *)calloc(attributes->capacity, sizeof(vattr_t));
}

/**
   Access vertex data from the file and interleave them within the referenced
   vertex attribute. Note that initially in GLTF format, all the vertex
   attributes are packed by attributes, meaning we have: [p1, p2, p3, p4, p5]
   [n1, n1, n3, n4, n5]
   etc.
 */
void primitive_vertex_attribute_create(VertexAttribute *vert_attr,
                                       cgltf_primitive *primitive) {

  const struct {
    const cgltf_attribute_type gltf_attribute;
    const VertexAttributeType type;
    const VertexAttributeDimension dimension;
    const VertexAttributeOffset offset;
    const vattr_t (*fallback)[4];
  } type_vertex_map[VERTEX_ATTRIBUTE_COUNT] = {
      {
          .gltf_attribute = cgltf_attribute_type_position,
          .type = VertexAttributeType_Position,
          .dimension = VertexAttributeDimension_Position,
          .offset = VertexAttributeOffset_Position,
      },
      {
          .gltf_attribute = cgltf_attribute_type_normal,
          .type = VertexAttributeType_Normal,
          .dimension = VertexAttributeDimension_Normal,
          .offset = VertexAttributeOffset_Normal,
          .fallback = &(vattr_t[4]){0.0f, 1.0f, 0.0f},
      },
      {
          .gltf_attribute = cgltf_attribute_type_tangent,
          .type = VertexAttributeType_Tangent,
          .dimension = VertexAttributeDimension_Tangent,
          .offset = VertexAttributeOffset_Tangent,
          .fallback = &(vattr_t[4]){-1.0f, 0.0f, 0.0f, -1.0f},
      },
      {
          .gltf_attribute = cgltf_attribute_type_color,
          .type = VertexAttributeType_Color,
          .dimension = VertexAttributeDimension_Color,
          .offset = VertexAttributeOffset_Color,
      },
      {
          .gltf_attribute = cgltf_attribute_type_texcoord,
          .type = VertexAttributeType_Uv,
          .dimension = VertexAttributeDimension_Uv,
          .offset = VertexAttributeOffset_Uv,
      },
  };

  // traverse and check if gltf attribute match
  for (size_t j = 0; j < primitive->attributes_count; j++) {

    const cgltf_attribute *attribute = &primitive->attributes[j];
    const cgltf_accessor *accessor = attribute->data;
    const cgltf_attribute_type gltf_type = attribute->type;

    float *data = get_attributes(accessor);

    for (size_t i = 0; i < VERTEX_ATTRIBUTE_COUNT; i++) {

      size_t index = 0;
      const VertexAttributeDimension dimension = type_vertex_map[i].dimension;
      const VertexAttributeOffset offset = type_vertex_map[i].offset;
      const cgltf_attribute_type gltf_attr = type_vertex_map[i].gltf_attribute;

      if (primitive->attributes[j].type == gltf_attr) {

        for (size_t k = 0; k < accessor->count * dimension; k += dimension) {
          memcpy(&vert_attr->entries[index + offset], &data[k],
                 sizeof(vattr_t) * dimension);

          index += VERTEX_STRIDE;
        }
      }
    }
  }
}



void primitive_vertex_index(VertexIndex *vert_index, cgltf_primitive *source) {

  // index
  cgltf_accessor *index_accessor = source->indices;
  cgltf_buffer_view *index_buffer_view = index_accessor->buffer_view;
  size_t index_offset = index_buffer_view->offset + index_accessor->offset;
  size_t index_count = index_accessor->count;
  uint16_t *raw_index_data =
      (uint16_t *)((uint8_t *)index_buffer_view->buffer->data + index_offset);

  // gltf provide index a uint16, however the engine sur uint32, need to
  // manually cast it
  // alloc on heap cause VLA throws stack overflow
  vindex_t *index_data = malloc(sizeof(vindex_t) * index_count);
  for (size_t i = 0; i < index_count; i++)
    index_data[i] = (vindex_t)raw_index_data[i];

  *vert_index = (VertexIndex){
      .entries = index_data,
      .capacity = index_count,
      .count = index_count,
  };
}
