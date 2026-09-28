/* Copyright (c) 2025-2026, Sascha Willems
 * SPDX-License-Identifier: MIT
 */

#include "Application.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    try {
        const auto deviceIndex = argc > 1 ? static_cast<std::uint32_t>(std::stoi(argv[1])) : 0;
        Application application(deviceIndex);
        return application.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
