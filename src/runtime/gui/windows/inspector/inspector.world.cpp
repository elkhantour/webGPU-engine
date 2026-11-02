#include "inspector.world.hpp"
#include "backend/reflection/core.h"
#include "backend/shadow_map/core.h"
#include "backend/shadow_map/draw.h"
#include "imgui/imgui.h"
#include "runtime/gui/components/combobox_resolution.hpp"
#include "runtime/gui/components/input.hpp"
#include "runtime/gui/components/spacing.hpp"
#include "runtime/gui/components/tree_item.hpp"
#include "runtime/gui/core.h"
#include "runtime/light/list.h"
#include "runtime/mesh/core.h"
#include "runtime/pipeline/render.h"
#include "runtime/scene/core.h"
#include "runtime/scene/mesh_query.h"
#include "runtime/shader/update.h"
#include "runtime/texture/core.h"
#include "utils/name.h"
#include "webgpu/webgpu.h"

void UI::WorldTab::draw() {

  const InputStyle style = {
      .direction = InputDirection_Vertical,
      .label_width = gui_scale(gui, 85),
  };

  if (UI::TreeItem(gui, "Skybox").draw()) {

    ImGui::TreePop();
  }

  if (UI::TreeItem(gui, "Shadow").draw()) {

    { // === Point Light ===
      const WGPUTexture color_texture =
          renderer->shadow_map.lights[ShadowMapLightType_Point].texture.color;
      const int width = wgpuTextureGetWidth(color_texture);
      const int height = wgpuTextureGetHeight(color_texture);

      name_t default_value;
      name_compose(default_value, "%d x %d", width, height);

      ImGui::PushID("point_light_res");
      UI::ComboboxResolution(gui, "Point light resolution", default_value,
                             &style, (TextureResolution)width,
                             on_resolution_change_point_light)
          .draw();
      ImGui::PopID();
    }

    UI::Spacing(gui, ThemeSize_Space_Small).draw_y();

    { // === Directional Light ===
      const WGPUTexture color_texture =
          renderer->shadow_map.lights[ShadowMapLightType_Directional]
              .texture.color;
      const int width = wgpuTextureGetWidth(color_texture);
      const int height = wgpuTextureGetHeight(color_texture);

      name_t default_value;
      name_compose(default_value, "%d x %d", width, height);

      ImGui::PushID("dir_light_res");
      UI::ComboboxResolution(gui, "Dir light resolution", default_value, &style,
                             (TextureResolution)width,
                             on_resolution_change_dir_light)
          .draw();
      ImGui::PopID();
    }

    UI::Spacing(gui, ThemeSize_Space_Small).draw_y();

    ImGui::TreePop();
  }

  if (UI::TreeItem(gui, "Reflection").draw()) {

    const int width =
        wgpuTextureGetWidth(scene->probes.reflection_plane.pass.color.texture);
    const int height =
        wgpuTextureGetHeight(scene->probes.reflection_plane.pass.color.texture);

    name_t default_value;
    name_compose(default_value, "%d x %d", width, height);

    UI::ComboboxResolution(gui, "Planar resolution", default_value, &style,
                           (TextureResolution)width,
                           on_resolution_change_plane_reflection)
        .draw();

    ImGui::TreePop();
  }
}

void UI::WorldTab::on_resolution_change_point_light(
    Scene *scene, Renderer *renderer, const TextureResolution resolution) {

  // update each lit shadowed mesh view
  const MeshRefList *shadow_meshes;
  scene_mesh_query_with_flags(scene, RendererMeshFlag_Shadow, &shadow_meshes);

  ShadowMapLight *group_light =
      &renderer->shadow_map.lights[ShadowMapLightType_Point];

  shadow_map_update_light_resolution(group_light, resolution,
                                     WGPUTextureViewDimension_CubeArray);

  // update each mesh bound shadow view
  for (size_t j = 0; j < shadow_meshes->count; j++) {
    Mesh *mesh = shadow_meshes->entries[j];
    Shader *shader = mesh_shader(mesh, MeshShader_Texture);
    const PipelineBindingLightList *binding =
        shader_pipeline(shader)->bindings.light_list;

    WGPUTextureView shadow_map_view = group_light->view.depth;

    shader_update_texture_view(shader, binding->group, binding->point_texture,
                               shadow_map_view, SHADOW_DEPTH_FORMAT,
                               ShaderUpdateFlag_None);
  }

  // redraw lights with new resolution
  ShadowMap *sm = &renderer->shadow_map;
  shadow_map_draw_all_point_light(sm);
}

void UI::WorldTab::on_resolution_change_dir_light(
    Scene *scene, Renderer *renderer, const TextureResolution resolution) {

  // update each lit shadowed mesh view
  const MeshRefList *shadow_meshes;
  scene_mesh_query_with_flags(scene, RendererMeshFlag_Shadow, &shadow_meshes);

  ShadowMapLight *group_light =
      &renderer->shadow_map.lights[ShadowMapLightType_Directional];

  shadow_map_update_light_resolution(group_light, resolution,
                                     WGPUTextureViewDimension_2DArray);

  // update each mesh bound shadow view
  for (size_t j = 0; j < shadow_meshes->count; j++) {
    Mesh *mesh = shadow_meshes->entries[j];
    Shader *shader = mesh_shader(mesh, MeshShader_Texture);
    const PipelineBindingLightList *binding =
        shader_pipeline(shader)->bindings.light_list;

    WGPUTextureView shadow_map_view = group_light->view.depth;

    shader_update_texture_view(shader, binding->group,
                               binding->directional_texture, shadow_map_view,
                               SHADOW_DEPTH_FORMAT, ShaderUpdateFlag_None);
  }

  // redraw lights with new resolution
  ShadowMap *sm = &renderer->shadow_map;
  shadow_map_draw_all_spot_light(sm);
  shadow_map_draw_all_sun_light(sm);
}

void UI::WorldTab::on_resolution_change_plane_reflection(
    Scene *scene, Renderer *renderer, const TextureResolution resolution) {

  renderer_probe_reflection_update_resolution(
      &scene->probes.reflection_plane.pass, resolution,
      WGPUTextureViewDimension_2DArray);

  const MeshRefList *reflection_meshes;
  scene_mesh_query_with_flags(scene, RendererMeshFlag_Shadow,
                              &reflection_meshes);

  // update each lit mesh reflection view

  for (size_t k = 0; k < reflection_meshes->count; k++) {
    Mesh *mesh = reflection_meshes->entries[k];
    Shader *shader = mesh_shader(mesh, MeshShader_Texture);
    const PipelineBindingProbe *binding =
        shader_pipeline(shader)->bindings.probe;

    shader_update_texture_view(
        shader, binding->group, binding->reflection_plane_texture,
        scene->probes.reflection_plane.pass.color.attachment.view,
        TEXTURE_FORMAT_OFFSCREEN, ShaderUpdateFlag_None);
  }
}
