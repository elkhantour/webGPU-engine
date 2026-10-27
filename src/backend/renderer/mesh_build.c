#include "mesh_build.h"
#include "backend/job_system.h"
#include "backend/renderer/core.h"
#include "runtime/engine/core.h"
#include "runtime/geometry/line/core.h"
#include "runtime/geometry/vertex/attribute.h"
#include "runtime/geometry/vertex/index.h"
#include "runtime/mesh/core.h"
#include "runtime/mesh/shader/core.h"
#include "runtime/mesh/shader/texture.h"
#include "runtime/mesh/topology/boundbox.h"
#include "runtime/mesh/topology/wireframe.h"
#include "runtime/pipeline/render.h"
#include "runtime/systems/mesh_route_system.h"

// pipeline builders
static inline void build_mesh_texture(Scene *, Mesh *, const RendererMeshFlag);
static inline void build_mesh_solid(Scene *, Mesh *);
static inline void build_mesh_wireframe(Scene *, Mesh *, Renderer *,
                                        JobSystem *);
static inline void build_mesh_outline(Scene *, Mesh *);
static inline void build_mesh_boundbox(Scene *, Mesh *);

/**
   Build the mesh according to the current scene rendere mode and pipeline in
   which the mesh will be added to.
 */
RendererStatus renderer_build_mesh(Renderer *renderer, Scene *scene,
                                   JobSystem *job_system, Mesh *mesh,
                                   const RendererMeshFlag flag) {

  UBOManager *ubo = scene->ubo;

  // EDITORONLY
  build_mesh_outline(scene, mesh);
  build_mesh_texture(scene, mesh, flag);

  // only build those for dynamic meshes
  if ((flag & RendererMeshFlag_Fixed) == 0) {
    build_mesh_boundbox(scene, mesh);
    build_mesh_solid(scene, mesh);
    build_mesh_wireframe(scene, mesh, renderer, job_system);
  }

  return RendererStatus_Success;
}

/**
  ▗▄▄▖ ▗▖ ▗▖▗▄▄▄▖▗▖   ▗▄▄▄ ▗▄▄▄▖▗▄▄▖  ▗▄▄▖
  ▐▌ ▐▌▐▌ ▐▌  █  ▐▌   ▐▌  █▐▌   ▐▌ ▐▌▐▌
  ▐▛▀▚▖▐▌ ▐▌  █  ▐▌   ▐▌  █▐▛▀▀▘▐▛▀▚▖ ▝▀▚▖
  ▐▙▄▞▘▝▚▄▞▘▗▄█▄▖▐▙▄▄▖▐▙▄▄▀▐▙▄▄▖▐▌ ▐▌▗▄▄▞▘

   Build meshes Texture shader in each scene list
   Establish pipeline from previously set bind groups
 */
void build_mesh_texture(Scene *scene, Mesh *mesh,
                        const RendererMeshFlag flags) {

#ifdef VERBOSE_BUILDING_PHASE
  logger_add(LoggerFlag_MeshBuild, "Texture %s", mesh->name);
#endif

  UBOManager *ubo = scene->ubo;

  // compute boundbox bounds for collisions (lightweight)
  mesh_topology_boundbox_compute_bound(&mesh->topology.base, mesh->model,
                                       &mesh->topology.boundbox);

  // bind views
  mesh_shader_bind_mvp(mesh, MeshShader_Texture, ubo);

  /*
  TODO: For now although we use a hash based pipeline routing it is still a
  pre-configured 1 to 1 configuration where 1 renderer pipeline = 1
  configuration. In the future make it more versatile by creating the
  configuration on the fly.

  In this case we need to retrieve the renderer batch configuration to get the
  pipeline configuration flags as to determine if we need to bind lights,
  shadow maps or reflections relative resources.
 */
  if (RendererMeshFlag_Reflection & flags) {

    mesh_shader_bind_mvp(mesh, MeshShader_Reflection, ubo);

    mesh_shader_texture_update_environment(
        mesh, scene->environment.skybox.view,
        (SceneEnvironmentUniform *)scene->environment.ubo_slot.uniform, ubo);

    mesh_shader_texture_update_probes(
        mesh, scene->probes.reflection_plane.pass.color.attachment.view,
        scene->probes.reflection_probe.pass.color.attachment.view, ubo);
  }

  if (RendererMeshFlag_Lit & flags) {

    mesh_shader_texture_update_lights(mesh, MeshShader_Texture, ubo);
    mesh_shader_texture_update_lights(mesh, MeshShader_Reflection, ubo);
  }

  if (RendererMeshFlag_Shadow & flags) {

    mesh_shader_texture_update_shadow_maps(
        mesh, scene->lights.point.shadow.pass.depth.attachment.view,
        scene->lights.spot.shadow.pass.depth.attachment.view);

    // create mesh shadow shader
    mesh_shader_create_standard(mesh, MeshShader_Shadow);

    mesh_shader_bind_mp(mesh, MeshShader_Shadow, ubo, UBOType_ViewProjection);
  }
}

/**
   Build meshes Solid shader in each scene list
   Establish pipeline from previously set bind groups
 */
void build_mesh_solid(Scene *scene, Mesh *mesh) {

#ifdef VERBOSE_BUILDING_PHASE
  logger_add(LoggerFlag_MeshBuild, "Solid %s", mesh->name);
#endif

  // compute boundbox bounds for collisions (lightweight)
  mesh_topology_boundbox_compute_bound(&mesh->topology.base, mesh->model,
                                       &mesh->topology.boundbox);

  // create meshes' solid shader
  mesh_shader_create_standard(mesh, MeshShader_Solid);

  // bind views
  mesh_shader_bind_mvp(mesh, MeshShader_Solid, scene->ubo);
}

/**
   Build meshes Solid shader in each scene list
   Establish pipeline from previously set bind groups
 */
void build_mesh_outline(Scene *scene, Mesh *mesh) {

#ifdef VERBOSE_BUILDING_PHASE
  logger_add(LoggerFlag_MeshBuild, "Outline %s", mesh->name);
#endif

  // create meshes' solid shader
  if (mesh_shader_create_standard(mesh, MeshShader_Outline) ==
      MeshStatus_Success)
    mesh_shader_bind_mvp(mesh, MeshShader_Outline, scene->ubo);

  if (mesh_shader_create_standard(mesh, MeshShader_Stencil) ==
      MeshStatus_Success)
    mesh_shader_bind_mvp(mesh, MeshShader_Stencil, scene->ubo);
}

typedef struct {
  Scene *scene;
  Mesh *mesh;
  Renderer *renderer;
  JobGroup *group;
  MeshTopology src;             // read-only view of base topology
  MeshTopologyWireframe result; // worker writes here, NOT into the mesh
  uint32_t mesh_generation;     // detects mesh destroyed/rebuilt meanwhile
} WireframeJob;

static void wireframe_worker_start(void *data);
static void wireframe_worker_done(void *data);

// ---- worker thread: pure CPU, no GPU, no logger, no shared mutable state
static void wireframe_worker_start(void *data) {
  WireframeJob *job = data;
  mesh_topology_wireframe_create(&job->src, &job->result);
}

// ---- main thread: GPU side
static void wireframe_worker_done(void *data) {
  WireframeJob *job = data;
  JobGroup *group = job->group;
  Mesh *mesh = job->mesh;
  Scene *scene = job->scene;
  Renderer *renderer = job->renderer;

  // TODO: mesh was destroyed or rebuilt while we were computing -> discard
  // if (!mesh_is_alive(mesh) || mesh->generation != job->mesh_generation) {
  //   mesh_topology_wireframe_destroy(&job->result);
  //   free(job);
  //   return;
  // }

  mesh->topology.wireframe = job->result; // adopt the result

  mesh_topology_wireframe_upload(&mesh->topology.wireframe);

  // free the vertex attribute and index cpu data (else memory overflow)
  MeshTopology topo = mesh_topology_wireframe(mesh);
  vertex_attribute_free_data(topo.attribute);
  vertex_index_free_data(topo.index);

  if (mesh_shader_create_standard(mesh, MeshShader_Wireframe) ==
      MeshStatus_Success) {
    float line_thickness = LINE_THICKNESS_BASE;
    Shader *sh = mesh_shader(mesh, MeshShader_Wireframe);
    shader_update_uniform_data(sh, 1, 1, &line_thickness,
                               ShaderUpdateFlag_None);
    shader_update_uniform_data(sh, 1, 0, &(color){0, 0, 0, 1},
                               ShaderUpdateFlag_None);
    mesh_shader_bind_mvp(mesh, MeshShader_Wireframe, job->scene->ubo);
  }

  // refresh mesh in render pass cause we updated it's vertex/index buffer
  mesh_route_system_refresh_mesh(renderer, mesh);

  if (group->remaining == 0)
    free(group);

  free(job);
}

/**
   Build meshes Wireframe shader in each scene list
   Establish pipeline from previously set bind groups
 */
void build_mesh_wireframe(Scene *scene, Mesh *mesh, Renderer *renderer,
                          JobSystem *job_system) {

#ifdef VERBOSE_BUILDING_PHASE
  logger_add(LoggerFlag_MeshBuild, "Wireframe %s", mesh->name);
#endif

  // compute boundbox bounds for collisions (lightweight)
  mesh_topology_boundbox_compute_bound(&mesh->topology.base, mesh->model,
                                       &mesh->topology.boundbox);

  // create wireframe topology
  MeshTopology src_topo = mesh_topology_base_vertex(&mesh->topology.base);
  MeshTopologyWireframe *dest_topo = &mesh->topology.wireframe;

  // use boundbox as placeholder prior the wireframe topoly is created
  vertex_attribute_copy(&mesh->topology.boundbox.attribute,
                        &mesh->topology.wireframe.attribute);
  mesh->topology.wireframe.attribute.buffer = NULL;

  vertex_index_copy(&mesh->topology.boundbox.index,
                    &mesh->topology.wireframe.index);
  mesh->topology.wireframe.index.buffer = NULL;

  mesh_topology_wireframe_upload(&mesh->topology.wireframe);

  JobGroup *wireframe_group = calloc(1, sizeof(JobGroup));
  job_group_init(wireframe_group, 2);

  // Create a worker for the wireframe creation (can be expensive for highpoly)
  WireframeJob *job = calloc(1, sizeof(*job));
  job->scene = scene;
  job->renderer = renderer;
  job->group = wireframe_group;
  job->mesh = mesh;
  job->src = mesh_topology_base_vertex(&mesh->topology.base);
  job->mesh_generation = 0; // TODO: remove it?

  // DEBUG
  //job_system_submit(job_system, wireframe_group, wireframe_worker_start,
  //                  wireframe_worker_done, job);
}

/**
   Build meshes Boundbox shader in each scene list
   Establish pipeline from previously set bind groups
 */
void build_mesh_boundbox(Scene *scene, Mesh *mesh) {

#ifdef VERBOSE_BUILDING_PHASE
  logger_add(LoggerFlag_MeshBuild, "Boundbox %s", mesh->name);
#endif

  // create full boundbox topology
  MeshTopologyBoundbox *dest_topo = &mesh->topology.boundbox;
  mesh_topology_boundbox_create(&mesh->topology.base, mesh->model, dest_topo);

  // create meshes' wireframe shader
  if (mesh_shader_create_standard(mesh, MeshShader_Wireframe) ==
      MeshStatus_Success) {

    float line_thickness = LINE_THICKNESS_BASE;
    shader_update_uniform_data(mesh_shader(mesh, MeshShader_Wireframe), 1, 1,
                               (void *)&line_thickness, ShaderUpdateFlag_None);

    // set wireframe random color
    shader_update_uniform_data(mesh_shader(mesh, MeshShader_Wireframe), 1, 0,
                               &(color){0.0f, 0.0f, 0.0f, 1.0f},
                               ShaderUpdateFlag_None);

    mesh_shader_bind_mvp(mesh, MeshShader_Wireframe, scene->ubo);
  }
}
