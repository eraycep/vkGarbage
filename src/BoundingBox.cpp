#include "BoundingBox.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

std::optional<float> BoundingBox::intersectRay(const glm::vec3& origin, const glm::vec3& direction) const
{
    float enter = 0.0f;
    float exit = std::numeric_limits<float>::infinity();
    if (glm::dot(direction, direction) == 0.0f) return std::nullopt;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis]) ||
            !std::isfinite(min[axis]) || !std::isfinite(max[axis]) || min[axis] > max[axis])
            return std::nullopt;
        if (direction[axis] == 0.0f) {
            if (origin[axis] < min[axis] || origin[axis] > max[axis]) return std::nullopt;
            continue;
        }
        float a = (min[axis] - origin[axis]) / direction[axis];
        float b = (max[axis] - origin[axis]) / direction[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        exit = std::min(exit, b);
        if (enter > exit) return std::nullopt;
    }
    return enter; // A ray starting inside the box hits at zero.
}
