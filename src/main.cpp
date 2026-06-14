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

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        uint32_t fi = backend.FrameBegin();
        backend.FrameEnd(fi);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
}
