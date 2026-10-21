#ifndef _LOADER_MBIN_H_
#define _LOADER_MBIN_H_

#include <stdint.h>

#include "runtime/geometry/vertex/vertex.h"
#include "runtime/primitive/primitive.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/index.h"
#include "runtime/primitive/core.h"

typedef uint32_t mbin_count_t;

/* TODO: unify mesh loading system:
   1. either directly push mesh in scene list
   2. or provide data to create scene mesh (++ flexibility)
 */

typedef uint32_t mbin_int;
typedef float mbin_vertex_t;
typedef mbin_int mbin_index_t;
typedef mbin_int mbin_data_t;
typedef uint8_t vec_dimension_t;


typedef enum {
  MBINLoaderStatus_Success,
  MBINLoaderStatus_AllocFail,
  MBINLoaderStatus_UndefError,
} MBINLoaderStatus;

typedef struct {
  const char *path;
  VertexAttribute *vertex;
  VertexIndex *index;
} MBINLoadDescriptor;

#pragma pack(push, 1)
typedef struct {
  mbin_int vertex_count;
  mbin_int vertex_size_type;
  mbin_int index_count;
  mbin_int index_size_type;
  mbin_data_t data[]; // NOTE: Avoid using pointers (meaningless in for binary)
} MBINFile;
#pragma pack(pop)

typedef union {
  mbin_index_t u;
  mbin_vertex_t f;
} MBIN_U32Float;

typedef struct {
  const char *path;
  Primitive *primitive;
} MBINLoadPrimitiveDescriptor;

MBINLoaderStatus loader_mbin_load(MBINFile **, const char *);
MBINLoaderStatus loader_mbin_load_primitive(MBINLoadPrimitiveDescriptor *);

#endif
