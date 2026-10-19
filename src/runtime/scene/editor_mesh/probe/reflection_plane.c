#include "reflection_plane.h"

#include <cglm/types.h>
#include <cglm/vec3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "backend/ubo.h"
#include "resources/loader/loader.mbin.h"
#include "runtime/geometry/aabb/aabb.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/transform.h"
#include "runtime/mesh/uniform.h"
#include "runtime/primitive/core.h"
#include "runtime/probe/reflection/core.h"
#include "runtime/probe/reflection/plane.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/editor_mesh/builder/wireframe.h"
#include "runtime/scene/editor_mesh/list.h"
#include "runtime/systems/scene_editor_mesh_system.h"
#include "utils/color.h"
#include "utils/path.h"

void sem_probe_reflection_plane_create(SceneEditorMeshList *list,
                                       ProbeReflectionPlane *probe,
                                       const SEMCreateDescriptor *desc) {

  const uint16_t sem_mesh_count = 4;
  sem_list_create(list, sem_mesh_count, "Probe Reflection Plane",
                  RegEntryType_SceneEditorMeshList_ProbeReflectionPlane);

  list->origin = &list->entries[SEM_LIST_ORIGIN_INDEX];

  /*

     === Distance Cube ===

   */
  SceneEditorMesh *probe_cube = sem_list_new_entry(list);
  probe_cube->mesh = rem_new_mesh();

  Primitive primitive_cube;
  // TODO: cache MBIN
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("cube.mbin"),
      .primitive = &primitive_cube,
  });

  SEMCreateWireframeDescriptor wireframe_cube_desc = {
      .color = &(color){0.0f, 0.0f, 0.0f, 1.0f},
      .index = &primitive_cube.index,
      .vertex = &primitive_cube.vertex,
      .name = "sem probe reflection plane",
      .thickness = SEM_WIREFRAME_LINE_THICKNESS,
  };

  sem_create_wireframe(probe_cube->mesh, &wireframe_cube_desc);

  mesh_set_scale(probe_cube->mesh, (vec3){
                                       probe->scale[0],
                                       probe->distance,
                                       probe->scale[2],
                                   });
  mesh_set_position(probe_cube->mesh, probe->position);
  /*

    === Main refletion plane ===

   */
  SceneEditorMesh *probe_plane = sem_list_new_entry(list);
  probe_plane->mesh = rem_new_mesh();

  Primitive primitive;
  // TODO: cache MBIN
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("plane.mbin"),
      .primitive = &primitive,
  });

  SEMCreateWireframeDescriptor wireframe_desc = {
      .color = &(color){0.0f, 0.0f, 0.0f, 1.0f},
      .index = &primitive.index,
      .vertex = &primitive.vertex,
      .name = "sem probe reflection plane",
      .thickness = SEM_WIREFRAME_LINE_THICKNESS,
  };

  sem_create_wireframe(probe_plane->mesh, &wireframe_desc);

  mesh_set_scale(probe_plane->mesh, probe->scale);
  mesh_set_position(probe_plane->mesh, probe->position);

  mesh_child_add(probe_cube->mesh, probe_plane->mesh);

  /*

   === Normal Arrow ===

 */
  SceneEditorMesh *probe_arrow = sem_list_new_entry(list);
  probe_arrow->mesh = rem_new_mesh();

  Primitive primitive_arrow;
  // TODO: cache MBIN
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("arrow.mbin"),
      .primitive = &primitive_arrow,
  });

  SEMCreateWireframeDescriptor wireframe_arrow_desc = {
      .color = &(color){0.0f, 0.0f, 0.0f, 1.0f},
      .index = &primitive_arrow.index,
      .vertex = &primitive_arrow.vertex,
      .name = "sem probe reflection plane",
      .thickness = SEM_WIREFRAME_LINE_THICKNESS,
  };

  sem_create_wireframe(probe_arrow->mesh, &wireframe_arrow_desc);
  mesh_set_position(probe_arrow->mesh, probe->position);

  mesh_child_add(probe_cube->mesh, probe_arrow->mesh);

  // apply sem commons attributes
  for (uint8_t i = 0; i < list->count; i++) {

    list->entries[i].target = probe;
    list->entries[i].target_list_index = SCENE_EDITOR_MESH_TARGET_UNDEFINED;

    list->entries[i].transform_callback[GizmoMode_Position] =
        sem_system_probe_reflection_plane_set_position;
    list->entries[i].transform_callback[GizmoMode_Rotation] =
        sem_system_probe_reflection_plane_set_rotation;
    list->entries[i].transform_callback[GizmoMode_Scale] =
        sem_system_probe_reflection_plane_set_scale;

    list->entries[i].select_callback = sem_wireframe_select_callback;
    list->entries[i].deselect_callback = sem_wireframe_deselect_callback;
  }
}
