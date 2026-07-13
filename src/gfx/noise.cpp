#define GLM_SWIZZLE
#include "noise.hpp"

namespace noise
{

static glm::vec2 GetGradient(glm::ivec2 p, uint32_t seed = 0)
{
    uint32_t x = (uint32_t)p.x, y = (uint32_t)p.y;
    uint32_t hash = x * 73856093 ^ y * 19349663 ^ seed * 83492791;
    float angle = hash * 0.001f;
    return glm::vec2(std::cos(angle), std::sin(angle));
}

static glm::vec2 GetGradientZO(glm::ivec2 p, uint32_t seed = 0)
{
    uint32_t x = (uint32_t)p.x;
    uint32_t y = (uint32_t)p.y;
    uint32_t hash = x * 73856093U ^ y * 19349663U ^ seed * 83492791U;

    hash = hash ^ (hash >> 16);
    hash *= 0x7feb352dU;
    hash = hash ^ (hash >> 15);
    hash *= 0x846ca68bU;
    hash = hash ^ (hash >> 16);

    uint32_t hashX = hash;
    uint32_t hashY = (hash * 2654435769U);

    float offsetX = (float)hashX / (float)UINT32_MAX;
    float offsetY = (float)hashY / (float)UINT32_MAX;

    return glm::vec2(offsetX, offsetY);
}

static float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Perlin2D(glm::vec2 v, const NoiseConfig& cfg)
{
    glm::vec2 f = glm::floor(v);
    glm::vec2 t = v - f;

    float d00 = glm::dot(t - glm::vec2(0, 0),
                         GetGradient(f + glm::vec2(0, 0), cfg.Seed));
    float d01 = glm::dot(t - glm::vec2(0, 1),
                         GetGradient(f + glm::vec2(0, 1), cfg.Seed));
    float d10 = glm::dot(t - glm::vec2(1, 0),
                         GetGradient(f + glm::vec2(1, 0), cfg.Seed));
    float d11 = glm::dot(t - glm::vec2(1, 1),
                         GetGradient(f + glm::vec2(1, 1), cfg.Seed));

    float u = Fade(t.x), w = Fade(t.y);
    return glm::mix(glm::mix(d00, d10, u), glm::mix(d01, d11, u), w);
}

float Noise(glm::vec2 v, const NoiseConfig& cfg,
            float noiseFn(glm::vec2, const NoiseConfig&))
{
    float y = 0.0f;
    float freq = cfg.Frequency, amp = cfg.Amplitude;

    for (int i = 0; i < cfg.Octaves; i++)
    {
        y += amp * noiseFn(freq * v, cfg);
        freq *= cfg.Lacunarity;
        amp *= cfg.Gain;
    }

    if (noiseFn == Perlin2D)
        return (y + 0.7071f) / 1.4142f;
    return y;
}

float Voronoi2D(glm::vec2 v, const NoiseConfig& cfg)
{
    glm::vec2 f = glm::floor(v);

    float minDist = 2.0f;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            glm::vec2 n = glm::vec2(x, y);
            float dist =
                glm::length(GetGradientZO(f + n, cfg.Seed) + n - glm::fract(v));
            if (dist < minDist)
                minDist = dist;
        }
    }

    return minDist;
}

float ErosionFilter(glm::vec2 v, NoiseFunction noiseFn, const NoiseConfig& cfg)
{
    glm::vec2 f = glm::floor(v);

    float minDist = 2.0f;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            glm::vec2 n = glm::vec2(x, y);
            float dist =
                glm::length(GetGradientZO(f + n, cfg.Seed) + n - glm::fract(v));
            if (dist < minDist)
                minDist = dist;
        }
    }

    return minDist;
}

// Advanced Terrain Erosion Filter copyright (c) 2025 Rune Skovbo Johansen
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#define TAU 6.28318530717959

glm::vec2 hash(glm::vec2 x)
{
    const glm::vec2 k = glm::vec2(0.3183099, 0.3678794);
    x = x * k + k.yx();
    return -1.0f +
           2.0f * fract(16.0f * k * glm::fract(x.x * x.y * (x.x + x.y)));
}

glm::vec4 PhacelleNoise(glm::vec2 p, glm::vec2 normDir, float freq,
                        float offset, float normalization)
{
    glm::vec2 sideDir =
        normDir.yx() * glm::vec2(-1.0f, 1.0f) * freq * (float)TAU;
    offset *= TAU;

    glm::vec2 pInt = floor(p);
    glm::vec2 pFrac = fract(p);
    glm::vec2 phaseDir = glm::vec2(0.0f);
    float weightSum = 0.0f;
    for (int i = -1; i <= 2; i++)
    {
        for (int j = -1; j <= 2; j++)
        {
            glm::vec2 gridOffset = glm::vec2(i, j);

            glm::vec2 gridPoint = pInt + gridOffset;

            glm::vec2 randomOffset = hash(gridPoint) * 0.5f;

            glm::vec2 vectorFromCellPoint = pFrac - gridOffset - randomOffset;

            float sqrDist = dot(vectorFromCellPoint, vectorFromCellPoint);
            float weight = exp(-sqrDist * 2.0f);
            weight = glm::max(0.0f, weight - 0.01111f);

            weightSum += weight;

            float waveInput = dot(vectorFromCellPoint, sideDir) + offset;

            phaseDir += glm::vec2(cos(waveInput), sin(waveInput)) * weight;
        }
    }

    glm::vec2 interpolated = phaseDir / weightSum;
    float magnitude = sqrt(dot(interpolated, interpolated));
    magnitude = glm::max(1.0f - normalization, magnitude);
    return glm::vec4(interpolated / magnitude, sideDir);
}

#define clamp01(x) glm::clamp(x, 0.0f, 1.0f)

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
        return t - 0.5 * smoothing;
    return 0.5 * t * t / smoothing;
}

glm::vec2 safe_normalize(glm::vec2 n)
{
    float l = length(n);
    return (abs(l) > 1e-10) ? (n / l) : n;
}

glm::vec4 ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget,
                        float strength, float gullyWeight, float detail,
                        glm::vec4 rounding, glm::vec4 onset,
                        glm::vec2 assumedSlope, float scale, int octaves,
                        float lacunarity, float gain, float cellScale,
                        float normalization, float ridgeMap, float debug)
{
    strength *= scale;
    fadeTarget = glm::clamp(fadeTarget, -1.0f, 1.0f);

    glm::vec3 inputHeightAndSlope = heightAndSlope;
    float freq = 1.0f / (scale * cellScale);
    float slopeLength = glm::max(glm::length(heightAndSlope.yz()), 1e-10f);
    float magnitude = 0.0f;
    float roundingMult = 1.0f;

    float roundingForInput =
        glm::mix(rounding.y, rounding.x, clamp01(fadeTarget + 0.5f)) *
        rounding.z;
    float combiMask = ease_out(
        smooth_start(slopeLength * onset.x, roundingForInput * onset.x));

    float ridgeMapCombiMask = ease_out(slopeLength * onset.z);
    float ridgeMapFadeTarget = fadeTarget;

    glm::vec2 gullySlope =
        mix(heightAndSlope.yz(),
            heightAndSlope.yz() / slopeLength * assumedSlope.x, assumedSlope.y);

    for (int i = 0; i < octaves; i++)
    {
        glm::vec4 phacelle = PhacelleNoise(p * freq, safe_normalize(gullySlope),
                                           cellScale, 0.25, normalization);
        phacelle.zw() *= -freq;
        float sloping = abs(phacelle.y);

        gullySlope +=
            glm::sign(phacelle.y) * phacelle.zw() * strength * gullyWeight;

        glm::vec3 gullies = glm::vec3(phacelle.x, phacelle.y * phacelle.zw());
        glm::vec3 fadedGullies = mix(glm::vec3(fadeTarget, 0.0f, 0.0f),
                                     gullies * gullyWeight, combiMask);
        heightAndSlope += fadedGullies * strength;
        magnitude += strength;

        fadeTarget = fadedGullies.x;

        float roundingForOctave =
            glm::mix(rounding.y, rounding.x, clamp01(phacelle.x + 0.5f)) *
            roundingMult;
        float newMask = ease_out(
            smooth_start(sloping * onset.y, roundingForOctave * onset.y));
        combiMask = pow_inv(combiMask, detail) * newMask;

        ridgeMapFadeTarget =
            glm::mix(ridgeMapFadeTarget, gullies.x, ridgeMapCombiMask);
        float newRidgeMapMask = ease_out(sloping * onset.w);
        ridgeMapCombiMask = ridgeMapCombiMask * newRidgeMapMask;

        strength *= gain;
        freq *= lacunarity;
        roundingMult *= rounding.w;
    }

    ridgeMap = ridgeMapFadeTarget * (1.0f - ridgeMapCombiMask);
    debug = fadeTarget;

    glm::vec3 heightAndSlopeDelta = heightAndSlope - inputHeightAndSlope;
    return glm::vec4(heightAndSlopeDelta, magnitude);
}

glm::vec4 ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget,
                        const ErosionParameters& params)
{
    return ErosionFilter(
        p, heightAndSlope, fadeTarget, params.Strength, params.GullyWeight,
        params.Detail, params.Rounding, params.Onset, params.AssumedSlope,
        params.Scale, params.Octaves, params.Lacunarity, params.Gain,
        params.CellScale, params.Normalization, params.RidgeMap, params.Debug);
}

} // namespace noise
