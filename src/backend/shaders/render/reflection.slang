import core.types;
import core.fog;
import bindings.scene;
import lighting.lights;
import lighting.material;
import lighting.forward;
import materials.pbr_surface;

// Lightweight PBR used when rendering reflection probes:
// direct lights + ambient + fog. No shadows, no IBL, no alpha discard.
// Same bind groups as pbr.slang.

// ---- material (group 1) ----------------------------------------------------
[[vk::binding(0, 1)]] Texture2D<float4> diffuse_texture;
[[vk::binding(1, 1)]] Texture2D<float4> metallic_texture;
[[vk::binding(2, 1)]] Texture2D<float4> normal_texture;
[[vk::binding(3, 1)]] Texture2D<float4> emissive_texture;
[[vk::binding(4, 1)]] Texture2D<float4> occlusion_texture;
[[vk::binding(5, 1)]] SamplerState      linear_sampler;
[[vk::binding(6, 1)]] ConstantBuffer<PBRMaterial> uPBR;

// ---- environment (group 2) -------------------------------------------------
[[vk::binding(0, 2)]] ConstantBuffer<Environment> uEnvironment;
[[vk::binding(1, 2)]] ConstantBuffer<LightList>   uLights;

[shader("vertex")]
PbrVertexOut vs_main(VertexIn input)
{
    return pbr_vertex(input, uMesh, uCamera, uViewport);
}

[shader("fragment")]
float4 fs_main(PbrVertexOut input) : SV_Target
{
    float3 cam_pos = uCamera.position.xyz;
    float2 uv = input.vUv;

    float4 albedo    = diffuse_texture.Sample(linear_sampler, uv);
    float4 metallic  = metallic_texture.Sample(linear_sampler, uv);
    float4 normal    = normal_texture.Sample(linear_sampler, uv);
    float4 occlusion = occlusion_texture.Sample(linear_sampler, uv);
    float4 emissive  = emissive_texture.Sample(linear_sampler, uv);

    Material material = create_material(albedo, metallic, normal, occlusion, emissive, uPBR);
    float3 world_normal = normalize(tbn_transform(input.vTangent, input.vBitangent,
                                                  input.vNormal, material.normal));

    DirectLighting direct = accumulate_lights(material, uLights, input.vFrag, world_normal, cam_pos);

    float4 combined = float4(direct.ambient + direct.Lo + material.emissive, 1.0);
    return apply_fog(combined, uEnvironment.fog, length(input.vFrag - cam_pos));
}
