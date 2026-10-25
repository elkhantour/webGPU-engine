module brdf;
import core.constants;
import core.math;
import lighting.material;

// Fresnel using Schlick approximation.
//   Metallic surface   : F0 = albedo
//   Dielectric surface : F0 ~ 0.04 (plastic / wood...)
[ForceInline]
public float3 fresnel_schlick(float3 F0, float cos_theta) {
  return F0 + (1.0 - F0) * pow(1.0 - cos_theta, 5.0);
}

[ForceInline]
public float3 base_reflectivity(Material material) {
  return lerp(float3(0.04), material.albedo.rgb, material.metallic);
}

// Normal Distribution Function (GGX / Trowbridge-Reitz)
[ForceInline]
public float distribution_GGX(float NdotH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float NdotH2 = NdotH * NdotH;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom = PI * denom * denom;
  return a2 / denom;
}

[ForceInline]
public float geo_schlick_ggx(float NdotV, float roughness) {
  float r = roughness + 1.0;
  float k = (r * r) / 8.0;
  return NdotV / (NdotV * (1.0 - k) + k);
}

// Geometry function: handles shadowing and masking
[ForceInline]
public float geo_smith(float NdotV, float NdotL, float roughness) {
  return geo_schlick_ggx(NdotV, roughness) * geo_schlick_ggx(NdotL, roughness);
}

// Cook-Torrance BRDF for one light
public float3 compute_point_light(Material material, float3 fragment_position,
                                  float3 world_normal, float3 camera_position,
                                  float3 light_position, float4 light_color,
                                  float light_intensity) {

  float3 V = normalize(camera_position - fragment_position);
  float3 L = normalize(light_position - fragment_position);
  float3 H = normalize(V + L);

  float HdotV = dot_max(H, V);
  float3 F0 = base_reflectivity(material);
  float3 F = fresnel_schlick(F0, HdotV);

  float NdotH = dot_max(world_normal, H);
  float D = distribution_GGX(NdotH, material.roughness);

  float NdotV = dot_max(world_normal, V);
  float NdotL = dot_max(world_normal, L);
  float G = geo_smith(NdotV, NdotL, material.roughness);

  float3 specular = (D * F * G) / (4.0 * NdotV * NdotL + 0.001);

  // Metallic surfaces only use the specular term
  float3 kD = (float3(1.0) - F) * (1.0 - material.metallic);
  float3 diffuse = material.albedo.rgb / PI;

  return (kD * diffuse + specular) * light_color.rgb * light_intensity * NdotL;
}

public float3 compute_ambient_light(Material material, float light_intensity,
                                    float4 light_color) {
  return (light_color * light_intensity * material.albedo).rgb;
}
