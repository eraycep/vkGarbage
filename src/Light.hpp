#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct SpotLight {
    glm::vec3 position{0.0f, -10.0f, 10.0f};
    float intensity{1.0f};
    glm::vec3 color{1.0f};
    // World-space direction from the light toward the scene.
    glm::vec3 direction{glm::normalize(glm::vec3{0.0f, 10.0f, -10.0f})};
    // Half angles: 0 < inner < outer < 90 degrees.
    float innerConeDegrees{15.0f};
    float outerConeDegrees{25.0f};
    // Distances from the light, independent of the camera's clipping planes.
    float shadowNearPlane{0.1f};
    float shadowFarPlane{32.0f};
};
