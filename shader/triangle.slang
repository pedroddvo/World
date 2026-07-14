struct vs_input {
    [[vk::location(0)]] float4 pos;
    [[vk::location(1)]] float4 normal;
}; 

struct vs_output {
    float4 pos : SV_POSITION;
    float3 vpos, normal;
};

[shader("vertex")]
vs_output vertex_main(vs_input input, uniform float4x4 mvp) {
    vs_output output;
    output.pos = mul(mvp, float4(input.pos.xyz, 1.0));
    output.vpos = input.pos.xyz;
    output.normal = input.normal.xyz;
    return output;
}

[shader("fragment")]
float4 fragment_main(vs_output input) {
    float3 norm = normalize(input.normal.xyz);
    float3 lightDir = normalize(float3(0.0, 10000.0, 0.0) - input.vpos.xyz);
    float diff = max(dot(norm, lightDir), 0.0);
    float3 diffuse = diff * float3(1.0, 1.0, 1.0);
    return float4(diffuse, 1.0);
    // return float4(1.0, 0.0, 0.0, 1.0);
}
