#define VOLK_IMPLEMENTATION
#define VMA_IMPLEMENTATION
#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Window.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
const std::vector<const char*> requiredDeviceExtension = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning";
    std::cerr << "[Vulkan " << level << "] "
              << (data->pMessageIdName ? data->pMessageIdName : "validation") << ": "
              << (data->pMessage ? data->pMessage : "No message") << '\n';
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo()
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validationCallback,
    };
}

void requireValidationSupport()
{
    uint32_t count{0};
    chk(vkEnumerateInstanceLayerProperties(&count, nullptr));
    std::vector<VkLayerProperties> layers(count);
    chk(vkEnumerateInstanceLayerProperties(&count, layers.data()));
    if (std::none_of(layers.begin(), layers.end(), [](const auto& layer) {
            return std::strcmp(layer.layerName, validationLayer) == 0;
        })) {
        throw std::runtime_error("Debug build requires VK_LAYER_KHRONOS_validation. "
                                 "Install the Vulkan validation layers or configure the Vulkan SDK environment.");
    }

    std::vector<VkExtensionProperties> extensions;
    // Debug extensions can be exposed by the loader or by the validation layer.
    for (const char* layer : {static_cast<const char*>(nullptr), validationLayer}) {
        count = 0;
        chk(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr));
        std::vector<VkExtensionProperties> available(count);
        chk(vkEnumerateInstanceExtensionProperties(layer, &count, available.data()));
        extensions.insert(extensions.end(), available.begin(), available.end());
    }
    for (const char* required : {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME}) {
        if (std::none_of(extensions.begin(), extensions.end(), [required](const auto& extension) {
                return std::strcmp(extension.extensionName, required) == 0;
            })) {
            throw std::runtime_error(std::string("Debug build requires instance extension: ") + required);
        }
    }
}
} // namespace

VulkanContext::VulkanContext(const Window& window, std::uint32_t deviceIndex)
{
    createInstance(window);
    selectPhysicalDevice(deviceIndex);
    createDevice();
    createAllocator();
}

VulkanContext::~VulkanContext()
{
    cleanup();
}

void VulkanContext::createInstance(const Window& window)
{
    chk(volkInitialize());

    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vkGarbage",
        .apiVersion = VK_API_VERSION_1_3,
    };

    auto instanceExtensions = window.requiredInstanceExtensions();

    std::vector<const char*> requiredLayers;
    auto debugInfo = debugMessengerInfo();
    const VkValidationFeatureEnableEXT synchronization = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = &debugInfo,
        .enabledValidationFeatureCount = 1,
        .pEnabledValidationFeatures = &synchronization,
    };
    if (enableValidationLayers) {
        requireValidationSupport();
        requiredLayers.push_back(validationLayer);
        for (const char* extension : {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME}) {
            if (std::none_of(instanceExtensions.begin(), instanceExtensions.end(), [extension](const char* name) {
                    return std::strcmp(name, extension) == 0;
                })) {
                instanceExtensions.push_back(extension);
            }
        }
    }

    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = enableValidationLayers ? &validationFeatures : nullptr,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
        .ppEnabledLayerNames = requiredLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data()
    };

    chk(vkCreateInstance(&instanceCI, nullptr, &instance_));

    volkLoadInstance(instance_);
    setupDebugMessenger();
    surface_ = window.createSurface(instance_);
}

void VulkanContext::setupDebugMessenger()
{
    if (!enableValidationLayers) {
        return;
    }
    chk(vkCreateDebugUtilsMessengerEXT != nullptr);
    const auto info = debugMessengerInfo();
    chk(vkCreateDebugUtilsMessengerEXT(instance_, &info, nullptr, &debugMessenger_));
}

/* Select physical device (GPU) from the list, I went with 0
 * because I have a laptop with integrated GPU
 */
void VulkanContext::selectPhysicalDevice(std::uint32_t deviceIndex)
{
    bool deviceFound = false;

    uint32_t deviceCount{ 0 };
    chk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> devices(deviceCount);
    chk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    if (deviceIndex >= devices.size()) {
        std::cerr << "Requested Vulkan device index is unavailable\n";
        std::exit(EXIT_FAILURE);
    }

    for (auto i = 0; i < deviceCount; i++) {
        deviceFound = isDeviceSuitable(devices[i]);
        if (deviceFound) {
            deviceIndex = i;
            break;
        }
    }

    if (!deviceFound) {
        std::cerr << "ERROR: Failed to find suitable device\n";
        std::exit(EXIT_FAILURE);
    }

    physicalDevice_ = devices[deviceIndex];
}

// Create vulkan logical device
void VulkanContext::createDevice()
{
	const float qfpriorities{ 1.0f };
	VkDeviceQueueCreateInfo queueCI{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamily_, .queueCount = 1, .pQueuePriorities = &qfpriorities };

	VkPhysicalDeviceVulkan12Features enabledVk12Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .descriptorIndexing = true, .shaderSampledImageArrayNonUniformIndexing = true, .descriptorBindingVariableDescriptorCount = true, .runtimeDescriptorArray = true, .bufferDeviceAddress = true };
	VkPhysicalDeviceVulkan13Features enabledVk13Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &enabledVk12Features, .synchronization2 = true, .dynamicRendering = true };
	VkPhysicalDeviceFeatures enabledVk10Features{ .samplerAnisotropy = VK_TRUE };
    
    VkDeviceCreateInfo deviceCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabledVk13Features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data(),
        .pEnabledFeatures = &enabledVk10Features,
    };
    chk(vkCreateDevice(physicalDevice_, &deviceCI, nullptr, &device_));
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
}

void VulkanContext::createAllocator()
{
	VmaVulkanFunctions vkFunctions{ .vkGetInstanceProcAddr = vkGetInstanceProcAddr, .vkGetDeviceProcAddr = vkGetDeviceProcAddr, .vkCreateImage = vkCreateImage };
	VmaAllocatorCreateInfo allocatorCI{ .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, .physicalDevice = physicalDevice_, .device = device_, .pVulkanFunctions = &vkFunctions, .instance = instance_ };
	chk(vmaCreateAllocator(&allocatorCI, &allocator_));
}

void VulkanContext::cleanup()
{
    waitIdle();
    vmaDestroyAllocator(allocator_);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_ != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice const &physicalDevice)
{
    VkPhysicalDeviceProperties2 deviceProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);

    bool supportsVulkan1_3 = deviceProperties.properties.apiVersion >= VK_API_VERSION_1_3;

    uint32_t extensionCount{ 0 };
    std::vector<VkExtensionProperties> extensionProperties;
    chk(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr));
    extensionProperties.resize(extensionCount);
    chk(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensionProperties.data()));

    for (const char* required : requiredDeviceExtension) {
        bool found = false;
        for (const auto& available : extensionProperties) {
            if (strcmp(available.extensionName, required) == 0) {
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceVulkan12Features vulkan12Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features vulkan13Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };

    physicalDeviceFeatures.pNext = &vulkan12Features;
    vulkan12Features.pNext = &vulkan13Features;
    vulkan13Features.pNext = nullptr;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures);

    bool supportsRequiredFeatures = physicalDeviceFeatures.features.samplerAnisotropy && vulkan12Features.descriptorIndexing && 
                         vulkan13Features.synchronization2 && vulkan12Features.bufferDeviceAddress && 
                         vulkan13Features.dynamicRendering && vulkan12Features.runtimeDescriptorArray &&
                         vulkan12Features.shaderSampledImageArrayNonUniformIndexing && vulkan12Features.descriptorBindingVariableDescriptorCount;

    bool queueFound = false;
    uint32_t queueFamilyCount{ 0 };
    uint32_t graphicsQueueIndex{ 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        VkBool32 canPresent{VK_FALSE};
        chk(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface_, &canPresent));
        if (queueFamilies[i].queueCount > 0 &&
            (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent) {
            graphicsQueueIndex = i;
            queueFound = true;
            break;
        }
    }

    uint32_t surfaceFormatCount{ 0 };
    std::vector<VkSurfaceFormatKHR> surfaceFormats;
    chk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &surfaceFormatCount, nullptr));
    surfaceFormats.resize(surfaceFormatCount);
    chk(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &surfaceFormatCount, surfaceFormats.data()));

    bool surfaceFormatFound = false;
    for (const auto& format : surfaceFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormatFound = true;
            break;
        }
    }

    if (!surfaceFormatFound)
        return false;

    bool presentModeFound = false;
    uint32_t presentModeCount{ 0 };
    std::vector<VkPresentModeKHR> surfacePresentModes;
    chk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &presentModeCount, nullptr));
    surfacePresentModes.resize(presentModeCount);
    chk(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface_, &presentModeCount, surfacePresentModes.data()));

    for (const auto& presentMode : surfacePresentModes) {
        if (presentMode == VK_PRESENT_MODE_FIFO_KHR) {
            presentModeFound = true;
            break;
        }
    }

    if (!presentModeFound)
        return false;

    VkSurfaceCapabilitiesKHR surfaceCapabilities{};
    chk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface_, &surfaceCapabilities));

    if (!(surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return false;

    if (!(surfaceCapabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR))
        return false;

    if (supportsVulkan1_3 && supportsRequiredFeatures && queueFound) {
        deviceProperties_ = deviceProperties;
        graphicsQueueFamily_ = graphicsQueueIndex;
    }

    return supportsVulkan1_3 && supportsRequiredFeatures && queueFound;
}

void VulkanContext::waitIdle() const
{
    chk(vkDeviceWaitIdle(device_));
}

VkInstance VulkanContext::instance() const
{
    return instance_;
}

VkSurfaceKHR VulkanContext::surface() const
{
    return surface_;
}

VkPhysicalDevice VulkanContext::physicalDevice() const
{
    return physicalDevice_;
}

VkDevice VulkanContext::device() const
{
    return device_;
}

VkQueue VulkanContext::graphicsQueue() const
{
    return graphicsQueue_;
}

std::uint32_t VulkanContext::graphicsQueueFamily() const
{
    return graphicsQueueFamily_;
}

VmaAllocator VulkanContext::allocator() const
{
    return allocator_;
}

const VkPhysicalDeviceProperties2& VulkanContext::deviceProperties() const
{
    return deviceProperties_;
}

VkFormat VulkanContext::findDepthFormat() const
{
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    std::cerr << "No supported depth/stencil format\n";
    std::exit(EXIT_FAILURE);
}
