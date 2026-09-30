#include "Application.hpp"
#include "Assets.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"
#include "Light.hpp"

#include <algorithm>
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
    lastFrameTime_ = SDL_GetTicksNS();
    running_ = true;
}

int Application::run()
{
    initialize();
    while (running_) {
        const auto now = SDL_GetTicksNS();
        // Limit jumps after a long stall, such as dragging or pausing the window.
        const float deltaSeconds = std::min(static_cast<float>(now - lastFrameTime_) / 1e9f, 0.1f);
        lastFrameTime_ = now;
        // Keep processing input even while minimized or waiting for a resize.
        processEvents(deltaSeconds);
        if (!running_) {
            break;
        }

        updateMovement(deltaSeconds);

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

void Application::updateMovement(float deltaSeconds)
{
    if (SDL_GetKeyboardFocus() != window_->nativeHandle() ||
        (SDL_GetWindowFlags(window_->nativeHandle()) & SDL_WINDOW_MINIMIZED) != 0) {
        return;
    }
    // Event polling has refreshed SDL's keyboard state for this frame.
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const float zDirection = static_cast<float>(keys[SDL_SCANCODE_W]) -
                            static_cast<float>(keys[SDL_SCANCODE_S]);

    const float xDirection = static_cast<float>(keys[SDL_SCANCODE_D]) -
                             static_cast<float>(keys[SDL_SCANCODE_A]);

    glm::vec3 direction{xDirection, 0.0f, zDirection};
    if (glm::dot(direction, direction) > 0.0f) {
        direction = glm::normalize(direction);
    }
    scene_->moveCamera(direction * movementSpeed_ * deltaSeconds);
}

void Application::handleEvent(const SDL_Event& event, float deltaSeconds)
{
    float xrel;
    float yrel;

    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if ((event.motion.state & SDL_BUTTON_LMASK) == 0) {
            xrel = event.motion.xrel * cameraSensitivity_;
            yrel = event.motion.yrel * cameraSensitivity_;
            scene_->rotateCamera(xrel, yrel);
        } else {
            scene_->rotateSelected({-event.motion.yrel * deltaSeconds, event.motion.xrel * deltaSeconds});
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event.key.key == SDLK_PLUS || event.key.key == SDLK_KP_PLUS) {
            scene_->selectNext();
        } else if (event.key.key == SDLK_MINUS || event.key.key == SDLK_KP_MINUS) {
            scene_->selectPrevious();
        }

        if (event.key.key == SDLK_ESCAPE) {
            running_ = false;
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
