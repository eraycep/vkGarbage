#include "Renderer.hpp"
#include "Window.hpp"
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Scene.hpp"

Renderer::Renderer(VulkanContext& context, const Window& window, const Assets& assets) : context_(context), window_(window), assets_(assets)
{
    
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
        extent_ = { .width = static_cast<uint32_t>(framebufferExtent.width), .height = static_cast<uint32_t>(framebufferExtent.height) };
    }

	const VkFormat imageFormat{ VK_FORMAT_B8G8R8A8_SRGB };
	VkSwapchainCreateInfoKHR swapchainCI{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surfaceCaps.minImageCount,
		.imageFormat = imageFormat,
		.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = extent_.width, .height = extent_.height },
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};
    chk(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapchain_));
    uint32_t imageCount{ 0 };
    chk(vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr));
    swapchainImages_.resize(imageCount);
    chk(vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, swapchainImages_.data()));
    for (auto i = 0; i < swapchainImages_.size(); i++) {
        VkImageViewCreateInfo viewCI { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = swapchainImages_[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = imageFormat, .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 } };
        vkCreateImageView(device, &viewCI, nullptr, &swapchainImageViews_[i]);
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
        VmaAllocationCreateInfo uBufferAllocCreateInfo{ .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT, .usage = VMA_MEMORY_USAGE_AUTO };
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
        vkAllocateCommandBuffers(context_.device(), &cbAllocCI, &frames_[i].commandBuffer);
    }
}