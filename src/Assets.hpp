#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "Shader.hpp"
#include "SceneObject.hpp"
#include "BoundingBox.hpp"

#include <memory>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

class VulkanContext;

// Owns meshes, materials, textures, the shader module, and upload commands.
class Assets {
public:
    struct Vertex {
        glm::vec3 position{};
        glm::vec3 normal{};
        glm::vec2 uv{};
    };

    struct DrawRange {
        std::uint32_t firstIndex{0};
        std::uint32_t indexCount{0};
        BoundingBox boundingBox{};
    };

    struct Mesh {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkDeviceSize indexOffset{0}; // Vertex and index data share one buffer.
        DrawRange range{};
        VkIndexType indexType{VK_INDEX_TYPE_UINT32};
    };

    struct Texture {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
    };

    struct Material {
        TextureHandle baseColorTexture{invalidHandle};
        TextureHandle normalTexture{invalidHandle}; // Reserved for future shading.
        TextureHandle metallicRoughnessTexture{invalidHandle};
        glm::vec4 baseColorFactor{1.0f};
        float metallic{0.0f};
        float roughness{0.5f};
        // Current Phong shader properties; metallic/roughness are not used yet.
        glm::vec3 specularColor{1.0f};
        float specularStrength{0.75f};
        float shininess{16.0f};
    };

    // Startup handles: values are returned by loaders, not assumed vector indices.
    struct DefaultSceneAssets {
        MeshHandle suzanne{invalidHandle};
        MeshHandle plane{invalidHandle};
        std::array<MaterialHandle, 3> monkeyMaterials{};
        MaterialHandle floorMaterial{invalidHandle};
    };

    explicit Assets(VulkanContext& context);
    ~Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    // Load once before Renderer construction; resources stay alive during rendering.
    void load(const std::filesystem::path& directory = "assets");
    const DefaultSceneAssets& defaultSceneAssets() const { return defaultSceneAssets_; }
    // Append-only handle APIs. Load textures before descriptor creation.
    MeshHandle loadMesh(const std::filesystem::path& path);
    MeshHandle createPlane();
    TextureHandle loadTexture(const std::filesystem::path& path);
    MaterialHandle addMaterial(const Material& material);
    Material& material(MaterialHandle index);
    std::span<const Material> materials() const { return materials_; }
    const Mesh& mesh(MeshHandle index) const;
    const Material& material(MaterialHandle index) const;
    const Texture& texture(TextureHandle index) const;
    std::span<const Texture> textures() const;
    VkShaderModule shaderModule() const;

private:
    VkCommandBuffer beginUpload();
    void submitUploadAndWait(VkCommandBuffer commandBuffer);
    void cleanup();

    VulkanContext& context_; // Non-owning; context outlives Assets.
    DefaultSceneAssets defaultSceneAssets_{};

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::vector<Texture> textures_;

    std::unique_ptr<Shader> shader_;
    VkCommandPool uploadCommandPool_{VK_NULL_HANDLE};
};
