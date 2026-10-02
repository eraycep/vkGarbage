#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Light.hpp"
#include "SceneObject.hpp"

#include <span>
#include <array>
#include <cstdint>
#include <optional>

class Assets;

// CPU scene state, independent of SDL and Vulkan resource management.
class Scene {
public:
    static constexpr std::uint32_t objectCount = 4;

    // Matches the existing shader data layout in assets/shader.slang.
    struct ShaderData {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 model[objectCount]{};
        glm::mat4 normalMatrix[objectCount]{};
        // RGB tint, W texture index (-1 for untextured objects).
        glm::vec4 objectAppearance[objectCount]{};
        glm::vec4 lightPos{0.0f, -10.0f, 10.0f, 0.0f};
        glm::vec4 lightColorIntensity{1.0f, 1.0f, 1.0f, 1.0f};
        // View-space direction (xyz), cosine of the outer half angle (w).
        glm::vec4 lightDirectionOuterCos{};
        // World space to light clip space; depth is in [0, 1].
        glm::mat4 lightViewProjection{1.0f};
        // x: cosine of the inner half angle; yzw reserved.
        glm::vec4 lightCone{};
        std::uint32_t selected{1};
    };

    Scene();
    SceneObject& object(std::size_t index) { return objects_.at(index); }
    SpotLight& light() { return lights_[0]; }
    std::vector<SpotLight> lights() { return std::vector<SpotLight>(lights_.begin(), lights_.end()); }
    std::span<const SceneObject> objects() const { return objects_; }
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
    ShaderData shaderData(float aspectRatio) const;
    glm::vec3 cameraPosition() const;

private:
    glm::vec3 cameraPosition_{0.0f, 0.0f, 6.0f};
    glm::vec3 cameraFront_{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    glm::vec3 worldUp_{0.0f, 1.0f, 0.0f};
    std::array<SceneObject, objectCount> objects_{};
    std::array<SpotLight, 1> lights_;
    std::uint32_t selectedObject_{1};
    float fieldOfViewDegrees_{45.0f};
    float nearPlane_{0.1f};
    float farPlane_{32.0f};

    float yaw{-90.0f};
    float pitch{0.0f};
    float fov;
};
