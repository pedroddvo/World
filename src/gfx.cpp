#include "util.hpp"
#include <vector>
#include <vulkan/vulkan_core.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

#include <VkBootstrap.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_structs.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

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
                     .require_api_version(1, 2)
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

    VmaAllocatorCreateInfo vmaCreateInfo = {};
    vmaCreateInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    vmaCreateInfo.physicalDevice = m_Gpu;
    vmaCreateInfo.device = m_Device;
    vmaCreateInfo.instance = m_Instance;
    EnsureVk(vmaCreateAllocator(&vmaCreateInfo, &m_Allocator));

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

    m_TransferFence = m_Device.createFence({});
    m_TransferPool = m_Device.createCommandPool(
        vk::CommandPoolCreateInfo().setQueueFamilyIndex(m_XfrIndex));
    m_TransferCommand = m_Device.allocateCommandBuffers(
        vk::CommandBufferAllocateInfo()
            .setCommandPool(m_TransferPool)
            .setCommandBufferCount(1)
            .setLevel(vk::CommandBufferLevel::ePrimary))[0];

    vk::DescriptorPoolSize poolSizes[] = {
        {vk::DescriptorType::eUniformBuffer, 10},
        {vk::DescriptorType::eStorageBuffer, 10},
        {vk::DescriptorType::eStorageImage, 10},
        {vk::DescriptorType::eSampledImage, 10},
        {vk::DescriptorType::eSampler, 10},
    };
    m_DescriptorPool = m_Device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo().setMaxSets(10).setPoolSizes(poolSizes));

    InitImgui();
}

void Backend::InitImgui()
{
    vk::DescriptorPoolSize poolSizes[] = {
        {vk::DescriptorType::eSampler, 1000},
        {vk::DescriptorType::eCombinedImageSampler, 1000},
        {vk::DescriptorType::eSampledImage, 1000},
        {vk::DescriptorType::eStorageImage, 1000},
        {vk::DescriptorType::eUniformTexelBuffer, 1000},
        {vk::DescriptorType::eStorageTexelBuffer, 1000},
        {vk::DescriptorType::eUniformBuffer, 1000},
        {vk::DescriptorType::eStorageBuffer, 1000},
        {vk::DescriptorType::eUniformBufferDynamic, 1000},
        {vk::DescriptorType::eStorageBufferDynamic, 1000},
        {vk::DescriptorType::eInputAttachment, 1000}};
    m_ImguiPool = m_Device.createDescriptorPool(
        vk::DescriptorPoolCreateInfo()
            .setMaxSets(1000)
            .setPoolSizes(poolSizes)
            .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForVulkan(m_Window, true);
    ImGui_ImplVulkan_InitInfo imguiInfo = {};
    imguiInfo.Instance = m_Instance;
    imguiInfo.PhysicalDevice = m_Gpu;
    imguiInfo.Device = m_Device;
    imguiInfo.QueueFamily = m_GfxIndex;
    imguiInfo.Queue = m_GfxQueue;
    imguiInfo.DescriptorPool = m_ImguiPool;
    imguiInfo.MinImageCount = 2;
    imguiInfo.ImageCount = 2;
    imguiInfo.CheckVkResultFn = [](VkResult r) { EnsureVk(r); };
    imguiInfo.UseDynamicRendering = true;

    imguiInfo.PipelineInfoMain.PipelineRenderingCreateInfo =
        (VkPipelineRenderingCreateInfo)vk::PipelineRenderingCreateInfo()
            .setColorAttachmentFormats({m_SwcImageFormat})
            .setDepthAttachmentFormat(vk::Format::eD32Sfloat);
    ImGui_ImplVulkan_Init(&imguiInfo);
}

void Backend::BindPushConstant(PipelineObj pip, vk::ShaderStageFlags stageFlags,
                               void* data, size_t size)
{
    m_FrameCommands[m_CurrentFrame].pushConstants(m_Pipelines[pip.Id].Layout,
                                                  stageFlags, 0, size, data);
}

void Backend::BindIndexBuffer(BufferObj buf, vk::IndexType indexType)
{
    m_FrameCommands[m_CurrentFrame].bindIndexBuffer(m_Buffers[buf.Id].Handle, 0,
                                                    indexType);
}

void Backend::BindVertexBuffer(BufferObj buf)
{
    m_FrameCommands[m_CurrentFrame].bindVertexBuffers(
        0, {m_Buffers[buf.Id].Handle}, {0});
}

void Backend::BindPipeline(PipelineObj pipObj)
{
    auto pip = m_Pipelines[pipObj.Id];

    vk::PipelineBindPoint bindPoint = {};
    switch (pip.Kind)
    {
    case PipelineKind::Compute:
        bindPoint = vk::PipelineBindPoint::eCompute;
        break;
    case PipelineKind::Graphics:
        bindPoint = vk::PipelineBindPoint::eGraphics;
        break;
    }

    m_FrameCommands[m_CurrentFrame].bindPipeline(bindPoint, pip.Handle);

    if (pip.Descriptor != nullptr)
    {
        m_FrameCommands[m_CurrentFrame].bindDescriptorSets(
            bindPoint, pip.Layout, 0, {pip.Descriptor}, {});
    }
}

void Backend::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                          uint32_t firstIndex, int32_t vertexOffset,
                          uint32_t firstInstance)
{
    m_FrameCommands[m_CurrentFrame].drawIndexed(
        indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void Backend::Draw(uint32_t vertexCount, uint32_t instanceCount,
                   uint32_t firstVertex, uint32_t firstInstance)
{
    m_FrameCommands[m_CurrentFrame].draw(vertexCount, instanceCount,
                                         firstVertex, firstInstance);
}

void Backend::Dispatch(uint32_t groupCountX, uint32_t groupCountY,
                       uint32_t groupCountZ)
{
    m_FrameCommands[m_CurrentFrame].dispatch(groupCountX, groupCountY,
                                             groupCountZ);
}

uint32_t Backend::FrameBegin()
{
    EnsureVk(m_Device.waitForFences({m_WaitFences[m_CurrentFrame]}, true,
                                    UINT64_MAX));
    m_Device.resetFences({m_WaitFences[m_CurrentFrame]});

    if (m_PendingResize)
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);
        Resize(width, height);
    }

    vk::ResultValue<uint32_t> imageIndexResult = m_Device.acquireNextImageKHR(
        m_Swapchain, UINT64_MAX, m_PresentCompleteSemas[m_CurrentFrame]);
    if (imageIndexResult.result == vk::Result::eSuboptimalKHR)
        m_PendingResize = true;
    else
        EnsureVk(imageIndexResult.result);

    uint32_t imageIndex = imageIndexResult.value;

    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo());

    vk::ImageMemoryBarrier barrier =
        vk::ImageMemoryBarrier()
            .setImage(m_SwcImages[imageIndex])
            .setSrcAccessMask({})
            .setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                        vk::PipelineStageFlagBits::eColorAttachmentOutput, {},
                        {}, {}, {barrier});

    barrier =
        vk::ImageMemoryBarrier()
            .setImage(m_DepthImage.Handle)
            .setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
            .setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
            .setOldLayout(vk::ImageLayout::eUndefined)
            .setNewLayout(vk::ImageLayout::eDepthAttachmentOptimal)
            .setSubresourceRange({vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits::eLateFragmentTests,
                        vk::PipelineStageFlagBits::eEarlyFragmentTests |
                            vk::PipelineStageFlagBits::eLateFragmentTests,
                        {}, {}, {}, {barrier});

    return imageIndex;
}

void Backend::FrameBeginRender(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    vk::RenderingAttachmentInfo depthAttachment =
        vk::RenderingAttachmentInfo()
            .setImageView(m_DepthImage.View)
            .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
            .setLoadOp(vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearDepthStencilValue(1.0f, 0));
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
                              .setPDepthAttachment(&depthAttachment)
                              .setLayerCount(1));

    cmd.setScissor(0, {vk::Rect2D({}, m_SwcExtent)});
    cmd.setViewport(0,
                    {vk::Viewport(0.0f, m_SwcExtent.height, m_SwcExtent.width,
                                  -(float)m_SwcExtent.height, 0.0f, 1.0f)});
}

void Backend::FrameBeginCompute(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

    std::vector<vk::ImageMemoryBarrier> imgBarriers = {};
    for (ImageObj img : m_ImagesUsedInCompute)
    {
        imgBarriers.push_back(
            vk::ImageMemoryBarrier()
                .setImage(m_Images[img.Id].Handle)
                .setOldLayout(vk::ImageLayout::eUndefined)
                .setNewLayout(vk::ImageLayout::eGeneral)
                .setDstAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setSubresourceRange(
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }

    m_FrameCommands[m_CurrentFrame].pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, imgBarriers);
}

void Backend::FrameEndCompute(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

    std::vector<vk::BufferMemoryBarrier> bufBarriers = {};
    for (BufferObj buf : m_BuffersUsedInCompute)
    {
        bufBarriers.push_back(
            vk::BufferMemoryBarrier()
                .setBuffer(m_Buffers[buf.Id].Handle)
                .setSize(vk::WholeSize)
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eVertexAttributeRead));
    }

    std::vector<vk::ImageMemoryBarrier> imgBarriers = {};
    for (ImageObj img : m_ImagesUsedInCompute)
    {
        imgBarriers.push_back(
            vk::ImageMemoryBarrier()
                .setImage(m_Images[img.Id].Handle)
                .setOldLayout(vk::ImageLayout::eGeneral)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead)
                .setSubresourceRange(
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
    }

    m_FrameCommands[m_CurrentFrame].pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eVertexShader |
            vk::PipelineStageFlagBits::eFragmentShader,
        {}, {}, bufBarriers, imgBarriers);
}

void Backend::FrameEndRender(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    cmd.endRenderingKHR();
}

void Backend::FrameEnd(uint32_t imageIndex)
{
    vk::CommandBuffer cmd = m_FrameCommands[m_CurrentFrame];

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

    vk::Result presentResult = m_PrsQueue.presentKHR(
        vk::PresentInfoKHR()
            .setWaitSemaphores({m_RenderCompleteSemas[imageIndex]})
            .setSwapchains(m_Swapchain)
            .setImageIndices({imageIndex}));
    if (presentResult == vk::Result::eSuboptimalKHR)
        m_PendingResize = true;
    else
        EnsureVk(presentResult);

    m_CurrentFrame = (m_CurrentFrame + 1) % FramesInFlight;
}

void Backend::UpdatePipelineSampler(PipelineObj pipObj, uint32_t binding,
                                    SamplerObj sampObj)
{
    Ensure(pipObj.Kind == ObjectKind::Pipeline);
    Ensure(sampObj.Kind == ObjectKind::Sampler);

    Pipeline& pip = m_Pipelines[pipObj.Id];
    Ensure(pip.Alive);

    vk::WriteDescriptorSet write = {};
    write.dstBinding = binding;
    write.dstSet = pip.Descriptor;
    write.descriptorType = vk::DescriptorType::eSampler;

    Sampler& samp = m_Samplers[sampObj.Id];
    Ensure(samp.Alive);

    vk::DescriptorImageInfo imageInfo =
        vk::DescriptorImageInfo().setSampler(samp.Handle);
    write.setImageInfo({imageInfo});

    m_Device.updateDescriptorSets({write}, {});
}

void Backend::UpdatePipelineImage(PipelineObj pipObj, uint32_t binding,
                                  ImageObj imgObj,
                                  vk::DescriptorType descriptorType)
{
    Ensure(pipObj.Kind == ObjectKind::Pipeline);
    Ensure(imgObj.Kind == ObjectKind::Image);

    Pipeline& pip = m_Pipelines[pipObj.Id];
    Ensure(pip.Alive);

    vk::WriteDescriptorSet write = {};
    write.dstBinding = binding;
    write.dstSet = pip.Descriptor;
    write.descriptorType = descriptorType;

    Image& img = m_Images[imgObj.Id];
    Ensure(img.Alive);

    vk::DescriptorImageInfo imageInfo =
        vk::DescriptorImageInfo().setImageView(img.View).setImageLayout(
            descriptorType == vk::DescriptorType::eStorageImage
                ? vk::ImageLayout::eGeneral
                : vk::ImageLayout::eShaderReadOnlyOptimal);
    write.setImageInfo({imageInfo});

    if (pip.Kind == PipelineKind::Compute)
    {
        m_ImagesUsedInCompute.push_back(imgObj);
    }

    m_Device.updateDescriptorSets({write}, {});
}

void Backend::UpdatePipelineBuffer(PipelineObj pipObj, uint32_t binding,
                                   BufferObj bufObj,
                                   vk::DescriptorType descriptorType)
{
    Ensure(pipObj.Kind == ObjectKind::Pipeline);
    Ensure(bufObj.Kind == ObjectKind::Buffer);

    Pipeline& pip = m_Pipelines[pipObj.Id];
    Ensure(pip.Alive);

    vk::WriteDescriptorSet write = {};
    write.dstBinding = binding;
    write.dstSet = pip.Descriptor;
    write.descriptorType = descriptorType;

    Buffer& buf = m_Buffers[bufObj.Id];
    Ensure(buf.Alive);

    vk::DescriptorBufferInfo bufferInfo = vk::DescriptorBufferInfo()
                                              .setBuffer(buf.Handle)
                                              .setRange(vk::WholeSize);
    write.setBufferInfo(bufferInfo);

    if (pip.Kind == PipelineKind::Compute)
    {
        m_BuffersUsedInCompute.push_back(bufObj);
    }

    m_Device.updateDescriptorSets({write}, {});
}

SamplerObj Backend::CreateSampler(vk::Filter filter)
{
    vk::SamplerCreateInfo samplerInfo = {};
    samplerInfo.magFilter = filter;
    samplerInfo.minFilter = filter;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

    Sampler samp = {.Alive = true};
    samp.Handle = m_Device.createSampler(samplerInfo);

    m_Samplers.push_back(samp);
    return Object(m_Samplers.size() - 1, ObjectKind::Sampler);
}

ImageObj Backend::CreateImage(vk::Format format, size_t size, uint32_t width,
                              uint32_t height, vk::ImageUsageFlags usage,
                              uint32_t depth)
{
    vk::ImageCreateInfo imgInfo = {};
    imgInfo.imageType = vk::ImageType::e2D;
    imgInfo.format = format;
    imgInfo.extent = vk::Extent3D{width, height, depth};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = vk::SampleCountFlagBits::e1;
    imgInfo.tiling = vk::ImageTiling::eOptimal;
    imgInfo.usage = usage | vk::ImageUsageFlagBits::eSampled;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Image img = {.Alive = true, .Extent = imgInfo.extent, .Size = size};
    EnsureVk(vmaCreateImage(m_Allocator, (VkImageCreateInfo*)&imgInfo,
                            &allocInfo, &img.Handle, &img.Allocation, nullptr));

    vk::ImageViewCreateInfo viewInfo = {};
    viewInfo.image = img.Handle;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    img.View = m_Device.createImageView(viewInfo);

    m_Images.push_back(img);
    return Object(m_Images.size() - 1, ObjectKind::Image);
}

void Backend::DrawImageImGui(ImageObj obj, uint32_t width, uint32_t height)
{
    Image& img = m_Images[obj.Id];

    if (img.ImGuiDescriptor == nullptr)
    {
        img.ImGuiDescriptor = ImGui_ImplVulkan_AddTexture(
            img.View, (VkImageLayout)vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    ImGui::Image((ImTextureID)img.ImGuiDescriptor,
                 (width && height)
                     ? ImVec2(width, height)
                     : ImVec2(img.Extent.width, img.Extent.height));
}

void Backend::UploadImage(ImageObj obj, size_t size, void* data)
{
    Ensure(obj.Kind == ObjectKind::Image);

    Image& img = m_Images[obj.Id];
    Ensure(img.Alive);

    Buffer staging = {.Alive = true};
    vk::BufferCreateInfo createInfo = {};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    EnsureVk(vmaCreateBuffer(m_Allocator, (VkBufferCreateInfo*)&createInfo,
                             &allocInfo, &staging.Handle, &staging.Allocation,
                             &staging.AllocationInfo));

    memcpy(staging.AllocationInfo.pMappedData, data, size);
    PerformImmediateTransfer(
        [&](vk::CommandBuffer cmd)
        {
            vk::ImageMemoryBarrier barrier =
                vk::ImageMemoryBarrier()
                    .setImage(img.Handle)
                    .setSrcAccessMask({})
                    .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setOldLayout(vk::ImageLayout::eUndefined)
                    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setSubresourceRange(
                        {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                vk::PipelineStageFlagBits::eTransfer, {}, {},
                                {}, {barrier});

            vk::BufferImageCopy region = {};
            region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0,
                                       1};
            region.imageExtent = img.Extent;
            cmd.copyBufferToImage(staging.Handle, img.Handle,
                                  vk::ImageLayout::eTransferDstOptimal,
                                  {region});

            vk::ImageMemoryBarrier barrierReadable =
                barrier.setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                    .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                    .setDstAccessMask(vk::AccessFlagBits::eShaderRead);
            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eFragmentShader, {},
                                {}, {}, {barrier});
        });

    DestroyBuffer(staging);
}

BufferObj Backend::CreateBuffer(vk::BufferUsageFlags usage, size_t size)
{
    vk::BufferCreateInfo createInfo = {};
    createInfo.size = size;
    createInfo.usage = usage & vk::BufferUsageFlagBits::eStorageBuffer
                           ? usage
                           : usage | vk::BufferUsageFlagBits::eTransferDst;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Buffer buf = {.Alive = true, .Size = size};
    EnsureVk(vmaCreateBuffer(m_Allocator, (VkBufferCreateInfo*)&createInfo,
                             &allocInfo, &buf.Handle, &buf.Allocation,
                             &buf.AllocationInfo));

    m_Buffers.push_back(buf);
    return Object(m_Buffers.size() - 1, ObjectKind::Buffer);
}

void Backend::UploadBuffer(BufferObj obj, size_t size, void* data)
{
    Ensure(obj.Kind == ObjectKind::Buffer);

    Buffer& buf = m_Buffers[obj.Id];
    Ensure(buf.Alive);

    Buffer staging = {.Alive = true};
    vk::BufferCreateInfo createInfo = {};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    EnsureVk(vmaCreateBuffer(m_Allocator, (VkBufferCreateInfo*)&createInfo,
                             &allocInfo, &staging.Handle, &staging.Allocation,
                             &staging.AllocationInfo));

    memcpy(staging.AllocationInfo.pMappedData, data, size);
    PerformImmediateTransfer(
        [&](vk::CommandBuffer cmd) {
            cmd.copyBuffer(staging.Handle, buf.Handle,
                           {vk::BufferCopy(0, 0, size)});
        });

    DestroyBuffer(staging);
}

static vk::ShaderModule CreateShaderModule(vk::Device device,
                                           const char* filename)
{
    FILE* file = fopen(filename, "rb");
    Ensure(file != nullptr, "failed to read shader source {}", filename);

    fseek(file, 0, SEEK_END);
    long srcSz = ftell(file);
    rewind(file);

    char* src = new char[srcSz];
    fread(src, sizeof(char), srcSz, file);

    fclose(file);

    vk::ShaderModule module =
        device.createShaderModule(vk::ShaderModuleCreateInfo(
            {}, srcSz, reinterpret_cast<uint32_t*>(src)));
    delete[] src;

    return module;
}

PipelineObj
Backend::CreateComputePipeline(const CreateComputePipelineInfo& info)
{
    Ensure(info.ComputeShader != nullptr);

    vk::ShaderModule mod = CreateShaderModule(m_Device, info.ComputeShader);
    vk::PipelineShaderStageCreateInfo stage = vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eCompute, mod, "main");

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setPushConstantRanges(info.PushConstants);

    vk::DescriptorSetLayout descriptorLayout = nullptr;
    vk::DescriptorSet descriptor = nullptr;
    if (info.Descriptors.size() > 0)
    {
        descriptorLayout = m_Device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo().setBindings(info.Descriptors));

        descriptor = m_Device.allocateDescriptorSets(
            vk::DescriptorSetAllocateInfo()
                .setDescriptorPool(m_DescriptorPool)
                .setSetLayouts({descriptorLayout}))[0];
        pipelineLayoutInfo.setSetLayouts({descriptorLayout});
    }

    vk::PipelineLayout layout =
        m_Device.createPipelineLayout(pipelineLayoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.layout = layout;
    pipelineInfo.stage = stage;

    vk::Pipeline pip =
        EnsureVk(m_Device.createComputePipeline(nullptr, pipelineInfo));

    m_Device.destroyShaderModule(mod);

    m_Pipelines.push_back({
        .Alive = true,
        .Handle = pip,
        .Layout = layout,
        .Descriptor = descriptor,
        .DescriptorLayout = descriptorLayout,
        .Kind = PipelineKind::Compute,
    });
    return Object(m_Pipelines.size() - 1, ObjectKind::Pipeline);
}

PipelineObj
Backend::CreateGraphicsPipeline(const CreateGraphicsPipelineInfo& info)
{
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {};

    Ensure(info.VertexShader != nullptr);
    vk::ShaderModule vsMod = CreateShaderModule(m_Device, info.VertexShader);
    shaderStages.push_back(vk::PipelineShaderStageCreateInfo(
        {}, vk::ShaderStageFlagBits::eVertex, vsMod, "main"));

    vk::ShaderModule fsMod = nullptr;
    if (info.FragmentShader != nullptr)
    {
        fsMod = CreateShaderModule(m_Device, info.FragmentShader);
        shaderStages.push_back(vk::PipelineShaderStageCreateInfo(
            {}, vk::ShaderStageFlagBits::eFragment, fsMod, "main"));
    }

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.setVertexBindingDescriptions(info.Bindings);
    vertexInputInfo.setVertexAttributeDescriptions(info.Attributes);

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

    vk::PipelineViewportStateCreateInfo viewportState = {};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.lineWidth = 1.0f;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;

    vk::PipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.setAttachments({colorBlendAttachment});

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.setPushConstantRanges(info.PushConstants);

    vk::DescriptorSetLayout descriptorLayout = nullptr;
    vk::DescriptorSet descriptor = nullptr;
    if (info.Descriptors.size() > 0)
    {
        descriptorLayout = m_Device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo().setBindings(info.Descriptors));

        descriptor = m_Device.allocateDescriptorSets(
            vk::DescriptorSetAllocateInfo()
                .setDescriptorPool(m_DescriptorPool)
                .setSetLayouts({descriptorLayout}))[0];
        pipelineLayoutInfo.setSetLayouts({descriptorLayout});
    }

    vk::PipelineLayout layout =
        m_Device.createPipelineLayout(pipelineLayoutInfo);

    vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {};
    pipelineRenderingInfo.setColorAttachmentFormats({m_SwcImageFormat});
    pipelineRenderingInfo.depthAttachmentFormat = vk::Format::eD32Sfloat;

    vk::PipelineDynamicStateCreateInfo dynamicState = {};
    auto states = {vk::DynamicState::eScissor, vk::DynamicState::eViewport};
    dynamicState.dynamicStateCount = states.size();
    dynamicState.pDynamicStates = states.begin();

    vk::PipelineDepthStencilStateCreateInfo depthStencilState = {};
    if (info.DepthTest)
    {
        depthStencilState.depthTestEnable = vk::True,
        depthStencilState.depthWriteEnable = vk::True,
        depthStencilState.depthCompareOp = vk::CompareOp::eLessOrEqual,
        depthStencilState.depthBoundsTestEnable = vk::False,
        depthStencilState.stencilTestEnable = vk::False;
    }

    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.pNext = &pipelineRenderingInfo;
    pipelineInfo.setStages(shaderStages);
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.pDepthStencilState = &depthStencilState;
    pipelineInfo.layout = layout;

    vk::Pipeline pip =
        EnsureVk(m_Device.createGraphicsPipeline(nullptr, pipelineInfo));

    m_Device.destroyShaderModule(vsMod);
    m_Device.destroyShaderModule(fsMod);

    m_Pipelines.push_back({
        .Alive = true,
        .Handle = pip,
        .Layout = layout,
        .Descriptor = descriptor,
        .DescriptorLayout = descriptorLayout,
        .Kind = PipelineKind::Graphics,
    });
    return Object(m_Pipelines.size() - 1, ObjectKind::Pipeline);
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
    m_Device.waitIdle();
    m_SwcExtent = vk::Extent2D{width, height};

    vkb::Swapchain swc =
        EnsureVk(vkb::SwapchainBuilder(m_Gpu, m_Device, m_Surface, m_GfxIndex,
                                       m_PrsIndex)
                     .set_old_swapchain(m_Swapchain)
                     .set_desired_extent(width, height)
                     .build());
    m_SwcImageFormat = (vk::Format)swc.image_format;
    if (m_Swapchain != nullptr)
        DestroySwapchain();
    m_Swapchain = swc;

    m_SwcImages = EnsureVk(swc.get_images());
    m_SwcViews = EnsureVk(swc.get_image_views());
    m_RenderCompleteSemas.resize(m_SwcImages.size());
    for (vk::Semaphore& sema : m_RenderCompleteSemas)
        sema = m_Device.createSemaphore({});

    if (m_DepthImage.Handle != nullptr)
        DestroyImage(m_DepthImage);
    m_DepthImage = CreateDepthImage(width, height);

    m_PendingResize = false;
}

void Backend::Destroy(Object obj)
{
    switch (obj.Kind)
    {
    case ObjectKind::Buffer:
        DestroyBuffer(m_Buffers[obj.Id]);
        break;
    case ObjectKind::Pipeline:
        DestroyPipeline(m_Pipelines[obj.Id]);
        break;
    case ObjectKind::Image:
        DestroyImage(m_Images[obj.Id]);
        break;
    case ObjectKind::Sampler:
        DestroySampler(m_Samplers[obj.Id]);
        break;
    case ObjectKind::Undefined:
        Ensure(false && "unreachable");
        break;
    }
}

void Backend::DestroyPipeline(Pipeline& pip)
{
    if (pip.Alive)
    {
        m_Device.destroyPipeline(pip.Handle);
        m_Device.destroyPipelineLayout(pip.Layout);
        m_Device.destroyDescriptorSetLayout(pip.DescriptorLayout);
        pip.Alive = false;
    }
}

void Backend::DestroyBuffer(Buffer& buf)
{
    if (buf.Alive)
    {
        vmaDestroyBuffer(m_Allocator, buf.Handle, buf.Allocation);
        buf.Alive = false;
    }
}

void Backend::DestroySampler(Sampler& samp)
{
    if (samp.Alive)
    {
        m_Device.destroySampler(samp.Handle);
        samp.Alive = false;
    }
}

void Backend::DestroyImage(Image& img)
{
    if (img.Alive)
    {
        m_Device.destroyImageView(img.View);
        vmaDestroyImage(m_Allocator, img.Handle, img.Allocation);
        if (img.ImGuiDescriptor)
            ImGui_ImplVulkan_RemoveTexture(img.ImGuiDescriptor);
        img.Alive = false;
    }
}

Backend::~Backend()
{
    m_Device.waitIdle();

    m_Device.destroyCommandPool(m_TransferPool);
    m_Device.destroyFence(m_TransferFence);

    for (Pipeline& pip : m_Pipelines)
        DestroyPipeline(pip);
    for (Buffer& buf : m_Buffers)
        DestroyBuffer(buf);
    for (Image& buf : m_Images)
        DestroyImage(buf);
    for (Sampler& buf : m_Samplers)
        DestroySampler(buf);

    DestroyImage(m_DepthImage);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    m_Device.destroyDescriptorPool(m_DescriptorPool);
    m_Device.destroyDescriptorPool(m_ImguiPool);

    vmaDestroyAllocator(m_Allocator);
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

void Backend::PerformImmediateTransfer(
    std::function<void(vk::CommandBuffer)> proc)
{
    m_Device.resetCommandPool(m_TransferPool);
    m_TransferCommand.begin(vk::CommandBufferBeginInfo());
    proc(m_TransferCommand);
    m_TransferCommand.end();

    m_XfrQueue.submit(vk::SubmitInfo().setCommandBuffers({m_TransferCommand}),
                      m_TransferFence);
    EnsureVk(m_Device.waitForFences({m_TransferFence}, true, UINT64_MAX));

    m_Device.resetFences(m_TransferFence);
}

Backend::Image Backend::CreateDepthImage(uint32_t width, uint32_t height)
{
    vk::ImageCreateInfo imgInfo = {};
    imgInfo.imageType = vk::ImageType::e2D;
    imgInfo.format = vk::Format::eD32Sfloat;
    imgInfo.extent = vk::Extent3D{width, height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = vk::SampleCountFlagBits::e1;
    imgInfo.tiling = vk::ImageTiling::eOptimal;
    imgInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    Image img = {.Alive = true,
                 .Extent = imgInfo.extent,
                 .Size = width * height * sizeof(uint32_t)};
    EnsureVk(vmaCreateImage(m_Allocator, (VkImageCreateInfo*)&imgInfo,
                            &allocInfo, &img.Handle, &img.Allocation, nullptr));

    vk::ImageViewCreateInfo viewInfo = {};
    viewInfo.image = img.Handle;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = imgInfo.format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
    img.View = m_Device.createImageView(viewInfo);

    return img;
}

} // namespace gfx
