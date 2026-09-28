/* Copyright (c) 2025-2026, Sascha Willems
 * SPDX-License-Identifier: MIT
 */

#define VOLK_IMPLEMENTATION
#include <vulkan/vulkan.h>
#include <volk/volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <filesystem>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "Common.hpp"
#include "Window.hpp"
#include "VulkanContext.hpp"
#include "Assets.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"

int main(int argc, char* argv[])
{
	// Make sure asset folder is present from the current working directory
	if (!std::filesystem::is_directory("assets")) {
		std::cerr << "Could not locate assets folder from current working directory\n";
		exit(-1);
	}
    const uint32_t deviceIndex = argc > 1 ? static_cast<uint32_t>(std::stoi(argv[1])) : 0;
    Window window;
    VulkanContext context(window, deviceIndex);
    Assets assets(context);
    assets.load();
    Renderer renderer(context, window, assets);
    const VkDevice device = context.device();
    const VkQueue queue = context.graphicsQueue();
    const auto& mesh = assets.mesh();
    const VkDescriptorSet descriptorSetTex = renderer.textureSet();
    const VkPipeline pipeline = renderer.pipeline();
    const VkPipelineLayout pipelineLayout = renderer.pipelineLayout();
    Scene::ShaderData shaderData{};
    glm::vec3 camPos{0.0f, 0.0f, -6.0f};
    glm::vec3 objectRotations[Scene::objectCount]{};
    uint32_t frameIndex{0};
    uint32_t imageIndex{0};
    bool updateSwapchain{false};

	// Render loop
	uint64_t lastTime{ SDL_GetTicks() };
	bool quit{ false };
	while (!quit) {
        if (updateSwapchain) {
            if (!renderer.recreateSwapchain()) {
                SDL_Event event;
                if (window.pollEvent(event) && event.type == SDL_EVENT_QUIT) {
                    quit = true;
                }
                SDL_Delay(10);
                continue;
            }
            updateSwapchain = false;
        }
        const auto windowSize = renderer.extent();
        const auto& frame = renderer.frameResources(frameIndex);
        const VkSwapchainKHR swapchain = renderer.swapchain();
        const auto swapchainImages = renderer.swapchainImages();
        const auto swapchainImageViews = renderer.swapchainImageViews();
        const auto renderCompleteSemaphores = renderer.renderCompleteSemaphores();
        const VkImage depthImage = renderer.depthImage();
        const VkImageView depthImageView = renderer.depthImageView();
        chk(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, UINT64_MAX));
        const VkResult acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
            frame.imageAcquired, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            updateSwapchain = true;
            continue;
        }
        if (acquired == VK_SUBOPTIMAL_KHR) {
            updateSwapchain = true;
        } else {
            chk(acquired);
        }
        chk(vkResetFences(device, 1, &frame.fence));
		// Update shader data
		shaderData.projection = glm::perspective(glm::radians(45.0f), (float)windowSize.width / (float)windowSize.height, 0.1f, 32.0f);
		shaderData.view = glm::translate(glm::mat4(1.0f), camPos);
		for (auto i = 0; i < 3; i++) {
			auto instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
			shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(objectRotations[i]));
		}
		memcpy(frame.shaderDataAllocationInfo.pMappedData, &shaderData, sizeof(shaderData));
        chk(vmaFlushAllocation(context.allocator(), frame.shaderDataAllocation, 0, VK_WHOLE_SIZE));
		// Build command buffer
		auto cb = frame.commandBuffer;
		chk(vkResetCommandBuffer(cb, 0));
		VkCommandBufferBeginInfo cbBI { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
		chk(vkBeginCommandBuffer(cb, &cbBI));
		std::array<VkImageMemoryBarrier2, 2> outputBarriers{
			VkImageMemoryBarrier2{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = 0,
				.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = swapchainImages[imageIndex],
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
			},
			VkImageMemoryBarrier2{
				.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
				.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
				.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				.image = depthImage,
				.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
			}
		};
		VkDependencyInfo barrierDependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 2, .pImageMemoryBarriers = outputBarriers.data() };
		vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);
		VkRenderingAttachmentInfo colorAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = swapchainImageViews[imageIndex],
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
			.clearValue{.color{ 0.0f, 0.0f, 0.0f, 1.0f }}
		};
		VkRenderingAttachmentInfo depthAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = depthImageView,
			.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
			.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.clearValue = {.depthStencil = {1.0f,  0}}
		};
		VkRenderingInfo renderingInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea{.extent{.width = static_cast<uint32_t>(windowSize.width), .height = static_cast<uint32_t>(windowSize.height) }},
			.layerCount = 1,
			.colorAttachmentCount = 1,
			.pColorAttachments = &colorAttachmentInfo,
			.pDepthAttachment = &depthAttachmentInfo
		};
		vkCmdBeginRendering(cb, &renderingInfo);
		VkViewport vp{ .width = static_cast<float>(windowSize.width), .height = static_cast<float>(windowSize.height), .minDepth = 0.0f, .maxDepth = 1.0f};
		vkCmdSetViewport(cb, 0, 1, &vp);
		VkRect2D scissor{ .extent{ .width = static_cast<uint32_t>(windowSize.width), .height = static_cast<uint32_t>(windowSize.height) } };
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdSetScissor(cb, 0, 1, &scissor);
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSetTex, 0, nullptr);
		VkDeviceSize vOffset{ 0 };
		vkCmdBindVertexBuffers(cb, 0, 1, &mesh.buffer, &vOffset);
		vkCmdBindIndexBuffer(cb, mesh.buffer, mesh.indexOffset, mesh.indexType);
		vkCmdPushConstants(cb, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &frame.shaderDataAddress);
		vkCmdDrawIndexed(cb, mesh.indexCount, Scene::objectCount, 0, 0, 0);
		vkCmdEndRendering(cb);
		VkImageMemoryBarrier2 barrierPresent{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = 0,
			.oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			.image = swapchainImages[imageIndex],
			.subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
		};
		VkDependencyInfo barrierPresentDependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrierPresent };
		vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);
		chk(vkEndCommandBuffer(cb));
		// Submit to graphics queue
		VkSemaphoreSubmitInfo waitSemaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = frame.imageAcquired, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkCommandBufferSubmitInfo commandBufferSubmitInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb };
		VkSemaphoreSubmitInfo signalSemaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = renderCompleteSemaphores[imageIndex], .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo2 submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &waitSemaphoreInfo,
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &commandBufferSubmitInfo,
			.signalSemaphoreInfoCount = 1,
			.pSignalSemaphoreInfos = &signalSemaphoreInfo,
		};
		chk(vkQueueSubmit2(queue, 1, &submitInfo, frame.fence));
		frameIndex = (frameIndex + 1) % Renderer::maxFramesInFlight;
		VkPresentInfoKHR presentInfo{
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &renderCompleteSemaphores[imageIndex],
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &imageIndex
		};
		const VkResult presented = vkQueuePresentKHR(queue, &presentInfo);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            updateSwapchain = true;
        } else {
            chk(presented);
        }
		// Event polling
		float elapsedTime{ (SDL_GetTicks() - lastTime) / 1000.0f };
		lastTime = SDL_GetTicks();
		for (SDL_Event event; window.pollEvent(event);) {
			if (event.type == SDL_EVENT_QUIT) {
				quit = true;
				break;
			}
			if (event.type == SDL_EVENT_MOUSE_MOTION) {
				if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
					objectRotations[shaderData.selected].x -= (float)event.motion.yrel * elapsedTime;
					objectRotations[shaderData.selected].y += (float)event.motion.xrel * elapsedTime;
				}
			}
			if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				camPos.z += (float)event.wheel.y * elapsedTime * 10.0f;
			}
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
					shaderData.selected = (shaderData.selected < 2) ? shaderData.selected + 1 : 0;
				}
				if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
					shaderData.selected = (shaderData.selected > 0) ? shaderData.selected - 1 : 2;
				}
			}
			// Window resize
			if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
				updateSwapchain = true;
			}
		}
	}
    context.waitIdle();
    // Renderer, Assets, VulkanContext, then Window clean up in reverse order.
}
