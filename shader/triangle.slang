struct vs_input {
    [[vk::location(0)]] float4 pos;
}; 

struct vs_output {
    float4 pos : SV_POSITION;
};

[shader("vertex")]
vs_output vertex_main(vs_input input) {
    vs_output output;
    output.pos = input.pos;
    return output;
}


[shader("fragment")]
float4 fragment_main(vs_output input) {
    return float4(1.0, 0.0, 0.0, 1.0);
}
