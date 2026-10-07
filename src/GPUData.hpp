#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// GPU transfer records. Mirror field order AND padding in assets/common.slang.
// Matrices use the existing column-major Slang compilation setting.
struct alignas(16) GPUObject {
    glm::mat4 model{1.0f};
    // Inverse transpose of view * model, preserving current view-space lighting.
    glm::mat4 normalMatrix{1.0f};
    glm::vec4 color{1.0f}; // Per-instance tint, multiplied by material baseColor.
    std::uint32_t materialIndex{0};
    std::uint32_t padding[3]{};
};

struct alignas(16) GPUMaterial {
    glm::vec4 baseColor{1.0f};
    // RGB: specular color * strength; W: positive shininess.
    glm::vec4 specularShininess{0.75f, 0.75f, 0.75f, 16.0f};
    std::int32_t textureIndex{-1}; // -1 means no texture.
    std::uint32_t padding[3]{};
};

struct alignas(16) FrameData {
    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    glm::mat4 lightViewProjection{1.0f}; // World space to light clip space.
    glm::vec4 cameraPosition{0.0f, 0.0f, 6.0f, 1.0f}; // World space.
    glm::vec4 lightPos{0.0f, -10.0f, 10.0f, 1.0f}; // View space after packing.
    glm::vec4 lightColorIntensity{1.0f};
    glm::vec4 lightDirectionOuterCos{}; // View-space direction, cosine outer angle.
    glm::vec4 lightCone{}; // x: cosine inner angle, yzw reserved.
    VkDeviceAddress objectBuffer{0};
    VkDeviceAddress materialBuffer{0};
    std::uint32_t objectCount{0};
    std::uint32_t materialCount{0};
    std::uint32_t selected{std::numeric_limits<std::uint32_t>::max()}; // No selection.
    std::uint32_t debugMode{0};
    float shadowNearPlane{0.1f};
    float shadowFarPlane{32.0f};
    std::uint32_t padding[2]{};
};

static_assert(sizeof(VkDeviceAddress) == 8);
static_assert(std::is_standard_layout_v<GPUObject> && std::is_trivially_copyable_v<GPUObject>);
static_assert(std::is_standard_layout_v<GPUMaterial> && std::is_trivially_copyable_v<GPUMaterial>);
static_assert(std::is_standard_layout_v<FrameData> && std::is_trivially_copyable_v<FrameData>);
static_assert(sizeof(GPUObject) == 160);
static_assert(offsetof(GPUObject, normalMatrix) == 64);
static_assert(offsetof(GPUObject, color) == 128);
static_assert(offsetof(GPUObject, materialIndex) == 144);
static_assert(sizeof(GPUMaterial) == 48);
static_assert(offsetof(GPUMaterial, specularShininess) == 16);
static_assert(offsetof(GPUMaterial, textureIndex) == 32);
static_assert(sizeof(FrameData) == 320);
static_assert(offsetof(FrameData, view) == 64);
static_assert(offsetof(FrameData, lightViewProjection) == 128);
static_assert(offsetof(FrameData, cameraPosition) == 192);
static_assert(offsetof(FrameData, lightPos) == 208);
static_assert(offsetof(FrameData, lightColorIntensity) == 224);
static_assert(offsetof(FrameData, lightDirectionOuterCos) == 240);
static_assert(offsetof(FrameData, lightCone) == 256);
static_assert(offsetof(FrameData, objectBuffer) == 272);
static_assert(offsetof(FrameData, materialBuffer) == 280);
static_assert(offsetof(FrameData, objectCount) == 288);
static_assert(offsetof(FrameData, selected) == 296);
static_assert(offsetof(FrameData, debugMode) == 300);
static_assert(offsetof(FrameData, shadowNearPlane) == 304);
static_assert(offsetof(FrameData, shadowFarPlane) == 308);
