module pbr_surface;
import core.types;
import core.transform;

// Vertex output shared by the PBR family (pbr, reflection)
public struct PbrVertexOut {
  public float4 vPosition : SV_Position;
  [[vk::location(0)]]
  public float3 vNormal : NORMAL;
  [[vk::location(1)]]
  public float3 vTangent : TANGENT;
  [[vk::location(2)]]
  public float3 vBitangent : BINORMAL;
  [[vk::location(3)]]
  public float3 vCol : COLOR;
  [[vk::location(4)]]
  public float2 vUv : TEXCOORD0;
  [[vk::location(5)]]
  public float3 vFrag : TEXCOORD1;
};

// tangent_in_world:
//   true  -> tangent is rotated by the model matrix (correct for rotated
//   meshes) false -> tangent is passed through in object space (what pbr.wgsl
//   did)
public PbrVertexOut pbr_vertex(VertexIn input, Mesh mesh, Camera camera,
                               Viewport viewport) {

  float3 normal = normalize(mul((float3x3)mesh.modelInvTranspose, input.aNorm));
  float3 tangent =
      normalize(mul((float3x3)mesh.modelInvTranspose, input.aTan.xyz));

  // float3 tangent = normalize(mul((float3x3)mesh.model, input.aTan.xyz));
  tangent = normalize(tangent - normal * dot(normal, tangent));
  // re - orthogonalize

  float3 bitangent = input.aTan.w * cross(normal, tangent);

  float4 world = local_to_world(mesh, input.aPos);

  PbrVertexOut output;
  output.vPosition = world_to_clip(viewport, camera, world);
  output.vNormal = normal;
  output.vTangent = tangent;
  output.vBitangent = bitangent;
  output.vCol = input.aCol;
  output.vUv = input.aUv;
  // world-space position, used for lighting
  output.vFrag = world.xyz;
  return output;
}

// TBN * v, with the matrix built from its three basis vectors.
public float3 tbn_transform(float3 T, float3 B, float3 N, float3 v) {
  return v.x * T + v.y * B + v.z * N;
}
