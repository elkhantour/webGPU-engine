#include "gizmo.h"

#include "../include/loader.h"
#include "../include/resource_manager.h"

void example_gizmo(Engine *engine) {

  Mesh *gizmo = rem_new_mesh();
  Primitive mbin_primitive;
  loader_mbin_load_primitive(&(MBINLoadPrimitiveDescriptor){
      .path = PATH_MBIN("sphere.mbin"),
      .primitive = &mbin_primitive,
  });

  mesh_create_primitive(gizmo, &(MeshCreatePrimitiveDescriptor){
                                   .primitive = &mbin_primitive,
                                   .name = "gizmo",
                               });

  mesh_shader_create(
      gizmo, &(ShaderCreateDescriptor){
                 .pipeline = std_render_pipeline(RenderPipelineType_Line),
                 .name = "gizmo shader",
             });

  mesh_set_position(gizmo, (vec3){2.0f, 3.3f, 2.0f});

  engine_scene_add_mesh_custom(engine, gizmo, NULL,
                               &(RendererMeshConfigDescriptor){
                                   .flags = RendererMeshFlag_Fixed,
                                   .layer = RendererLayer_Gizmo,
                                   .pipeline = RenderPipelineType_Line,
                                   .draw_mode = RendererDrawMode_All,
                               },
                               EngineAddFlag_TreeHide);
}
