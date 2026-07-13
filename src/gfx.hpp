#pragma once
#include <functional>
#include <initializer_list>
#include <vulkan/vulkan.hpp>

#include <GLFW/glfw3.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

namespace gfx
{

enum class ObjectKind
{
    Buffer,
    Pipeline,
    Image,
    Sampler
};

struct Object
{

  private:
    Object(uint32_t id, ObjectKind kind) : Id(id), Kind(kind) {}

    uint32_t Id;
    ObjectKind Kind;

    friend class Backend;
};

using BufferObj = Object;
using PipelineObj = Object;
using ImageObj = Object;
using SamplerObj = Object;

struct CreatePipelineInfo
{
    const char* VertexShader = nullptr;
    const char* FragmentShader = nullptr;
    bool DepthTest = false;

    std::initializer_list<vk::VertexInputBindingDescription> Bindings = {};
    std::initializer_list<vk::VertexInputAttributeDescription> Attributes = {};

    std::initializer_list<vk::DescriptorSetLayoutBinding> Descriptors = {};
    std::initializer_list<vk::PushConstantRange> PushConstants = {};
};

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

    void BindVertexBuffer(BufferObj buf);
    void BindIndexBuffer(BufferObj buf, vk::IndexType indexType);
    void BindPipeline(PipelineObj pip);
    void BindPushConstant(PipelineObj pip, vk::ShaderStageFlags stageFlags,
                          void* data, size_t size);

    void Draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0);

    void Resize(uint32_t width, uint32_t height);
    void Destroy(Object obj);

    BufferObj CreateBuffer(vk::BufferUsageFlags usage, size_t size);
    void UploadBuffer(BufferObj obj, size_t size, void* data);

    ImageObj CreateImage(vk::Format format, size_t size, uint32_t width,
                         uint32_t height, uint32_t depth = 1);
    void UploadImage(ImageObj obj, size_t size, void* data);
    void DrawImageImGui(ImageObj obj, uint32_t width = 0, uint32_t height = 0);

    SamplerObj CreateSampler(vk::Filter filter);

    PipelineObj CreatePipeline(const CreatePipelineInfo& info);
    void UpdatePipelineImage(PipelineObj pip, uint32_t binding, ImageObj img,
                             SamplerObj samp);
    void UpdatePipelineBuffer(
        PipelineObj pip, uint32_t binding, BufferObj buf,
        vk::DescriptorType descriptorType = vk::DescriptorType::eUniformBuffer);

  private:
    void DestroySwapchain();
    void InitImgui();

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
    vk::Format m_SwcImageFormat = {};

    static constexpr uint32_t FramesInFlight = 2;
    std::array<vk::Semaphore, FramesInFlight> m_PresentCompleteSemas = {};
    std::array<vk::Fence, FramesInFlight> m_WaitFences = {};
    std::vector<vk::Semaphore> m_RenderCompleteSemas = {};

    vk::CommandPool m_FramePool = {};
    std::array<vk::CommandBuffer, FramesInFlight> m_FrameCommands = {};
    uint32_t m_CurrentFrame = 0;

    vk::CommandPool m_TransferPool = {};
    vk::CommandBuffer m_TransferCommand = {};
    vk::Fence m_TransferFence = {};

    VmaAllocator m_Allocator = {};

    VkDescriptorPool m_DescriptorPool = {};
    VkDescriptorPool m_ImguiPool = {};

    struct Buffer
    {
        bool Alive = false;
        VkBuffer Handle = {};
        VmaAllocation Allocation = {};
        VmaAllocationInfo AllocationInfo = {};
        size_t Size = 0;
    };
    std::vector<Buffer> m_Buffers = {};
    void DestroyBuffer(Buffer& buf);

    struct Image
    {
        bool Alive = false;
        VkImage Handle = {};
        VkImageView View = {};
        VkExtent3D Extent = {};
        VmaAllocation Allocation = {};
        VkDescriptorSet ImGuiDescriptor = {};
        size_t Size = 0;
    };
    std::vector<Image> m_Images = {};

    Image CreateDepthImage(uint32_t width, uint32_t height);
    Image m_DepthImage = {};

    void DestroyImage(Image& img);

    struct Pipeline
    {
        bool Alive = false;
        VkPipeline Handle = {};
        VkPipelineLayout Layout = {};
        VkDescriptorSet Descriptor = {};
        VkDescriptorSetLayout DescriptorLayout = {};
    };
    std::vector<Pipeline> m_Pipelines = {};
    void DestroyPipeline(Pipeline& pip);

    struct Sampler
    {
        bool Alive = false;
        VkSampler Handle = {};
    };
    std::vector<Sampler> m_Samplers = {};
    void DestroySampler(Sampler& samp);

    void PerformImmediateTransfer(std::function<void(vk::CommandBuffer)> proc);
};

} // namespace gfx
