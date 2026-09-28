#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <vector>

class VulkanContext;
class Window;
class Assets;
class Scene;

// Owns rendering resources; its referenced components must outlive it.
class Renderer {
public:
    static constexpr std::uint32_t maxFramesInFlight = 2;

    Renderer(VulkanContext& context, const Window& window, const Assets& assets);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void drawFrame(const Scene& scene);
    void requestResize();

private:
    struct FrameResources {
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore imageAcquired{VK_NULL_HANDLE};
        VkBuffer shaderDataBuffer{VK_NULL_HANDLE};
        VmaAllocation shaderDataAllocation{VK_NULL_HANDLE};
        VmaAllocationInfo shaderDataAllocationInfo{};
        VkDeviceAddress shaderDataAddress{0};
    };

    void createSwapchain();
    void createDepthResources();
    void createFrameResources();
    void createDescriptors();
    void createPipeline();
    // Returns false while the framebuffer has zero extent (e.g. minimized).
    bool recreateSwapchain();
    void updateShaderData(const Scene& scene);
    void recordCommands();
    void submitAndPresent();
    void destroySwapchainResources();
    void cleanup();

    VulkanContext& context_;
    const Window& window_;
    const Assets& assets_;

    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    VkExtent2D extent_{};
    VkFormat colorFormat_{VK_FORMAT_B8G8R8A8_SRGB};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    // Presentation completion semaphores belong to swapchain images, not frames.
    std::vector<VkSemaphore> renderCompleteSemaphores_;
    VkImage depthImage_{VK_NULL_HANDLE};
    VmaAllocation depthAllocation_{VK_NULL_HANDLE};
    VkImageView depthImageView_{VK_NULL_HANDLE};

    VkCommandPool commandPool_{VK_NULL_HANDLE};
    std::array<FrameResources, maxFramesInFlight> frames_{};
    std::uint32_t frameIndex_{0};
    std::uint32_t imageIndex_{0};
    bool resizeRequested_{false};

    VkDescriptorPool descriptorPool_{VK_NULL_HANDLE};
    VkDescriptorSetLayout textureSetLayout_{VK_NULL_HANDLE};
    VkDescriptorSet textureSet_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
};
