import core.types;
import core.math;

[[vk::binding(0, 0)]]
ConstantBuffer<Projection> viewProjection;
[[vk::binding(1, 0)]]
ConstantBuffer<Mesh> uMesh;

struct VertexOut {
  float4 vPosition : SV_Position;
};

[shader("vertex")]
VertexOut vs_main(VertexIn input) {
  float4 model = mul(uMesh.model, float4(input.aPos, 1.0));

  VertexOut output;
  output.vPosition = mul(viewProjection.view, model);
  return output;
}

[shader("fragment")]
float4 fs_main(VertexOut input) : SV_Target {
  float depth = linearize_depth(input.vPosition.z, 0.1, 100.0);
  return float4(float3(depth), 1.0);
  // NOTE: with an orthographic projection z is already linear.
}
