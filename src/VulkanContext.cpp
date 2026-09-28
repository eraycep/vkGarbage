#include "VulkanContext.hpp"
#include "Common.hpp"
#include "Window.hpp"

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
        .pApplicationName = "tutorial",
        .apiVersion = VK_API_VERSION_1_3,
    };

    const auto instanceExtensions = window.requiredInstanceExtensions();

    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size()),
        .ppEnabledExtensionNames = instanceExtensions.data(),
    };

    chk(vkCreateInstance(&instanceCI, nullptr, &instance_));

    volkLoadInstance(instance_);
    surface_ = window.createSurface(instance_);
}

/* Select physical device (GPU) from the list, I went with 0
 * because I have a laptop with integrated GPU
 */
void VulkanContext::selectPhysicalDevice(std::uint32_t deviceIndex)
{
    uint32_t deviceCount{ 0 };
    chk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> devices(deviceCount);
    chk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    if (deviceIndex >= devices.size()) {
        std::cerr << "Requested Vulkan device index is unavailable\n";
        std::exit(EXIT_FAILURE);
    }

    VkPhysicalDeviceProperties2 deviceProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(devices[deviceIndex], &deviceProperties);
    std::cout << "Selected device: " << deviceProperties.properties.deviceName <<  "\n";

    deviceProperties_ = deviceProperties;
    physicalDevice_ = devices[deviceIndex];

    uint32_t queueFamilyCount{ 0 };
    vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[deviceIndex], &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        VkBool32 canPresent{VK_FALSE};
        chk(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface_, &canPresent));
        if (queueFamilies[i].queueCount > 0 &&
            (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && canPresent) {
            graphicsQueueFamily_ = i;
            return;
        }
    }
    std::cerr << "No queue family supports both graphics and presentation\n";
    std::exit(EXIT_FAILURE);
}

// Create vulkan logical device
void VulkanContext::createDevice()
{
	const float qfpriorities{ 1.0f };
	VkDeviceQueueCreateInfo queueCI{ .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = graphicsQueueFamily_, .queueCount = 1, .pQueuePriorities = &qfpriorities };

	VkPhysicalDeviceVulkan12Features enabledVk12Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .descriptorIndexing = true, .shaderSampledImageArrayNonUniformIndexing = true, .descriptorBindingVariableDescriptorCount = true, .runtimeDescriptorArray = true, .bufferDeviceAddress = true };
	VkPhysicalDeviceVulkan13Features enabledVk13Features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &enabledVk12Features, .synchronization2 = true, .dynamicRendering = true };
	VkPhysicalDeviceFeatures enabledVk10Features{ .samplerAnisotropy = VK_TRUE };
	const std::vector<const char*> deviceExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    
    VkDeviceCreateInfo deviceCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabledVk13Features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCI,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
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
    vkDestroyInstance(instance_, nullptr);
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
