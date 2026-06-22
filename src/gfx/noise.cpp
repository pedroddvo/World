#include "noise.hpp"

namespace noise
{

static glm::vec2 GetGradient(glm::ivec2 p)
{
    unsigned int x = (unsigned int)p.x, y = (unsigned int)p.y;
    unsigned int hash = x * 73856093 ^ y * 19349663;
    float angle = hash * 0.001f;
    return glm::vec2(std::cos(angle), std::sin(angle));
}

static float Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float Perlin2D(glm::vec2 v)
{
    glm::vec2 f = glm::floor(v);
    glm::vec2 t = v - f;

    float d00 =
        glm::dot(t - glm::vec2(0, 0), GetGradient(f + glm::vec2(0, 0)));
    float d01 =
        glm::dot(t - glm::vec2(0, 1), GetGradient(f + glm::vec2(0, 1)));
    float d10 =
        glm::dot(t - glm::vec2(1, 0), GetGradient(f + glm::vec2(1, 0)));
    float d11 =
        glm::dot(t - glm::vec2(1, 1), GetGradient(f + glm::vec2(1, 1)));

    float u = Fade(t.x), w = Fade(t.y);
    return glm::mix(glm::mix(d00, d10, u), glm::mix(d01, d11, u), w);
}

} // namespace noise
