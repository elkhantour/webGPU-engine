#include "camera.h"

#include <stddef.h>
#include <stdint.h>

#include "backend/registry.h"
#include "backend/resource_manager.h"
#include "backend/theme/core.h"
#include "resources/loader/mbin.h"
#include "runtime/camera/core.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/group.h"
#include "runtime/geometry/vertex/index.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/topology/base.h"
#include "runtime/mesh/topology/wireframe.h"
#include "runtime/mesh/transform.h"
#include "runtime/primitive/core.h"
#include "runtime/scene/add.h"
#include "runtime/scene/core.h"
#include "runtime/scene/editor_mesh/builder/builder.h"
#include "runtime/scene/editor_mesh/list.h"
#include "runtime/systems/scene_editor_mesh_system.h"
#include "utils/color.h"
#include "utils/path.h"

void sem_camera_create(SceneEditorMeshList *list, Camera *camera,
                       const SEMCreateDescriptor *desc) {

  const uint8_t sem_mesh_count = 2;
  sem_list_create(list, sem_mesh_count, "Camera",
                  RegEntryType_SceneEditorMeshList_Camera);

  list->origin = &list->entries[SEM_LIST_ORIGIN_INDEX];

  // create new mesh in the mesh list
  SceneEditorMesh *icon = sem_list_new_entry(list);
  icon->mesh = rem_new_mesh();
  icon->target = camera;
  icon->target_list_index = desc->target_list_index;

  const ThemeIconCell *icon_uv = theme_icon_cell(&g_theme, ThemeIcon_Camera);

  // create icon mesh
  sem_create_billboard(icon->mesh,
                       &(SEMCreateBillboardDescriptor){
                           .view = theme_icon_atlas(&g_theme),
                           .position = &camera->position,
                           .scale = &SEM_BILLBOARD_SCALE,
                           .uv0 = {icon_uv->uv0[0], icon_uv->uv0[1]},
                           .uv1 = {icon_uv->uv1[0], icon_uv->uv1[1]},
                       });

  // create box mesh
  Primitive cube_primitive;
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("cube.mbin"),
      .primitive = &cube_primitive,
  });

  // set callback
  icon->transform_callback[GizmoMode_Position] = sem_system_camera_set_position;
  icon->transform_callback[GizmoMode_Rotation] = sem_system_camera_set_rotation;
  icon->transform_callback[GizmoMode_Scale] = sem_system_camera_set_scale;

  SceneEditorMesh *cube = sem_list_new_entry(list);
  cube->mesh = rem_new_mesh();
  // create manually wirerfame since sem is part of fixed rendering, so the
  // mesh topology generation isn't automatically handled.
  sem_create_wireframe(cube->mesh,
                       &(SEMCreateWireframeDescriptor){
                           .color = &(color){1.0f, 0.7f, 0.4f, 1.0f},
                           .thickness = SEM_WIREFRAME_LINE_THICKNESS,
                           .vertex = &cube_primitive.vertex,
                           .index = &cube_primitive.index,
                           .name = "sem camera",
                       });

  // init vertex groups
  VertexGroupSet *cube_group = &cube->mesh->topology.base.group;
  if (vertex_group_set_create(cube_group, VERTEX_GROUP_CAPACITY_DEFAULT) ==
      VertexGroupStatus_Success) {

    vertex_group_set_insert(cube_group,
                            &(VertexGroup){
                                .name = "front",
                                .entries = (vindex_t[]){0, 2, 4, 10},
                                .count = 4,
                                .capacity = 4,
                            });

    vertex_group_set_insert(cube_group, &(VertexGroup){
                                            .name = "back",
                                            .entries = (vindex_t[]){1, 9, 7, 8},
                                            .count = 4,
                                            .capacity = 4,
                                        });
  }

  // translate cube upward
  mesh_set_position(cube->mesh, (vec3){0.0f, 1.0f, 0.0f});

  // set callback
  cube->transform_callback[GizmoMode_Position] = sem_system_camera_set_position;
  cube->transform_callback[GizmoMode_Rotation] = sem_system_camera_set_rotation;
  cube->transform_callback[GizmoMode_Scale] = sem_system_camera_set_scale;

  // set fov deformation
  sem_camera_fov(list, 90.0f);
}

void sem_camera_lookat(SceneEditorMeshList *list, vec3 position, vec3 target) {

  // update icon position
  Mesh *icon = list->entries[0].mesh;
  mesh_set_position(icon, position);

  // update camera matrix
  // camera_lookat(sem->target, position, target);

  // update sem cube mesh rotation
  Mesh *cube = list->entries[1].mesh;
  mesh_lookat(cube, position, target);
}

/**
   Deform camera sem mesh according to fov
   (Goes from cube to prism)
 */
void sem_camera_fov(SceneEditorMeshList *list, float fov) {

  /**
        checked on blender to get faces vertex indices:

             4.-----------.0     Back (CW):  4, 0, 1, 5
             /|          /|      Front (CW): 6, 2, 3, 7
            / |         / |
          6'--+--------'2 |
           |  |        |  |
           | 5'--------|--'1
           | /         | /
           |/          |/
          7'-----------'3

   */

  size_t cube_mesh_id = 1;

  Mesh *cube = list->entries[cube_mesh_id].mesh;
  // get vertex attributes + index for line mesh composition
  VertexAttribute *cube_base_attribute = mesh_topology_base(cube).attribute;

  VertexGroupSet *cube_group = &cube->topology.base.group;
  VertexGroup *back_face = vertex_group_set_find(cube_group, "back");

  mesh_topology_base_set_position(&cube->topology.base, back_face,
                                  &(vec3){0.0f, 0.0f, -1.0f});

  mesh_topology_base_set_scale(&cube->topology.base, back_face,
                               &(vec3){0.5f, 0.5f, 0.5f});

  // update wireframe topology according to base
  mesh_topology_wireframe_update(&cube->topology.base,
                                 &cube->topology.wireframe);
}
