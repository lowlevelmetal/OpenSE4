cmake_minimum_required(VERSION 3.25)

project(OpenSE4
    VERSION 0.2.0
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
# Redistributable builds (the dist-* presets, docs/BUILDING.md "Release packages").
option(OPENSE4_STATIC "Link statically for redistribution: SDL3 is built as a static library, and so are the C++ runtime (and on Windows everything else)" OFF)
option(OPENSE4_EMBED_RESOURCES "Build our own fonts into the executable (files on disk still win)" ON)
option(OPENSE4_DEV_PATHS "Let the client fall back to assets/ in the source tree" ON)

if(OPENSE4_STATIC AND NOT MSVC)
    if(WIN32)
        # MinGW: static libgcc, libstdc++ and winpthreads; only system DLLs remain.
        add_link_options(-static)
    else()
        # Linux: everything but the C library, which must stay shared so SDL can load
        # the system's X11/Wayland, audio and GPU driver libraries at run time.
        add_link_options(-static-libstdc++ -static-libgcc)
    endif()
endif()
if(NOT OPENSE4_DEV_PATHS AND NOT MSVC)
    # Keep the build machine's paths out of the binary (source file names in logs).
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()

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

# `cmake --install`: the programs, and on Linux the desktop entry, icons and
# AppStream metadata that let desktops list the game (packaging/linux).
include(GNUInstallDirs)
install(TARGETS opense4-server opense4-datacheck RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(TARGET opense4)
    install(TARGETS opense4 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
if(UNIX AND NOT APPLE)
    set(opense4_app_id io.github.lowlevelmetal.OpenSE4)
    install(FILES "packaging/linux/${opense4_app_id}.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "packaging/linux/${opense4_app_id}.metainfo.xml" DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    install(DIRECTORY packaging/linux/icons/hicolor DESTINATION "${CMAKE_INSTALL_DATADIR}/icons")
endif()

if(OPENSE4_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Linux release builds run on glibc 2.34 and later (cmake/GlibcCompat.cmake).
if(OPENSE4_STATIC AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT MSVC)
    include(GlibcCompat)
    opense4_apply_glibc_compat("${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_SOURCE_DIR}/tests")
endif()
