cmake_minimum_required(VERSION 3.25)

project(OpenSE4
    VERSION 0.10.0
    DESCRIPTION "Open-source engine reimplementation for Space Empires IV Deluxe"
    LANGUAGES C CXX)

# Apple's linker warns about a static library listed twice, as CMake lists our
# libraries for one another: let CMake drop the repeats there.
if(APPLE AND POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()

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
# The same game on every machine (docs/ENGINE.md, "Same on every platform"): no
# fused multiply-add. ARM has it, and GCC would fuse a * b + c there by default,
# rounding once where x86_64 rounds twice. Everything, the libraries included.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()
# 32-bit Linux (armhf): 64-bit time_t and file offsets in every library, so that
# times after 2038 and files over 2 GiB work as on 64-bit machines (glibc 2.34+).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_compile_definitions(_FILE_OFFSET_BITS=64 _TIME_BITS=64)
endif()
if(NOT OPENSE4_DEV_PATHS AND NOT MSVC)
    # Keep the build machine's paths out of the binary (source file names in logs).
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
include(Shaders)

# Windows 7 SP1 is the oldest Windows the programs support (docs/BUILDING.md,
# "Windows 7 to 11"): the Windows headers then declare nothing newer, so a call to
# a newer function fails to compile and must go through GetProcAddress with a
# fallback. For our own code and Dear ImGui; SDL3 picks its own version and loads
# newer functions at run time, and miniupnpc is set in cmake/Dependencies.cmake.
# tools/check_windows_imports.py checks the built programs.
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

# Common warning flags for first-party targets.
add_library(opense4_warnings INTERFACE)
if(MSVC)
    target_compile_options(opense4_warnings INTERFACE /W4 /permissive- /utf-8)
    # The C library's getenv, fopen and the like are standard: no "use the _s
    # variant" deprecation warnings (C4996).
    target_compile_definitions(opense4_warnings INTERFACE _CRT_SECURE_NO_WARNINGS)
    if(OPENSE4_WARNINGS_AS_ERRORS)
        target_compile_options(opense4_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(opense4_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers)  # Vulkan's `VkFoo info{VK_STRUCTURE_TYPE_FOO}` idiom
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC for 32-bit ARM notes at every std::format that GCC 9.1 changed how its
        # arguments are passed; that concerns only code built by older compilers.
        target_compile_options(opense4_warnings INTERFACE -Wno-psabi)
    endif()
    if(OPENSE4_WARNINGS_AS_ERRORS)
        target_compile_options(opense4_warnings INTERFACE -Werror)
    endif()
endif()

# Windows finds DLLs next to the executable: copy the shared libraries a program
# needs there (SDL3, unless it is linked statically).
function(opense4_copy_runtime_dlls target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E "$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${target}>>,copy_if_different,true>"
                    "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
endfunction()

# Every program gets packaging/windows/opense4.manifest on Windows: the UTF-8
# code page, so narrow paths mean the same as on Linux (docs/ENGINE.md, "Same
# on every platform"). MSVC merges it into the manifest its linker writes;
# MinGW-w64 links it as the manifest resource.
function(opense4_windows_manifest target)
    if(NOT WIN32)
        return()
    endif()
    set(manifest "${CMAKE_SOURCE_DIR}/packaging/windows/opense4.manifest")
    if(MSVC)
        target_sources(${target} PRIVATE "${manifest}")
    else()
        set(rc "${CMAKE_BINARY_DIR}/generated/${target}_manifest.rc")
        file(WRITE "${rc}.in" "// Generated by CMakeLists.txt (opense4_windows_manifest).\n1 24 \"${manifest}\"\n")
        configure_file("${rc}.in" "${rc}" COPYONLY)
        target_sources(${target} PRIVATE "${rc}")
        set_source_files_properties("${rc}" PROPERTIES OBJECT_DEPENDS "${manifest}")
    endif()
endfunction()

add_subdirectory(src)
add_subdirectory(tools)

# `cmake --install`: the programs, and on Linux the desktop entry, icons and
# AppStream metadata that let desktops list the game (packaging/linux).
include(GNUInstallDirs)
install(TARGETS opense4-server opense4-datacheck opense4-sdk RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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

# Windows builds with libc++ (the release toolchain) convert narrow strings to and
# from paths as UTF-8 on every Windows version (cmake/WindowsCompat.cmake).
if(WIN32 AND NOT MSVC)
    include(WindowsCompat)
    opense4_apply_windows_compat("${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_SOURCE_DIR}/tests")
endif()
