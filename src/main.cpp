#include "camera.hpp"
#include "gfx.hpp"
#include "gfx/noise.hpp"
#include "imgui.h"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <vulkan/vulkan_enums.hpp>

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods);

static std::vector<uint8_t> GenerateNoise(const noise::PerlinConfig& noiseCfg)
{

    std::vector<uint8_t> data = {};

    for (int j = 0; j < 600; j++)
    {
        for (int i = 0; i < 800; i++)
        {
            float v = noise::Perlin2D({i / 800.0f, j / 600.0f}, noiseCfg);
            v = (v + 0.7071f) / 1.4142f;

            uint8_t pixel =
                static_cast<uint8_t>(glm::clamp(v * 255.0f, 0.0f, 255.0f));
            data.insert(data.end(), {pixel, pixel, pixel, 255});
        }
    }

    return data;
}

Camera g_Camera = {{0.0f, 0.0f, 2.0f}};
FlyController g_FlyController = {};

int main()
{
    Ensure(glfwInit(), "failed to initialize glfw");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "vox", nullptr, nullptr);
    Ensure(window != nullptr, "failed to create window");
    glfwSetKeyCallback(window, KeyCallback);

    gfx::Backend backend = gfx::Backend(window);
    gfx::PipelineObj pip = backend.CreatePipeline({
        .VertexShader = "shader/triangle.vert.spv",
        .FragmentShader = "shader/triangle.frag.spv",

        .Bindings = {{0, sizeof(float) * 4}},
        .Attributes = {{0, 0, vk::Format::eR32G32B32A32Sfloat}},
        .Descriptors = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment}},
        .PushConstants = {vk::PushConstantRange{
            vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4)}},
    });

    float vertices[] = {
        // Triangle 1
        // X,     Y,     U,     V
        -1.0f, -1.0f, 0.0f, 0.0f, // Top-Left
        -1.0f, 1.0f, 0.0f, 1.0f,  // Bottom-Left
        1.0f, 1.0f, 1.0f, 1.0f,   // Bottom-Right

        // Triangle 2
        -1.0f, -1.0f, 0.0f, 0.0f, // Top-Left
        1.0f, 1.0f, 1.0f, 1.0f,   // Bottom-Right
        1.0f, -1.0f, 1.0f, 0.0f   // Top-Right
    };

    gfx::BufferObj buf = backend.CreateBuffer(
        vk::BufferUsageFlagBits::eVertexBuffer, sizeof(vertices));
    backend.UploadBuffer(buf, sizeof(vertices), vertices);

    noise::PerlinConfig noiseCfg = {};
    std::vector<uint8_t> data = GenerateNoise(noiseCfg);

    gfx::ImageObj image = backend.CreateImage(
        vk::Format::eR8G8B8A8Srgb, data.size() * sizeof(uint8_t), 800, 600);
    backend.UploadImage(image, data.size() * sizeof(uint8_t), data.data());

    gfx::SamplerObj samp = backend.CreateSampler(vk::Filter::eLinear);
    backend.UpdatePipelineImage(pip, 0, image, samp);

    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float frame = static_cast<float>(glfwGetTime());
        float dt = frame - lastFrame;
        lastFrame = frame;

        uint32_t fi = backend.FrameBegin();

        ImGui::Begin("Noise Parameters");

        ImGui::Text("%f", dt);
        ImGui::SliderFloat("Frequency", &noiseCfg.Frequency, 0.1, 16.0);
        ImGui::SliderFloat("Amplitude", &noiseCfg.Amplitude, 0.1, 2.0);

        uint32_t octMin = 1, octMax = 4;
        ImGui::SliderScalar("Octaves", ImGuiDataType_U32, &noiseCfg.Octaves,
                            &octMin, &octMax);
        ImGui::SliderFloat("Lacunarity", &noiseCfg.Lacunarity, 0.1, 16.0);
        ImGui::SliderFloat("Gain", &noiseCfg.Gain, 0.1, 2.0);

        if (ImGui::Button("Noise"))
        {
            data = GenerateNoise(noiseCfg);
            backend.UploadImage(image, data.size() * sizeof(uint8_t),
                                data.data());
        }

        ImGui::End();

        ImGui::Begin("Noise");
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        backend.DrawImageImGui(image, availSize.x, availSize.y);
        ImGui::End();

	g_FlyController.Update(&g_Camera, dt);
        glm::mat4 mvp = g_Camera.ViewProjection(800.0f / 600.0f);

        backend.BindPipeline(pip);
        backend.BindPushConstant(pip, vk::ShaderStageFlagBits::eVertex, &mvp,
                                 sizeof(glm::mat4));
        backend.BindVertexBuffer(buf);
        backend.Draw(6, 1);
        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods)
{
    bool down = action == GLFW_PRESS;

    switch (key)
    { // clang-format off
    case GLFW_KEY_W:		g_FlyController.Forward		= down; break;
    case GLFW_KEY_S:		g_FlyController.Backward	= down; break;
    case GLFW_KEY_D:		g_FlyController.Right		= down; break;
    case GLFW_KEY_A:		g_FlyController.Left		= down; break;
    case GLFW_KEY_SPACE:	g_FlyController.Up		= down; break;
    case GLFW_KEY_LEFT_SHIFT:	g_FlyController.Down		= down; break;
							     
    } // clang-format on
}
