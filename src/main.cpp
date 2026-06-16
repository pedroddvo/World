#include "gfx.hpp"
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
    });

    float vertices[] = {0.0f, 0.5f, 0.0f, 1.0f, 
        -0.5f, -0.5f, 0.0f, 1.0f, 
        0.5f, -0.5f, 0.0f,  1.0f};
    gfx::BufferObj buf = backend.CreateBuffer(
        vk::BufferUsageFlagBits::eVertexBuffer, sizeof(vertices));
    backend.UploadBuffer(buf, sizeof(vertices), vertices);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        uint32_t fi = backend.FrameBegin();
        backend.BindPipeline(pip);
        backend.BindVertexBuffer(buf);
        backend.Draw(3, 1);
        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
