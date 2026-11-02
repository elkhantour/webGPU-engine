module forward;
import lighting.lights;
import lighting.material;
import lighting.brdf;

public struct DirectLighting
{
    public float3 Lo;                 // sum of direct (point + spot + sun)
    public float3 ambient;            // sum of ambient lights
    public float  ambient_intensity;
};

// Accumulates every light in the list for one fragment.
// (Spot and sun lights are shaded like point lights; their shadows are
// handled separately in lighting.shadows.)
public DirectLighting accumulate_lights(Material material, ConstantBuffer<LightList> lights,
                                        float3 frag, float3 world_normal, float3 cam_pos)
{
    DirectLighting r;
    r.Lo = float3(0.0);
    r.ambient = float3(0.0);
    r.ambient_intensity = 0.0;

    for (uint i = 0u; i < lights.point_count; i++)
    {
        PointLight l = lights.point_entries[i];
        r.Lo += compute_point_light(material, frag, world_normal, cam_pos, l.position, l.color, l.intensity);
    }
    for (uint i = 0u; i < lights.spot_count; i++)
    {
        SpotLight l = lights.spot_entries[i];
        r.Lo += compute_point_light(material, frag, world_normal, cam_pos, l.position, l.color, l.intensity);
    }
    for (uint i = 0u; i < lights.sun_count; i++)
    {
        SunLight l = lights.sun_entries[i];
        r.Lo += compute_point_light(material, frag, world_normal, cam_pos, l.position, l.color, l.intensity);
    }
    for (uint i = 0u; i < lights.ambient_count; i++)
    {
        AmbientLight l = lights.ambient_entries[i];
        r.ambient += compute_ambient_light(material, l.intensity, l.color);
        r.ambient_intensity += l.intensity;
    }
    return r;
}
