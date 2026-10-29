module material;

// Evaluated surface material
public struct Material {
  public float4 albedo;
  public float metallic;
  public float roughness;
  public float3 normal;
  public float occlusion;
  public float3 emissive;
  public float alpha;
};

// glTF-style factors (uniform)
public struct PBRMaterial {
  public float4 base_color_factor;
  public float3 emissive_factor;
  public float metallic_factor;
  public float roughness_factor;
  public float specular_factor;
  public float normal_scale;
  public float occlusion_strength;
  public float alpha_threshold;
};

public Material create_material(float4 diffuse, float4 metallic, float4 normal,
                                float4 occlusion, float4 emissive,
                                PBRMaterial factors) {
  Material material;
  material.albedo = diffuse * factors.base_color_factor;
  material.metallic = metallic.r * factors.metallic_factor;
  material.roughness =
      metallic.g * factors.roughness_factor; // roughness in G channel
  // material.normal    = normalize(normal.rgb * 2.0 - 1.0) *
  // factors.normal_scale; // [0,1] -> [-1,+1]
  material.normal = normalize((normal.rgb * 2.0 - 1.0) * float3(factors.normal_scale, factors.normal_scale, 1.0f));
  material.occlusion = occlusion.r * factors.occlusion_strength;
  material.emissive = emissive.rgb * factors.emissive_factor;
  material.alpha = factors.base_color_factor.a * diffuse.a;
  return material;
}
