module types;

// ---------------------------------------------------------------------------
// Types shared by (almost) every pass: the standard mesh vertex and the three
// "scene" uniforms (viewport / camera / mesh).
//
// Matrices are declared `column_major` so that the memory layout matches
// WGSL / cglm (columns contiguous). This makes the modules independent of the
// -matrix-layout-* compiler flag. In Slang code use `mul(M, v)` as usual.
// NOTE: struct members must be `public` to be visible from importing modules.
// ---------------------------------------------------------------------------

public struct VertexIn {
  [[vk::location(0)]]
  public float3 aPos : POSITION;
  [[vk::location(1)]]
  public float3 aNorm : NORMAL;
  [[vk::location(2)]]
  public float4 aTan : TANGENT;
  [[vk::location(3)]]
  public float3 aCol : COLOR;
  [[vk::location(4)]]
  public float2 aUv : TEXCOORD0;
};

public struct Mesh {
  public column_major float4x4 model;
  public column_major float4x4 modelInvTranspose;
  public float4 position;
  public uint probe_reflection_plane_count;
  public uint probe_reflection_grid_count;
};

public struct Camera {
  public column_major float4x4 view;
  public float4 position;
  public float4 lookat;
  public uint mode;
};

public struct Viewport {
  public column_major float4x4 projection;
  public uint width;
  public uint height;
  public uint2 _pad;
};

// A lone view-projection matrix (shadow / reflection passes).
public struct Projection {
  public column_major float4x4 view;
};
