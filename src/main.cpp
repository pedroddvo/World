#include "camera.hpp"
#include "gfx.hpp"
#include "gfx/noise.hpp"
#include "glm/fwd.hpp"
#include "imgui.h"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods);
static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

enum NoiseType : int
{
    NOISE_PERLIN = 0,
    NOISE_VORONOI,
};

struct NoiseConfig
{
    NoiseType Type = NOISE_PERLIN;
    uint32_t Seed = 0;
    uint32_t Octaves = 1;

    float Lacunarity = 2.0f, Gain = 0.5f;
    float Amplitude = 0.5f, Frequency = 1.0f;
};

struct Vertex
{
    glm::vec3 Position = {};
    float Padding1 = 0.0f;
    glm::vec3 Normal = {};
    float Padding2 = 0.0f;
};

struct GenerateNoiseResult
{
    std::vector<Vertex> Vertices = {};
    std::vector<uint32_t> Indices = {};
    std::vector<uint8_t> Data = {};
};

static GenerateNoiseResult
GenerateNoise(noise::NoiseFunction noiseFn, const noise::NoiseConfig& noiseCfg,
              const noise::ErosionParameters& erosionParameters)
{
    std::vector<Vertex> vertices = {};
    std::vector<uint32_t> indices = {};
    std::vector<uint8_t> data = {};

    for (int y = 0; y < 800; y++)
    {
        for (int x = 0; x < 800; x++)
        {
            glm::vec2 uv = {x / 800.0f, y / 800.0f};
            float v = noise::Noise({x / 800.0f, y / 800.0f}, noiseCfg, noiseFn);

            float vu =
                noise::Noise({x / 800.0f, (y + 1) / 800.0f}, noiseCfg, noiseFn);
            float vd =
                noise::Noise({x / 800.0f, (y - 1) / 800.0f}, noiseCfg, noiseFn);
            float vr =
                noise::Noise({(x + 1) / 800.0f, y / 800.0f}, noiseCfg, noiseFn);
            float vl =
                noise::Noise({(x - 1) / 800.0f, y / 800.0f}, noiseCfg, noiseFn);
            glm::vec2 n = normalize(glm::vec2(vd - vu, vl - vr));

            // glm::vec4 h = noise::ErosionFilter(uv, glm::vec3(v, n), 1.0f,
            //                                    erosionParameters);
            // v = v + h.x;

            uint8_t pixel =
                static_cast<uint8_t>(glm::clamp(v * 255.0f, 0.0f, 255.0f));
            data.insert(data.end(), {pixel, pixel, pixel, 255});

            vertices.push_back(Vertex{
                .Position = {x - 400, (1.0f - v) * 100.0f, y - 400},
                .Normal = glm::normalize(glm::vec3(n, 1.0f)),
            });
        }
    }

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

    return {vertices, indices, data};
}

Camera g_Camera = {{0.0f, 0.0f, 2.0f}};
FlyController g_FlyController = {.Speed = 100.0f};

int main()
{
    Ensure(glfwInit(), "failed to initialize glfw");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "vox", nullptr, nullptr);
    Ensure(window != nullptr, "failed to create window");
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    bool depthTest = true;
    gfx::Backend backend = gfx::Backend(window);

    gfx::CreateGraphicsPipelineInfo drawPipInfo = {
        .VertexShader = "shader/triangle.vert.spv",
        .FragmentShader = "shader/triangle.frag.spv",
        .DepthTest = depthTest,

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
    };
    gfx::PipelineObj drawPip = backend.CreateGraphicsPipeline(drawPipInfo);

    gfx::CreateComputePipelineInfo computePipInfo = {
        .ComputeShader = "shader/heightmap.comp.spv",
        .Descriptors = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eStorageBuffer, 1,
            vk::ShaderStageFlagBits::eCompute}},
        .PushConstants = {{vk::ShaderStageFlagBits::eCompute, 0,
                           sizeof(NoiseConfig)}},
    };
    gfx::PipelineObj computePip = backend.CreateComputePipeline(computePipInfo);

    auto noiseFnStrings = {"Perlin", "Voronoi"};

    noise::ErosionParameters erosionParams = {};
    NoiseConfig noiseCfg = {};
    // GenerateNoiseResult gnr =
    //     GenerateNoise(noiseFns[currentNoiseFn], noiseCfg, erosionParams);

    // gfx::ImageObj heightMap = backend.CreateImage(
    //     vk::Format::eR8G8B8A8Srgb, gnr.Data.size() * sizeof(uint8_t), 800,
    //     800);
    //
    // gfx::ImageObj normalMap =
    //     backend.CreateImage(vk::Format::eR8G8B8A8Srgb,
    //                         gnr.Vertices.size() * sizeof(uint8_t), 800, 800);
    gfx::BufferObj vertexBuf =
        backend.CreateBuffer(vk::BufferUsageFlagBits::eVertexBuffer |
                                 vk::BufferUsageFlagBits::eStorageBuffer,
                             sizeof(Vertex) * 800 * 800);
    uint32_t indicesCount = 799 * 799 * 6;
    gfx::BufferObj indexBuf = backend.CreateBuffer(
        vk::BufferUsageFlagBits::eIndexBuffer, sizeof(uint32_t) * indicesCount);
    backend.UpdatePipelineBuffer(computePip, 0, vertexBuf,
                                 vk::DescriptorType::eStorageBuffer);

    std::vector<uint32_t> indices = {};
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
    backend.UploadBuffer(indexBuf, sizeof(uint32_t) * indices.size(),
                         indices.data());

    // auto CreateNoise = [&]()
    // {
    //     gnr = GenerateNoise(noiseFns[currentNoiseFn], noiseCfg,
    //     erosionParams); backend.UploadImage(heightMap, gnr.Data.size() *
    //     sizeof(uint8_t),
    //                         gnr.Data.data());
    //     backend.UploadBuffer(indexBuf, sizeof(uint32_t) * gnr.Indices.size(),
    //                          gnr.Indices.data());
    //     // backend.UploadBuffer(vertexBuf, sizeof(Vertex) *
    //     gnr.Vertices.size(),
    //     //                      gnr.Vertices.data());
    //     std::vector<uint8_t> normalImg = {};
    //     for (Vertex v : gnr.Vertices)
    //     {
    //         auto n = v.Normal;
    //         glm::vec<3, uint8_t> c =
    //             glm::clamp((n + 1.0f) / 2.0f * 255.0f, 0.0f, 255.0f);
    //         normalImg.insert(normalImg.end(), {c.x, c.y, c.z, 255});
    //     }
    //     backend.UploadImage(normalMap, normalImg.size() * sizeof(uint8_t),
    //                         normalImg.data());
    // };
    // CreateNoise();

    uint32_t cfi = backend.FrameBegin();
    backend.BindPipeline(computePip);
    backend.BindPushConstant(computePip, vk::ShaderStageFlagBits::eCompute,
                             &noiseCfg, sizeof(NoiseConfig));
    backend.Dispatch(800.0f / 16.0f, 800.0f / 16.0f, 1.0f);
    backend.FrameEnd(cfi);

    bool updateNoise = false;
    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float frame = static_cast<float>(glfwGetTime());
        float dt = frame - lastFrame;
        lastFrame = frame;

        uint32_t fi = backend.FrameBegin();

        if (updateNoise)
        {
            backend.FrameBeginCompute(fi);
            backend.BindPipeline(computePip);
            backend.BindPushConstant(computePip,
                                     vk::ShaderStageFlagBits::eCompute,
                                     &noiseCfg, sizeof(NoiseConfig));
            backend.Dispatch(800.0f / 16.0f, 800.0f / 16.0f, 1.0f);
            backend.FrameEndCompute(fi);
            updateNoise = false;
        }

        backend.FrameBeginRender(fi);
        ImGui::Begin("Noise Parameters");

        updateNoise |=
            ImGui::Combo("Noise Function", (int*)&noiseCfg.Type,
                         noiseFnStrings.begin(), noiseFnStrings.size());

        updateNoise |=
            ImGui::SliderFloat("Frequency", &noiseCfg.Frequency, 0.1, 16.0);
        updateNoise |=
            ImGui::SliderFloat("Amplitude", &noiseCfg.Amplitude, 0.1, 2.0);

        uint32_t octMin = 1, octMax = 4;
        updateNoise |= ImGui::SliderScalar("Octaves", ImGuiDataType_U32,
                                           &noiseCfg.Octaves, &octMin, &octMax);
        updateNoise |=
            ImGui::SliderFloat("Lacunarity", &noiseCfg.Lacunarity, 0.1, 16.0);
        updateNoise |= ImGui::SliderFloat("Gain", &noiseCfg.Gain, 0.1, 2.0);

        bool depthTestNew = depthTest;
        ImGui::Checkbox("Depth Testing", &depthTestNew);
        if (depthTestNew != depthTest)
        {
            depthTest = depthTestNew;
            backend.Destroy(drawPip);
            drawPip = backend.CreateGraphicsPipeline(drawPipInfo);
        }
        ImGui::End();

        ImGui::Begin("Erosion Filter");
        ImGui::SliderFloat("Strength", &erosionParams.Strength, 0.0f, 1.0f);
        ImGui::SliderFloat("Gully Weight", &erosionParams.GullyWeight, 0.0f,
                           1.0f);
        ImGui::SliderFloat("Detail", &erosionParams.Detail, 0.0f, 4.0f);

        ImGui::Separator();

        ImGui::SliderFloat4("Rounding (RGBA/XYZW)", &erosionParams.Rounding.x,
                            0.0f, 2.0f);
        ImGui::SliderFloat4("Onset (RGBA/XYZW)", &erosionParams.Onset.x, 0.0f,
                            4.0f);
        ImGui::SliderFloat2("Assumed Slope (XY)", &erosionParams.AssumedSlope.x,
                            0.0f, 1.0f);

        ImGui::Separator();

        ImGui::SliderFloat("Scale", &erosionParams.Scale, 0.0f, 1.0f);

        ImGui::SliderInt("Octaves", &erosionParams.Octaves, 1, 8);

        ImGui::SliderFloat("Lacunarity", &erosionParams.Lacunarity, 0.0f, 4.0f);
        ImGui::SliderFloat("Gain", &erosionParams.Gain, 0.0f, 2.0f);
        ImGui::SliderFloat("Cell Scale", &erosionParams.CellScale, 0.0f, 1.0f);

        ImGui::Separator();

        ImGui::SliderFloat("Normalization", &erosionParams.Normalization, 0.0f,
                           1.0f);
        ImGui::SliderFloat("Ridge Map", &erosionParams.RidgeMap, 0.0f, 1.0f);
        ImGui::SliderFloat("Debug", &erosionParams.Debug, 0.0f, 1.0f);
        ImGui::End();

        ImGui::Begin("Noise");
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        // backend.DrawImageImGui(heightMap, availSize.x, availSize.y);
        // backend.DrawImageImGui(normalMap, availSize.x, availSize.y);
        ImGui::End();

        g_FlyController.Update(&g_Camera, dt);
        glm::mat4 mvp = g_Camera.ViewProjection(800.0f / 600.0f);

        backend.BindPipeline(drawPip);
        backend.BindPushConstant(drawPip, vk::ShaderStageFlagBits::eVertex,
                                 &mvp, sizeof(glm::mat4));
        backend.BindVertexBuffer(vertexBuf);
        backend.BindIndexBuffer(indexBuf, vk::IndexType::eUint32);
        backend.DrawIndexed(indicesCount, 1);
        backend.FrameEndRender(fi);

        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods)
{
    bool down = action == GLFW_PRESS;

    if (down || action == GLFW_RELEASE)
    {
        switch (key)
        {
        case GLFW_KEY_M:
            if (action == GLFW_RELEASE)
                break;

            glfwSetInputMode(window, GLFW_CURSOR,
                             glfwGetInputMode(window, GLFW_CURSOR) ==
                                     GLFW_CURSOR_DISABLED
                                 ? GLFW_CURSOR_NORMAL
                                 : GLFW_CURSOR_DISABLED);
            break;

            // clang-format off
	case GLFW_KEY_W:	  g_FlyController.Forward	= down; break;
	case GLFW_KEY_S:	  g_FlyController.Backward	= down; break;
	case GLFW_KEY_D:	  g_FlyController.Right		= down; break;
	case GLFW_KEY_A:	  g_FlyController.Left		= down; break;
	case GLFW_KEY_SPACE:	  g_FlyController.Up		= down; break;
	case GLFW_KEY_LEFT_SHIFT: g_FlyController.Down		= down; break;
            // clang-format on
        }
    }
}

glm::vec2 g_LastMouse = {400.0f, 300.0f};
static void MouseCallback(GLFWwindow* window, double xpos, double ypos)
{
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
        return;

    glm::vec2 delta = {xpos - g_LastMouse.x, g_LastMouse.y - ypos};
    g_LastMouse = {xpos, ypos};
    g_FlyController.MoveMouse(&g_Camera, delta);
}
