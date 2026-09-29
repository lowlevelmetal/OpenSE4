cmake_minimum_required(VERSION 3.25)

project(OpenSE4
    VERSION 0.1.0
    DESCRIPTION "Open-source engine reimplementation for Space Empires IV Deluxe"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(OPENSE4_BUILD_TESTS "Build unit tests" ON)
option(OPENSE4_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Shaders)

# Common warning flags for first-party targets.
add_library(opense4_warnings INTERFACE)
if(MSVC)
    target_compile_options(opense4_warnings INTERFACE /W4 /permissive- /utf-8)
    if(OPENSE4_WARNINGS_AS_ERRORS)
        target_compile_options(opense4_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(opense4_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers)  # Vulkan's `VkFoo info{VK_STRUCTURE_TYPE_FOO}` idiom
    if(OPENSE4_WARNINGS_AS_ERRORS)
        target_compile_options(opense4_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(src)
add_subdirectory(tools)

if(OPENSE4_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
