#include "render.hpp"
#include "imgui.h"
#include <arm/_mcontext.h>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

static constexpr uint32_t g_IndicesCount = 799 * 799 * 6;
static constexpr std::array g_NoiseFnStrings = {"Perlin", "Voronoi"};

struct alignas(16) ComputeConfig
{
    NoiseType Type = NoiseType::Perlin;
    noise::NoiseConfig noiseCfg;

    float Pad0;
    alignas(16) noise::ErosionParameters erosionParams;
};

struct Vertex
{
    glm::vec3 Position = {};
    float Padding1 = 0.0f;
    glm::vec3 Normal = {};
    float Padding2 = 0.0f;
};

void Renderer::ComputeNoise()
{
    uint32_t fi = m_FrameIndex;
    if (m_FrameIndex == UINT32_MAX)
        fi = m_Backend.FrameBegin();

    m_Backend.FrameBeginCompute(fi);
    m_Backend.BindPipeline(m_ComputePipeline);
    ComputeConfig compCfg = {m_NoiseType, m_NoiseCfg, 0.0, m_ErosionParams};
    m_Backend.BindPushConstant(m_ComputePipeline,
                               vk::ShaderStageFlagBits::eCompute, &compCfg,
                               sizeof(ComputeConfig));
    m_Backend.Dispatch(800.0f / 16.0f, 800.0f / 16.0f, 1.0f);
    m_Backend.FrameEndCompute(fi);

    if (m_FrameIndex == UINT32_MAX)
        m_Backend.FrameEnd(fi);
}

Renderer::Renderer(GLFWwindow* window) : m_Window(window), m_Backend(window)
{
    m_DrawPipeline = m_Backend.CreateGraphicsPipeline({
        .VertexShader = "shader/triangle.vert.spv",
        .FragmentShader = "shader/triangle.frag.spv",
        .DepthTest = true,

        .Bindings = {{0, sizeof(Vertex)}},
        .Attributes = {vk::VertexInputAttributeDescription{
                           0, 0, vk::Format::eR32G32B32A32Sfloat},
                       vk::VertexInputAttributeDescription{
                           1, 0, vk::Format::eR32G32B32A32Sfloat}},
        // .Descriptors = {vk::DescriptorSetLayoutBinding{
        //     0, vk::DescriptorType::eUniformBuffer, 1,
        //     vk::ShaderStageFlagBits::eFragment}},
        .PushConstants = {vk::PushConstantRange{
            vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}},
    });

    m_ComputePipeline = m_Backend.CreateComputePipeline({
        .ComputeShader = "shader/heightmap.comp.spv",
        .Descriptors = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eCompute}},
        .PushConstants = {vk::PushConstantRange{
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(ComputeConfig)}},
    });

    m_VertexBuffer =
        m_Backend.CreateBuffer(vk::BufferUsageFlagBits::eVertexBuffer |
                                   vk::BufferUsageFlagBits::eStorageBuffer,
                               sizeof(Vertex) * 800 * 800);
    m_Backend.UpdatePipelineBuffer(m_ComputePipeline, 0, m_VertexBuffer,
                                   vk::DescriptorType::eStorageBuffer);

    std::vector<uint32_t> indices = {};
    m_IndexBuffer =
        m_Backend.CreateBuffer(vk::BufferUsageFlagBits::eIndexBuffer,
                               sizeof(uint32_t) * g_IndicesCount);
    for (int y = 0; y < 799; y++)
    {
        for (int x = 0; x < 799; x++)
        {
            uint32_t i = y * 800 + x;

            indices.push_back(i);
            indices.push_back(i + 800);
            indices.push_back(i + 1);

            indices.push_back(i + 1);
            indices.push_back(i + 800);
            indices.push_back(i + 801);
        }
    }
    m_Backend.UploadBuffer(m_IndexBuffer, sizeof(uint32_t) * indices.size(),
                           indices.data());

    ComputeNoise();
}

void Renderer::Render(float dt)
{
    m_FrameIndex = m_Backend.FrameBegin();
    if (m_UpdateNoise)
        ComputeNoise();

    m_Backend.FrameBeginRender(m_FrameIndex);
    ImGui::Begin("Noise Parameters");

    m_UpdateNoise |=
        ImGui::Combo("Noise Function", (int*)&m_NoiseType,
                     g_NoiseFnStrings.begin(), g_NoiseFnStrings.size());

    m_UpdateNoise |=
        ImGui::SliderFloat("Frequency", &m_NoiseCfg.Frequency, 0.1, 16.0);
    m_UpdateNoise |=
        ImGui::SliderFloat("Amplitude", &m_NoiseCfg.Amplitude, 0.1, 2.0);

    uint32_t octMin = 1, octMax = 4;
    m_UpdateNoise |= ImGui::SliderScalar("Octaves", ImGuiDataType_U32,
                                         &m_NoiseCfg.Octaves, &octMin, &octMax);
    m_UpdateNoise |=
        ImGui::SliderFloat("Lacunarity", &m_NoiseCfg.Lacunarity, 0.1, 16.0);
    m_UpdateNoise |= ImGui::SliderFloat("Gain", &m_NoiseCfg.Gain, 0.1, 2.0);
    ImGui::End();

    ImGui::Begin("Erosion Filter");
    m_UpdateNoise |=
        ImGui::SliderFloat("Strength", &m_ErosionParams.Strength, 0.0f, 1.0f);
    m_UpdateNoise |= ImGui::SliderFloat(
        "Gully Weight", &m_ErosionParams.GullyWeight, 0.0f, 1.0f);
    m_UpdateNoise |=
        ImGui::SliderFloat("Detail", &m_ErosionParams.Detail, 0.0f, 4.0f);

    ImGui::Separator();

    m_UpdateNoise |= ImGui::SliderFloat4(
        "Rounding (RGBA/XYZW)", &m_ErosionParams.Rounding.x, 0.0f, 2.0f);
    m_UpdateNoise |= ImGui::SliderFloat4("Onset (RGBA/XYZW)",
                                         &m_ErosionParams.Onset.x, 0.0f, 4.0f);
    m_UpdateNoise |= ImGui::SliderFloat2(
        "Assumed Slope (XY)", &m_ErosionParams.AssumedSlope.x, 0.0f, 1.0f);

    ImGui::Separator();

    m_UpdateNoise |=
        ImGui::SliderFloat("Scale", &m_ErosionParams.Scale, 0.0f, 1.0f);

    m_UpdateNoise |=
        ImGui::SliderInt("Octaves", &m_ErosionParams.Octaves, 1, 8);

    m_UpdateNoise |= ImGui::SliderFloat(
        "Lacunarity", &m_ErosionParams.Lacunarity, 0.0f, 4.0f);
    m_UpdateNoise |=
        ImGui::SliderFloat("Gain", &m_ErosionParams.Gain, 0.0f, 2.0f);
    m_UpdateNoise |= ImGui::SliderFloat("Cell Scale",
                                        &m_ErosionParams.CellScale, 0.0f, 1.0f);

    ImGui::Separator();

    m_UpdateNoise |= ImGui::SliderFloat(
        "Normalization", &m_ErosionParams.Normalization, 0.0f, 1.0f);
    m_UpdateNoise |=
        ImGui::SliderFloat("Ridge Map", &m_ErosionParams.RidgeMap, 0.0f, 1.0f);
    m_UpdateNoise |=
        ImGui::SliderFloat("Debug", &m_ErosionParams.Debug, 0.0f, 1.0f);
    ImGui::End();

    ImGui::Begin("Noise");
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    // backend.DrawImageImGui(heightMap, availSize.x, availSize.y);
    // backend.DrawImageImGui(normalMap, availSize.x, availSize.y);
    ImGui::End();

    m_FlyController.Update(&m_Camera, dt);
    glm::mat4 mvp = m_Camera.ViewProjection(800.0f / 600.0f);

    m_Backend.BindPipeline(m_DrawPipeline);
    m_Backend.BindPushConstant(m_DrawPipeline, vk::ShaderStageFlagBits::eVertex,
                               &mvp, sizeof(glm::mat4));
    m_Backend.BindVertexBuffer(m_VertexBuffer);
    m_Backend.BindIndexBuffer(m_IndexBuffer, vk::IndexType::eUint32);
    m_Backend.DrawIndexed(g_IndicesCount, 1);
    m_Backend.FrameEndRender(m_FrameIndex);

    m_Backend.FrameEnd(m_FrameIndex);
}

void Renderer::OnKeyInput(int key, int action)
{
    bool down = action == GLFW_PRESS;

    if (down || action == GLFW_RELEASE)
    {
        switch (key)
        {
        case GLFW_KEY_M:
            if (action == GLFW_RELEASE)
                break;

            glfwSetInputMode(m_Window, GLFW_CURSOR,
                             glfwGetInputMode(m_Window, GLFW_CURSOR) ==
                                     GLFW_CURSOR_DISABLED
                                 ? GLFW_CURSOR_NORMAL
                                 : GLFW_CURSOR_DISABLED);
            break;

            // clang-format off
	case GLFW_KEY_W:	  m_FlyController.Forward	= down; break;
	case GLFW_KEY_S:	  m_FlyController.Backward	= down; break;
	case GLFW_KEY_D:	  m_FlyController.Right		= down; break;
	case GLFW_KEY_A:	  m_FlyController.Left		= down; break;
	case GLFW_KEY_SPACE:	  m_FlyController.Up		= down; break;
	case GLFW_KEY_LEFT_SHIFT: m_FlyController.Down		= down; break;
            // clang-format on
        }
    }
}

void Renderer::OnMouseMove(glm::vec2 delta)
{
    m_FlyController.MoveMouse(&m_Camera, delta);
}
