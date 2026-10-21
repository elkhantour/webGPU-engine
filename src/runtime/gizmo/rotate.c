#include "rotate.h"

#include "./utils.h"
#include "backend/resource_manager.h"
#include "core.h"
#include "resources/loader/mbin.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/list.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/primitive/core.h"
#include "utils/color.h"
#include "utils/path.h"

void gizmo_rotation_create(MeshRefList *visual_list,
                           MeshRefList *interactive_list,
                           const GizmoCreateDescriptor *desc) {

  mesh_ref_list_create(visual_list, 4);

  // sphere mask first
  Mesh *sphere = rem_new_mesh();
  Primitive sphere_primitive;
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("rotate_mask.mbin"),
      .primitive = &sphere_primitive,
  });

  gizmo_create_mesh(sphere, &sphere_primitive,
                    &(color){0.2f, 0.2f, 0.2f, 0.0f});

  mesh_ref_list_insert(visual_list, sphere);

  // free primitive
  primitive_destroy(&sphere_primitive);

  // create axis then
  gizmo_create_handles(visual_list, interactive_list,
                       &(GizmoCreateMeshDescriptor){
                           .mbin_path = PATH_MBIN("rotate.mbin"),
                       });
}
