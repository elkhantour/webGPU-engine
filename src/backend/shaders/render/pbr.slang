import core.types;
import core.math;
import core.fog;
import bindings.scene;
import lighting.lights;
import lighting.probes;
import lighting.material;
import lighting.forward;
import lighting.ibl;
import lighting.shadows;
import materials.pbr_surface;

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
[[vk::binding(2, 2)]] ConstantBuffer<ProbeList>   uProbes;
[[vk::binding(3, 2)]] PointShadowMaps             point_shadow_maps;
[[vk::binding(4, 2)]] DirectionalShadowMaps       directional_shadow_maps;
[[vk::binding(5, 2)]] ShadowSampler               shadow_sampler; // comparison sampler (depth) or plain sampler (colour)
[[vk::binding(6, 2)]] Texture2DArray<float4>      probe_reflection_plane_texture;
[[vk::binding(7, 2)]] TextureCubeArray<float4>    probe_reflection_grid_texture;
[[vk::binding(8, 2)]] TextureCube<float4>         skybox_texture;

[shader("vertex")]
PbrVertexOut vs_main(VertexIn input)
{
    return pbr_vertex(input, uMesh, uCamera, uViewport, /*tangent_in_world*/ false);
}

[shader("fragment")]
float4 fs_main(PbrVertexOut input) : SV_Target
{
    Camera camera = uCamera;
    float3 cam_pos = camera.position.xyz;
    float2 uv = input.vUv;

    float4 albedo    = diffuse_texture.SampleLevel(linear_sampler, uv, 3.0);
    float4 metallic  = metallic_texture.SampleLevel(linear_sampler, uv, 3.0);
    float4 normal    = normal_texture.SampleLevel(linear_sampler, uv, 3.0);
    float4 occlusion = occlusion_texture.SampleLevel(linear_sampler, uv, 3.0);
    float4 emissive  = emissive_texture.SampleLevel(linear_sampler, uv, 3.0);

    Material material = create_material(albedo, metallic, normal, occlusion, emissive, uPBR);
    if (material.alpha < uPBR.alpha_threshold)
        discard;

    // TBN is actually NBT depending on the normal texture encoding:
    //   BGRA8 -> NBT, RGBA8 -> TBN
    float3 world_normal = normalize(tbn_transform(input.vTangent, input.vBitangent,
                                                  input.vNormal, material.normal));

    // direct + ambient lights
    DirectLighting direct = accumulate_lights(material, uLights, input.vFrag, world_normal, cam_pos);
    float3 ambient = direct.ambient;

    // reflection IBL
    float3 V = normalize(cam_pos - input.vFrag);
    float4 plane_reflection = float4(0.0);
    // TODO: planar reflection (disabled). Per plane i < uMesh.probe_reflection_plane_count:
    //   uv = compute_reflection_uv(uViewport.projection, uProbes.reflection_plane[i].view, vFrag)
    //   plane_reflection = probe_reflection_plane_texture.SampleLevel(linear_sampler,
    //                          float3(uv, texture_layer), 0)
    //   plane_reflection.a *= saturate(dot(world_normal, plane.normal)) / 1.3;
    ambient += compute_ibl(material, world_normal, V, skybox_texture, linear_sampler, plane_reflection);

    // shadows
    float point_shadow = point_shadow_factor(uLights, point_shadow_maps, shadow_sampler, input.vFrag);
    float spot_shadow  = spot_shadow_factor(uLights, directional_shadow_maps, shadow_sampler, input.vFrag);
    float sun_shadow   = sun_shadow_factor(uLights, directional_shadow_maps, shadow_sampler, input.vFrag);
    float3 combined_shadow = lerp(float3(min(direct.ambient_intensity, 1.0)), float3(1.0),
                                  point_shadow + spot_shadow + sun_shadow);

    float4 combined = float4((ambient + direct.Lo * combined_shadow) + material.emissive, 1.0);
    //return apply_fog(combined, uEnvironment.fog, length(input.vFrag - cam_pos));
    return albedo;
}
