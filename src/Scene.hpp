#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Light.hpp"
#include "SceneObject.hpp"
#include "GPUData.hpp"

#include <span>
#include <vector>
#include <array>
#include <cstdint>
#include <optional>

class Assets;

// CPU scene state, independent of SDL and Vulkan resource management.
class Scene {
public:
    // Values match the debug branches in assets/shader.slang.
    enum class DebugMode : std::uint32_t { Lit = 0, Normals = 1, ShadowVisibility = 2, ShadowDepth = 3 };

    explicit Scene(const Assets& assets);
    std::span<const RenderObject> renderObjects() const { return renderObjects_; }
    RenderObject& renderObject(std::size_t index) { return renderObjects_.at(index); }
    DebugMode debugMode() const { return debugMode_; }
    void setDebugMode(DebugMode mode) { debugMode_ = mode; }
    SpotLight& light() { return lights_[0]; }
    std::vector<SpotLight> lights() { return std::vector<SpotLight>(lights_.begin(), lights_.end()); }
    void rotateSelected(const glm::vec2& deltaRadians);
    void rotateCamera(float x, float y);
    void moveCamera(float forwardDistance);
    // Camera-local displacement: right, up, forward.
    void moveCamera(glm::vec3 delta);
    void selectNext();
    void selectPrevious();
    void selectObject(uint32_t index);
    std::optional<uint32_t> pickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Assets& assets) const;
    std::uint32_t selectedObject() const;
    FrameData frameData(float aspectRatio) const;
    glm::vec3 cameraPosition() const;

    std::vector<GPUObject> gpuObjects(const glm::mat4& view) const;
    std::vector<GPUMaterial> gpuMaterials(const Assets& assets) const;

private:
    glm::vec3 cameraPosition_{0.0f, 0.0f, 6.0f};
    glm::vec3 cameraFront_{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};
    std::vector<RenderObject> renderObjects_{};
    std::array<SpotLight, 1> lights_;
    std::uint32_t selectedObject_{1};
    DebugMode debugMode_{DebugMode::Lit};
    float fieldOfViewDegrees_{45.0f};
    float nearPlane_{0.1f};
    float farPlane_{32.0f};

    float yaw{-90.0f};
    float pitch{0.0f};
    float fov;
};
