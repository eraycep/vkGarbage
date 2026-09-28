#pragma once

#include <vulkan/vulkan.h>
#include <cstdlib>
#include <iostream>

inline void chk(VkResult result) {
    if (result != VK_SUCCESS) {
        std::cerr << "Vulkan call returned an error (" << result << ")\n";
        std::exit(EXIT_FAILURE);
    }
}

inline void chk(bool result) {
    if (!result) {
        std::cerr << "Call returned an error\n";
        std::exit(EXIT_FAILURE);
    }
}
