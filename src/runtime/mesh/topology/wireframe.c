#include "wireframe.h"

#include <cglm/types.h>
#include <stdbool.h>
#include <stdlib.h>

#include "anchor.h"
#include "backend/context.h"
#include "backend/resource_manager.h"
#include "runtime/geometry/edge/core.h"
#include "runtime/geometry/line/core.h"
#include "runtime/geometry/vertex/core.h"
#include "string.h"
#include "utils/math.h"
#include "webgpu/webgpu.h"

static bool mesh_topology_wireframe_is_face(VertexIndex *);

static void mesh_topology_wireframe_store_unique_edges(EdgeHashSet *,
                                                       MeshTopology *);
static void mesh_topology_wireframe_create_points(EdgeHashSet *,
                                                  MeshTopologyAnchorList *,
                                                  MeshTopology *,
                                                  MeshTopologyWireframe *);

/**
   Create lines and anchors from base topology

    ================ 1. EDGE CREATION PHASE ================

   Create a edge hash set to store unique edges.
   To store unique data, hash tables are more efficient since we can directly
   check if the hash exist via the hashing function instead of every time
   traverse the array and compate the data.

      Object                                  Hash Table
    .--------.                              .-------------.
    | attr a | --.   .-------------.        |     ..      |
    |--------|   '-- |   Hashing   | --.    |-------------|
    | attr b | --'   '-------------'   |    |     ..      |
    '--------'                         |    |-------------|
                                       '--> |     34      |
                                            |-------------|
                                            |     ..      |
                                            '-------------'

               Data Attributes + Hash = Index

    =================== 2. CLUSTER PHASE ==================

    Create anchors hash set with base position as key:
    If positions a, b, c are equals:

        Positions                     Wireframe Index (Cluster)

                                           .-----------.
     [0] Xa Ya Za ... ----------.          | Hash(abc) |
     [1] Xb Yb Zb ... -------------------> |---.---.---|
     [2] Xc Yc Zc ... ----------'          | 0 | 1 | 2 |
                                           '---'---'---'

     ================== 3. MAPPING PHASE ==================

     Now that base indexes are clustered based on their positions
     we create a new anchor list where we map each base index to a wireframe
     cluster based on the base position.

     .-----------------------.---------------------.---------------------.
     |         Step          |        Input        |       Output        |
     |-----------------------+---------------------+---------------------|
     |     .------------.    |                     |                     |
     |     | Base Index |    |  0                  |                     |
     |     '------------'    |                     |                     |
     |           v           |---------------------+---------------------|
     |   .---------------.   |                     |                     |
     |   | Base Position |   |  0                  |  [1.0, -1.0, 1.0]   |
     |   '---------------'   |                     |                     |
     |           v           |---------------------+---------------------|
     |      .----------.     |                     |                     |
     |      |   Hash   |     |  [1.0, -1.0, 1.0]   |  348                |
     |      '----------'     |                     |                     |
     |           v           |---------------------|---------------------|
     |     .------------.    |                     |                     |
     |     |   Cluster  |    |  348                |  [2,3,4,1]          |
     |     '------------'    |                     |                     |
     |           v           |---------------------|---------------------|
     |     .-------------.   |                     |                     |
     |     |     Map     |   |  0                  |  [2,3,4,1]          |
     |     '-------------'   |                     |                     |
     '-----------------------'---------------------'---------------------'

     The point of remapping the clusters (initially ordered by hash) is to have
     an easier access to them afterward when we need to update the wireframe
     vertex based on the base topology.

     The mapped version will help to directly target the cluster based on the
     base index.

     This extra step is due to the fact that out cluster list is created based
   on unique edges, which doesn't include all the base index topology.

 */
MeshTopologyWireframeStatus
mesh_topology_wireframe_create(MeshTopology *src_topo,
                               MeshTopologyWireframe *dest_topo) {

  // reset existing wireframe buffer if exists
  // DELETEME:
  /*WGPUBuffer vertex_buffer = dest_topo->attribute.buffer;
  WGPUBuffer index_buffer = dest_topo->index.buffer;
  if (vertex_buffer) {
    wgpuBufferRelease(vertex_buffer);
    vertex_buffer = NULL;
  }

  if (index_buffer) {
    wgpuBufferRelease(index_buffer);
    index_buffer = NULL;
  }*/

  EdgeHashSet edges;
  edge_hash_set_create(&edges, 40);

  // store unique edges
  mesh_topology_wireframe_store_unique_edges(&edges, src_topo);

  // arrays from edges
  size_t vertex_capacity = edges.count * LINE_VERTEX_COUNT * VERTEX_STRIDE;
  dest_topo->attribute = (VertexAttribute){
      .entries = malloc(vertex_capacity * sizeof(vattr_t)),
      .capacity = vertex_capacity,
      .count = 0,
      .buffer = NULL,
  };

  size_t index_capacity = edges.count * LINE_INDEX_COUNT;
  dest_topo->index = (VertexIndex){
      .entries = malloc(index_capacity * sizeof(vindex_t)),
      .capacity = index_capacity,
      .count = 0,
      .buffer = NULL,
  };

  MeshTopologyAnchorList hashed_anchors;
  // create cluster anchor list
  mesh_topology_anchor_list_create(&hashed_anchors,
                                   MESH_TOPOLOGY_ANCHOR_LIST_DEFAULT_CAPACITY);

  // create mapped anchor list
  mesh_topology_anchor_list_create(&dest_topo->anchors, src_topo->index->count);

  // create points from unique edges
  mesh_topology_wireframe_create_points(&edges, &hashed_anchors, src_topo,
                                        dest_topo);

  // map wireframe index cluster based on base topology index
  mesh_topology_anchor_list_map(&hashed_anchors, src_topo, &dest_topo->anchors);

  edge_hash_set_destroy(&edges);

  return MeshTopologyWireframeStatus_Success;
}

/**
 * Create and populate buffer for the given data
 * We can't create buffer or any related GPU work on the create function
 * Cause the wireframe use multithreading and webgpu use the main thread
 * to manage resource lifecycle.
 */
MeshTopologyWireframeStatus
mesh_topology_wireframe_upload(MeshTopologyWireframe *topo) {

  {
    // upload vertex attributes
    if (topo->attribute.buffer != NULL)
      rem_destroy_buffer(&topo->attribute.buffer);

    const size_t va_size = topo->attribute.count * sizeof(vattr_t);

    topo->attribute.buffer = rem_new_buffer(&(WGPUBufferDescriptor){
        .label = "Wireframe Topology Vertex Attributes",
        .mappedAtCreation = false,
        .size = va_size,
        .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
    });

    rem_write_buffer(topo->attribute.buffer, 0, (void *)topo->attribute.entries,
                     va_size, REMWriteFlag_None);
  }

  {
    // upload vertex index
    if (topo->index.buffer != NULL)
      rem_destroy_buffer(&topo->index.buffer);

    const size_t vi_size = topo->index.count * sizeof(vindex_t);

    topo->index.buffer = rem_new_buffer(&(WGPUBufferDescriptor){
        .label = "Wireframe Topology Vertex Indexes",
        .mappedAtCreation = false,
        .size = vi_size,
        .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
    });

    rem_write_buffer(topo->index.buffer, 0, (void *)topo->index.entries,
                     vi_size, REMWriteFlag_None);
  }

  return MeshTopologyWireframeStatus_Success;
}

/**
   Convert MeshTopology Wireframe to a simple Mesh Topology
 */
MeshTopology mesh_topology_wireframe_vertex(MeshTopologyWireframe *topo) {
  return (MeshTopology){
      .attribute = &topo->attribute,
      .index = &topo->index,
  };
}

/**
   Traverse the base topology index, retrieve the position and update relative
   wireframe vertex attribute based on achor's index.
 */
int mesh_topology_wireframe_update(const MeshTopologyBase *base_topo,
                                   MeshTopologyWireframe *dest_topo) {

  for (size_t b = 0; b < base_topo->index.count; b++) {

    vindex_t base_index = base_topo->index.entries[b];
    vattr_t *base_vertex =
        &base_topo->attribute.entries[base_index * VERTEX_STRIDE];

    // look up base index in anchor list
    MeshTopologyAnchor *anchor = &dest_topo->anchors.entries[base_index];

    //  adjust anchor's linked index attributes
    if (anchor != NULL) {
      for (size_t w = 0; w < anchor->count; w++) {
        vindex_t wireframe_index = anchor->entries[w];
        vattr_t *wireframe_vertex =
            &dest_topo->attribute.entries[wireframe_index * VERTEX_STRIDE];

        // update wireframe vertex position
        memcpy(wireframe_vertex, base_vertex, sizeof(vertex_position));
      }

    } else {
      return MeshTopologyAnchorStatus_Unset;
    }
  }

  // update buffer or use map_write for direct link with CPU
  rem_write_buffer(dest_topo->attribute.buffer, 0, dest_topo->attribute.entries,
                   dest_topo->attribute.count * sizeof(vattr_t),
                   REMWriteFlag_None);

  return MeshTopologyWireframeStatus_Success;
}

/**
 Go through unique edges set add populate temp vertex & index array.
 Also add the newly creater index to the anchor array.
 */
void mesh_topology_wireframe_create_points(EdgeHashSet *edges,
                                           MeshTopologyAnchorList *hashed_list,
                                           MeshTopology *src_topo,
                                           MeshTopologyWireframe *dest_topo) {
  vec3 color = {0};

  for (size_t l = 0; l < edges->count; l++) {

    size_t index = edges->occupied[l];
    EdgeBucket *current_edge = &edges->entries[index];

    // base vertex
    int base_index = current_edge->key[0];
    float *base_attributes =
        &src_topo->attribute->entries[base_index * VERTEX_STRIDE];
    Vertex base_vertex = vertex_from_array(base_attributes);

    // opposite vertex
    int opp_index = current_edge->key[1];
    float *opp_attributes =
        &src_topo->attribute->entries[opp_index * VERTEX_STRIDE];
    Vertex opp_vertex = vertex_from_array(opp_attributes);

    // add points to vertex attributes and index
    line_add_point(base_vertex.position, opp_vertex.position, color,
                   &dest_topo->attribute, &dest_topo->index);

    /**
       Based on the line add point function, the triangles are build with the
       following pattern:

              A
         c    .    b        Pattern:    a, b, c, a, c, d
          o---;---o                     ^  ^  ^        ^
          |\  ;   |         Offset:     0  1  2        5
          | \ ;   |                     ^  ^  ^        ^
          |  \;   |         Anchor:     A  B  B        A
          |   ;   |
          |   ;\  |
          |   ; \ |
          |   ;  \|
          o---;---o
         d    '    a
              B
     */

    // append base anchor
    const size_t index_len = dest_topo->index.count;
    vindex_t temp_base_index[] = {
        dest_topo->index.entries[index_len - 6],
        dest_topo->index.entries[index_len - 1],
    };

    mesh_topology_anchor_list_insert(hashed_list, &base_vertex.position,
                                     temp_base_index, 2);

    // append opp anchor
    vindex_t temp_opp_index[] = {
        dest_topo->index.entries[index_len - 5],
        dest_topo->index.entries[index_len - 4],
    };

    mesh_topology_anchor_list_insert(hashed_list, &opp_vertex.position,
                                     temp_opp_index, 2);
  }
}

void mesh_topology_wireframe_store_unique_edges(EdgeHashSet *edges,
                                                MeshTopology *src_topo) {

  // store edges

  /* Check if source topology is a 'line' of 'face' type structure
     Line type have an even index count ( len(i) % 2 == 0)
     Whereas Face type have an odd index count as they are composed of
     triangles.
     If the source topology is a Line type, then we need to triangulate first
     before.
   */

  const bool is_face = mesh_topology_wireframe_is_face(src_topo->index);
  const int stride = is_face ? 3 : 2;
  for (int i = 0; i < src_topo->index->count; i += stride) {

    unsigned int a = src_topo->index->entries[i];
    unsigned int b = src_topo->index->entries[i + 1];

    EdgeKey ab = {MIN(a, b), MAX(a, b)};

    edge_hash_set_insert(edges, ab);

    if (is_face) {
      unsigned int c = src_topo->index->entries[i + 2];
      EdgeKey bc = {MIN(b, c), MAX(b, c)};
      EdgeKey ca = {MIN(a, c), MAX(a, c)};

      edge_hash_set_insert(edges, bc);
      edge_hash_set_insert(edges, ca);
    }
  }
}

/**
   Detect if a index list is Face or Line type.
   However this method is not robust shall be replaced with a more type or
   enum-based way. (TODO)
 */
bool mesh_topology_wireframe_is_face(VertexIndex *index) {

  vindex_t *id = index->entries;

  for (size_t i = 1; i < MIN(6, index->count); i++) {
    if (id[i] != id[i - 1] + 1)
      return true;
  }

  return false;
}
