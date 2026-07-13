#include "camera.hpp"
#include "gfx.hpp"
#include "gfx/noise.hpp"
#include "glm/fwd.hpp"
#include "imgui.h"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <vulkan/vulkan_enums.hpp>

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods);
static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

struct GenerateNoiseResult
{
    std::vector<glm::vec3> Vertices = {};
    std::vector<glm::vec3> Normals = {};
    std::vector<uint32_t> Indices = {};
    std::vector<uint8_t> Data = {};
};

static GenerateNoiseResult GenerateNoise(noise::NoiseFunction noiseFn,
                                         const noise::NoiseConfig& noiseCfg)
{
    std::vector<glm::vec3> vertices = {}, normals = {};
    std::vector<uint32_t> indices = {};
    std::vector<uint8_t> data = {};

    for (int y = 0; y < 800; y++)
    {
        for (int x = 0; x < 800; x++)
        {
            float v = noise::Noise({x / 800.0f, y / 800.0f}, noiseCfg, noiseFn);

            float vu =
                noise::Noise({x / 800.0f, (y + 1) / 800.0f}, noiseCfg, noiseFn);
            float vd =
                noise::Noise({x / 800.0f, (y - 1) / 800.0f}, noiseCfg, noiseFn);
            float vr =
                noise::Noise({(x + 1) / 800.0f, y / 800.0f}, noiseCfg, noiseFn);
            float vl =
                noise::Noise({(x - 1) / 800.0f, y / 800.0f}, noiseCfg, noiseFn);

            normals.push_back(glm::normalize(glm::vec3{vl - vr, vd - vu, 1.0}));

            uint8_t pixel =
                static_cast<uint8_t>(glm::clamp(v * 255.0f, 0.0f, 255.0f));
            data.insert(data.end(), {pixel, pixel, pixel, 255});

            vertices.push_back({x - 400, (1.0f - v) * 100.0f, y - 400});
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

    return {vertices, normals, indices, data};
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
    gfx::PipelineObj pip = backend.CreatePipeline({
        .VertexShader = "shader/triangle.vert.spv",
        .FragmentShader = "shader/triangle.frag.spv",
        .DepthTest = depthTest,

        .Bindings = {{0, sizeof(float) * 3}},
        .Attributes = {{0, 0, vk::Format::eR32G32B32Sfloat}},
        .Descriptors = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment}},
        .PushConstants = {vk::PushConstantRange{
            vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}},
    });

    noise::NoiseConfig noiseCfg = {};
    GenerateNoiseResult gnr = GenerateNoise(noise::Perlin2D, noiseCfg);

    gfx::ImageObj heightMap = backend.CreateImage(
        vk::Format::eR8G8B8A8Srgb, gnr.Data.size() * sizeof(uint8_t), 800, 800);
    backend.UploadImage(heightMap, gnr.Data.size() * sizeof(uint8_t),
                        gnr.Data.data());

    gfx::ImageObj normalMap =
        backend.CreateImage(vk::Format::eR8G8B8A8Srgb,
                            gnr.Normals.size() * sizeof(uint8_t), 800, 800);
    std::vector<uint8_t> normalImg = {};
    for (glm::vec3 n : gnr.Normals)
    {
        glm::vec<3, uint8_t> c =
            glm::clamp((n + 1.0f) / 2.0f * 255.0f, 0.0f, 255.0f);
        normalImg.insert(normalImg.end(), {c.x, c.y, c.z, 255});
    }
    backend.UploadImage(normalMap, normalImg.size() * sizeof(uint8_t),
                        normalImg.data());

    gfx::BufferObj vertexBuf =
        backend.CreateBuffer(vk::BufferUsageFlagBits::eVertexBuffer,
                             sizeof(glm::vec3) * gnr.Vertices.size());
    gfx::BufferObj indexBuf =
        backend.CreateBuffer(vk::BufferUsageFlagBits::eIndexBuffer,
                             sizeof(uint32_t) * gnr.Indices.size());
    backend.UploadBuffer(indexBuf, sizeof(uint32_t) * gnr.Indices.size(),
                         gnr.Indices.data());
    backend.UploadBuffer(vertexBuf, sizeof(glm::vec3) * gnr.Vertices.size(),
                         gnr.Vertices.data());

    gfx::SamplerObj samp = backend.CreateSampler(vk::Filter::eLinear);
    backend.UpdatePipelineImage(pip, 0, heightMap, samp);

    noise::NoiseFunction noiseFns[] = {noise::Perlin2D, noise::Voronoi2D};
    auto noiseFnStrings = {"Perlin", "Voronoi"};
    int currentNoiseFn = 0;

    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float frame = static_cast<float>(glfwGetTime());
        float dt = frame - lastFrame;
        lastFrame = frame;

        uint32_t fi = backend.FrameBegin();

        ImGui::Begin("Noise Parameters");

        ImGui::Combo("Noise Function", &currentNoiseFn, noiseFnStrings.begin(),
                     noiseFnStrings.size());

        ImGui::SliderFloat("Frequency", &noiseCfg.Frequency, 0.1, 16.0);
        ImGui::SliderFloat("Amplitude", &noiseCfg.Amplitude, 0.1, 2.0);

        uint32_t octMin = 1, octMax = 4;
        ImGui::SliderScalar("Octaves", ImGuiDataType_U32, &noiseCfg.Octaves,
                            &octMin, &octMax);
        ImGui::SliderFloat("Lacunarity", &noiseCfg.Lacunarity, 0.1, 16.0);
        ImGui::SliderFloat("Gain", &noiseCfg.Gain, 0.1, 2.0);

        if (ImGui::Button("Noise"))
        {
            gnr = GenerateNoise(noiseFns[currentNoiseFn], noiseCfg);
            backend.UploadImage(heightMap, gnr.Data.size() * sizeof(uint8_t),
                                gnr.Data.data());
            backend.UploadBuffer(indexBuf,
                                 sizeof(uint32_t) * gnr.Indices.size(),
                                 gnr.Indices.data());
            backend.UploadBuffer(vertexBuf,
                                 sizeof(glm::vec3) * gnr.Vertices.size(),
                                 gnr.Vertices.data());
            std::vector<uint8_t> normalImg = {};
            for (glm::vec3 n : gnr.Normals)
            {
                glm::vec<3, uint8_t> c =
                    glm::clamp((n + 1.0f) / 2.0f * 255.0f, 0.0f, 255.0f);
                normalImg.insert(normalImg.end(), {c.x, c.y, c.z, 255});
            }
            backend.UploadImage(normalMap, normalImg.size() * sizeof(uint8_t),
                                normalImg.data());
        }

        bool depthTestNew = depthTest;
        ImGui::Checkbox("Depth Testing", &depthTestNew);
        if (depthTestNew != depthTest)
        {
            depthTest = depthTestNew;
            backend.Destroy(pip);
            pip = backend.CreatePipeline({
                .VertexShader = "shader/triangle.vert.spv",
                .FragmentShader = "shader/triangle.frag.spv",
                .DepthTest = depthTest,

                .Bindings = {{0, sizeof(float) * 3}},
                .Attributes = {{0, 0, vk::Format::eR32G32B32Sfloat}},
                .Descriptors = {vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eFragment}},
                .PushConstants = {vk::PushConstantRange{
                    vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}},
            });
        }

        ImGui::End();

        ImGui::Begin("Noise");
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        backend.DrawImageImGui(heightMap, availSize.x, availSize.y);
        backend.DrawImageImGui(normalMap, availSize.x, availSize.y);
        ImGui::End();

        g_FlyController.Update(&g_Camera, dt);
        glm::mat4 mvp = g_Camera.ViewProjection(800.0f / 600.0f);

        backend.BindPipeline(pip);
        backend.BindPushConstant(pip, vk::ShaderStageFlagBits::eVertex, &mvp,
                                 sizeof(glm::mat4));
        backend.BindVertexBuffer(vertexBuf);
        backend.BindIndexBuffer(indexBuf, vk::IndexType::eUint32);
        backend.DrawIndexed(gnr.Indices.size(), 1);
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
