#include <vulkan/vulkan_core.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <VkBootstrap.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "GLFW/glfw3.h"
#include "gfx.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE;

namespace gfx
{

template <typename... Args>
static inline void
EnsureVk_(vk::Result result, int line, const char* file, const char* exp,
          spdlog::format_string_t<Args...> msg = "no info provided",
          Args&&... args)
{
    if (result == vk::Result::eSuccess)
        return;

    spdlog::critical("[{}:{}] vulkan error in '{}' ({})", file, line, exp,
                     vk::to_string(result));
    spdlog::critical(msg, std::forward<Args>(args)...);
    abort();
}

template <typename... Args>
static inline void
EnsureVk_(VkResult result, int line, const char* file, const char* exp,
          spdlog::format_string_t<Args...> msg = "no info provided",
          Args&&... args)
{
    EnsureVk_((vk::Result)result, line, file, exp, msg, args...);
}

template <typename T, typename... Args>
static inline T EnsureVk_(
    vk::ResultValue<T> result, int line, const char* file, const char* exp,
    spdlog::format_string_t<Args...> msg = "no info provided", Args&&... args)
{
    EnsureVk_(result.result, line, file, exp, msg, args...);
    return result.value;
}

template <typename T, typename... Args>
static inline T
EnsureVk_(vkb::Result<T> result, int line, const char* file, const char* exp,
          spdlog::format_string_t<Args...> msg = "no info provided",
          Args&&... args)
{
    if (result.has_value())
        return *result;

    spdlog::critical("[{}:{}] vulkan error ({})", file, line,
                     result.error().message());
    spdlog::critical(msg, std::forward<Args>(args)...);
    abort();
}

#define EnsureVk(cond, ...)                                                    \
    EnsureVk_((cond), __LINE__, __FILE__, #cond, ##__VA_ARGS__)

Backend::Backend(GLFWwindow* window) : m_Window(window)
{
    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    uint32_t glfwExtCount = 0;
    const char** glfwExt = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    vkb::Instance vkbInstance =
        EnsureVk(vkb::InstanceBuilder()
                     .request_validation_layers()
                     .enable_extensions(glfwExtCount, glfwExt)
                     .use_default_debug_messenger()
                     .build());
    m_Instance = vkbInstance;
    m_DbgMsg = vkbInstance.debug_messenger;
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Instance);

    EnsureVk(glfwCreateWindowSurface(vkbInstance, window, nullptr,
                                     (VkSurfaceKHR*)&m_Surface),
             "failed to create window surface");

    auto dyren = vk::PhysicalDeviceDynamicRenderingFeatures(true);
    auto sync2 = vk::PhysicalDeviceSynchronization2Features(true);
    auto extensions = {vk::KHRSynchronization2ExtensionName,
                       vk::KHRDynamicRenderingExtensionName,
                       vk::KHRDepthStencilResolveExtensionName,
                       vk::KHRCreateRenderpass2ExtensionName,
                       vk::KHRMultiviewExtensionName,
                       vk::KHRMaintenance2ExtensionName};

    vkb::PhysicalDevice vkbGpu =
        EnsureVk(vkb::PhysicalDeviceSelector(vkbInstance)
                     .set_surface(m_Surface)
                     .add_required_extensions(extensions)
                     .add_required_extension_features(sync2)
                     .add_required_extension_features(dyren)
                     .select());
    m_Gpu = vkbGpu;

    vkb::Device vkbDevice = EnsureVk(vkb::DeviceBuilder(vkbGpu).build());
    m_Device = vkbDevice;
    m_GfxQueue = EnsureVk(vkbDevice.get_queue(vkb::QueueType::graphics));
    m_GfxIndex = EnsureVk(vkbDevice.get_queue_index(vkb::QueueType::graphics));
    m_XfrQueue = EnsureVk(vkbDevice.get_queue(vkb::QueueType::transfer));
    m_XfrIndex = EnsureVk(vkbDevice.get_queue_index(vkb::QueueType::transfer));
    m_PrsQueue = EnsureVk(vkbDevice.get_queue(vkb::QueueType::present));
    m_PrsIndex = EnsureVk(vkbDevice.get_queue_index(vkb::QueueType::present));
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Device);

    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    Resize(width, height);

    m_FramePool = m_Device.createCommandPool(
        vk::CommandPoolCreateInfo()
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
            .setQueueFamilyIndex(m_GfxIndex));

    for (uint32_t i = 0; i < FramesInFlight; i++)
    {
        m_PresentCompleteSemas[i] = m_Device.createSemaphore({});
        m_WaitFences[i] =
            m_Device.createFence({vk::FenceCreateFlagBits::eSignaled});
        m_FrameCommands[i] = m_Device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo()
                .setCommandPool(m_FramePool)
                .setCommandBufferCount(1)
                .setLevel(vk::CommandBufferLevel::ePrimary))[0];
    }
}

uint32_t Backend::FrameBegin()
{
    EnsureVk(m_Device.waitForFences({m_WaitFences[m_CurrentFrame]}, true,
                                    UINT64_MAX));
    m_Device.resetFences({m_WaitFences[m_CurrentFrame]});

    uint32_t imageIndex = EnsureVk(m_Device.acquireNextImageKHR(
        m_Swapchain, UINT64_MAX, m_PresentCompleteSemas[m_CurrentFrame]));

    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo());

    vk::ImageMemoryBarrier barrier =
        vk::ImageMemoryBarrier()
            .setImage(m_SwcImages[imageIndex])
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eAttachmentOptimal)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, {},
                        {}, {}, {barrier});

    vk::RenderingAttachmentInfo colorAttachment =
        vk::RenderingAttachmentInfo()
            .setImageView(m_SwcViews[imageIndex])
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue({{0.0f, 0.0f, 0.0f, 0.0f}});
    cmd.beginRenderingKHR(vk::RenderingInfo()
                              .setRenderArea({{}, m_SwcExtent})
                              .setColorAttachments({colorAttachment})
                              .setLayerCount(1));

    return imageIndex;
}

void Backend::FrameEnd(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

    cmd.endRenderingKHR();

    vk::ImageMemoryBarrier barrier =
        vk::ImageMemoryBarrier()
            .setImage(m_SwcImages[imageIndex])
            .setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
            .setDstAccessMask({})
            .setOldLayout(vk::ImageLayout::eAttachmentOptimal)
            .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eNone, {}, {}, {},
                        {barrier});

    cmd.end();

    vk::PipelineStageFlags waitStageMask =
        vk::PipelineStageFlagBits::eColorAttachmentOutput;
    vk::SubmitInfo submit =
        vk::SubmitInfo()
            .setWaitSemaphores({m_PresentCompleteSemas[m_CurrentFrame]})
            .setWaitDstStageMask({waitStageMask})
            .setCommandBuffers({cmd})
            .setSignalSemaphores({m_RenderCompleteSemas[imageIndex]});
    m_GfxQueue.submit({submit}, m_WaitFences[m_CurrentFrame]);

    EnsureVk(m_PrsQueue.presentKHR(
        vk::PresentInfoKHR()
            .setWaitSemaphores({m_RenderCompleteSemas[imageIndex]})
            .setSwapchains(m_Swapchain)
            .setImageIndices({imageIndex})));

    m_CurrentFrame = (m_CurrentFrame + 1) % FramesInFlight;
}

void Backend::DestroySwapchain()
{
    for (vk::ImageView view : m_SwcViews)
        m_Device.destroyImageView(view);
    for (vk::Semaphore sema : m_RenderCompleteSemas)
        m_Device.destroySemaphore(sema);
    m_Device.destroySwapchainKHR(m_Swapchain);

    m_RenderCompleteSemas.clear();
}

void Backend::Resize(uint32_t width, uint32_t height)
{
    m_SwcExtent = vk::Extent2D{width, height};

    vkb::Swapchain swc =
        EnsureVk(vkb::SwapchainBuilder(m_Gpu, m_Device, m_Surface, m_GfxIndex,
                                       m_PrsIndex)
                     .set_old_swapchain(m_Swapchain)
                     .set_desired_extent(width, height)
                     .build());
    if (m_Swapchain != nullptr)
        DestroySwapchain();
    m_Swapchain = swc;

    m_SwcImages = EnsureVk(swc.get_images());
    m_SwcViews = EnsureVk(swc.get_image_views());
    m_RenderCompleteSemas.resize(m_SwcImages.size());
    for (vk::Semaphore& sema : m_RenderCompleteSemas)
        sema = m_Device.createSemaphore({});
}

Backend::~Backend()
{
    m_Device.waitIdle();

    DestroySwapchain();

    m_Device.destroyCommandPool(m_FramePool);
    for (uint32_t i = 0; i < FramesInFlight; i++)
    {
        m_Device.destroySemaphore(m_PresentCompleteSemas[i]);
        m_Device.destroyFence(m_WaitFences[i]);
    }

    m_Device.destroy();
    m_Instance.destroySurfaceKHR(m_Surface);
    m_Instance.destroyDebugUtilsMessengerEXT(m_DbgMsg);
    m_Instance.destroy();
}

} // namespace gfx
