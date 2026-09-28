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
    Scene scene;
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
        const auto& frame = renderer.currentFrameResources();
        const VkSwapchainKHR swapchain = renderer.swapchain();
        const auto renderCompleteSemaphores = renderer.renderCompleteSemaphores();
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
        renderer.updateShaderData(scene);
        renderer.recordCommands(imageIndex);
		// Submit to graphics queue
		VkSemaphoreSubmitInfo waitSemaphoreInfo{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = frame.imageAcquired, .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkCommandBufferSubmitInfo commandBufferSubmitInfo{ .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = frame.commandBuffer };
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
        renderer.advanceFrame();
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
					scene.rotateSelected({-event.motion.yrel * elapsedTime, event.motion.xrel * elapsedTime});
				}
			}
			if (event.type == SDL_EVENT_MOUSE_WHEEL) {
				scene.moveCamera(event.wheel.y * elapsedTime * 10.0f);
			}
			if (event.type == SDL_EVENT_KEY_DOWN) {
				if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
					scene.selectNext();
				}
				if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
					scene.selectPrevious();
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
