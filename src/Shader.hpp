#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>

// Compiles a Slang module and owns its Vulkan shader module.
// The device must outlive this object. Entry points are selected by the pipeline.
class Shader {
public:
    Shader(VkDevice device, const std::filesystem::path& path);
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    VkShaderModule module() const { return module_; }

private:
    VkDevice device_;
    VkShaderModule module_{VK_NULL_HANDLE};
};
