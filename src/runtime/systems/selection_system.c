#include "selection_system.h"
#include "backend/render_pass/core.h"
#include "backend/renderer/core.h"
#include "backend/shaders/core.h"
#include "backend/shadow_map/draw.h"
#include "runtime/camera/raycast/core.h"
#include "runtime/gizmo/core.h"
#include "runtime/input/core.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/ref_list.h"
#include "runtime/scene/editor_mesh/core.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/scene/selection/core.h"
#include "runtime/scene/selection/filter.h"
#include "runtime/systems/mesh_route_system.h"
#include "runtime/systems/scene_system.h"
#include <stdint.h>

// clang-format off
static const selection_system_highlight_callback highlight_callbacks[] = {
    [SceneSelectionType_Mesh] = selection_system_callback_mesh_highlight,
    [SceneSelectionType_MeshShadow] = selection_system_callback_mesh_highlight,
    [SceneSelectionType_SEM] = selection_system_callback_sem_highlight,
};

static const selection_system_transform_callback transform_callbacks[] = {
    [SceneSelectionType_Mesh] = selection_system_callback_mesh_transform,
    [SceneSelectionType_MeshShadow] = selection_system_callback_mesh_shadow_transform,
    [SceneSelectionType_SEM] = selection_system_callback_sem_transform,
};
// clang-format on

SelectionSystemCallbackData selection_system_event_payload = {0};
MeshRefListArray selection_system_gizmo_raycast_list = {0};

void selection_system_init(SceneSelection *selection, Scene *scene,
                           Renderer *renderer) {

  selection_system_event_payload = (SelectionSystemCallbackData){
      .selection = selection,
      .renderer = renderer,
      .scene = scene,
  };

  scene_selection_init(selection);

  selection_system_init_mouse_events(selection, scene, renderer);
  selection_system_init_key_events(selection, scene, renderer);

  renderer_add_draw_callback(renderer, selection_system_draw_callback,
                             (void *)&selection_system_event_payload,
                             RendererDrawMode_All);
}

/**
   To transform the selected meshes and gizmo we poll the mouse event and check
   if the selection pipeline has count.

   Basically our camera raycast/ html events are only used to:
     1. push/pop mesh from the selection array (on right click)
     2. update the gizmo transform active axis (on left click)

    We then constantly through the loop:
     1. check if the mouse is pressed

    According to those checkes we then transform the meshes.

 */
void selection_system_draw_callback(void *data) {

  SelectionSystemCallbackData *payload = (SelectionSystemCallbackData *)data;
  Scene *scene = payload->scene;
  SceneSelection *selection = payload->selection;
  Renderer *renderer = payload->renderer;
  Gizmo *gizmo = &scene->gizmo;

  if (gizmo->cache.init_distance != 0.0f) {

    // use each selection filters transform callbacks on their respective meshes
    for (int i = 0; i < SCENE_SELECTION_TYPE_COUNT; i++) {

      vec3 delta;

      // 1. transform gizmo
      gizmo_transform_callback gizmo_transform_callback =
          gizmo->transform_callback[gizmo->mode];

      gizmo_transform_callback(gizmo, scene->active_camera, &scene->viewport,
                               &delta);

      // 2. transform filter selection with delta calculated by gizmo
      SceneSelectionFilter *filter = &selection->filters[i];

      // look-up filter transform callback depending on gizmo mode
      // (loc/rot/scale)
      selection_system_transform_callback mesh_transform_callback =
          transform_callbacks[i];

      SceneSelectionTransform transform = {
          .selection = &filter->selection,
          .delta = &delta,
          .axis = gizmo->axis,
          .transform_mode = gizmo->mode,
          .scene = scene,
          .renderer = renderer,
      };
      mesh_transform_callback(&transform);

      gizmo_update_ubo(gizmo, scene->ubo);
    }
  }
}

/**
   Handle the overall flow of selection state, including:
   - Find the mesh relative filter
   - Add mesh to filter selection list
   - Trigger highlight callback
   - Handle the gizmo visibility

   This function is used as the main function to add/remove mesh from the
   selection depending on the trigger method (click, shortcut, UI)
 */
void selection_system_toggle_mesh(SceneSelection *selection, Scene *scene,
                                  Renderer *renderer, Mesh *mesh) {

  bool selected;
  size_t filter_index;
  SceneSelectionFilter *filter = scene_selection_find_filter_of_mesh(
      selection, mesh, &selected, &filter_index);

  if (filter == NULL)
    return;

  if (!selected) {

    if (input_key(INPUT_KEY_SHIFT) == false)
      scene_selection_empty(selection);

    scene_selection_filter_selection_add_mesh(filter, mesh, NULL);
  } else {
    scene_selection_filter_selection_remove_mesh(filter, mesh);
  }

  // handle gizmo
  Gizmo *gizmo = &scene->gizmo;

  if (scene_selection_count(selection) > 0) {
    selection_system_update_gizmo_pos_to_selection(&scene->gizmo, selection,
                                                   scene->ubo);

    mesh_route_system_show_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);
  } else {
    mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);
  }

  highlight_callbacks[filter_index](selection, scene, renderer,
                                    &filter->selection, NULL);
}

/**

   ▗▖ ▗▖▗▄▄▄▖ ▗▄▄▖▗▖ ▗▖▗▖   ▗▄▄▄▖ ▗▄▄▖▗▖ ▗▖▗▄▄▄▖
   ▐▌ ▐▌  █  ▐▌   ▐▌ ▐▌▐▌     █  ▐▌   ▐▌ ▐▌  █
   ▐▛▀▜▌  █  ▐▌▝▜▌▐▛▀▜▌▐▌     █  ▐▌▝▜▌▐▛▀▜▌  █
   ▐▌ ▐▌▗▄█▄▖▝▚▄▞▘▐▌ ▐▌▐▙▄▄▖▗▄█▄▖▝▚▄▞▘▐▌ ▐▌  █


   Function triggered when meshes are selected.

   We hereby mark the meshes as highlighted by pushing their pointer to the
   Scene Selection List.

   We then enables the meshes in each render pass that uses this Scene Selection
   List as Source list.

   NOTE: To enable the outline effect we need to target 2 passes:
   1. The stencil pass (included in the Default pass): the drawn mesh using
   shader will just be used to write in the stencil.
   2. The outline pass (included in the Outline pass since it need to always be
   in front): read the stencil result from the previous pass and manipulate the
   stencil.

 */

static bool selection_contains(const SceneSelectionObjectList *list,
                               const Mesh *mesh) {
  for (size_t i = 0; i < list->count; i++)
    if (list->entries[i].mesh == mesh)
      return true;
  return false;
}

// Line draw modes (wireframe/boundbox) show the selection as a tint on the
// wireframe shader. Same shader for both modes, so it is written once.
static void mesh_set_highlight_color(Mesh *mesh, const color *c) {
  Shader *shader = mesh_shader(mesh, MeshShader_Wireframe);
  if (shader)
    shader_update_uniform_data(shader, 1, 0, (void *)c, ShaderUpdateFlag_None);
}

void selection_system_callback_mesh_highlight(
    SceneSelection *selection, Scene *scene, Renderer *renderer,
    SceneSelectionObjectList *selected_objects, void *data) {

  static const color highlight_color = {1.0f, 0.0f, 0.0f, 1.0f};
  static const color default_color = {0.0f, 0.0f, 0.0f, 1.0f};

  // meshes currently highlighted are the ones holding the Selection flag
  const MeshRefList *highlighted = NULL;
  if (scene_mesh_query_with_flags(scene, RendererMeshFlag_Selection,
                                  &highlighted) != SceneMeshCacheStatus_Success)
    return;

  MeshRefList changed;
  if (mesh_ref_list_create(&changed, SCENE_MESH_LIST_DEFAULT_CAPACITY) !=
      DynamicListStatus_Success)
    return;

  // 1. deselect. mesh_remove_flags is a pure write, so the cache list we are
  //    iterating stays untouched until the refresh below.
  for (size_t i = 0; i < highlighted->count; i++) {
    Mesh *mesh = highlighted->entries[i];

    if (selection_contains(selected_objects, mesh))
      continue;

    if (mesh_remove_flags(mesh, RendererMeshFlag_Selection)) {
      mesh_set_highlight_color(mesh, &default_color);
      mesh_ref_list_insert(&changed, mesh);
    }
  }

  // 2. select
  for (size_t i = 0; i < selected_objects->count; i++) {
    Mesh *mesh = selected_objects->entries[i].mesh;
    if (mesh == NULL)
      continue;

    if (mesh_add_flags(mesh, RendererMeshFlag_Selection)) {
      mesh_set_highlight_color(mesh, &highlight_color);
      mesh_ref_list_insert(&changed, mesh);
    }
  }

  // 3. refresh the cache once, route the changed meshes, update the counts
  mesh_route_system_update_mesh_ref_list(scene, renderer, &changed);

  mesh_ref_list_free(&changed);
}

/**
   For SEM Object we use a OOP approach (similar to the transform callback)
   where each SEM Mesh has its own transform and highlight callback.

   Since SEM are such polymorphic objects, it just easier and less messy to
   hook each mesh a transform and highlight callback.
 */
void selection_system_callback_sem_highlight(SceneSelection *selection,
                                             Scene *scene, Renderer *renderer,
                                             SceneSelectionObjectList *list,
                                             void *data) {

  // disable selected ones
  SceneEditorMeshListArray *sem_array = &scene->editor_meshes;
  for (size_t i = 0; i < sem_array->count; i++)
    for (size_t j = 0; j < sem_array->entries[i].count; j++) {
      SceneEditorMesh *sem = &sem_array->entries[i].entries[j];
      if (sem->deselect_callback)
        sem->deselect_callback(&(SEMHighlightCallback){sem});
    }

  // enable selected ones
  for (size_t i = 0; i < list->count; i++) {
    const RegEntry *reg_obj = reg_lookup(list->entries[i].target);
    SceneEditorMeshList *sem_list = (SceneEditorMeshList *)reg_obj->ptr;

    for (size_t j = 0; j < sem_list->count; j++) {
      SceneEditorMesh *sem = &sem_list->entries[j];
      if (sem->select_callback)
        sem->select_callback(&(SEMHighlightCallback){sem});
    }
  }
}

/*

   ▗▄▄▄▖▗▄▄▖  ▗▄▖ ▗▖  ▗▖ ▗▄▄▖▗▄▄▄▖ ▗▄▖ ▗▄▄▖ ▗▖  ▗▖
     █  ▐▌ ▐▌▐▌ ▐▌▐▛▚▖▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌ ▐▌▐▛▚▞▜▌
     █  ▐▛▀▚▖▐▛▀▜▌▐▌ ▝▜▌ ▝▀▚▖▐▛▀▀▘▐▌ ▐▌▐▛▀▚▖▐▌  ▐▌
     █  ▐▌ ▐▌▐▌ ▐▌▐▌  ▐▌▗▄▄▞▘▐▌   ▝▚▄▞▘▐▌ ▐▌▐▌  ▐▌

 */

static const mesh_transform_callback transform_callback_mesh[] = {
    [GizmoMode_Position] = mesh_set_position,
    [GizmoMode_Rotation] = mesh_set_rotation,
    [GizmoMode_Scale] = mesh_set_scale,
};

static inline void
selection_system_callback_mesh_transform_core(Mesh *, vec3 *,
                                              SceneSelectionTransform *);

/**
   Update the mesh probes uniform (planar and grid) if the mesh goes within or
   out of the probe bounds/radius
 */
void selection_system_mesh_update_probe_uniform(
    Mesh *mesh, ProbeReflectionGridList *grid_list,
    ProbeReflectionPlaneList *plane_list, UBOManager *ubo) {
  size_t i = 0;

  MeshUniform *uniform = mesh_uniform(mesh);

  for (i = 0; i < plane_list->count; i++) {

    ProbeReflectionPlane *probe = &plane_list->entries[i];
    bool intersect =
        aabb_intersect(&mesh->topology.boundbox.world, &probe->boundbox);

    if (intersect)
      mesh_uniform_set_probe_reflection_plane(mesh, ubo);
    else
      mesh_uniform_clear_probe_reflection_plane(mesh, ubo);
  }

  for (i = 0; i < grid_list->count; i++) {

    ProbeReflectionGrid *grid = grid_list->entries[i];
    bool intersect =
        aabb_intersect(&mesh->topology.boundbox.world, &grid->boundbox);

    // if (intersect)
  }
}

void selection_system_callback_mesh_transform_core(
    Mesh *mesh, vec3 *init_attribute, SceneSelectionTransform *desc) {

  // calculate offset from delta
  vec3 offset_attribute;
  glm_vec3_add(*init_attribute, *desc->delta, offset_attribute);

  // transform mesh
  transform_callback_mesh[desc->transform_mode](mesh, offset_attribute);

  mesh_uniform_update(mesh);
  ubo_update_queue_insert(desc->scene->ubo, UBOType_Mesh, mesh->ubo_slot.id);
}

/* Mesh based transform */
void selection_system_callback_mesh_transform(SceneSelectionTransform *desc) {

  for (size_t i = 0; i < desc->selection->count; i++) {
    Mesh *mesh = desc->selection->entries[i].mesh;
    vec3 *init_attribute = &desc->selection->entries[i].initial_attribute;

    // transform mesh
    selection_system_callback_mesh_transform_core(mesh, init_attribute, desc);
  }
}

/* Mesh shadow based transform.
   Note that this filter only incudes meshes that are in the LitShadow
   pipelines
 */

void selection_system_callback_mesh_shadow_transform(
    SceneSelectionTransform *desc) {

  for (size_t i = 0; i < desc->selection->count; i++) {
    Mesh *mesh = desc->selection->entries[i].mesh;
    vec3 *init_attribute = &desc->selection->entries[i].initial_attribute;
    // transform mesh
    selection_system_callback_mesh_transform_core(mesh, init_attribute, desc);
  }

  // recalculate shadow maps
  if (desc->renderer->draw_mode == RendererDrawMode_Texture) {

    const MeshRefList *shadow_meshes;
    scene_mesh_query_with_flags(desc->scene, RendererMeshFlag_Shadow,
                                &shadow_meshes);

    shadow_map_draw_all(&desc->renderer->shadow_map);
  }
}

static inline void
selection_system_sem_transform_core(SceneEditorMesh *, vec3 *,
                                    sem_transform_callback,
                                    SceneSelectionTransform *);

void selection_system_sem_transform_core(
    SceneEditorMesh *sem, vec3 *init_attribute,
    sem_transform_callback transform_callback, SceneSelectionTransform *desc) {

  //  calculate offset from delta
  vec3 offset_attribute;
  glm_vec3_add(*init_attribute, *desc->delta, offset_attribute);

  // transform sem via their own callback
  transform_callback(&(SEMTransform){
      .sem = sem,
      .value = offset_attribute,
      .renderer = desc->renderer,
      .light_list = &desc->scene->lights,
      .probe_list = &desc->scene->probes,
      .ubo = desc->scene->ubo,
  });

  mesh_uniform_update(sem->mesh);
  ubo_update_queue_insert(desc->scene->ubo, UBOType_Mesh,
                          sem->mesh->ubo_slot.id);
}

/*
  SEM based transform

   .--------------------------------------------------------------.
   |                       Actives Meshes                         |
   |------------------------------.-------------------------------|
   | Mesh 1 |  Mesh 2  |  Mesh 3  |  Mesh 1  |  Mesh 2  | Mesh 3  |
   |------------------------------+-------------------------------|
   |           SEM 1              |            SEM 2              |
   '------------------------------'-------------------------------'

 */
void selection_system_callback_sem_transform(SceneSelectionTransform *desc) {

  size_t offset = 0;

  for (size_t i = 0; i < desc->selection->count; i++) {

    vec3 *init_attribute = &desc->selection->entries[i].initial_attribute;
    Mesh *mesh = desc->selection->entries[i].mesh;

    const RegEntry *reg_entry = reg_lookup(desc->selection->entries[i].target);
    SceneEditorMeshList *sem_list = (SceneEditorMeshList *)reg_entry->ptr;

    for (size_t i = 0; i < sem_list->count; i++) {
      SceneEditorMesh *sem = &sem_list->entries[i];
      selection_system_sem_transform_core(
          sem, init_attribute, sem->transform_callback[desc->transform_mode],
          desc);
    }
  }
}

/**
   ▗▖  ▗▖ ▗▄▖ ▗▖ ▗▖ ▗▄▄▖▗▄▄▄▖
   ▐▛▚▞▜▌▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌
   ▐▌  ▐▌▐▌ ▐▌▐▌ ▐▌ ▝▀▚▖▐▛▀▀▘
   ▐▌  ▐▌▝▚▄▞▘▝▚▄▞▘▗▄▄▞▘▐▙▄▄▖

    ▗▄▄▖ ▗▄▖ ▗▖   ▗▖   ▗▄▄▖  ▗▄▖  ▗▄▄▖▗▖ ▗▖ ▗▄▄▖
   ▐▌   ▐▌ ▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌▗▞▘▐▌
   ▐▌   ▐▛▀▜▌▐▌   ▐▌   ▐▛▀▚▖▐▛▀▜▌▐▌   ▐▛▚▖  ▝▀▚▖
   ▝▚▄▄▖▐▌ ▐▌▐▙▄▄▖▐▙▄▄▖▐▙▄▞▘▐▌ ▐▌▝▚▄▄▖▐▌ ▐▌▗▄▄▞▘

   Callback called during the scene main camera raycast mouse click.
   Define the logic for the selection process such as:
   - Adding / Removing meshes from the selection pipeline
   - Showing / Hidding the transform gizmo based on hit count

    1. Manipulate each method (mesh/ shader) source list
    2. If method has a destination then transfert source -> destination
    3. Merge both method source list to gizmo selection

     Mesh based highlight                 Shader based highlight
      .---------------.                      .---------------.
      |  Source list  |                      |  Source list  |
      '-------.-------'                      '-------.-------'
              |                                      |
      [[ Push / Pop  ]] ---------.---------- [[ Push / Pop  ]]
              |                  |                   |
          copy to                |           .-------'-------.
              |                  |           |    Update     |
      .-------'------.           |           | Uniform flag  |
      | Destination  |           |           '---------------'
      |  (pipeline)  |           |
      '------.-------'           |
             |                   |
      .--------------.           |
      | Render Pass  |           |
      | (Highlight)  |	         |
      '--------------'           |
                                 |
                                 |
                                 |
                      Gizmo Selection list (merge)
                                 |
              .-- array ---------|--------------------.
              | Mesh Source List + Shader Source List |
              '------------------|--------------------'
                                 |
              .------------------'--------------------.
              |   --------------------------------.   |
              |  ▲     Gizmo Transform Loop       ▼   |
              |  '--------------------------------    |
              '---------------------------------------'

 */

void selection_system_init_mouse_events(SceneSelection *selection, Scene *scene,
                                        Renderer *renderer) {

  /**
      ===================== ADD SELECTION RELATED EVENTS ===================

     1. Add a right click raycast: push/pop meshes from the selection
     pipeline.

     2. Add a left click raycast: on gizmo transform only to define selected
     axis.

     3. Add a draw callback: to poll mouse events and loop through selection
     to apply transform.

     4. Add a html event on mouse up

   */

  /*

     ===== MESHES EVENTS =====

   */

  // right click raycast on scene main camera (to select meshes)
  camera_raycast(
      scene->camera,
      &(CameraRaycastDescriptor){
          .label = "Selection System - Mesh Raycast Callback",
          .target = CameraRaycastTarget_MousePosition,
          .event = CameraRaycastEvent_MouseDown,
          .space = CameraRaycastSpace_WorldSpace,
          .bound = CameraRaycastBound_OBB,
          .viewport = &scene->viewport,
          .callback = selection_system_callback_raycast_mesh,
          .data = (void *)&selection_system_event_payload,
          .size = 0,
          .include =
              &(MeshRefListArray){
                  .lists =
                      {
                          &selection->filters[SceneSelectionType_Mesh].meshes,
                          &selection->filters[SceneSelectionType_MeshShadow]
                               .meshes,
                          &selection->filters[SceneSelectionType_SEM].meshes,
                      },
                  .count = SCENE_SELECTION_TYPE_COUNT,
                  .capacity = SCENE_SELECTION_TYPE_COUNT,
              },
          .exclude = 0,
      },
      CameraRaycastAlloc_All);

  /*

     ===== GIZMO EVENTS =====

   */

  static const struct {
    CameraRaycastEvent event;
    camera_raycast_callback callback;
  } selection_gizmo_mouse_events[2] = {
      {
          .event = CameraRaycastEvent_MouseDown,
          .callback = selection_system_callback_raycast_gizmo_down,
      },
      {
          .event = CameraRaycastEvent_MouseHover,
          .callback = selection_system_callback_raycast_gizmo_hover,
      },
  };

  mesh_ref_list_array_create(&selection_system_gizmo_raycast_list);
  mesh_ref_list_array_insert(
      &selection_system_gizmo_raycast_list,
      &scene->gizmo.interactive_handles[scene->gizmo.mode]);

  // map selection gizmo mouse events
  for (uint8_t i = 0; i < 2; i++)
    camera_raycast(scene->camera,
                   &(CameraRaycastDescriptor){
                       .label = "Selection System - Gizmo Raycast Callback",
                       .target = CameraRaycastTarget_MousePosition,
                       .event = selection_gizmo_mouse_events[i].event,
                       // use scree-space since gizmo have fixed scale
                       .space = CameraRaycastSpace_ScreenSpace,
                       .screen_space_size = GIZMO_SIZE, // Gizmo size
                       .include = &selection_system_gizmo_raycast_list,
                       .exclude = 0,
                       .viewport = &scene->viewport,
                       .callback = selection_gizmo_mouse_events[i].callback,
                       .data = (void *)&selection_system_event_payload,
                       .size = 0,
                   },
                   CameraRaycastAlloc_None);

  // reset on mouse up
  html_event_add_mouse_up(&(HTMLEventMouse){
      .owner = scene->id,
      .callback = selection_system_callback_html_reset,
      .destructor = NULL,
      .data = (void *)&selection_system_event_payload,
      .size = 0,
  });
}

void selection_system_callback_raycast_mesh(
    CameraRaycastCallback *cast_data, const EmscriptenMouseEvent *mouseEvent,
    void *user_data) {

  // clang-format off
  Scene *scene = ((SelectionSystemCallbackData *)user_data)->scene;
  SceneSelection *selection = ((SelectionSystemCallbackData *)user_data)->selection;
  Renderer *renderer = ((SelectionSystemCallbackData *)user_data)->renderer;
  // clang-format on

  Gizmo *gizmo = &scene->gizmo;

  // else retrieve first hit only (closest to camera)
  CameraRaycastHit *hit = &cast_data->hits->entries[0];

  if (mouseEvent->button != 2)
    return;

  if (cast_data->hits->count > 0 && hit)
    selection_system_toggle_mesh(selection, scene, renderer, hit->mesh);
  else {
    scene_selection_empty(selection);
    mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);

    for (uint8_t i = 0; i < SCENE_SELECTION_TYPE_COUNT; i++)
      highlight_callbacks[i](selection, scene, renderer,
                             &selection->filters[i].selection, NULL);
  }
}

/**
   Left click raycast callback.
   Check if one of the gizmo is clicked and define the axis.
 */
void selection_system_callback_raycast_gizmo_down(
    CameraRaycastCallback *cast_data, const EmscriptenMouseEvent *mouseEvent,
    void *user_data) {

  SelectionSystemCallbackData *cast_user_data =
      (SelectionSystemCallbackData *)user_data;

  Mesh *hit = cast_data->hits->entries[0].mesh;

  if (mouseEvent->button == 0 && hit) {

    Scene *scene = cast_user_data->scene;
    SceneSelection *selection = cast_user_data->selection;
    Gizmo *gizmo = &scene->gizmo;

    // if init distance > 0, means the gizmo is already active (from the hotkeyh
    // as instance)
    if (gizmo->cache.init_distance == 0.0) {

      // map active axis from hit handle pointer
      gizmo_set_axis_from_mesh(gizmo, hit);

      // cache scene selection initial attributes
      scene_selection_cache_initial_attributes(selection, gizmo->mode);

      // set active handle from current mode and initialize offset
      gizmo_set_active(gizmo, scene->active_camera, &scene->viewport);
    }
  }
}

/**
   Hover on gizmo raycast callback.
   Check if one of the gizmo is clicked and define the axis.
 */

void selection_system_callback_raycast_gizmo_hover(
    CameraRaycastCallback *cast_data, const EmscriptenMouseEvent *mouseEvent,
    void *user_data) {

  SelectionSystemCallbackData *cast_user_data =
      (SelectionSystemCallbackData *)user_data;

  Gizmo *gizmo = &cast_user_data->scene->gizmo;
  CameraRaycastHit *hit = NULL;

  // Since we recieve multiple hits (gizmo-mode agnostic) we need to filter down
  // and select the hit from the right gizmo_mode, else we may hover the rotate
  // gizmo being in the position mode.
  for (size_t i = 0; i < cast_data->hits->count; i++) {

    if (mesh_ref_list_find(&gizmo->interactive_handles[gizmo->mode],
                           cast_data->hits->entries[i].mesh, NULL)) {
      hit = &cast_data->hits->entries[i];
      break;
    }
  }

  if (hit == NULL) {
    if (cast_data->last_hit->mesh != NULL &&
        g_input.mouse.state[InputMouseButton_Left] == InputMouseState_Up)
      gizmo_reset_color_uniform(gizmo);
    return;
  }

  // update only once
  if (cast_data->last_hit->mesh != hit->mesh &&
      // if mouse is down >> lock
      g_input.mouse.state[InputMouseButton_Left] == InputMouseState_Up) {

    gizmo_reset_color_uniform(gizmo);
    // update hovered gizmo color
    shader_update_uniform_data(mesh_shader(hit->mesh, MeshShader_Texture), 1, 0,
                               (void *)COLOR_GIZMO_HOVER,
                               ShaderUpdateFlag_None);
  }
}

/**

    ▗▖ ▗▖▗▄▄▄▖▗▖  ▗▖▗▄▄▖  ▗▄▖  ▗▄▖ ▗▄▄▖ ▗▄▄▄
    ▐▌▗▞▘▐▌    ▝▚▞▘ ▐▌ ▐▌▐▌ ▐▌▐▌ ▐▌▐▌ ▐▌▐▌  █
    ▐▛▚▖ ▐▛▀▀▘  ▐▌  ▐▛▀▚▖▐▌ ▐▌▐▛▀▜▌▐▛▀▚▖▐▌  █
    ▐▌ ▐▌▐▙▄▄▖  ▐▌  ▐▙▄▞▘▝▚▄▞▘▐▌ ▐▌▐▌ ▐▌▐▙▄▄▀

     ▗▄▄▖ ▗▄▖ ▗▖   ▗▖   ▗▄▄▖  ▗▄▖  ▗▄▄▖▗▖ ▗▖ ▗▄▄▖
    ▐▌   ▐▌ ▐▌▐▌   ▐▌   ▐▌ ▐▌▐▌ ▐▌▐▌   ▐▌▗▞▘▐▌
    ▐▌   ▐▛▀▜▌▐▌   ▐▌   ▐▛▀▚▖▐▛▀▜▌▐▌   ▐▛▚▖  ▝▀▚▖
    ▝▚▄▄▖▐▌ ▐▌▐▙▄▄▖▐▙▄▄▖▐▙▄▞▘▐▌ ▐▌▝▚▄▄▖▐▌ ▐▌▗▄▄▞▘

 */

typedef struct {
  keyrec_t sequence[3];
  size_t count;
  input_keyrec_callback callback;
  Axis axis;
  GizmoMode mode;
} SelectionKeySequence;

const uint8_t seq_count_select = 1;
const uint8_t seq_count_mode = 3;
const uint8_t seq_count_transform = 6;
const uint8_t seq_count_total =
    seq_count_select + seq_count_mode + seq_count_transform;

static SelectionKeySequence selection_key_sequences_select[1] = {
    // select all
    {
        .sequence = {'A'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_select_all,
    },
};

static SelectionKeySequence selection_key_sequences_mode[3] = {
    // mode switch
    {
        .sequence = {'G'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_set_gizmo_mode,
        .mode = GizmoMode_Position,
    },
    {
        .sequence = {'S'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_set_gizmo_mode,
        .mode = GizmoMode_Scale,
    },
    {
        .sequence = {'R'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_set_gizmo_mode,
        .mode = GizmoMode_Rotation,
    },
};

static SelectionKeySequence selection_key_sequences_transform[6] = {
    // transform view/general
    {
        .sequence = {'G'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_View,
        .mode = GizmoMode_Position,
    },
    {
        .sequence = {'R'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_View,
        .mode = GizmoMode_Rotation,
    },
    {
        .sequence = {'S'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_XYZ,
        .mode = GizmoMode_Scale,
    },
    // transform axis
    {
        .sequence = {'X'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_X,
    },
    {
        .sequence = {'Y'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_Y,
    },
    {
        .sequence = {'Z'},
        .count = 1,
        .callback = selection_system_callback_key_sequence_transform,
        .axis = Axis_Z,
    },
};

static const uint8_t seq_count = 3;
/**
   List of hot key sequences.
   Note that the order is important. Switching entry 2 and 3 will break the
   overall sequence logic (1 hit G -> switch mode, 2 hit G -> transform).
 */
static const struct {
  SelectionKeySequence *sequences;
  size_t count;
} selection_key_sequences[3] = {
    {
        .sequences = selection_key_sequences_select,
        .count = seq_count_select,
    },
    {
        .sequences = selection_key_sequences_transform,
        .count = seq_count_transform,
    },
    {
        .sequences = selection_key_sequences_mode,
        .count = seq_count_mode,
    },
};

void selection_system_init_key_events(SceneSelection *selection, Scene *scene,
                                      Renderer *renderer) {

  for (size_t i = 0; i < seq_count; i++) {

    // dispatch to global input key record sequence
    SelectionKeySequence *sequences = selection_key_sequences[i].sequences;
    size_t count = selection_key_sequences[i].count;

    for (size_t j = 0; j < count; j++) {
      SelectionKeySequence *seq = &sequences[j];
      input_key_sequence_add(&(KeyRecordSequence){
          .sequence = seq->sequence,
          .callback = seq->callback,
          .count = seq->count,
          .data = &selection_system_event_payload,
          .owner = scene->id,
      });
    }
  }
}

void selection_system_callback_key_sequence_select_all(KeyRecordSequence *seq,
                                                       void *data) {

  if (g_input.locked & InputLockState_Keyboard)
    return;

  SelectionSystemCallbackData *user_data = (SelectionSystemCallbackData *)data;
  Scene *scene = user_data->scene;
  SceneSelection *selection = user_data->selection;
  Renderer *renderer = user_data->renderer;
  Gizmo *gizmo = &scene->gizmo;

  // if already selection => unselect everything
  if (scene_selection_count(selection)) {
    scene_selection_empty(selection);
    mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);
  } else {
    scene_selection_all(selection);
    selection_system_update_gizmo_pos_to_selection(gizmo, selection,
                                                   scene->ubo);
    mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);
  }

  for (SceneSelectionType i = 0; i < SCENE_SELECTION_TYPE_COUNT; i++)
    highlight_callbacks[i](selection, scene, renderer,
                           &selection->filters[i].selection, NULL);
}

void selection_system_callback_key_sequence_set_gizmo_mode(
    KeyRecordSequence *seq, void *data) {

  if (g_input.locked & InputLockState_Keyboard)
    return;

  SelectionSystemCallbackData *user_data = (SelectionSystemCallbackData *)data;
  Scene *scene = user_data->scene;
  SceneSelection *selection = user_data->selection;
  Renderer *renderer = user_data->renderer;
  Gizmo *gizmo = &scene->gizmo;

  // search for same sequence in static array and assign mode to gizmo
  for (size_t i = 0; i < seq_count_mode; i++)
    if (keyrec_sequence_equal(selection_key_sequences_mode[i].sequence,
                              seq->sequence, seq->count)) {
      gizmo->mode = selection_key_sequences_mode[i].mode;
      // reset hover colored on change mode
      gizmo_reset_color_uniform(gizmo);
    }

  // hide gizmo
  mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                       &gizmo->handles[gizmo->mode]);

  selection_system_update_gizmo_raycast_list(gizmo, gizmo->mode);

  // show gizmo if has selection
  if (scene_selection_count(selection)) {
    // update location to selection average
    selection_system_update_gizmo_pos_to_selection(gizmo, selection,
                                                   scene->ubo);
    mesh_route_system_hide_mesh_ref_list(scene, renderer,
                                         &gizmo->handles[gizmo->mode]);
  }
}

void selection_system_callback_key_sequence_transform(
    KeyRecordSequence *current_seq, void *data) {

  if (g_input.locked & InputLockState_Keyboard)
    return;

  SelectionSystemCallbackData *user_data = (SelectionSystemCallbackData *)data;
  Scene *scene = user_data->scene;
  SceneSelection *selection = user_data->selection;
  Renderer *renderer = user_data->renderer;
  Gizmo *gizmo = &scene->gizmo;

  // use the count as a flag to detect if gizmo already active or not
  if (scene_selection_count(selection) == 0)
    return;

  // cache scene selection initial attributes
  scene_selection_clear_initial_attributes(selection);

  for (size_t i = 0; i < seq_count_transform; i++) {

    SelectionKeySequence *key_seq = &selection_key_sequences_transform[i];
    // find equal key sequence
    if (keyrec_sequence_equal(current_seq->sequence, key_seq->sequence,
                              current_seq->count)) {

      Axis key_seq_axis = key_seq->axis;
      GizmoMode key_seq_mode = key_seq->mode;

      // If gizmo is NOT already in the mode we do NOT transform
      // only switch mode
      if (key_seq_mode != gizmo->mode)
        return;

      // map axis from static sequences
      gizmo->axis = key_seq_axis;

      // cache scene selection initial attributes
      scene_selection_cache_initial_attributes(selection, gizmo->mode);

      // set active handle from current mode and initialize offset
      gizmo_set_active(gizmo, scene->active_camera, &scene->viewport);
    }
  }
}

/**
   Set the gizmo active handle to NULL which acts as a trigger.
   This wall the loop callback doesn't move the meshes anymore if the mouse is
   down again.
 */
bool selection_system_callback_html_reset(
    int eventType, const EmscriptenMouseEvent *mouseEvent, void *userData) {

  SelectionSystemCallbackData *user_data =
      (SelectionSystemCallbackData *)userData;
  Scene *scene = user_data->scene;
  SceneSelection *selection = user_data->selection;
  Gizmo *gizmo = &scene->gizmo;

  // clear gizmo cache
  gizmo_clear_active(gizmo);

  // reset selection initial cached attributes
  scene_selection_clear_initial_attributes(selection);

  return EM_FALSE;
}

/**
   Get the selection average position (used to translate the gizmo).
 */
void selection_system_update_gizmo_pos_to_selection(Gizmo *gizmo,
                                                    SceneSelection *selection,
                                                    UBOManager *ubo) {
  // get average position
  vec3 position;
  scene_selection_average_position(selection, &position);
  gizmo_set_position(gizmo, position);

  // update ubo matrix buffer
  gizmo_update_ubo(gizmo, ubo);
}

/**
   Updates the mesh list in the raycast event system so it only targets the
   handles from the active Gizmo mode.
 */
void selection_system_update_gizmo_raycast_list(Gizmo *gizmo,
                                                const GizmoMode mode) {

  selection_system_gizmo_raycast_list.lists[0] =
      &gizmo->interactive_handles[mode];
}
