#include "Renderer.hpp"
#include "Window.hpp"
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Scene.hpp"
#include "Assets.hpp"
#include "ShadowMap.hpp"
#include "EditorUi.hpp"
#include "Shader.hpp"
#include <algorithm>
#include <cstring>

Renderer::Renderer(VulkanContext& context, const Window& window, const Assets& assets, ShadowMap& shadowMap) : context_(context), window_(window), assets_(assets), shadowMap_(shadowMap)
{
    const bool ready = createSwapchain();
    if (ready) {
        createDepthResources();
    }
    createFrameResources();
    createDescriptors();
    createShadowMapDescriptors();
    ui_ = std::make_unique<EditorUi>(context_, window_);
    if (ready) {
        createPipeline();
    } else {
        requestResize();
    }
}

Renderer::~Renderer()
{
    context_.waitIdle();
    ui_.reset();
    cleanup();
}

bool Renderer::drawFrame(Scene& scene)
{
    if (resizeRequested_ && !recreateSwapchain()) {
        return false;
    }

    if (acquireNextImage() == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }

    ui_->prepare(colorFormat_, depthFormat_, static_cast<std::uint32_t>(swapchainImages_.size()));
    ui_->build(scene);
    updateShaderData(scene);
    recordCommands(scene);
    submitAndPresent();
    return true;
}

bool Renderer::createSwapchain()
{
    const VkDevice device = context_.device();
    const VkPhysicalDevice physicalDevice = context_.physicalDevice();
    const VkSurfaceKHR surface = context_.surface();
    const auto framebufferExtent = window_.framebufferExtent();
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0) {
        return false;
    }

    VkSurfaceCapabilitiesKHR caps{};
    chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps));
    extent_ = caps.currentExtent;
    if (extent_.width == UINT32_MAX) {
        extent_ = {std::clamp(framebufferExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width),
                   std::clamp(framebufferExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height)};
    }
    if (extent_.width == 0 || extent_.height == 0) {
        return false;
    }
    chk((caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0);

    uint32_t count{0};
    std::vector<VkSurfaceFormatKHR> formats;
    VkResult result;
    do {
        chk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr));
        formats.resize(count);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
    } while (result == VK_INCOMPLETE);
    chk(result);
    formats.resize(count);
    // Preserve the renderer's sRGB output; do not silently switch to an HDR/linear format.
    auto selected = formats.end();
    for (VkFormat preferred : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
        selected = std::find_if(formats.begin(), formats.end(), [preferred](const auto& format) {
            return format.format == preferred && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        if (selected != formats.end()) {
            break;
        }
    }
    if (selected == formats.end()) {
        std::cerr << "Surface has no supported sRGB swapchain format\n";
        std::exit(EXIT_FAILURE);
    }
    const auto surfaceFormat = *selected;
    colorFormat_ = surfaceFormat.format;

    std::vector<VkPresentModeKHR> modes;
    do {
        chk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, nullptr));
        modes.resize(count);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &count, modes.data());
    } while (result == VK_INCOMPLETE);
    chk(result);
    modes.resize(count);
    chk(std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) != modes.end());

    VkCompositeAlphaFlagBitsKHR compositeAlpha{};
    for (auto mode : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & mode) {
            compositeAlpha = mode;
            break;
        }
    }
    chk(compositeAlpha != 0);
    uint32_t requestedImages = caps.minImageCount;
    if (requestedImages < UINT32_MAX) {
        ++requestedImages;
    }
    if (caps.maxImageCount != 0) {
        requestedImages = std::min(requestedImages, caps.maxImageCount);
    }
    VkSwapchainCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = requestedImages,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
    };
    result = vkCreateSwapchainKHR(device, &info, nullptr, &swapchain_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return false; // The surface changed between the query and creation; retry next frame.
    }
    chk(result);
    do {
        chk(vkGetSwapchainImagesKHR(device, swapchain_, &count, nullptr));
        swapchainImages_.resize(count);
        result = vkGetSwapchainImagesKHR(device, swapchain_, &count, swapchainImages_.data());
    } while (result == VK_INCOMPLETE);
    chk(result);
    swapchainImages_.resize(count);
    swapchainImageViews_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchainImages_[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = colorFormat_,
            .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
        };
        chk(vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews_[i]));
    }
    return true;
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
        frames_[i].objectCapacity = 16;
        frames_[i].materialCapacity = 16;

        VkBufferCreateInfo uBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(FrameData), .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo uBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &uBufferCI, &uBufferAllocCreateInfo, &frames_[i].frameDataBuffer, &frames_[i].frameDataAllocation, &frames_[i].frameDataAllocationInfo));
        VkBufferDeviceAddressInfo uBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = frames_[i].frameDataBuffer };
        frames_[i].frameDataAddress = vkGetBufferDeviceAddress(context_.device(), &uBufferBdaInfo);

        VkBufferCreateInfo oBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(GPUObject) * frames_[i].objectCapacity, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo oBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &oBufferCI, &oBufferAllocCreateInfo, &frames_[i].objectBuffer, &frames_[i].objectDataAllocation, &frames_[i].objectDataAllocationInfo));
        VkBufferDeviceAddressInfo oBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = frames_[i].objectBuffer };
        frames_[i].objectDataAddress = vkGetBufferDeviceAddress(context_.device(), &oBufferBdaInfo);

        VkBufferCreateInfo mBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(GPUMaterial) * frames_[i].materialCapacity, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo mBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &mBufferCI, &mBufferAllocCreateInfo, &frames_[i].materialBuffer, &frames_[i].materialDataAllocation, &frames_[i].materialDataAllocationInfo));
        VkBufferDeviceAddressInfo mBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = frames_[i].materialBuffer };
        frames_[i].materialDataAddress = vkGetBufferDeviceAddress(context_.device(), &mBufferBdaInfo);
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
    VkDescriptorPoolSize poolSize{ .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = static_cast<uint32_t>(assets_.textures().size() + 1) };
	VkDescriptorPoolCreateInfo descPoolCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 1, .pPoolSizes = &poolSize };
	chk(vkCreateDescriptorPool(context_.device(), &descPoolCI, nullptr, &descriptorPool_));

	uint32_t variableDescCount{ static_cast<uint32_t>(assets_.textures().size()) };
	VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO, .descriptorSetCount = 1, .pDescriptorCounts = &variableDescCount};
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

void Renderer::createShadowMapDescriptors()
{
    VkDescriptorSetLayoutBinding descLayoutBindingShadow{ .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT };
    VkDescriptorSetLayoutCreateInfo descLayoutShadowCI{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &descLayoutBindingShadow };
    chk(vkCreateDescriptorSetLayout(context_.device(), &descLayoutShadowCI, nullptr, &shadowSetLayout_));

    VkDescriptorSetAllocateInfo shadowDescSetAlloc{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = descriptorPool_, .descriptorSetCount = 1, .pSetLayouts = &shadowSetLayout_ };
    chk(vkAllocateDescriptorSets(context_.device(), &shadowDescSetAlloc, &shadowSet_));

    VkDescriptorImageInfo shadowDecriptorImage = shadowMap_.descriptorInfo();
    VkWriteDescriptorSet writeDescSet{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = shadowSet_, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &shadowDecriptorImage };
    vkUpdateDescriptorSets(context_.device(), 1, &writeDescSet, 0, nullptr);
}

void Renderer::createPipeline()
{
    std::array<VkDescriptorSetLayout, 2> descriptorSetLayouts{textureSetLayout_, shadowSetLayout_};
    VkPushConstantRange pushConstantRange{ .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, .size = sizeof(VkDeviceAddress) };
    VkPipelineLayoutCreateInfo pipelineLayoutCI{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()), .pSetLayouts = descriptorSetLayouts.data(), .pushConstantRangeCount = 1, .pPushConstantRanges = &pushConstantRange };
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

    // Share attachment formats/layout, but draw a depth preview without mesh input or depth tests.
    const Shader preview(context_.device(), "assets/shadowDepth.slang");
    for (auto& stage : shaderStages) stage.module = preview.module();
    vertexInputState.vertexBindingDescriptionCount = 0;
    vertexInputState.pVertexBindingDescriptions = nullptr;
    vertexInputState.vertexAttributeDescriptionCount = 0;
    vertexInputState.pVertexAttributeDescriptions = nullptr;
    depthStencilState.depthTestEnable = VK_FALSE;
    depthStencilState.depthWriteEnable = VK_FALSE;
    chk(vkCreateGraphicsPipelines(context_.device(), VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &shadowDepthPipeline_));
}

VkResult Renderer::acquireNextImage()
{
    const auto& frame = frames_[frameIndex_];
    chk(vkWaitForFences(context_.device(), 1, &frame.fence, VK_TRUE, UINT64_MAX));
    const VkResult result = vkAcquireNextImageKHR(context_.device(), swapchain_, UINT64_MAX,
                                                frame.imageAcquired, VK_NULL_HANDLE, &imageIndex_);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        requestResize();
    } else {
        chk(result);
    }
    return result;
}

void Renderer::recordCommands(const Scene& scene)
{
    chk(imageIndex_ < swapchainImages_.size());
    auto cb = frames_[frameIndex_].commandBuffer;
    chk(vkResetCommandBuffer(cb, 0));
    VkCommandBufferBeginInfo cbBI{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
    chk(vkBeginCommandBuffer(cb, &cbBI));
    
    // render shadow map
    shadowMap_.Render(cb, frames_[frameIndex_].frameDataAddress, assets_, scene.renderObjects());

    std::array<VkImageMemoryBarrier2, 2> outputBarriers{
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImages_[imageIndex_],
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        },
        VkImageMemoryBarrier2{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = depthImage_,
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
        },
    };
    VkDependencyInfo barrierDependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 2, .pImageMemoryBarriers = outputBarriers.data() };
    vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);
    VkRenderingAttachmentInfo colorAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swapchainImageViews_[imageIndex_],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    VkRenderingAttachmentInfo depthAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depthImageView_,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f,  0}}
    };
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .extent{.width = static_cast<uint32_t>(extent_.width), .height = static_cast<uint32_t>(extent_.height)} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cb, &renderingInfo);
    VkViewport vp{ .width = static_cast<float>(extent_.width), .height = static_cast<float>(extent_.height), .minDepth = 0.0f, .maxDepth = 1.0f };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{ .extent{ .width = static_cast<uint32_t>(extent_.width), .height = static_cast<uint32_t>(extent_.height) } };
    vkCmdSetScissor(cb, 0, 1, &scissor);
    const bool depthPreview = scene.debugMode() == Scene::DebugMode::ShadowDepth;
    if (depthPreview) {
        // Preserve the square shadow map's aspect ratio.
        const float side = static_cast<float>(std::min(extent_.width, extent_.height));
        VkViewport previewViewport{(extent_.width - side) * 0.5f,
            (extent_.height - side) * 0.5f, side, side, 0.0f, 1.0f};
        vkCmdSetViewport(cb, 0, 1, &previewViewport);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      depthPreview ? shadowDepthPipeline_ : pipeline_);
    std::array<VkDescriptorSet, 2> descriptorSets{textureSet_, shadowSet_};
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0, nullptr);
    vkCmdPushConstants(cb, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkDeviceAddress), &frames_[frameIndex_].frameDataAddress);
    if (depthPreview) {
        vkCmdDraw(cb, 3, 1, 0, 0);
    } else for (std::uint32_t i = 0; i < scene.renderObjects().size(); ++i) {
        const auto& object = scene.renderObjects()[i];
        const auto& mesh = assets_.mesh(object.mesh);
        const auto& range = assets_.mesh(object.mesh).range;
        VkDeviceSize offset{0};

        vkCmdBindVertexBuffers(cb, 0, 1, &mesh.buffer, &offset);
        vkCmdBindIndexBuffer(cb, mesh.buffer, mesh.indexOffset, mesh.indexType);
        vkCmdDrawIndexed(cb, range.indexCount, 1, range.firstIndex, 0, i);
    }
    ui_->record(cb);
    vkCmdEndRendering(cb);
    VkImageMemoryBarrier2 barrierPresent{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = swapchainImages_[imageIndex_],
		.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
    };
    VkDependencyInfo barrierPresentDependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrierPresent };
    vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);
    chk(vkEndCommandBuffer(cb));
}

void Renderer::submitAndPresent()
{
    VkSemaphoreSubmitInfo waitSemaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = currentFrameResources().imageAcquired, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkCommandBufferSubmitInfo commandBufferSubmitInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = currentFrameResources().commandBuffer };
    VkSemaphoreSubmitInfo signalSemaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = renderCompleteSemaphores_[imageIndex_], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSemaphoreInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferSubmitInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalSemaphoreInfo
    };
    // Reset only when submitting, so an unsuccessful acquire leaves the fence signaled.
    chk(vkResetFences(context_.device(), 1, &currentFrameResources().fence));
    chk(vkQueueSubmit2(context_.graphicsQueue(), 1, &submitInfo, currentFrameResources().fence));
    VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderCompleteSemaphores_[imageIndex_],
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex_
    };
    const VkResult presented = vkQueuePresentKHR(context_.graphicsQueue(), &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        requestResize();
    } else {
        chk(presented);
    }
    advanceFrame();
}

void Renderer::updateShaderData(const Scene& scene)
{
    chk(extent_.width > 0 && extent_.height > 0);
    FrameData frameData = scene.frameData(static_cast<float>(extent_.width) / extent_.height);

    const auto objects = scene.gpuObjects(frameData.view);
    const auto materials = scene.gpuMaterials(assets_);

    auto& frame = frames_[frameIndex_];

    if (objects.size() > frames_[frameIndex_].objectCapacity) {
        VkBuffer objectBuffer{VK_NULL_HANDLE};
        VmaAllocation objectDataAllocation{VK_NULL_HANDLE};
        VmaAllocationInfo objectDataAllocationInfo{};
        VkDeviceAddress objectDataAddress{0};

        const size_t newCapacity = std::max(objects.size(), frames_[frameIndex_].objectCapacity * 2);

        VkBufferCreateInfo oBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(GPUObject) * newCapacity, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo oBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &oBufferCI, &oBufferAllocCreateInfo, &objectBuffer, &objectDataAllocation, &objectDataAllocationInfo));
        VkBufferDeviceAddressInfo oBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = objectBuffer };
        objectDataAddress = vkGetBufferDeviceAddress(context_.device(), &oBufferBdaInfo);

        vmaDestroyBuffer(context_.allocator(), frame.objectBuffer, frame.objectDataAllocation);
        frame.objectCapacity = newCapacity;
        frame.objectBuffer = objectBuffer;
        frame.objectDataAllocation = objectDataAllocation;
        frame.objectDataAllocationInfo = objectDataAllocationInfo;
        frame.objectDataAddress = objectDataAddress;
    }

    if (materials.size() > frames_[frameIndex_].materialCapacity) {
        VkBuffer materialBuffer{VK_NULL_HANDLE};
        VmaAllocation materialDataAllocation{VK_NULL_HANDLE};
        VmaAllocationInfo materialDataAllocationInfo{};
        VkDeviceAddress materialDataAddress{0};

        const size_t newCapacity = std::max(materials.size(), frames_[frameIndex_].materialCapacity * 2);

        VkBufferCreateInfo mBufferCI{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = sizeof(GPUMaterial) * newCapacity, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT };
        VmaAllocationCreateInfo mBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |  VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
        chk(vmaCreateBuffer(context_.allocator(), &mBufferCI, &mBufferAllocCreateInfo, &materialBuffer, &materialDataAllocation, &materialDataAllocationInfo));
        VkBufferDeviceAddressInfo mBufferBdaInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = materialBuffer };
        materialDataAddress = vkGetBufferDeviceAddress(context_.device(), &mBufferBdaInfo);

        vmaDestroyBuffer(context_.allocator(), frame.materialBuffer, frame.materialDataAllocation);
        frame.materialCapacity = newCapacity;
        frame.materialBuffer = materialBuffer;
        frame.materialDataAllocation = materialDataAllocation;
        frame.materialDataAllocationInfo = materialDataAllocationInfo;
        frame.materialDataAddress = materialDataAddress;
    }

    memcpy(frame.objectDataAllocationInfo.pMappedData,
       objects.data(), objects.size() * sizeof(GPUObject));
    memcpy(frame.materialDataAllocationInfo.pMappedData,
       materials.data(), materials.size() * sizeof(GPUMaterial));

    chk(vmaFlushAllocation(context_.allocator(),
                       frame.objectDataAllocation, 0, VK_WHOLE_SIZE));
    chk(vmaFlushAllocation(context_.allocator(),
                       frame.materialDataAllocation, 0, VK_WHOLE_SIZE));

    frameData.objectBuffer = frame.objectDataAddress;
    frameData.materialBuffer = frame.materialDataAddress;
    frameData.objectCount = static_cast<uint32_t>(objects.size());
    frameData.materialCount = static_cast<uint32_t>(materials.size());

    memcpy(frames_[frameIndex_].frameDataAllocationInfo.pMappedData, &frameData, sizeof(frameData));
    chk(vmaFlushAllocation(context_.allocator(), frames_[frameIndex_].frameDataAllocation, 0, VK_WHOLE_SIZE));
}

void Renderer::requestResize()
{
    resizeRequested_ = true;
}

bool Renderer::recreateSwapchain()
{
    const auto size = window_.framebufferExtent();
    if (size.width == 0 || size.height == 0) {
        requestResize();
        return false;
    }
    context_.waitIdle();
    // Retain the pipeline's formats across retries that defer swapchain creation.
    const VkFormat previousColorFormat = colorFormat_;
    const VkFormat previousDepthFormat = depthFormat_;
    destroySwapchainResources();
    if (!createSwapchain()) {
        colorFormat_ = previousColorFormat;
        requestResize();
        return false;
    }
    createDepthResources();
    if (pipeline_ == VK_NULL_HANDLE || colorFormat_ != previousColorFormat || depthFormat_ != previousDepthFormat) {
        vkDestroyPipeline(context_.device(), shadowDepthPipeline_, nullptr);
        shadowDepthPipeline_ = VK_NULL_HANDLE;
        vkDestroyPipeline(context_.device(), pipeline_, nullptr);
        vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        createPipeline();
    }
    VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    renderCompleteSemaphores_.resize(swapchainImages_.size());
    for (auto& semaphore : renderCompleteSemaphores_) {
        chk(vkCreateSemaphore(context_.device(), &info, nullptr, &semaphore));
    }
    resizeRequested_ = false;
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
    vkDestroyPipeline(context_.device(), shadowDepthPipeline_, nullptr);
    vkDestroyPipeline(context_.device(), pipeline_, nullptr);
    vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
    vkDestroyDescriptorPool(context_.device(), descriptorPool_, nullptr);
    vkDestroyDescriptorSetLayout(context_.device(), textureSetLayout_, nullptr);
    vkDestroyDescriptorSetLayout(context_.device(), shadowSetLayout_, nullptr);
    for (const auto& frame : frames_) {
        vkDestroyFence(context_.device(), frame.fence, nullptr);
        vkDestroySemaphore(context_.device(), frame.imageAcquired, nullptr);
        vmaDestroyBuffer(context_.allocator(), frame.frameDataBuffer, frame.frameDataAllocation);
        vmaDestroyBuffer(context_.allocator(), frame.objectBuffer, frame.objectDataAllocation);
        vmaDestroyBuffer(context_.allocator(), frame.materialBuffer, frame.materialDataAllocation);
    }
    vkDestroyCommandPool(context_.device(), commandPool_, nullptr);
    destroySwapchainResources();
}

const Renderer::FrameResources& Renderer::currentFrameResources() const { return frames_[frameIndex_]; }

void Renderer::advanceFrame()
{
    frameIndex_ = (frameIndex_ + 1) % maxFramesInFlight;
}
