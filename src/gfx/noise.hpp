#pragma once
#include <glm/glm.hpp>

namespace noise
{

struct NoiseConfig
{
    uint32_t Seed = 0;
    uint32_t Octaves = 1;

    float Lacunarity = 2.0f, Gain = 0.5f;
    float Amplitude = 0.5f, Frequency = 1.0f;
};

using NoiseFunction = float(*)(glm::vec2, const NoiseConfig&);

float Perlin2D(glm::vec2 v, const NoiseConfig& cfg);
float Voronoi2D(glm::vec2 v, const NoiseConfig& cfg);

float Noise(glm::vec2 v, const NoiseConfig& cfg, NoiseFunction noiseFn);
// float Voronoi2D(glm::vec2 v);

} // namespace noise
