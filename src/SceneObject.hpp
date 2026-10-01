#pragma once

#include <glm/glm.hpp>
#include <cstdint>

enum class MeshId : std::uint32_t { Suzanne, Plane, Count };

struct SceneObject {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f}; // Euler angles in radians.
    glm::vec3 scale{1.0f};   // Keep components nonzero.
    MeshId mesh{MeshId::Suzanne};
    std::int32_t textureIndex{-1}; // -1 uses color without a texture.
    glm::vec3 color{1.0f};
    bool selectable{true};
};
