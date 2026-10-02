#include "Application.hpp"
#include "Assets.hpp"
#include "Renderer.hpp"
#include "Scene.hpp"
#include "VulkanContext.hpp"
#include "Window.hpp"
#include "Light.hpp"
#include "ShadowMap.hpp"
#include "EditorUi.hpp"

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
    shadowMap_ = std::make_unique<ShadowMap>(*context_);
    assets_ = std::make_unique<Assets>(*context_);
    assets_->load();
    scene_ = std::make_unique<Scene>();
    renderer_ = std::make_unique<Renderer>(*context_, *window_, *assets_, *shadowMap_);
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
        processEvents();
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

void Application::processEvents()
{
    SDL_Event event;
    while (running_ && window_->pollEvent(event)) {
        renderer_->ui().processEvent(event);
        handleEvent(event);
    }
}

void Application::updateMovement(float deltaSeconds)
{
    if (renderer_->ui().wantsKeyboard()) return;
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

void Application::handleEvent(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // Future picking/dragging belongs here. UI mouse input stays with ImGui.
        if (!renderer_->ui().wantsMouse() && (event.motion.state & SDL_BUTTON_LMASK) != 0) {
            scene_->rotateSelected({-event.motion.yrel * 0.005f, event.motion.xrel * 0.005f});
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button != SDL_BUTTON_LEFT || renderer_->ui().wantsMouse()) {
            break;
        } else {
            int windowWidth, windowHeight;
            SDL_GetWindowSize(window_->nativeHandle(), &windowWidth, &windowHeight);

            const VkExtent2D extent = window_->framebufferExtent();
            if (windowWidth <= 0 || windowHeight <= 0 ||
                extent.width == 0 || extent.height == 0) {
                break;
            }

            const float aspectRatio =
                static_cast<float>(extent.width) / extent.height;

            const auto data = scene_->shaderData(aspectRatio);
            const glm::mat4 inverseVP =
                glm::inverse(data.projection * data.view);

            const float x = 2.0f * event.button.x / windowWidth - 1.0f;
            const float y = 2.0f * event.button.y / windowHeight - 1.0f;

            glm::vec4 nearPoint = inverseVP * glm::vec4(x, y, 0.0f, 1.0f);
            glm::vec4 farPoint  = inverseVP * glm::vec4(x, y, 1.0f, 1.0f);

            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;

            const glm::vec3 rayOrigin = glm::vec3(nearPoint);
            const glm::vec3 rayDirection =
                glm::normalize(glm::vec3(farPoint - nearPoint));

            if (const auto hit =
                    scene_->pickObject(rayOrigin, rayDirection, *assets_)) {
                scene_->selectObject(*hit);
            }
            break;
        }
    case SDL_EVENT_KEY_DOWN:
        if (renderer_->ui().wantsKeyboard()) break;
        if (event.key.key == SDLK_RIGHT) {
            scene_->selectNext();
        } else if (event.key.key == SDLK_LEFT) {
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
    shadowMap_.reset();
    scene_.reset();
    assets_.reset();
    context_.reset();
    window_.reset();
}
