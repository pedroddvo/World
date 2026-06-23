#include "gfx.hpp"
#include "gfx/noise.hpp"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>

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
        .Descriptors = {vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}},
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

    FILE* fp = fopen("out.ppm", "wb");
    fprintf(fp, "P3\n%d %d\n255\n", 800, 600);
    std::vector<uint8_t> data = {};
    for (int j = 0; j < 600; j++)
    {
        for (int i = 0; i < 800; i++)
        {
            uint8_t v = (uint8_t)(noise::Perlin2D({i / 800.0f * 16.0f,
                                                   j / 600.0f * 16.0f}) *
                                  255);
            fprintf(fp, "%d %d %d ", v, v, v);
            data.insert(data.end(), {v, v, v, 255});
        }
        fprintf(fp, "\n");
    }
    fclose(fp);

    gfx::ImageObj image = backend.CreateImage(
        vk::Format::eR8G8B8A8Srgb, data.size() * sizeof(uint8_t), 800, 600);
    backend.UploadImage(image, data.size() * sizeof(uint8_t), data.data());

    gfx::SamplerObj samp = backend.CreateSampler(vk::Filter::eLinear);
    backend.UpdatePipelineImage(pip, 0, image, samp);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        uint32_t fi = backend.FrameBegin();
        backend.BindPipeline(pip);
        backend.BindVertexBuffer(buf);
        backend.Draw(6, 1);
        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
