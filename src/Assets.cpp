#include "Assets.hpp"
#include "Common.hpp"
#include "VulkanContext.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include <ktx.h>
#include <ktxvulkan.h>
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
    loadMesh(directory / "suzanne.obj");
    for (std::size_t i = 0; i < textures_.size(); ++i)
    {
        textures_[i] = loadTexture(directory / ("suzanne" + std::to_string(i) + ".ktx"));
    }
}

void Assets::loadMesh(const std::filesystem::path &path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    chk(tinyobj::LoadObj(&attrib, &shapes, &materials, nullptr, nullptr, path.c_str()));
    const VkDeviceSize indexCount{shapes[0].mesh.indices.size()};
    std::vector<Vertex> vertices{};
    std::vector<uint16_t> indices{};

    // load vertex and index data
    for (auto &index : shapes[0].mesh.indices)
    {
        Vertex v{
            .position = {attrib.vertices[index.vertex_index * 3], -attrib.vertices[index.vertex_index * 3 + 1],
                         attrib.vertices[index.vertex_index * 3 + 2]},
            .normal = {attrib.normals[index.normal_index * 3], -attrib.normals[index.normal_index * 3 + 1],
                       attrib.normals[index.normal_index * 3 + 2]},
            .uv = {attrib.texcoords[index.texcoord_index * 2], 1.0 - attrib.texcoords[index.texcoord_index * 2 + 1]}};
        vertices.push_back(v);
        indices.push_back(indices.size());
    }
    VkDeviceSize vBufSize{sizeof(Vertex) * vertices.size()};
    VkDeviceSize iBufSize{sizeof(uint16_t) * indices.size()};
    VkBufferCreateInfo bufferCI{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                .size = vBufSize + iBufSize,
                                .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT};
    VmaAllocationCreateInfo vBufferAllocCI{.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                                    VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                                                    VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                           .usage = VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo vBufferAllocInfo{};
    chk(vmaCreateBuffer(context_.allocator(), &bufferCI, &vBufferAllocCI, &mesh_.buffer, &mesh_.allocation,
                        &vBufferAllocInfo));
    memcpy(vBufferAllocInfo.pMappedData, vertices.data(), vBufSize);
    memcpy((char *)vBufferAllocInfo.pMappedData + vBufSize, indices.data(), iBufSize);
    chk(vmaFlushAllocation(context_.allocator(), mesh_.allocation, 0, VK_WHOLE_SIZE));
    mesh_.indexOffset = vBufSize;
    mesh_.indexCount = static_cast<uint32_t>(indexCount);
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
    vkDestroyShaderModule(context_.device(), shaderModule_, nullptr);
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
