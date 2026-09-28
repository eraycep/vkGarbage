#include "Renderer.hpp"
#include "Window.hpp"
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Scene.hpp"
#include "Assets.hpp"
#include <algorithm>
#include <cstring>

Renderer::Renderer(VulkanContext& context, const Window& window, const Assets& assets) : context_(context), window_(window), assets_(assets)
{
    createSwapchain();
    createDepthResources();
    createFrameResources();
    createDescriptors();
    createPipeline();
}

Renderer::~Renderer()
{
    context_.waitIdle();
    cleanup();
}

void Renderer::createSwapchain()
{
    const VkDevice device = context_.device();
    const VkSurfaceKHR surface = context_.surface();
    auto framebufferExtent = window_.framebufferExtent();

    VkSurfaceCapabilitiesKHR surfaceCaps{};
    chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context_.physicalDevice(), surface, &surfaceCaps));
    extent_ = { surfaceCaps.currentExtent };
    if (surfaceCaps.currentExtent.width == 0xFFFFFFFF) {
        extent_ = {std::clamp(framebufferExtent.width, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width),
                   std::clamp(framebufferExtent.height, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height)};
    }

	const VkFormat imageFormat{ colorFormat_ };
	VkSwapchainCreateInfoKHR swapchainCI{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = extent_.width, .height = extent_.height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = surfaceCaps.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
    chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain_));
    uint32_t imageCount{ 0 };
    chk(vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr));
    swapchainImages_.resize(imageCount);
    chk(vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, swapchainImages_.data()));
    swapchainImageViews_.resize(imageCount);
    for (auto i = 0; i < swapchainImages_.size(); i++) {
        VkImageViewCreateInfo viewCI { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchainImages_[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
        chk(vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews_[i]));
    }
}

void Renderer::createDepthResources()
{
    const VkDevice device = context_.device();

    depthFormat_ = context_.findDepthFormat();
    VkImageCreateInfo depthImageCI{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat_,
		.extent{.width = static_cast<uint32_t>(extent_.width), .height = static_cast<uint32_t>(extent_.height), .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VmaAllocationCreateInfo allocCI{ .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
	chk(vmaCreateImage(context_.allocator(), &depthImageCI, &allocCI, &depthImage_, &depthAllocation_, nullptr));
	VkImageViewCreateInfo depthViewCI{ .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = depthImage_, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = depthFormat_, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 } };
	chk(vkCreateImageView(device, &depthViewCI, nullptr, &depthImageView_));
}

void Renderer::createFrameResources()
{
    for (auto i = 0; i < maxFramesInFlight; i++) {
        VkBufferCreateInfo uBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(Scene::ShaderData), .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo uBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &uBufferCI, &uBufferAllocCreateInfo, &frames_[i].shaderDataBuffer, &frames_[i].shaderDataAllocation, &frames_[i].shaderDataAllocationInfo));
        VkBufferDeviceAddressInfo uBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = frames_[i].shaderDataBuffer };
        frames_[i].shaderDataAddress = vkGetBufferDeviceAddress(context_.device(), &uBufferBdaInfo);
    }

    VkSemaphoreCreateInfo semCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceCI{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
    for (auto i = 0; i < maxFramesInFlight; i++) {
        chk(vkCreateFence(context_.device(), &fenceCI, nullptr, &frames_[i].fence));
        chk(vkCreateSemaphore(context_.device(), &semCI, nullptr, &frames_[i].imageAcquired));
    }

    renderCompleteSemaphores_.resize(swapchainImages_.size());
    for (auto& semaphore : renderCompleteSemaphores_) {
        chk(vkCreateSemaphore(context_.device(), &semCI, nullptr, &semaphore));
    }

    VkCommandPoolCreateInfo poolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = context_.graphicsQueueFamily() };
    chk(vkCreateCommandPool(context_.device(), &poolCI, nullptr, &commandPool_));
    VkCommandBufferAllocateInfo cbAllocCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = commandPool_, .commandBufferCount = 1 };
    for (auto i = 0; i < maxFramesInFlight; i++) {
        chk(vkAllocateCommandBuffers(context_.device(), &cbAllocCI, &frames_[i].commandBuffer));
    }
}

void Renderer::createDescriptors()
{
    VkDescriptorBindingFlags descVariableFlag{ VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT };
    VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = 1, .pBindingFlags = &descVariableFlag };
    VkDescriptorSetLayoutBinding descLayoutBindingTex{ .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = static_cast<uint32_t>(assets_.textures().size()), .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo descLayoutTexCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &descBindingFlags, .bindingCount = 1, .pBindings = &descLayoutBindingTex };
    chk(vkCreateDescriptorSetLayout(context_.device(), &descLayoutTexCI, nullptr, &textureSetLayout_));
    VkDescriptorPoolSize poolSize{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = static_cast<uint32_t>(assets_.textures().size()) };
	VkDescriptorPoolCreateInfo descPoolCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &poolSize };
	chk(vkCreateDescriptorPool(context_.device(), &descPoolCI, nullptr, &descriptorPool_));

	uint32_t variableDescCount{ static_cast<uint32_t>(assets_.textures().size()) };
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT, .descriptorSetCount = 1, .pDescriptorCounts = &variableDescCount};
	VkDescriptorSetAllocateInfo texDescSetAlloc{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .pNext = &variableDescCountAI, .descriptorPool = descriptorPool_, .descriptorSetCount = 1, .pSetLayouts = &textureSetLayout_ };
	chk(vkAllocateDescriptorSets(context_.device(), &texDescSetAlloc, &textureSet_));

    std::vector<VkDescriptorImageInfo> textureDescriptors;
    for (const auto& texture : assets_.textures()) {
    textureDescriptors.push_back({
        .sampler = texture.sampler,
        .imageView = texture.view,
        .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
    });
}

	VkWriteDescriptorSet writeDescSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = textureSet_, .dstBinding = 0, .descriptorCount = static_cast<uint32_t>(textureDescriptors.size()), .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = textureDescriptors.data() };
	vkUpdateDescriptorSets(context_.device(), 1, &writeDescSet, 0, nullptr);
}

void Renderer::createPipeline()
{
    VkPushConstantRange pushConstantRange{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = sizeof(VkDeviceAddress) };
    VkPipelineLayoutCreateInfo pipelineLayoutCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &textureSetLayout_, .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange };
    chk(vkCreatePipelineLayout(context_.device(), &pipelineLayoutCI, nullptr, &pipelineLayout_));
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = assets_.shaderModule(), .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = assets_.shaderModule(), .pName = "main" },
    };

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
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    std::vector<VkDynamicState> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates.data() };
    VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rasterizationState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .lineWidth = 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampleState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
	VkPipelineDepthStencilStateCreateInfo depthStencilState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL };
	VkPipelineColorBlendAttachmentState blendAttachment{ .colorWriteMask = 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlendState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blendAttachment };
    VkPipelineRenderingCreateInfo renderingCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &colorFormat_, .depthAttachmentFormat = depthFormat_ };
    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 2,
        .pStages = shaderStages.data(),
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

void Renderer::updateShaderData(const Scene& scene)
{
    chk(extent_.width > 0 && extent_.height > 0);
    const Scene::ShaderData shaderData = scene.shaderData(static_cast<float>(extent_.width) / extent_.height);

    memcpy(frames_[frameIndex_].shaderDataAllocationInfo.pMappedData, &shaderData, sizeof(shaderData));
    chk(vmaFlushAllocation(context_.allocator(), frames_[frameIndex_].shaderDataAllocation, 0, VK_WHOLE_SIZE));
}

bool Renderer::recreateSwapchain()
{
    const auto size = window_.framebufferExtent();
    if (size.width == 0 || size.height == 0) {
        return false;
    }
    context_.waitIdle();
    destroySwapchainResources();
    createSwapchain();
    createDepthResources();
    VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderCompleteSemaphores_.resize(swapchainImages_.size());
    for (auto& semaphore : renderCompleteSemaphores_) {
        chk(vkCreateSemaphore(context_.device(), &info, nullptr, &semaphore));
    }
    return true;
}

void Renderer::destroySwapchainResources()
{
    for (auto semaphore : renderCompleteSemaphores_) {
        vkDestroySemaphore(context_.device(), semaphore, nullptr);
    }
    renderCompleteSemaphores_.clear();
    vkDestroyImageView(context_.device(), depthImageView_, nullptr);
    vmaDestroyImage(context_.allocator(), depthImage_, depthAllocation_);
    depthImageView_ = VK_NULL_HANDLE;
    depthImage_ = VK_NULL_HANDLE;
    depthAllocation_ = VK_NULL_HANDLE;
    for (auto view : swapchainImageViews_) {
        vkDestroyImageView(context_.device(), view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    vkDestroySwapchainKHR(context_.device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void Renderer::cleanup()
{
    vkDestroyPipeline(context_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(context_.device(), descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(context_.device(), textureSetLayout_, nullptr);
    for (const auto& frame : frames_) {
        vkDestroyFence(context_.device(), frame.fence, nullptr);
        vkDestroySemaphore(context_.device(), frame.imageAcquired, nullptr);
        vmaDestroyBuffer(context_.allocator(), frame.shaderDataBuffer, frame.shaderDataAllocation);
    }
    vkDestroyCommandPool(context_.device(), commandPool_, nullptr);
    destroySwapchainResources();
}

VkSwapchainKHR Renderer::swapchain() const { return swapchain_; }
VkExtent2D Renderer::extent() const { return extent_; }
VkFormat Renderer::colorFormat() const { return colorFormat_; }
VkFormat Renderer::depthFormat() const { return depthFormat_; }
VkImage Renderer::depthImage() const { return depthImage_; }
VkImageView Renderer::depthImageView() const { return depthImageView_; }
VkDescriptorSetLayout Renderer::textureSetLayout() const { return textureSetLayout_; }
VkDescriptorSet Renderer::textureSet() const { return textureSet_; }
VkPipeline Renderer::pipeline() const { return pipeline_; }
VkPipelineLayout Renderer::pipelineLayout() const { return pipelineLayout_; }
std::span<const VkImage> Renderer::swapchainImages() const { return swapchainImages_; }
std::span<const VkImageView> Renderer::swapchainImageViews() const { return swapchainImageViews_; }
std::span<const VkSemaphore> Renderer::renderCompleteSemaphores() const { return renderCompleteSemaphores_; }
const Renderer::FrameResources& Renderer::currentFrameResources() const { return frames_[frameIndex_]; }

void Renderer::advanceFrame()
{
    frameIndex_ = (frameIndex_ + 1) % maxFramesInFlight;
}
