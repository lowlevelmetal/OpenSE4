# Third-party dependencies.
#
# System packages: SDL3 (falls back to fetching), Vulkan headers + glslc.
# Everything else is fetched at a pinned version with a verified hash.
# The Vulkan loader is NOT linked: it is loaded at runtime through SDL so the
# game still starts (on the OpenGL backend) on machines without Vulkan.

include(FetchContent)

# --- SDL3 --------------------------------------------------------------------
find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3-shared)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found on the system; fetching it")
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.16
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- Vulkan headers + shader compiler -----------------------------------------
# Only the headers are needed at build time; prefer the system copy, else fetch.
find_package(VulkanHeaders CONFIG QUIET)
if(NOT TARGET Vulkan::Headers)
    FetchContent_Declare(VulkanHeaders
        URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.357.tar.gz
        URL_HASH SHA256=7dc0dbcf1d49dd3d7da3761c251c6097dfbaac475321a4a8a99269d3d5abecdc)
    FetchContent_MakeAvailable(VulkanHeaders)
endif()

find_program(OPENSE4_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
if(NOT OPENSE4_GLSLC)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or shaderc (it provides glslc).")
endif()

# --- Header-only / single-file libraries ------------------------------------
# SOURCE_SUBDIR points at a directory without a CMakeLists.txt so that
# MakeAvailable only downloads; we define the targets ourselves below.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9.tar.gz
    URL_HASH SHA256=af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(volk
    URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.357.0.tar.gz
    URL_HASH SHA256=6400c7b23e24d17e4f04bac49b55b06c4e87677d33398e90344743ec73560ca6
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(vma
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(tomlplusplus
    URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=8517f65938a4faae9ccf8ebb36631a38c1cadfb5efa85d9a72e15b9e97d25155
    SOURCE_SUBDIR _no_cmake)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz
    URL_HASH SHA256=9a955b1b49a4410088a2e0ee2a9c057c3c907d0c1d75454144cb980aca0ba515
    SOURCE_SUBDIR _no_cmake)
# dr_mp3 (public domain / MIT-0): decodes the classic game's MP3 music at runtime.
FetchContent_Declare(drlibs
    URL https://github.com/mackron/dr_libs/archive/dfe8377631000664666519fdb83da193fd8037f4.tar.gz
    URL_HASH SHA256=4654acb029f4f2a43ac2edb60c4cb09f40615b4b5bee9709954f910cb979e5fd
    SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(imgui volk vma tomlplusplus stb drlibs)
add_library(drlibs INTERFACE)
target_include_directories(drlibs SYSTEM INTERFACE "${drlibs_SOURCE_DIR}")

if(OPENSE4_BUILD_TESTS)
    FetchContent_Declare(doctest
        URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
        URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
        SOURCE_SUBDIR _no_cmake)
    FetchContent_MakeAvailable(doctest)
    add_library(doctest INTERFACE)
    target_include_directories(doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")
endif()

# --- miniupnpc (BSD-3-Clause): UPnP port mapping for hosts of network games -----
# Only its static library is built. For offline builds, point
# FETCHCONTENT_SOURCE_DIR_MINIUPNPC at an unpacked copy of the same release.
option(OPENSE4_ENABLE_UPNP "Forward the host port on the router with UPnP (miniupnpc)" ON)
if(OPENSE4_ENABLE_UPNP)
    set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
    set(UPNPC_NO_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(miniupnpc
        URL https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_3_3.tar.gz
        URL_HASH SHA256=8cf2c833b3e76fc4893ff29c2a376e3394962449e5970e373c0a91421724d222
        SOURCE_SUBDIR miniupnpc
        SYSTEM)
    FetchContent_MakeAvailable(miniupnpc)
    # Third-party code: don't apply or show warnings.
    target_compile_options(libminiupnpc-static PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
endif()

# Dear ImGui core + SDL3 platform backend. Rendering goes through our own RHI.
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp")
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${CMAKE_SOURCE_DIR}/src/third_party_config")
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imconfig_opense4.h")
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# volk: Vulkan function loader (we feed it SDL's vkGetInstanceProcAddr).
add_library(volk STATIC "${volk_SOURCE_DIR}/volk.c")
target_include_directories(volk SYSTEM PUBLIC "${volk_SOURCE_DIR}")
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(volk PUBLIC Vulkan::Headers ${CMAKE_DL_LIBS})

add_library(vma INTERFACE)
target_include_directories(vma SYSTEM INTERFACE "${vma_SOURCE_DIR}/include")

add_library(tomlplusplus INTERFACE)
target_include_directories(tomlplusplus SYSTEM INTERFACE "${tomlplusplus_SOURCE_DIR}/include")

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# Khronos OpenGL core-profile headers (vendored so Windows builds work too).
add_library(khronos_gl INTERFACE)
target_include_directories(khronos_gl SYSTEM INTERFACE "${CMAKE_SOURCE_DIR}/third_party/khronos")
