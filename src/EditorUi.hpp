#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>

class VulkanContext;
class Window;
class Scene;
union SDL_Event;

// Owns the ImGui context and backends. Must outlive recorded UI work on the GPU.
class EditorUi {
public:
    EditorUi(VulkanContext& context, const Window& window);
    ~EditorUi();
    EditorUi(const EditorUi&) = delete;
    EditorUi& operator=(const EditorUi&) = delete;

    void processEvent(const SDL_Event& event);
    bool wantsMouse() const;
    bool wantsKeyboard() const;
    void prepare(VkFormat colorFormat, VkFormat depthFormat, std::uint32_t imageCount);
    void build(Scene& scene);
    void record(VkCommandBuffer cb);

private:
    VulkanContext& context_;
    bool vulkanReady_{false};
    bool showDemo_{false};
    VkFormat colorFormat_{VK_FORMAT_UNDEFINED};
    VkFormat depthFormat_{VK_FORMAT_UNDEFINED};
    std::uint32_t imageCount_{0};
};
