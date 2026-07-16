struct vs_input {
    [[vk::location(0)]] float2 pos;
}; 

struct vs_output {
    float4 pos : SV_POSITION;
    float3 vpos, normal;
};

[[vk::binding(0, 0)]]
Texture2D<float4> HeightMap;

[[vk::binding(1, 0)]]
SamplerState Sampler;

[shader("vertex")]
vs_output vertex_main(vs_input input, uniform float4x4 mvp) {
    vs_output output;

    float4 h = HeightMap.SampleLevel(Sampler, input.pos, 0);
    float4 p = float4(input.pos.x*800.0, (1.0-h.x)*100.0, input.pos.y*800.0, 1.0);

    output.pos = mul(mvp, p);
    output.vpos = p.xyz;
    output.normal = float3(h.xy, sqrt(saturate(1.0 - dot(h.xy, h.xy))));
    return output;
}

[shader("fragment")]
float4 fragment_main(vs_output input) {
    float3 lightColor = float3(1.0, 1.0, 1.0);
    float3 ambient = 0.1 * lightColor;

    float3 norm = normalize(input.normal.xyz);
    float3 lightDir = normalize(float3(0.0, 10000.0, 0.0) - input.vpos);
    float diff = max(dot(norm, lightDir), 0.0);

    float3 diffuse = diff * lightColor;
    // return float4(ambient + diffuse, 1.0);
    float w = input.vpos.y/100.0;
    return float4(w, w, w, 1.0);
    // return float4(1.0, 0.0, 0.0, 1.0);
}
