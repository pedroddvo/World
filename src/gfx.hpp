#pragma once
#include <vulkan/vulkan.hpp>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_handles.hpp>

namespace gfx
{

class Backend
{
  public:
    Backend(const Backend&) = delete;
    Backend(Backend&&) = delete;
    Backend& operator=(const Backend&) = delete;
    Backend& operator=(Backend&&) = delete;

    Backend(GLFWwindow* window);
    ~Backend();

    uint32_t FrameBegin();
    void FrameEnd(uint32_t imageIndex);

    void Resize(uint32_t width, uint32_t height);

  private:
    void DestroySwapchain();

    GLFWwindow* m_Window = nullptr;

    vk::Instance m_Instance = {};
    vk::PhysicalDevice m_Gpu = {};
    vk::Device m_Device = {};
    vk::SurfaceKHR m_Surface = {};
    vk::DebugUtilsMessengerEXT m_DbgMsg = {};

    vk::Queue m_GfxQueue = {}, m_XfrQueue = {}, m_PrsQueue = {};
    uint32_t m_GfxIndex = 0, m_XfrIndex = 0, m_PrsIndex = 0;

    vk::SwapchainKHR m_Swapchain = {};
    std::vector<VkImage> m_SwcImages = {};
    std::vector<VkImageView> m_SwcViews = {};
    vk::Extent2D m_SwcExtent = {};

    static constexpr uint32_t FramesInFlight = 2;
    std::array<vk::Semaphore, FramesInFlight> m_PresentCompleteSemas = {};
    std::array<vk::Fence, FramesInFlight> m_WaitFences = {};
    std::vector<vk::Semaphore> m_RenderCompleteSemas = {};

    vk::CommandPool m_FramePool = {};
    std::array<vk::CommandBuffer, FramesInFlight> m_FrameCommands = {};

    uint32_t m_CurrentFrame = 0;
};

} // namespace gfx
