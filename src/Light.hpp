#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct PointLight {
    glm::vec3 position{0.0f, -10.0f, 10.0f};
    float intensity{1.0f};
    glm::vec3 color{1.0f};
};