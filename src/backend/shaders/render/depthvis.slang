// depth_vis.slang

struct Params
{
    float nearZ;   // renamed: `near`/`far` are reserved words in HLSL-family languages
    float farZ;
    float lo;
    float hi;
    uint  flags;   // bit 0: linearize, bit 1: reverse-Z
    uint  p0;
    uint  p1;
    uint  p2;
};

[[vk::binding(0, 0)]] DepthTexture2DArray depthTex;
[[vk::binding(1, 0)]] ConstantBuffer<Params> u;

[shader("vertex")]
float4 vs_main(uint i : SV_VertexID) : SV_Position
{
    // Fullscreen triangle
    float2 p = float2((i << 1u) & 2u, i & 2u);
    return float4(p * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fs_main(float4 pos : SV_Position) : SV_Target
{
    // Load(int4(x, y, arrayLayer, mipLevel))
    float d = depthTex.Load(int4(int2(pos.xy), 0, 0));

    if ((u.flags & 2u) != 0u)
        d = 1.0 - d;

    if ((u.flags & 1u) != 0u)
        d = (u.nearZ * u.farZ / (u.farZ - d * (u.farZ - u.nearZ))) / u.farZ;

    float v = clamp((d - u.lo) / max(u.hi - u.lo, 0.000001), 0.0, 1.0);
    return float4(v, v, v, 1.0);
}
