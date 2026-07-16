enum NoiseType : uint
{
    Perlin = 0,
    Voronoi,
};

struct ErosionParameters
{
    float Strength = 0.22f, GullyWeight = 0.5f, Detail = 1.5f;
    float4 Rounding = float4(0.1f, 0.0f, 0.1f, 2.0f);
    float4 Onset = float4(1.25f, 1.25f, 2.8f, 1.5f);
    float2 AssumedSlope = float2(0.7f, 1.0f);
    float Scale = 0.15f;
    int Octaves = 4;
    float Lacunarity = 2.0f, Gain = 0.5f, CellScale = 0.7f;
    float Normalization = 0.5f, RidgeMap = 0.5f, Debug = 0.0f;
};

struct NoiseConfig {
    uint Seed = 0, Octaves = 1;

    float Lacunarity = 2.0f, Gain = 0.5f;
    float Amplitude = 0.5f, Frequency = 1.0f;
};

struct ComputeConfig
{
    NoiseType Type;
    NoiseConfig noiseCfg;
    ErosionParameters erosionParams;
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

// Advanced Terrain Erosion Filter copyright (c) 2025 Rune Skovbo Johansen
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#define TAU 6.28318530717959

float2 hash(float2 x)
{
    const float2 k = float2(0.3183099f, 0.3678794f);
    x = x * k + k.yx;
    return -1.0f + 2.0f * fract(16.0f * k * fract(x.x * x.y * (x.x + x.y)));
}

float4 PhacelleNoise(float2 p, float2 normDir, float freq,
                     float offset, float normalization)
{
    float2 sideDir = normDir.yx * float2(-1.0f, 1.0f) * freq * (float)TAU;
    offset *= (float)TAU;

    float2 pInt = floor(p);
    float2 pFrac = fract(p);
    float2 phaseDir = float2(0.0f);
    float weightSum = 0.0f;

    for (int i = -1; i <= 2; i++)
    {
        for (int j = -1; j <= 2; j++)
        {
            float2 gridOffset = float2((float)i, (float)j);

            float2 gridPoint = pInt + gridOffset;

            float2 randomOffset = hash(gridPoint) * 0.5f;

            float2 vectorFromCellPoint = pFrac - gridOffset - randomOffset;

            float sqrDist = dot(vectorFromCellPoint, vectorFromCellPoint);
            float weight = exp(-sqrDist * 2.0f);
            weight = max(0.0f, weight - 0.01111f);

            weightSum += weight;

            float waveInput = dot(vectorFromCellPoint, sideDir) + offset;

            phaseDir += float2(cos(waveInput), sin(waveInput)) * weight;
        }
    }

    float2 interpolated = phaseDir / weightSum;
    float magnitude = sqrt(dot(interpolated, interpolated));
    magnitude = max(1.0f - normalization, magnitude);
    return float4(interpolated / magnitude, sideDir);
}

float clamp01(float x) 
{
    return clamp(x, 0.0f, 1.0f);
}

float pow_inv(float t, float power)
{
    return 1.0f - pow(1.0f - clamp01(t), power);
}

float ease_out(float t)
{
    float v = 1.0f - clamp01(t);
    return 1.0f - v * v;
}

float smooth_start(float t, float smoothing)
{
    if (t >= smoothing)
        return t - 0.5f * smoothing;
    return 0.5f * t * t / smoothing;
}

float2 safe_normalize(float2 n)
{
    float l = length(n);
    return (abs(l) > 1e-10f) ? (n / l) : n;
}

float4 ErosionFilter(float2 p, float3 heightAndSlope, float fadeTarget,
                     float strength, float gullyWeight, float detail,
                     float4 rounding, float4 onset,
                     float2 assumedSlope, float scale, int octaves,
                     float lacunarity, float gain, float cellScale,
                     float normalization, float ridgeMap, float debug)
{
    strength *= scale;
    fadeTarget = clamp(fadeTarget, -1.0f, 1.0f);

    float3 inputHeightAndSlope = heightAndSlope;
    float freq = 1.0f / (scale * cellScale);
    float slopeLength = max(length(heightAndSlope.yz), 1e-10f);
    float magnitude = 0.0f;
    float roundingMult = 1.0f;

    float roundingForInput =
        lerp(rounding.y, rounding.x, clamp01(fadeTarget + 0.5f)) * rounding.z;
    float combiMask = ease_out(
        smooth_start(slopeLength * onset.x, roundingForInput * onset.x));

    float ridgeMapCombiMask = ease_out(slopeLength * onset.z);
    float ridgeMapFadeTarget = fadeTarget;

    float2 gullySlope =
        lerp(heightAndSlope.yz,
             heightAndSlope.yz / slopeLength * assumedSlope.x, assumedSlope.y);

    for (int i = 0; i < octaves; i++)
    {
        float4 phacelle = PhacelleNoise(p * freq, safe_normalize(gullySlope),
                                         cellScale, 0.25f, normalization);
        phacelle.zw *= -freq;
        float sloping = abs(phacelle.y);

        gullySlope += sign(phacelle.y) * phacelle.zw * strength * gullyWeight;

        float3 gullies = float3(phacelle.x, phacelle.y * phacelle.zw);
        float3 fadedGullies = lerp(float3(fadeTarget, 0.0f, 0.0f),
                                   gullies * gullyWeight, combiMask);
        heightAndSlope += fadedGullies * strength;
        magnitude += strength;

        fadeTarget = fadedGullies.x;

        float roundingForOctave =
            lerp(rounding.y, rounding.x, clamp01(phacelle.x + 0.5f)) * roundingMult;
        float newMask = ease_out(
            smooth_start(sloping * onset.y, roundingForOctave * onset.y));
        combiMask = pow_inv(combiMask, detail) * newMask;

        ridgeMapFadeTarget = lerp(ridgeMapFadeTarget, gullies.x, ridgeMapCombiMask);
        float newRidgeMapMask = ease_out(sloping * onset.w);
        ridgeMapCombiMask = ridgeMapCombiMask * newRidgeMapMask;

        strength *= gain;
        freq *= lacunarity;
        roundingMult *= rounding.w;
    }

    ridgeMap = ridgeMapFadeTarget * (1.0f - ridgeMapCombiMask);
    debug = fadeTarget;

    float3 heightAndSlopeDelta = heightAndSlope - inputHeightAndSlope;
    return float4(heightAndSlopeDelta, magnitude);
}

float4 ErosionFilter(float2 p, float3 heightAndSlope, float fadeTarget,
                     ErosionParameters params)
{
    return ErosionFilter(
        p, heightAndSlope, fadeTarget, params.Strength, params.GullyWeight,
        params.Detail, params.Rounding, params.Onset, params.AssumedSlope,
        params.Scale, params.Octaves, params.Lacunarity, params.Gain,
        params.CellScale, params.Normalization, params.RidgeMap, params.Debug);
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

float3 SampleNoise<F : INoiseFunction>(float2 v, const NoiseConfig cfg)
{
    float vu = Noise<F>({v.x, v.y + 1.0/800.0}, cfg);
    float vd = Noise<F>({v.x, v.y - 1.0/800.0}, cfg);
    float vr = Noise<F>({v.x + 1.0/800.0, v.y}, cfg);
    float vl = Noise<F>({v.x - 1.0/800.0, v.y}, cfg);
    return float3(Noise<F>(v, cfg), normalize(float2(vd - vu, vl - vr)));
}

struct Vertex
{
    float4 Position, Normal;
}

[[vk::binding(0, 0)]]
[format("rgba32f")]
globallycoherent RWTexture2D<float4> HeightMap;

[numthreads(16, 16, 1)]
[shader("compute")]
void compute_main(uint3 id : SV_DispatchThreadID, uniform ComputeConfig ComputeCfg) 
{
    uint2 globalID = id.xy;
    float2 uv = float2(globalID) / float2(800.0, 800.0);

    float3 n = float3(0.0);
    switch (ComputeCfg.Type)
    {
    case NoiseType::Perlin: n = SampleNoise<Perlin2D>(uv, ComputeCfg.noiseCfg); break;
    case NoiseType::Voronoi: n = SampleNoise<Voronoi2D>(uv, ComputeCfg.noiseCfg); break;
    }

    float4 h = ErosionFilter(uv, n, 1.0f, ComputeCfg.erosionParams);
    n.x += h.x;

    HeightMap[globalID.xy] = float4(n, 0.0);
}
