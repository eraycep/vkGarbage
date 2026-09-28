#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

// CPU scene state, independent of SDL and Vulkan resource management.
class Scene {
public:
    static constexpr std::uint32_t objectCount = 3;

    // Matches the existing shader data layout in assets/shader.slang.
    struct ShaderData {
        glm::mat4 projection{1.0f};
        glm::mat4 view{1.0f};
        glm::mat4 model[objectCount]{};
        glm::vec4 lightPos{0.0f, -10.0f, 10.0f, 0.0f};
        std::uint32_t selected{1};
    };

    Scene();
    void rotateSelected(const glm::vec2& deltaRadians);
    void moveCamera(float deltaZ);
    void selectNext();
    void selectPrevious();
    std::uint32_t selectedObject() const;
    ShaderData shaderData(float aspectRatio) const;

private:
    glm::vec3 cameraPosition_{0.0f, 0.0f, -6.0f};
    std::array<glm::vec3, objectCount> objectPositions_{};
    std::array<glm::vec3, objectCount> objectRotations_{};
    glm::vec4 lightPosition_{0.0f, -10.0f, 10.0f, 0.0f};
    std::uint32_t selectedObject_{1};
    float fieldOfViewDegrees_{45.0f};
    float nearPlane_{0.1f};
    float farPlane_{32.0f};
};
