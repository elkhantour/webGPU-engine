#ifndef _SCENE_EDITOR_OBJECT_H_
#define _SCENE_EDITOR_OBJECT_H_

#include "backend/renderer/core.h"
#include "runtime/camera/core.h"
#include "runtime/gizmo/gizmo.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/probe/core.h"
#include "runtime/viewport/core.h"
#include "utils/name.h"

#define SCENE_EDITOR_MESH_TARGET_UNDEFINED UINT32_MAX

typedef struct SceneEditorMesh SceneEditorMesh;
typedef struct SceneEditorMeshList SceneEditorMeshList;

typedef struct {
  SceneEditorMesh *sem;
  float *offset;
} SEMTransformCallback;

typedef struct {
  SceneEditorMesh *sem;
} SEMHighlightCallback;

typedef struct {
  SceneEditorMesh *sem;
  UBOManager *ubo;
  Renderer *renderer;
  LightList *light_list;
  ProbeList *probe_list;
  const float *value;
} SEMTransform;

typedef struct {
  SceneEditorMeshList *sem_list;
  UBOManager *ubo;
  Renderer *renderer;
  LightList *light_list;
  ProbeList *probe_list;
  const float *value;
} SEMListTransform;

typedef void (*sem_transform_callback)(const SEMTransform *);
typedef void (*sem_transform_highlight_callback)(SEMHighlightCallback *);

// Link each SEM a dedicated callback
struct SceneEditorMesh {
  reg_id_t id;
  Mesh *mesh;
  // camera, light 'abstract' objects the meshes drives through transformation
  void *target;
  // index of object (ex in LightList or CameraList), not sure about this
  // flow...
  size_t target_list_index;
  sem_transform_callback transform_callback[GIZMO_MODE_COUNT];
  sem_transform_highlight_callback select_callback;
  sem_transform_highlight_callback deselect_callback;
};

static const int SEM_LIST_ORIGIN_INDEX = 0;

/*

 */
struct SceneEditorMeshList {
  reg_id_t id;
  name_t name;

  // Mesh list composing the SEM (light handle, spot target...)
  SceneEditorMesh *entries;
  size_t capacity;
  size_t count;

  // Since Editor Mesh List may have multiple targets and meshes, we still need
  // to define a refernce/ main mesh, i.e. the origin.
  // The origin mesh help to define the SEM list main position and target.
  // Usually the origin will be the first mesh of the list.
  SceneEditorMesh *origin;
};

typedef struct {
  SceneEditorMeshList *entries;
  size_t capacity;
  size_t count;
} SceneEditorMeshListArray;

typedef struct {
  size_t target_list_index; // the index of the object in it's parent list, so
                            // no need for lookup
  Camera *camera;
  Viewport *viewport;
} SEMCreateDescriptor;

#endif
