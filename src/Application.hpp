#pragma once

#include <cstdint>
#include <memory>

union SDL_Event;
class Window;
class VulkanContext;
class Assets;
class Scene;
class Renderer;

// Owns the components and coordinates initialization, input, rendering, and shutdown.
class Application {
public:
    static constexpr float movementSpeed_ = 10.0f;
    static constexpr float cameraSensitivity_ = 0.2f;

    explicit Application(std::uint32_t deviceIndex = 0);
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void initialize();
    void processEvents(float deltaSeconds);
    void updateMovement(float deltaSeconds);
    void handleEvent(const SDL_Event& event, float deltaSeconds);
    void shutdown();

    std::uint32_t deviceIndex_{0};
    bool running_{false};
    std::uint64_t lastFrameTime_{0};

    // Reverse destruction order keeps dependencies alive through cleanup.
    // shutdown() must wait for the device before releasing GPU resources.
    std::unique_ptr<Window> window_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<Assets> assets_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Renderer> renderer_;
};
