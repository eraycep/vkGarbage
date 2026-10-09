#pragma once

#include "Assets.hpp"

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <vector>
#include <span>

class VulkanContext;

class ShadowMap 
{
public:
    explicit ShadowMap(VulkanContext& vulkanContext);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void Render(VkCommandBuffer cb, VkDeviceAddress frameDataAddress, const Assets& assets, std::span<const RenderObject> objects);

    VkDescriptorImageInfo descriptorInfo() const;

private:
    void createDepthTexture();
    void createPipeline();
    void cleanup();

    VulkanContext& context_;
    VkImage depthImage_{VK_NULL_HANDLE};
    VkImageView depthImageView_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkSampler depthSampler_{VK_NULL_HANDLE};
    VkPipeline pipeline_{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    VkExtent2D extent_{1024, 1024};
};
