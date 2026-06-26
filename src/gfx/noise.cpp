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

static float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float NoisePerlin2D(glm::vec2 v, const PerlinConfig& cfg)
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

float Perlin2D(glm::vec2 v, const PerlinConfig& cfg)
{
    float y = 0.0f;
    float freq = cfg.Frequency, amp = cfg.Amplitude;

    for (int i = 0; i < cfg.Octaves; i++)
    {
        y += amp * NoisePerlin2D(freq * v, cfg);
        freq *= cfg.Lacunarity;
        amp *= cfg.Gain;
    }

    return y;
}

} // namespace noise
