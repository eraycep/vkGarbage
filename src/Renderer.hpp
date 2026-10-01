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
class ShadowMap;

// Owns rendering resources; its referenced components must outlive it.
class Renderer {
public:
    static constexpr std::uint32_t maxFramesInFlight = 2;

    Renderer(VulkanContext& context, const Window& window, const Assets& assets, ShadowMap& shadowMap);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool drawFrame(const Scene& scene);
    void requestResize();

private:
    // Returns false while the window has zero extent.
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

    bool createSwapchain();
    void createDepthResources();
    void createFrameResources();
    void createDescriptors();
    void createShadowMapDescriptors();
    void createPipeline();
    void advanceFrame();
    void destroySwapchainResources();
    void cleanup();

    const FrameResources& currentFrameResources() const;
    // Call after waiting for the current frame fence and acquiring an image.
    void updateShaderData(const Scene& scene);
    // Wait for the current frame and store the image index selected by Vulkan.
    // Marks resize requests; caller skips recording on OUT_OF_DATE.
    VkResult acquireNextImage();
    // Call only after successful acquisition (SUCCESS or SUBOPTIMAL).
    void recordCommands(const Scene& scene);
    // Submit the recorded frame, handle presentation status, and advance the frame.
    void submitAndPresent();

    VulkanContext& context_;
    ShadowMap& shadowMap_;
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

    VkDescriptorSetLayout shadowSetLayout_{VK_NULL_HANDLE};
    VkDescriptorSet shadowSet_{VK_NULL_HANDLE};
};
