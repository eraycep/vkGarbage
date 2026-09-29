#include "Application.hpp"
#include "Assets.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

Application::Application(std::uint32_t deviceIndex) : deviceIndex_(deviceIndex)
{
}

Application::~Application()
{
    shutdown();
}

void Application::initialize()
{
    if (!std::filesystem::is_directory("assets")) {
        throw std::runtime_error("Could not locate assets folder from current working directory");
    }
    window_ = std::make_unique<Window>();
    context_ = std::make_unique<VulkanContext>(*window_, deviceIndex_);
    assets_ = std::make_unique<Assets>(*context_);
    assets_->load();
    scene_ = std::make_unique<Scene>();
    renderer_ = std::make_unique<Renderer>(*context_, *window_, *assets_);
    lastFrameTime_ = SDL_GetTicks();
    running_ = true;
}

int Application::run()
{
    initialize();
    while (running_) {
        const auto now = SDL_GetTicks();
        const float deltaSeconds = static_cast<float>(now - lastFrameTime_) / 1000.0f;
        lastFrameTime_ = now;
        // Keep processing input even while minimized or waiting for a resize.
        processEvents(deltaSeconds);
        if (!running_) {
            break;
        }

        // A suboptimal acquisition still owns an image: finish before resizing.
        if (!renderer_->drawFrame(*scene_)) {
            SDL_Delay(10);
            continue;
        }
    }
    shutdown();
    return EXIT_SUCCESS;
}

void Application::processEvents(float deltaSeconds)
{
    SDL_Event event;
    while (running_ && window_->pollEvent(event)) {
        handleEvent(event, deltaSeconds);
    }
}

void Application::handleEvent(const SDL_Event& event, float deltaSeconds)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
            scene_->rotateSelected({-event.motion.yrel * deltaSeconds, event.motion.xrel * deltaSeconds});
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        scene_->moveCamera(event.wheel.y * deltaSeconds * 10.0f);
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
            scene_->selectNext();
        } else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
            scene_->selectPrevious();
        }
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        renderer_->requestResize();
        break;
    default:
        break;
    }
}

void Application::shutdown()
{
    running_ = false;
    if (context_) {
        context_->waitIdle();
    }
    renderer_.reset();
    scene_.reset();
    assets_.reset();
    context_.reset();
    window_.reset();
}
