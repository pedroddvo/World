enum NoiseType
{
    Perlin = 0,
    Voronoi,
};

struct NoiseConfig {
    NoiseType Type;
    uint Seed = 0, Octaves = 1;

    float Lacunarity = 2.0f, Gain = 0.5f;
    float Amplitude = 0.5f, Frequency = 1.0f;
};

float2 GetGradient(int2 p, uint seed = 0)
{
    uint x = (uint)p.x;
    uint y = (uint)p.y;
    uint hash = x * 73856093u ^ y * 19349663u ^ seed * 83492791u;
    float angle = hash * 0.001f;
    return float2(cos(angle), sin(angle));
}

float2 GetGradientZO(int2 p, uint seed = 0)
{
    uint x = (uint)p.x;
    uint y = (uint)p.y;
    uint hash = x * 73856093u ^ y * 19349663u ^ seed * 83492791u;

    hash = hash ^ (hash >> 16);
    hash *= 0x7feb352du;
    hash = hash ^ (hash >> 15);
    hash *= 0x846ca68bu;
    hash = hash ^ (hash >> 16);

    uint hashX = hash;
    uint hashY = (hash * 2654435769u);

    float offsetX = (float)hashX / 4294967295.0f;
    float offsetY = (float)hashY / 4294967295.0f;

    return float2(offsetX, offsetY);
}

float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

interface INoiseFunction
{
    static float evaluate(float2 v, const NoiseConfig cfg);
}

struct Perlin2D : INoiseFunction
{
    static float evaluate(float2 v, const NoiseConfig cfg)
    {
        float2 f = floor(v);
        float2 t = v - f;

        int2 iFloor = (int2)f;

        float d00 = dot(t - float2(0.0f, 0.0f), GetGradient(iFloor + int2(0, 0), cfg.Seed));
        float d01 = dot(t - float2(0.0f, 1.0f), GetGradient(iFloor + int2(0, 1), cfg.Seed));
        float d10 = dot(t - float2(1.0f, 0.0f), GetGradient(iFloor + int2(1, 0), cfg.Seed));
        float d11 = dot(t - float2(1.0f, 1.0f), GetGradient(iFloor + int2(1, 1), cfg.Seed));

        float u = Fade(t.x);
        float w = Fade(t.y);
        
        return lerp(lerp(d00, d10, u), lerp(d01, d11, u), w);
    }
}

struct Voronoi2D : INoiseFunction
{
    static float evaluate(float2 v, const NoiseConfig cfg)
    {
        float2 f = floor(v);

        float minDist = 2.0f;

        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            [unroll]
            for (int y = -1; y <= 1; y++)
            {
                float2 n = float2((float)x, (float)y);

                int2 cellCoord = (int2)(f + n);
                float dist = length(GetGradientZO(cellCoord, cfg.Seed) + n - fract(v));
                if (dist < minDist)
                {
                    minDist = dist;
                }
            }
        }

        return minDist;    
    }
}

float Noise<F : INoiseFunction>(float2 v, const NoiseConfig cfg)
{
    float y = 0.0f;
    float freq = cfg.Frequency, amp = cfg.Amplitude;

    for (int i = 0; i < cfg.Octaves; i++)
    {
        y += amp * F.evaluate(freq * v, cfg);
        freq *= cfg.Lacunarity;
        amp *= cfg.Gain;
    }

    if (F is Perlin2D)
        return (y + 0.7071f) / 1.4142f;
    return y;
}

struct Vertex
{
    float4 Position, Normal;
}

[[vk::binding(0, 0)]]
globallycoherent RWStructuredBuffer<Vertex> Vertices;

[numthreads(16, 16, 1)]
[shader("compute")]
void compute_main(uint3 id : SV_DispatchThreadID, uniform NoiseConfig cfg) 
{
    uint2 globalID = id.xy;
    float2 uv = float2(globalID) / float2(800.0, 800.0);
    float height = 0.0f;

    switch (cfg.Type)
    {
    case NoiseType::Perlin: height = Noise<Perlin2D>(uv, cfg); break;
    case NoiseType::Voronoi: height = Noise<Voronoi2D>(uv, cfg); break;
    }

    uint index = globalID.y * 800 + globalID.x;
    Vertices[index].Position = 
        float4(uv.x*800.0-400.0, (1.0f - height) * 100.0f, uv.y*800.0-400.0, 0.0);
    Vertices[index].Normal = float4(0.0, 1.0, 0.0, 0.0);
}
