#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>

using MeshHandle = std::uint32_t;
using MaterialHandle = std::uint32_t;
using TextureHandle = std::uint32_t;
inline constexpr std::uint32_t invalidHandle = std::numeric_limits<std::uint32_t>::max();

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // Radians.
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const {
        return glm::translate(glm::mat4(1.0f), position) *
            glm::mat4_cast(glm::quat(rotation)) *
            glm::scale(glm::mat4(1.0f), scale);
    }
};

struct RenderObject {
    MeshHandle mesh{invalidHandle};
    MaterialHandle material{invalidHandle};
    Transform transform;
    bool selectable{true};
};
