#pragma once

#include <vulkan/vulkan.h>
#include <volk/volk.h>
#include <SDL3/SDL_vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <cstdint>
#include <vector>

class Window;

// Owns the Vulkan instance, surface, device, and allocator. Window outlives it.
class VulkanContext {
public:
    #ifdef NDEBUG
    static constexpr bool enableValidationLayers = false;
    #else
    static constexpr bool enableValidationLayers = true;
    #endif

    explicit VulkanContext(const Window& window, std::uint32_t deviceIndex = 0);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    void waitIdle() const;
    VkInstance instance() const;
    VkSurfaceKHR surface() const;
    VkPhysicalDevice physicalDevice() const;
    VkDevice device() const;
    VkQueue graphicsQueue() const;
    std::uint32_t graphicsQueueFamily() const;
    VmaAllocator allocator() const;
    const VkPhysicalDeviceProperties2& deviceProperties() const;
    VkFormat findDepthFormat() const;

private:
    void createInstance(const Window& window);
    void setupDebugMessenger();
    void selectPhysicalDevice(std::uint32_t deviceIndex);
    void createDevice();
    void createAllocator();
    void cleanup();

    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties2 deviceProperties_{};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue graphicsQueue_{VK_NULL_HANDLE};
    // Select a queue family supporting both graphics and this surface.
    std::uint32_t graphicsQueueFamily_{0};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
};
