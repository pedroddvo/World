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

struct ErosionParameters
{
    float Strength = 0.22f, GullyWeight = 0.5f, Detail = 1.5f;
    glm::vec4 Rounding = glm::vec4(0.1f, 0.0f, 0.1f, 2.0f);
    glm::vec4 Onset = glm::vec4(1.25f, 1.25f, 2.8f, 1.5f);
    glm::vec2 AssumedSlope = glm::vec2(0.7f, 1.0f);
    float Scale = 0.15f;
    int Octaves = 4;
    float Lacunarity = 2.0f, Gain = 0.5f, CellScale = 0.7f;
    float Normalization = 0.5f, RidgeMap = 0.5f, Debug = 0.0f;
};

using NoiseFunction = float (*)(glm::vec2, const NoiseConfig&);

float Perlin2D(glm::vec2 v, const NoiseConfig& cfg);
float Voronoi2D(glm::vec2 v, const NoiseConfig& cfg);

float Noise(glm::vec2 v, const NoiseConfig& cfg, NoiseFunction noiseFn);
glm::vec4 ErosionFilter(glm::vec2 p, glm::vec3 heightAndSlope, float fadeTarget,
                        const ErosionParameters& params);

} // namespace noise
