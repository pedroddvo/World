struct vs_input {
    [[vk::location(0)]] float4 pos;
}; 

struct vs_output {
    float4 pos : SV_POSITION;
    float2 uv;
};

[shader("vertex")]
vs_output vertex_main(vs_input input) {
    vs_output output;
    output.pos = float4(input.pos.xy, 0.0, 1.0);
    output.uv = input.pos.zw;
    return output;
}

[[vk::binding(0, 0)]] Sampler2D    tex;

[shader("fragment")]
float4 fragment_main(vs_output input) {
    return tex.Sample(input.uv);
}
