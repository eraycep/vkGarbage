#include "ShadowMap.hpp"
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Assets.hpp"
#include "Shader.hpp"

ShadowMap::ShadowMap(VulkanContext& vulkanContext) : context_(vulkanContext)
{

}

void ShadowMap::CreateDepthTexture()
{
    const VkDevice& device = context_.device();

    VkFormat depthFormat = context_.findDepthFormat();
    VkImageCreateInfo imageCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthFormat,
        .extent = {.width = static_cast<uint32_t>(1024), .height = static_cast<uint32_t>(1024), .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocationCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
    chk(vmaCreateImage(context_.allocator(), &imageCI, &allocationCI, &depthImage_, &allocation_, nullptr));
    VkImageViewCreateInfo imageViewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = depthImage_, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = depthFormat, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 } };
    chk(vkCreateImageView(device, &imageViewCI, nullptr, &depthImageView_));

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = std::min(8.0f, context_.deviceProperties().properties.limits.maxSamplerAnisotropy),
        .maxLod = 0
    };
    chk(vkCreateSampler(device, &samplerCI, nullptr, &depthSampler_));
}

void ShadowMap::CreatePipeline()
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
    VkPipelineRenderingCreateInfo renderingCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 0, .pColorAttachmentFormats = nullptr, .depthAttachmentFormat = context_.findDepthFormat(), .stencilAttachmentFormat = VK_FORMAT_UNDEFINED };
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