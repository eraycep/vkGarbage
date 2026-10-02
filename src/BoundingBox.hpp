#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>

struct BoundingBox {
    glm::vec3 min;
    glm::vec3 max;

    std::optional<float> intersectRay(
        const glm::vec3& origin,
        const glm::vec3& direction) const;
};