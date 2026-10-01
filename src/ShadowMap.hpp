#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <array>
#include <cstdint>
#include <vector>

class VulkanContext;

class ShadowMap 
{
public:
    explicit ShadowMap(VulkanContext& vulkanContext);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void CreateDepthTexture();
    void CreatePipeline();

private:
    VulkanContext& context_;
    VkImage depthImage_;
    VkImageView depthImageView_;
    VmaAllocation allocation_;
    VkSampler depthSampler_;
    VkPipeline pipeline_;
    VkPipelineLayout pipelineLayout_;
};