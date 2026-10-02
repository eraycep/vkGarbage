# Copyright (c) 2025-2026, Sascha Willems
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
include(FetchContent)
set(NAME vkGarbage)
project(${NAME})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not detected, if installed please set VULKAN_SDK env var or manually provide libraries")
else()
    message(STATUS "Found Vulkan SDK in $ENV{VULKAN_SDK}")
endif()

# Volk, VMA and Slang are taken from the SDK.
# SDL3 and glm are also provided by some SDK distributions.
include_directories(SYSTEM $ENV{VULKAN_SDK}/include)
# These are not part of the SDK
include_directories(external/tinyobj)
include_directories(external/ktx/include)
include_directories(external/ktx/other_include)

find_package (Vulkan REQUIRED)

if (NOT WIN32)
    FetchContent_Declare(GLM
        GIT_REPOSITORY https://github.com/g-truc/glm
        GIT_TAG 1.0
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(GLM)
endif()

set(KTX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)
add_library(ktx STATIC ${KTX_SOURCES})

if (TARGET Vulkan::Vulkan)
    target_link_libraries(ktx PUBLIC Vulkan::Vulkan)
elseif (APPLE)
    target_link_libraries(ktx PUBLIC $ENV{VULKAN_SDK}/lib/libvulkan.dylib)
elseif (NOT WIN32)
    target_link_libraries(ktx PUBLIC $ENV{VULKAN_SDK}/lib/libvulkan.so)
endif()

find_package(SDL3 CONFIG QUIET HINTS "$ENV{VULKAN_SDK}")
if (NOT TARGET SDL3::SDL3)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.10
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(SDL3)
endif()
# Dear ImGui v1.91.9b, vendored with its MIT license for reproducible offline builds.
add_library(imgui STATIC
    external/imgui/imgui.cpp external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp external/imgui/imgui_widgets.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/backends/imgui_impl_sdl3.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC external/imgui)
target_compile_definitions(imgui PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

find_library(Slang_LIBRARY NAMES slang slang-compiler HINTS "$ENV{VULKAN_SDK}/lib" REQUIRED)

if(WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
elseif(LINUX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WAYLAND_KHR")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX -D_USE_MATH_DEFINES")
if(MSVC)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

add_executable(${NAME} main.cpp assets/shader.slang)
target_sources(${NAME} PRIVATE
    src/Common.hpp
    src/EditorUi.cpp src/EditorUi.hpp
    src/Shader.cpp src/Shader.hpp
    src/Application.cpp src/Application.hpp
    src/Window.cpp src/Window.hpp
    src/Renderer.cpp src/Renderer.hpp
    src/Scene.cpp src/Scene.hpp src/SceneObject.hpp
    src/VulkanContext.cpp src/VulkanContext.hpp
    src/Assets.cpp src/Assets.hpp src/ShadowMap.cpp
    src/ShadowMap.hpp src/BoundingBox.hpp 
    src/BoundingBox.cpp
)
target_compile_definitions(${NAME} PRIVATE VK_NO_PROTOTYPES)
target_include_directories(${NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if (WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(${NAME} PROPERTIES DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(${NAME} PRIVATE cxx_std_20)
target_link_libraries(${NAME} PRIVATE imgui SDL3::SDL3 ktx ${Slang_LIBRARY})
if (NOT WIN32)
    target_link_libraries(${NAME} PRIVATE glm::glm-header-only)
endif()
