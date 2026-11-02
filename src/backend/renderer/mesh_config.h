#ifndef _RENDERER_MESH_CONFIG_H_
#define _RENDERER_MESH_CONFIG_H_

#include "backend/shaders/core.h"
#include "runtime/pipeline/render.h"
#include "utils/defines.h"
#include "utils/hsht.h"

typedef enum {
  RendererMeshPreset_Skybox,
  RendererMeshPreset_Unlit,
  RendererMeshPreset_Gizmo,
  RendererMeshPreset_Outline,
  RendererMeshPreset_Stencil,
  RendererMeshPreset_GlassProbeGrid,
  RendererMeshPreset_GlassProbePlane,
  RendererMeshPreset_Reflection,
  RendererMeshPreset_Default,
  RendererMeshPreset_Solid,
  RendererMeshPreset_PBR,
  RendererMeshPreset_PBR_DoubleSided,
  RendererMeshPreset_PBR_Alpha,
  RendererMeshPreset_Grid,
  RendererMeshPreset_Line,
  RendererMeshPreset_Screen,
  RendererMeshPreset_Shadow,
  RendererMeshPreset_Billboard,
  RendererMeshPreset_COUNT,
} RendererMeshPreset;

typedef enum {
  RendererMeshConfigStatus_Success,
  RendererMeshConfigStatus_AllocFail,
  RendererMeshConfigStatus_OutOfBound,
  RendererMeshConfigStatus_InitFail,
  RendererMeshConfigStatus_UnfoundBatch,
  RendererMeshConfigStatus_UndefError,
} RendererMeshConfigStatus;

typedef enum {
  RendererMeshFlag_None = 0,
  //  used for shadow map
  RendererMeshFlag_Shadow = 1 << 0,
  //  used for reflection pass
  RendererMeshFlag_Reflection = 1 << 1,
  //  keep the same shader (texture) no matter the rendering mode
  RendererMeshFlag_Fixed = 1 << 2,
  //  use alpha blend transparency and require dedicated sorting
  RendererMeshFlag_Alpha = 1 << 3,
  //  use light scene lights data (but not shadow map)
  RendererMeshFlag_Lit = 1 << 4,
  //  will host selected mesh (outline, stencil)
  RendererMeshFlag_Selection = 1 << 5,
  // hidden
  RendererMeshFlag_Hidden = 1 << 6,
} RendererMeshFlag;
#define RENDERER_FLAG_COUNT 8

typedef enum {
  RendererLayer_Default,
  RendererLayer_Outline,
  RendererLayer_Gizmo,
} RendererLayer;
#define RENDERER_LAYER_COUNT 3

typedef enum {
  RendererDrawMode_None = 0,
  RendererDrawMode_Boundbox = 1 << 0,
  RendererDrawMode_Wireframe = 1 << 1,
  RendererDrawMode_Solid = 1 << 2,
  RendererDrawMode_Texture = 1 << 3,
  RendererDrawMode_All = ~0,
} RendererDrawMode;
#define RENDERER_DRAW_MODE_COUNT 4

typedef struct {
  const char *label;
  RenderPipelineType pipeline;
  uint32_t flags;
  RendererLayer layer;
  int32_t draw_mode;
} RendererMeshConfig;

typedef struct {
  RenderPipelineType pipeline;
  uint32_t flags;
  RendererLayer layer;
  int32_t draw_mode;
} RendererMeshConfigDescriptor;

EXTERN_C_BEGIN

static const RendererMeshConfig RENDERER_MESH_CONFIG_PRESET[] = {
    [RendererMeshPreset_Skybox] =
        {
            "Skybox",
            RenderPipelineType_Skybox,
            RendererMeshFlag_Fixed,
            RendererLayer_Default,
            RendererDrawMode_Texture,
        },
    [RendererMeshPreset_Unlit] =
        {
            "Unlit",
            RenderPipelineType_Unlit_Stencil,
            RendererMeshFlag_Reflection,
            RendererLayer_Default,
            RendererDrawMode_Texture,
        },
    [RendererMeshPreset_Gizmo] =
        {
            "Gizmo",
            RenderPipelineType_Unlit,
            RendererMeshFlag_Fixed,
            RendererLayer_Gizmo,
            RendererDrawMode_All,
        },
    [RendererMeshPreset_Outline] =
        {
            "Outline",
            RenderPipelineType_Outline,
            RendererMeshFlag_Selection,
            RendererLayer_Outline,
            RendererDrawMode_Solid | RendererDrawMode_Texture,
        },
    [RendererMeshPreset_Stencil] =
        {
            "Stencil",
            RenderPipelineType_Stencil,
            RendererMeshFlag_Selection,
            RendererLayer_Default,
            RendererDrawMode_Solid | RendererDrawMode_Texture,
        },
    [RendererMeshPreset_GlassProbeGrid] =
        {
            "Glass Probe Grid",
            RenderPipelineType_GlassProbeGrid,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_Texture,
        },
    [RendererMeshPreset_GlassProbePlane] =
        {
            "Glass Probe Plane",
            RenderPipelineType_GlassProbePlane,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_Texture,
        },
    [RendererMeshPreset_Reflection] =
        {
            "Reflection",
            RenderPipelineType_Reflection,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_None,
        },
    [RendererMeshPreset_Default] =
        {
            "Default",
            RenderPipelineType_Default,
            RendererMeshFlag_Shadow | RendererMeshFlag_Reflection,
            RendererLayer_Default,
            RendererDrawMode_Texture,
        },
    [RendererMeshPreset_Solid] =
        {
            "Solid",
            RenderPipelineType_Solid,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_Solid,
        },
    [RendererMeshPreset_PBR] =
        {
            "PBR",
            RenderPipelineType_PBR,
            RendererMeshFlag_Shadow | RendererMeshFlag_Reflection |
                RendererMeshFlag_Lit,
            RendererLayer_Default,
            RendererDrawMode_All,
        },
    [RendererMeshPreset_PBR_DoubleSided] =
        {
            "PBR Double Sided",
            RenderPipelineType_PBR_DoubleSided,
            RendererMeshFlag_Shadow | RendererMeshFlag_Reflection |
                RendererMeshFlag_Lit,
            RendererLayer_Default,
            RendererDrawMode_All,
        },
    [RendererMeshPreset_PBR_Alpha] =
        {
            "PBR Alpha",
            RenderPipelineType_PBR_Alpha,
            RendererMeshFlag_Shadow | RendererMeshFlag_Reflection |
                RendererMeshFlag_Lit,
            RendererLayer_Default,
            RendererDrawMode_All,
        },
    [RendererMeshPreset_Grid] =
        {
            "Grid",
            RenderPipelineType_Grid,
            RendererMeshFlag_Fixed,
            RendererLayer_Default,
            RendererDrawMode_All,
        },
    [RendererMeshPreset_Line] =
        {
            "Line", // For SEM that use lines, use another config with a fixed
                    // flag. For line-based SEMs, the texture shader is set to
                    // Line and wireframe topology is used as the base.
            RenderPipelineType_Line,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_Wireframe | RendererDrawMode_Boundbox,
        },
    [RendererMeshPreset_Screen] =
        {
            "Screen",
            RenderPipelineType_Screen,
            RendererMeshFlag_Fixed,
            RendererLayer_Default,
            RendererDrawMode_None,
        },
    [RendererMeshPreset_Shadow] =
        {
            "Shadow",
            RenderPipelineType_Shadow,
            RendererMeshFlag_None,
            RendererLayer_Default,
            RendererDrawMode_None,
        },
    [RendererMeshPreset_Billboard] =
        {
            "Billboard",
            RenderPipelineType_Billboard,
            RendererMeshFlag_Fixed,
            RendererLayer_Default,
            RendererDrawMode_All,
        },
};

const RendererMeshConfig *
renderer_mesh_config_from_preset(const RendererMeshPreset);

const RendererMeshConfig *
renderer_mesh_config_from_pipeline(const RenderPipelineType);

const RendererMeshConfig *
renderer_mesh_config_from_descriptor(const RendererMeshConfigDescriptor *);

EXTERN_C_END

#endif
