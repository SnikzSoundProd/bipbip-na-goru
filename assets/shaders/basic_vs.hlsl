// basic vertex shader: world-space transform + lambert inputs
cbuffer PerFrame : register(b0) {
    float4x4 viewProj;
};
cbuffer PerObject : register(b1) {
    float4x4 world;
};

struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float3 color   : COLOR0;
};

struct PSIn {
    float4 pos     : SV_POSITION;
    float3 wpos    : WORLDPOS;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float3 color   : COLOR0;
};

PSIn main(VSIn i) {
    PSIn o;
    float4 wp = mul(world, float4(i.pos, 1.0));
    o.wpos = wp.xyz;
    o.pos = mul(viewProj, wp);
    o.normal = normalize(mul((float3x3)world, i.normal));
    o.uv = i.uv;
    o.color = i.color;
    return o;
}
