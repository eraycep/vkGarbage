#include "Assets.hpp"
#include "Common.hpp"
#include "VulkanContext.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <ktx.h>
#include <ktxvulkan.h>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Assets::Assets(VulkanContext &context) : context_(context)
{
    VkCommandPoolCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = context_.graphicsQueueFamily(),
    };
    chk(vkCreateCommandPool(context_.device(), &info, nullptr, &uploadCommandPool_));
}

Assets::~Assets()
{
    context_.waitIdle();
    cleanup();
}

void Assets::load(const std::filesystem::path &directory)
{
    chk(mesh_.buffer == VK_NULL_HANDLE); // load() is a one-time operation.
    shader_ = std::make_unique<Shader>(context_.device(), directory / "shader.slang");
    loadMesh(directory / "suzanne.obj");
    for (std::size_t i = 0; i < textures_.size(); ++i)
    {
        textures_[i] = loadTexture(directory / ("suzanne" + std::to_string(i) + ".ktx"));
    }
}

namespace {
struct MeshData {
    std::vector<Assets::Vertex> vertices;
    std::vector<std::uint32_t> indices;
    struct BoundingBox boundingBox;
};

MeshData loadMeshData(const std::filesystem::path& path)
{
    const auto filename = path.string();
    auto fail = [&filename](const std::string& reason) {
        throw std::runtime_error("Could not load mesh '" + filename + "': " + reason);
    };
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warning, error;
    auto materialDirectory = path.parent_path().string();
    if (!materialDirectory.empty()) {
        materialDirectory += std::filesystem::path::preferred_separator;
    }
    const bool loaded = tinyobj::LoadObj(&attrib, &shapes, &materials, &warning, &error,
                                       filename.c_str(), materialDirectory.c_str(), true);
    if (!warning.empty()) {
        std::cerr << "Mesh '" << filename << "': " << warning << '\n';
    }
    if (!loaded || !error.empty()) {
        fail(error.empty() ? "OBJ parser failed" : error);
    }
    if (attrib.vertices.size() % 3 != 0 || attrib.normals.size() % 3 != 0 || attrib.texcoords.size() % 2 != 0) {
        fail("incomplete vertex attribute data");
    }
    std::size_t totalIndices = 0;
    for (const auto& shape : shapes) {
        if (shape.mesh.indices.size() % 3 != 0 ||
            shape.mesh.num_face_vertices.size() != shape.mesh.indices.size() / 3 ||
            std::any_of(shape.mesh.num_face_vertices.begin(), shape.mesh.num_face_vertices.end(),
                        [](auto count) { return count != 3; })) {
            fail("shape '" + shape.name + "' is not a triangulated mesh");
        }
        if (shape.mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() - totalIndices) {
            fail("too many indices for a 32-bit draw count");
        }
        totalIndices += shape.mesh.indices.size();
    }
    if (totalIndices == 0) {
        fail("no triangles found");
    }
    // One expanded vertex and one sequential index per OBJ face corner.
    constexpr auto bytesPerCorner = sizeof(Assets::Vertex) + sizeof(std::uint32_t);
    if (totalIndices > std::numeric_limits<std::size_t>::max() / bytesPerCorner ||
        totalIndices > std::numeric_limits<VkDeviceSize>::max() / bytesPerCorner) {
        fail("mesh buffer size overflows");
    }
    MeshData data;
    data.vertices.reserve(totalIndices);
    data.indices.reserve(totalIndices);
    auto validIndex = [](int index, std::size_t count) {
        return index >= 0 && static_cast<std::size_t>(index) < count;
    };
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            const auto location = "shape '" + shape.name + "', corner " + std::to_string(data.indices.size());
            if (!validIndex(index.vertex_index, attrib.vertices.size() / 3)) {
                fail(location + ": missing or invalid position index " + std::to_string(index.vertex_index));
            }
            // Normals and UVs are required by this textured, lit renderer.
            if (!validIndex(index.normal_index, attrib.normals.size() / 3)) {
                fail(location + ": missing or invalid normal index " + std::to_string(index.normal_index));
            }
            if (!validIndex(index.texcoord_index, attrib.texcoords.size() / 2)) {
                fail(location + ": missing or invalid UV index " + std::to_string(index.texcoord_index));
            }
            const auto position = static_cast<std::size_t>(index.vertex_index) * 3;
            const auto normal = static_cast<std::size_t>(index.normal_index) * 3;
            const auto uv = static_cast<std::size_t>(index.texcoord_index) * 2;
            Assets::Vertex vertex{
                .position = {attrib.vertices[position], -attrib.vertices[position + 1], attrib.vertices[position + 2]},
                .normal = {attrib.normals[normal], -attrib.normals[normal + 1], attrib.normals[normal + 2]},
                .uv = {attrib.texcoords[uv], 1.0f - attrib.texcoords[uv + 1]},
            };
            if (data.vertices.empty()) {
                data.boundingBox = {vertex.position, vertex.position};
            } else {
                data.boundingBox.min = glm::min(data.boundingBox.min, vertex.position);
                data.boundingBox.max = glm::max(data.boundingBox.max, vertex.position);
            }
            for (float value : {vertex.position.x, vertex.position.y, vertex.position.z,
                                vertex.normal.x, vertex.normal.y, vertex.normal.z, vertex.uv.x, vertex.uv.y}) {
                if (!std::isfinite(value)) {
                    fail(location + ": non-finite vertex attribute");
                }
            }
            data.indices.push_back(static_cast<std::uint32_t>(data.vertices.size()));
            data.vertices.push_back(vertex);
        }
    }
    return data;
}
} // namespace

void Assets::loadMesh(const std::filesystem::path& path)
{
    // Finish all parsing and validation before allocating GPU resources.
    auto data = loadMeshData(path);
    const auto meshIndexCount = static_cast<std::uint32_t>(data.indices.size());
    const auto floorVertex = static_cast<std::uint32_t>(data.vertices.size());
    // This scene uses +Y downward. The floor faces upward, toward -Y.
    data.vertices.insert(data.vertices.end(), {
        {{-1.0f, 0.0f, -1.0f}, {0, -1, 0}, {0, 0}},
        {{ 1.0f, 0.0f, -1.0f}, {0, -1, 0}, {1, 0}},
        {{ 1.0f, 0.0f,  1.0f}, {0, -1, 0}, {1, 1}},
        {{-1.0f, 0.0f,  1.0f}, {0, -1, 0}, {0, 1}}
    });
    for (std::uint32_t index : {0u, 1u, 2u, 0u, 2u, 3u}) {
        data.indices.push_back(floorVertex + index);
    }
    const VkDeviceSize vertexBytes = sizeof(Vertex) * data.vertices.size();
    const VkDeviceSize indexBytes = sizeof(std::uint32_t) * data.indices.size();
    VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertexBytes + indexBytes,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    };
    VmaAllocationCreateInfo allocationInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    VmaAllocationInfo mappedInfo{};
    chk(vmaCreateBuffer(context_.allocator(), &bufferInfo, &allocationInfo,
                        &mesh_.buffer, &mesh_.allocation, &mappedInfo));
    std::memcpy(mappedInfo.pMappedData, data.vertices.data(), static_cast<std::size_t>(vertexBytes));
    std::memcpy(static_cast<char*>(mappedInfo.pMappedData) + vertexBytes,
                data.indices.data(), static_cast<std::size_t>(indexBytes));
    chk(vmaFlushAllocation(context_.allocator(), mesh_.allocation, 0, VK_WHOLE_SIZE));
    mesh_.indexOffset = vertexBytes;
    mesh_.ranges[static_cast<std::size_t>(MeshId::Suzanne)] = {0, meshIndexCount, data.boundingBox};
    mesh_.ranges[static_cast<std::size_t>(MeshId::Plane)] = {meshIndexCount, 6, {{-1, 0, -1}, {1, 0, 1}}};
    mesh_.indexType = VK_INDEX_TYPE_UINT32;
}

Assets::Texture Assets::loadTexture(const std::filesystem::path &path)
{
    Texture texture{};
    ktxTexture *source{nullptr};
    const auto filename = path.string();
    const auto result =
        ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &source);
    if (result != KTX_SUCCESS || source == nullptr)
    {
        std::cerr << "Could not load texture " << filename << " (KTX error " << result << ")\n";
        std::exit(EXIT_FAILURE);
    }
    std::unique_ptr<ktxTexture, decltype(&ktxTexture_Destroy)> sourceOwner(source, &ktxTexture_Destroy);
    // This loader handles one ordinary 2D color texture, with mip data in the file.
    chk(source->numDimensions == 2 && !source->isArray && !source->isCubemap && source->numFaces == 1 &&
        source->numLevels > 0 && source->numLevels <= 32 && source->baseWidth > 0 && source->baseHeight > 0 &&
        source->pData != nullptr && source->dataSize > 0);
    const VkFormat format = ktxTexture_GetVkFormat(source);
    chk(format != VK_FORMAT_UNDEFINED);
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.physicalDevice(), format, &properties);
    constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    chk((properties.optimalTilingFeatures & required) == required);
    VkImageCreateInfo texImgCI{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = format,
                               .extent = {.width = source->baseWidth, .height = source->baseHeight, .depth = 1},
                               .mipLevels = source->numLevels,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VmaAllocationCreateInfo texImageAllocCI{.usage = VMA_MEMORY_USAGE_AUTO};
    chk(vmaCreateImage(context_.allocator(), &texImgCI, &texImageAllocCI, &texture.image, &texture.allocation,
                       nullptr));
    VkImageViewCreateInfo texImgViewCI{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                       .image = texture.image,
                                       .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                       .format = texImgCI.format,
                                       .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                            .levelCount = source->numLevels,
                                                            .layerCount = 1}};
    chk(vkCreateImageView(context_.device(), &texImgViewCI, nullptr, &texture.view));

    VkBuffer imgSrcBuffer{};
    VmaAllocation imgSrcAllocation{};
    VkBufferCreateInfo imgSrcBufferCI{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                      .size = static_cast<VkDeviceSize>(source->dataSize),
                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VmaAllocationCreateInfo imgSrcAllocCI{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                   VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo imgSrcAllocInfo{};
    chk(vmaCreateBuffer(context_.allocator(), &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation,
                        &imgSrcAllocInfo));
    memcpy(imgSrcAllocInfo.pMappedData, source->pData, source->dataSize);
    chk(vmaFlushAllocation(context_.allocator(), imgSrcAllocation, 0, VK_WHOLE_SIZE));
    VkCommandBuffer cbOneTime = beginUpload();
    VkImageMemoryBarrier2 barrierTexImage{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                          .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
                                          .srcAccessMask = VK_ACCESS_2_NONE,
                                          .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                          .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                          .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                          .image = texture.image,
                                          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                               .levelCount = source->numLevels,
                                                               .layerCount = 1}};
    VkDependencyInfo barrierTexInfo{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                    .imageMemoryBarrierCount = 1,
                                    .pImageMemoryBarriers = &barrierTexImage};
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    std::vector<VkBufferImageCopy> copyRegions{};
    for (uint32_t j = 0; j < source->numLevels; j++)
    {
        ktx_size_t mipOffset{0};
        KTX_error_code ret = ktxTexture_GetImageOffset(source, j, 0, 0, &mipOffset);
        chk(ret == KTX_SUCCESS);
        uint32_t rowLength{0};
        if (!source->isCompressed)
        {
            const auto elementSize = ktxTexture_GetElementSize(source);
            const auto rowPitch = ktxTexture_GetRowPitch(source, j);
            // Vulkan expresses row stride in texels. Reject unrepresentable KTX padding.
            if (elementSize == 0 || rowPitch % elementSize != 0)
            {
                std::cerr << "Unsupported padded row layout in " << filename << "\n";
                std::exit(EXIT_FAILURE);
            }
            rowLength = rowPitch / elementSize;
        }
        copyRegions.push_back({
            .bufferOffset = mipOffset,
            .bufferRowLength = rowLength,
            .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = (uint32_t)j, .layerCount = 1},
            .imageExtent{.width = std::max(1u, source->baseWidth >> j),
                         .height = std::max(1u, source->baseHeight >> j),
                         .depth = 1},
        });
    }
    vkCmdCopyBufferToImage(cbOneTime, imgSrcBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
    VkImageMemoryBarrier2 barrierTexRead{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                                         .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                         .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                         .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                         .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                                         .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .image = texture.image,
                                         .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                              .levelCount = source->numLevels,
                                                              .layerCount = 1}};
    barrierTexInfo.pImageMemoryBarriers = &barrierTexRead;
    vkCmdPipelineBarrier2(cbOneTime, &barrierTexInfo);
    submitUploadAndWait(cbOneTime);
    vmaDestroyBuffer(context_.allocator(), imgSrcBuffer, imgSrcAllocation);

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = std::min(8.0f, context_.deviceProperties().properties.limits.maxSamplerAnisotropy),
        .maxLod = static_cast<float>(source->numLevels - 1),
    };
    chk(vkCreateSampler(context_.device(), &samplerCI, nullptr, &texture.sampler));
    return texture;
}

VkCommandBuffer Assets::beginUpload()
{
    VkCommandBuffer cbOneTime{};
    VkCommandBufferAllocateInfo cbOneTimeAI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                            .commandPool = uploadCommandPool_,
                                            .commandBufferCount = 1};
    chk(vkAllocateCommandBuffers(context_.device(), &cbOneTimeAI, &cbOneTime));
    VkCommandBufferBeginInfo cbOneTimeBI{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    chk(vkBeginCommandBuffer(cbOneTime, &cbOneTimeBI));
    return cbOneTime;
}

void Assets::submitUploadAndWait(VkCommandBuffer commandBuffer)
{
    chk(vkEndCommandBuffer(commandBuffer));
    VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence{VK_NULL_HANDLE};
    chk(vkCreateFence(context_.device(), &fenceInfo, nullptr, &fence));
    VkCommandBufferSubmitInfo commandInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commandBuffer,
    };
    VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
    };
    chk(vkQueueSubmit2(context_.graphicsQueue(), 1, &submitInfo, fence));
    chk(vkWaitForFences(context_.device(), 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(context_.device(), fence, nullptr);
    vkFreeCommandBuffers(context_.device(), uploadCommandPool_, 1, &commandBuffer);
}

void Assets::cleanup()
{
    for (const auto &texture : textures_)
    {
        vkDestroySampler(context_.device(), texture.sampler, nullptr);
        vkDestroyImageView(context_.device(), texture.view, nullptr);
        vmaDestroyImage(context_.allocator(), texture.image, texture.allocation);
    }
    vmaDestroyBuffer(context_.allocator(), mesh_.buffer, mesh_.allocation);
    shader_.reset();
    vkDestroyCommandPool(context_.device(), uploadCommandPool_, nullptr);
}

const Assets::Mesh &Assets::mesh() const
{
    return mesh_;
}

std::span<const Assets::Texture> Assets::textures() const
{
    return textures_;
}

VkShaderModule Assets::shaderModule() const
{
    return shader_ ? shader_->module() : VK_NULL_HANDLE;
}
