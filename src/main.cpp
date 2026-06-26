#include "gfx.hpp"
#include "gfx/noise.hpp"
#include "imgui.h"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>

static std::vector<uint8_t> GenerateNoise(const noise::PerlinConfig& noiseCfg)
{

    std::vector<uint8_t> data = {};
    
    for (int j = 0; j < 600; j++)
    {
        for (int i = 0; i < 800; i++)
        {
            float v = noise::Perlin2D({i / 800.0f * 16.0f, j / 600.0f * 16.0f},
                                      noiseCfg);
            v = (v + 0.7071f) / 1.4142f;

            uint8_t pixel = static_cast<uint8_t>(v * 255.0f);
            data.insert(data.end(), {pixel, pixel, pixel, 255});
        }
    }

    return data;
}

int main()
{
    Ensure(glfwInit(), "failed to initialize glfw");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "vox", nullptr, nullptr);
    Ensure(window != nullptr, "failed to create window");

    gfx::Backend backend = gfx::Backend(window);
    gfx::PipelineObj pip = backend.CreatePipeline({
        .VertexShader = "shader/triangle.vert.spv",
        .FragmentShader = "shader/triangle.frag.spv",

        .Bindings = {{0, sizeof(float) * 4}},
        .Attributes = {{0, 0, vk::Format::eR32G32B32A32Sfloat}},
        .Descriptors = {vk::DescriptorSetLayoutBinding{
            0, vk::DescriptorType::eCombinedImageSampler, 1,
            vk::ShaderStageFlagBits::eFragment}},
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

        ImGui::Text("%f", dt);
        if (ImGui::Button("Noise"))
        {
            noiseCfg.seed += 1;
	    data = GenerateNoise(noiseCfg);
            backend.UploadImage(image, data.size() * sizeof(uint8_t),
                                data.data());
        }

        backend.BindPipeline(pip);
        backend.BindVertexBuffer(buf);
        backend.Draw(6, 1);
        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
