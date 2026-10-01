#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/glm.hpp>
#include "Shader.hpp"
#include <memory>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

class VulkanContext;

// Owns the tutorial's mesh, textures, shader module, and upload commands.
class Assets {
public:
    struct Vertex {
        glm::vec3 position{};
        glm::vec3 normal{};
        glm::vec2 uv{};
    };

    struct Mesh {
        VkBuffer buffer{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkDeviceSize indexOffset{0}; // Vertex and index data share one buffer.
        std::uint32_t indexCount{0};
        VkIndexType indexType{VK_INDEX_TYPE_UINT32};
    };

    struct Texture {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkSampler sampler{VK_NULL_HANDLE};
    };

    explicit Assets(VulkanContext& context);
    ~Assets();
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;

    // Load once before Renderer construction; resources stay alive during rendering.
    void load(const std::filesystem::path& directory = "assets");
    const Mesh& mesh() const;
    std::span<const Texture> textures() const;
    VkShaderModule shaderModule() const;

private:
    void loadMesh(const std::filesystem::path& path);
    Texture loadTexture(const std::filesystem::path& path);
    VkCommandBuffer beginUpload();
    void submitUploadAndWait(VkCommandBuffer commandBuffer);
    void cleanup();

    VulkanContext& context_; // Non-owning; context outlives Assets.
    Mesh mesh_{};
    std::array<Texture, 3> textures_{};
    std::unique_ptr<Shader> shader_;
    VkCommandPool uploadCommandPool_{VK_NULL_HANDLE};
};
