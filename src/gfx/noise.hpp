#pragma once
#include <glm/glm.hpp>

namespace noise
{

struct PerlinConfig
{
    uint32_t Seed = 0;
    uint32_t Octaves = 1;

    float Lacunarity = 2.0f, Gain = 0.5f;
    float Amplitude = 0.5f, Frequency = 1.0f;
};

float Perlin2D(glm::vec2 v, const PerlinConfig& cfg);

} // namespace noise
