#include "glm/fwd.hpp"
#include "render.hpp"
#include "util.hpp"
#include <GLFW/glfw3.h>
#include <stdio.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods);
static void MouseCallback(GLFWwindow* window, double xpos, double ypos);

Renderer* g_Renderer = nullptr;

int main()
{
    Ensure(glfwInit(), "failed to initialize glfw");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(800, 600, "vox", nullptr, nullptr);
    Ensure(window != nullptr, "failed to create window");
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    g_Renderer = new Renderer(window);

    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        float frame = static_cast<float>(glfwGetTime());
        float dt = frame - lastFrame;
        lastFrame = frame;

        g_Renderer->Render(dt);
    }

    delete g_Renderer;
    glfwDestroyWindow(window);
    glfwTerminate();
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action,
                        int mods)
{
    g_Renderer->OnKeyInput(key, action);
}

glm::vec2 g_LastMouse = {400.0f, 300.0f};
static void MouseCallback(GLFWwindow* window, double xpos, double ypos)
{
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL)
        return;

    glm::vec2 delta = {xpos - g_LastMouse.x, g_LastMouse.y - ypos};
    g_LastMouse = {xpos, ypos};
    g_Renderer->OnMouseMove(delta);
}

// struct GenerateNoiseResult
// {
//     std::vector<Vertex> Vertices = {};
//     std::vector<uint32_t> Indices = {};
//     std::vector<uint8_t> Data = {};
// };
//
// static GenerateNoiseResult
// GenerateNoise(noise::NoiseFunction noiseFn, const noise::NoiseConfig&
// noiseCfg,
//               const noise::ErosionParameters& erosionParameters)
// {
//     std::vector<Vertex> vertices = {};
//     std::vector<uint32_t> indices = {};
//     std::vector<uint8_t> data = {};
//
//     for (int y = 0; y < 800; y++)
//     {
//         for (int x = 0; x < 800; x++)
//         {
//             glm::vec2 uv = {x / 800.0f, y / 800.0f};
//             float v = noise::Noise({x / 800.0f, y / 800.0f}, noiseCfg,
//             noiseFn);
//
//             float vu =
//                 noise::Noise({x / 800.0f, (y + 1) / 800.0f}, noiseCfg,
//                 noiseFn);
//             float vd =
//                 noise::Noise({x / 800.0f, (y - 1) / 800.0f}, noiseCfg,
//                 noiseFn);
//             float vr =
//                 noise::Noise({(x + 1) / 800.0f, y / 800.0f}, noiseCfg,
//                 noiseFn);
//             float vl =
//                 noise::Noise({(x - 1) / 800.0f, y / 800.0f}, noiseCfg,
//                 noiseFn);
//             glm::vec2 n = normalize(glm::vec2(vd - vu, vl - vr));
//
//             glm::vec4 h = noise::ErosionFilter(uv, glm::vec3(v, n), 1.0f,
//                                                erosionParameters);
//             v = v + h.x;
//
//             uint8_t pixel =
//                 static_cast<uint8_t>(glm::clamp(v * 255.0f, 0.0f, 255.0f));
//             data.insert(data.end(), {pixel, pixel, pixel, 255});
//
//             vertices.push_back(Vertex{
//                 .Position = {x - 400, (1.0f - v) * 100.0f, y - 400},
//                 .Normal = glm::normalize(glm::vec3(n, 1.0f)),
//             });
//         }
//     }
//
//     for (int y = 0; y < 799; y++)
//     {
//         for (int x = 0; x < 799; x++)
//         {
//             uint32_t i = y * 800 + x;
//
//             indices.push_back(i);
//             indices.push_back(i + 800);
//             indices.push_back(i + 1);
//
//             indices.push_back(i + 1);
//             indices.push_back(i + 800);
//             indices.push_back(i + 801);
//         }
//     }
//
//     return {vertices, indices, data};
// }
