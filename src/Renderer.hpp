#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <vector>
#include <span>

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

    // Borrowed resources for main.cpp until drawing is moved into Renderer.
    VkSwapchainKHR swapchain() const;
    VkExtent2D extent() const;
    VkFormat colorFormat() const;
    VkFormat depthFormat() const;
    VkImage depthImage() const;
    VkImageView depthImageView() const;
    VkDescriptorSetLayout textureSetLayout() const;
    VkDescriptorSet textureSet() const;
    VkPipeline pipeline() const;
    VkPipelineLayout pipelineLayout() const;
    std::span<const VkImage> swapchainImages() const;
    std::span<const VkImageView> swapchainImageViews() const;
    std::span<const VkSemaphore> renderCompleteSemaphores() const;
    // Returns false when minimized; try again before acquiring an image.
    bool recreateSwapchain();

    struct FrameResources {
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        VkSemaphore imageAcquired{VK_NULL_HANDLE};
        VkBuffer shaderDataBuffer{VK_NULL_HANDLE};
        VmaAllocation shaderDataAllocation{VK_NULL_HANDLE};
        VmaAllocationInfo shaderDataAllocationInfo{};
        VkDeviceAddress shaderDataAddress{0};
    };

    const FrameResources& currentFrameResources() const;
    // Call after waiting for the current frame fence and acquiring an image.
    void updateShaderData(const Scene& scene);
    // Record the current frame for the image returned by vkAcquireNextImageKHR.
    void recordCommands(std::uint32_t acquiredImageIndex);
    // Call only after a frame has been submitted and presented.
    void advanceFrame();

private:
    void createSwapchain();
    void createDepthResources();
    void createFrameResources();
    void createDescriptors();
    void createPipeline();
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
