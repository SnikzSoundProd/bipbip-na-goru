// basic pixel shader: single directional light lambert + ambient
struct PSIn {
    float4 pos     : SV_POSITION;
    float3 wpos    : WORLDPOS;
    float3 normal  : NORMAL;
    float2 uv      : TEXCOORD0;
    float3 color   : COLOR0;
};

float4 main(PSIn i) : SV_Target {
    float3 n = normalize(i.normal);
    float3 l = normalize(float3(-0.5, -1.0, -0.35)); // light dir (from sun)
    float ndl = max(dot(n, -l), 0.0);
    float3 ambient = float3(0.35, 0.38, 0.45);
    float3 sun = float3(1.0, 0.95, 0.85) * ndl;
    float3 rgb = i.color * (ambient + sun);
    return float4(rgb, 1.0);
}
