#include "Window.hpp"
#include "Common.hpp"

Window::Window(const char* title, int width, int height)
{
    chk(SDL_Init(SDL_INIT_VIDEO));
    videoInitialized_ = true;
	chk(SDL_Vulkan_LoadLibrary(NULL));
    vulkanLibraryLoaded_ = true;

    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    SDL_SetWindowRelativeMouseMode(window_, true);
    chk(window_ != nullptr);
}

Window::~Window()
{
    SDL_DestroyWindow(window_);
    if (vulkanLibraryLoaded_) {
        SDL_Vulkan_UnloadLibrary();
    }
    if (videoInitialized_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

bool Window::pollEvent(SDL_Event& event)
{
    return SDL_PollEvent(&event);
}

SDL_Window* Window::nativeHandle() const
{
    return window_;
}

VkExtent2D Window::framebufferExtent() const
{
    int width{0};
    int height{0};
    chk(SDL_GetWindowSizeInPixels(window_, &width, &height));

    return {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };
}

std::vector<const char*> Window::requiredInstanceExtensions() const
{
    uint32_t instanceExtensionCount{ 0 };
    char const* const* instanceExtensions{ SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount) };
    chk(instanceExtensions != nullptr);
    return std::vector<const char*>(instanceExtensions, instanceExtensions + instanceExtensionCount);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const
{
    VkSurfaceKHR surface;
    chk(SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface));
    return surface;
}
