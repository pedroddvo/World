#pragma once
#include "camera.hpp"
#include "gfx.hpp"
#include "gfx/noise.hpp"

enum class NoiseType : uint32_t
{
    Perlin = 0,
    Voronoi,
};

class Renderer
{
  public:
    Renderer(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    Renderer(GLFWwindow* window);

    void Render(float dt);
    void OnKeyInput(int key, int action);
    void OnMouseMove(glm::vec2 delta);

  private:
    void ComputeNoise();

    uint32_t m_FrameIndex = UINT32_MAX;
    gfx::Backend m_Backend;

    gfx::PipelineObj m_DrawPipeline, m_ComputePipeline;
    gfx::BufferObj m_VertexBuffer, m_IndexBuffer;

    gfx::ImageObj m_HeightMap;
    gfx::SamplerObj m_HeightMapSamp;

    NoiseType m_NoiseType = NoiseType::Perlin;
    noise::ErosionParameters m_ErosionParams = {};
    noise::NoiseConfig m_NoiseCfg = {};
    bool m_UpdateNoise = false;

    Camera m_Camera = {{0.0f, 0.0f, 2.0f}};
    FlyController m_FlyController = {.Speed = 100.0f};
    GLFWwindow* m_Window = {};
};
