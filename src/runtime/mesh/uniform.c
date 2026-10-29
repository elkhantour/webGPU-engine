#include "uniform.h"

#include <cglm/mat4.h>
#include <cglm/types.h>
#include <cglm/vec4.h>
#include <string.h>

#include "core.h"

MeshUniform *mesh_uniform(Mesh *mesh) {
  return (MeshUniform *)mesh->ubo_slot.uniform;
}

void mesh_uniform_update(Mesh *mesh) {

  vec4 position = {
      mesh->position[0],
      mesh->position[1],
      mesh->position[2],
      1.0f,
  };

  mat4 inv_transpose;
  glm_mat4_inv(mesh->model, inv_transpose);
  glm_mat4_transpose(inv_transpose);

  MeshUniform *uniform = (MeshUniform *)mesh->ubo_slot.uniform;
  glm_mat4_copy(mesh->model, uniform->model);
  glm_mat4_copy(inv_transpose, uniform->model_inv_transpose);
  glm_vec4_copy(position, uniform->position);
}

void mesh_uniform_model_update_callback(void *callback_mesh, void *entry_data) {

  Mesh *cast_mesh = (Mesh *)callback_mesh;

  MeshUniform *updated_data = (MeshUniform *)cast_mesh->ubo_slot.uniform;
  MeshUniform *new_data = (MeshUniform *)entry_data;

  //  transfer updated camera values (position and view) to new data
  glm_mat4_copy(updated_data->model, new_data->model);
  glm_mat4_copy(updated_data->model_inv_transpose, new_data->model_inv_transpose);
  glm_vec4_copy(updated_data->position, new_data->position);
}

bool mesh_uniform_model_compare_callback(void *callback_data,
                                         const void *entry_data) {

  Mesh *cast_mesh = (Mesh *)callback_data;
  MeshUniform *cast_uniform = (MeshUniform *)entry_data;

  return memcmp(cast_mesh->model, cast_uniform->model, sizeof(mat4)) != 0;
}
