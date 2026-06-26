#pragma once
#include <glm/glm.hpp>

namespace noise
{

struct PerlinConfig
{
    uint32_t seed = 0;
};

float Perlin2D(glm::vec2 v, const PerlinConfig& cfg);

}
