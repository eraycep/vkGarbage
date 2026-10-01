#include "ShadowMap.hpp"
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Shader.hpp"
#include "Scene.hpp"
#include <stdexcept>

ShadowMap::ShadowMap(VulkanContext& vulkanContext) : context_(vulkanContext)
{
    try {
        createDepthTexture();
        createPipeline();
    } catch (...) {
        cleanup();
        throw;
    }
}

ShadowMap::~ShadowMap()
{
    cleanup();
}

void ShadowMap::Render(VkCommandBuffer& cb, VkDeviceAddress shaderDataAddress, const Assets::Mesh& mesh, std::span<const SceneObject> objects)
{
    VkImageMemoryBarrier2 depthBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depthImage_,
        .subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo dependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &depthBarrier };
    vkCmdPipelineBarrier2(cb, &dependencyInfo);

    VkRenderingAttachmentInfo depthAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .depthStencil = {1.0f, 0} }
    };
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = 0, .extent = extent_ },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pDepthAttachment = &depthAttachmentInfo
    };
    vkCmdBeginRendering(cb, &renderingInfo);
    VkViewport viewport{ .width = static_cast<float>(extent_.width), .height = static_cast<float>(extent_.height), .minDepth = 0.0f, .maxDepth = 1.0f };
    vkCmdSetViewport(cb, 0, 1, &viewport);
    VkRect2D scissor{ .extent{ .width = static_cast<uint32_t>(extent_.width), .height = static_cast<uint32_t>(extent_.height) } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDeviceSize vOffset{ 0 };
    vkCmdBindVertexBuffers(cb, 0, 1, &mesh.buffer, &vOffset);
    vkCmdBindIndexBuffer(cb, mesh.buffer, mesh.indexOffset, mesh.indexType);
    vkCmdPushConstants(cb, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &shaderDataAddress);
    for (std::uint32_t i = 0; i < objects.size(); ++i) {
        const auto& range = mesh.ranges[static_cast<std::size_t>(objects[i].mesh)];
        vkCmdDrawIndexed(cb, range.indexCount, 1, range.firstIndex, 0, i);
    }
    vkCmdEndRendering(cb);

    depthBarrier.srcStageMask =
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    depthBarrier.srcAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    depthBarrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    depthBarrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier2(cb, &dependencyInfo);
}

void ShadowMap::createDepthTexture()
{
    const VkDevice& device = context_.device();

    // Keep a depth/stencil format: the barriers transition both aspects.
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    for (VkFormat candidate : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context_.physicalDevice(), candidate, &properties);
        if ((properties.optimalTilingFeatures & required) == required) {
            depthFormat_ = candidate;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No sampled depth/stencil format available for the shadow map");
    }
    VkImageCreateInfo imageCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat_,
        .extent = {extent_.width, extent_.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocationCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
    chk(vmaCreateImage(context_.allocator(), &imageCI, &allocationCI, &depthImage_, &allocation_, nullptr));
    VkImageViewCreateInfo imageViewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = depthImage_, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = depthFormat_, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 } };
    chk(vkCreateImageView(device, &imageViewCI, nullptr, &depthImageView_));

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .maxLod = 0
    };
    chk(vkCreateSampler(device, &samplerCI, nullptr, &depthSampler_));
}

void ShadowMap::createPipeline()
{
    const Shader shader(context_.device(), "assets/shadowMap.slang");
    VkPushConstantRange pushConstantRange{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .offset = 0, .size = sizeof(VkDeviceAddress)};
    VkPipelineLayoutCreateInfo pipelineLayoutCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange };
    chk(vkCreatePipelineLayout(context_.device(), &pipelineLayoutCI, nullptr, &pipelineLayout_));

    VkVertexInputBindingDescription vertexBinding{ .binding = 0, .stride = sizeof(Assets::Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> vertexAttributes{
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT },
		{ .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Assets::Vertex, normal) },
		{ .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Assets::Vertex, uv) },
	};

    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertexBinding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };

    const VkDynamicState states[]{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = states
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rasterizationState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineDepthStencilStateCreateInfo depthStencilState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS, .depthBoundsTestEnable = VK_FALSE, .stencilTestEnable = VK_FALSE };
    VkPipelineColorBlendStateCreateInfo colorBlendState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .logicOpEnable = VK_FALSE, .attachmentCount = 0, .pAttachments = nullptr };
    VkPipelineRenderingCreateInfo renderingCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 0, .pColorAttachmentFormats = nullptr, .depthAttachmentFormat = depthFormat_, .stencilAttachmentFormat = VK_FORMAT_UNDEFINED };
    VkPipelineShaderStageCreateInfo shaderStageCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = shader.module(), .pName = "main" };
    
    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 1,
        .pStages = &shaderStageCI,
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout_
    };
    chk(vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline_));
}

void ShadowMap::cleanup()
{
    vkDestroyPipeline(context_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
    vkDestroySampler(context_.device(), depthSampler_, nullptr);
    vkDestroyImageView(context_.device(), depthImageView_, nullptr);
    vmaDestroyImage(context_.allocator(), depthImage_, allocation_);
}

VkDescriptorImageInfo ShadowMap::descriptorInfo() const
{
    VkDescriptorImageInfo descriptorInfo{
        .sampler = depthSampler_,
        .imageView = depthImageView_,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
    };
    
    return descriptorInfo;
}