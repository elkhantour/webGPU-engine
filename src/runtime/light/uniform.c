#include "uniform.h"

#include <cglm/mat4.h>
#include <cglm/vec3.h>

#include "core.h"
#include "runtime/light/list.h"
#include "utils/projection.h"

/* Uniforms */
void point_light_uniform_update(PointLight *light) {

  PointLightUniform *uniform =
      (PointLightUniform *)light->ubo_uniform.uniform.point;

  uniform->intensity = light->intensity;
  uniform->cutoff = light->cutoff;
  uniform->inner_cutoff = light->inner_cutoff;
  uniform->near = light->near;
  uniform->far = light->far;

  glm_vec4_copy(light->color, uniform->color);
  glm_vec3_copy(light->position, uniform->position);

  // copy 6 points views for shader depth comparison
  Projection points_views;
  projection_point(&points_views, light->position, light->near, light->far);

  for (uint8_t v = 0; v < points_views.count; v++)
    glm_mat4_copy(points_views.combined[v], uniform->views[v]);
}

void ambient_light_uniform_update(AmbientLight *light) {

  // map light to light uniform (including paddings...)
  AmbientLightUniform *uniform =
      (AmbientLightUniform *)light->ubo_uniform.uniform.ambient;

  uniform->intensity = light->intensity;
  glm_vec4_copy(light->color, uniform->color);
}

void spot_light_uniform_update(SpotLight *light) {

  SpotLightUniform *uniform =
      (SpotLightUniform *)light->ubo_uniform.uniform.spot;

  uniform->intensity = light->intensity;
  uniform->cutoff = light->cutoff;
  uniform->inner_cutoff = light->inner_cutoff;
  glm_vec4_copy(light->color, uniform->color);
  glm_vec3_copy(light->target, uniform->target);
  glm_vec3_copy(light->position, uniform->position);

  // get light view matrix
  Projection spot_view;
  projection_spot(&spot_view, light->position, light->target, light->angle);

  for (uint8_t v = 0; v < spot_view.count; v++)
    glm_mat4_copy(spot_view.combined[v], uniform->view);
}

void sun_light_uniform_update(SunLight *light) {

  // DEBUG
  printf("<%p> update sun uniform\n", light);

  SunLightUniform *uniform = (SunLightUniform *)light->ubo_uniform.uniform.sun;

  uniform->intensity = light->intensity;
  glm_vec3_copy(light->position, uniform->position);
  glm_vec4_copy(light->color, uniform->color);

  // get light view matrix
  Projection sun_view;
  projection_sun(&sun_view, light->position, light->size);

  for (uint8_t v = 0; v < sun_view.count; v++)
    glm_mat4_copy(sun_view.combined[v], uniform->view);
}

/**
   Only recalulcate the count of each light list, doesn't handle the per light
   uniform update. Since each lights have they uniform pointing to the UBO list,
   we can directly use the dedicated methods 'light_T_uniform_update()' to
   update the light uniform.

  This function may be called when a light is added or removed from the scene.
 */
void light_list_uniform_update(LightList *list) {

  LightListUniform *uniform = (LightListUniform *)list->ubo_slot.uniform;

  uniform->ambient_count = list->ambient.count;
  uniform->point_count = list->point.count;
  uniform->spot_count = list->spot.count;
  uniform->sun_count = list->sun.count;
}
