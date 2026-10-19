#include "translate.h"

#include "./utils.h"
#include "backend/resource_manager.h"
#include "backend/shaders/core.h"
#include "core.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/mesh/transform.h"
#include "runtime/primitive/plane.h"
#include "runtime/shader/core.h"
#include "utils/path.h"
#include <stdint.h>
#include <stdio.h>

#include "utils/system.h"

void gizmo_position_create(MeshRefList *visual_list,
                           MeshRefList *interactive_list,
                           const GizmoCreateDescriptor *desc) {

  gizmo_create_handles(visual_list, interactive_list,
                       &(GizmoCreateMeshDescriptor){
                           .mbin_path = PATH_MBIN("translate.mbin"),
                           .offset = {0.0f, 0.1f, 0.0f},
                       });

  {
    Primitive prim_plane = primitive_plane();
    Mesh *plane[GIZMO_AXIS_COUNT];
    float plane_scale = 0.2f;
    float distance = 1.3f;

    for (uint8_t i = 0; i < GIZMO_AXIS_COUNT; i++) {
      plane[i] = rem_new_mesh();

      color plane_color;
      glm_vec4_copy((float *)gizmo_handle_color[3 + i], plane_color);

      gizmo_create_mesh(plane[i], &prim_plane, &plane_color);

      vertex_attribute_set_position_add(&plane[i]->topology.base.attribute,
                                        (vec3){distance, 0.0f, distance});

      mesh_topology_base_update_buffer(&plane[i]->topology.base);

      mesh_set_scale(plane[i], (vec3){plane_scale, plane_scale, plane_scale});
      mesh_set_rotation(plane[i], (vec3){
                                      (i == 2) * -90.0f,
                                      0.0f,
                                      (i == 0) * 90.0f,
                                  });

      mesh_ref_list_insert(visual_list, plane[i]);
      mesh_ref_list_insert(interactive_list, plane[i]);
    }

    // primitive_destroy(&prim_plane);
  }
}
