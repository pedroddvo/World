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
    });

    float vertices[] = {0.0f, 0.5f, 0.0f, 1.0f,  -0.5f, -0.5f,
                        0.0f, 1.0f, 0.5f, -0.5f, 0.0f,  1.0f};
    gfx::BufferObj buf = backend.CreateBuffer(
        vk::BufferUsageFlagBits::eVertexBuffer, sizeof(vertices));
    backend.UploadBuffer(buf, sizeof(vertices), vertices);

    FILE* fp = fopen("out.ppm", "wb");
    fprintf(fp, "P3\n%d %d\n255\n", 800, 600);
    for (int j = 0; j < 600; j++)
    {
        for (int i = 0; i < 800; i++)
        {
	    int v = (int)(noise::Perlin2D({i/800.0f*16.0f, j/600.0f*16.0f})*255);
	    fprintf(fp, "%d %d %d ", v, v, v);
        }
	fprintf(fp, "\n");
    }
    fclose(fp);

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
