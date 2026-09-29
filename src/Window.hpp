#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vector>

// Owns SDL video initialization, Vulkan library loading, and the native window.
class Window {
public:
    explicit Window(const char* title = "vkGarbage", int width = 1280, int height = 720);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool pollEvent(SDL_Event& event);
    SDL_Window* nativeHandle() const;
    VkExtent2D framebufferExtent() const;
    std::vector<const char*> requiredInstanceExtensions() const;

    // The caller owns the returned surface and must destroy it before the window.
    VkSurfaceKHR createSurface(VkInstance instance) const;

private:
    SDL_Window* window_{nullptr};
    bool videoInitialized_{false};
    bool vulkanLibraryLoaded_{false};
};
