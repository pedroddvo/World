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
    uint32_t hashY =
        (hash * 2654435769U); 

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

} // namespace noise
