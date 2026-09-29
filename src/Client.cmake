# --- gfx: RHI (Vulkan + OpenGL backends), 2D renderer, ImGui bridge ------------------
add_library(opense4_gfx STATIC
    gfx/device.cpp
    gfx/imgui_renderer.cpp
    gfx/renderer2d.cpp
    gfx/opengl/gl_device.cpp
    gfx/vulkan/vk_device.cpp
    gfx/vulkan/vma_impl.cpp)
target_link_libraries(opense4_gfx
    PUBLIC opense4_core SDL3::SDL3 imgui
    PRIVATE volk vma khronos_gl stb opense4_warnings)
opense4_add_shaders(opense4_gfx
    "${CMAKE_SOURCE_DIR}/shaders/basic2d.vert"
    "${CMAKE_SOURCE_DIR}/shaders/basic2d.frag")
# Third-party implementation file: don't apply our strict warnings.
set_source_files_properties(gfx/vulkan/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

# --- the game executable --------------------------------------------------------------
add_executable(opense4
    client/app.cpp
    client/classic/classic_mode.cpp
    client/game_session.cpp
    client/main.cpp
    client/prototype_mode.cpp
    client/ui/hud.cpp
    client/ui/theme.cpp
    client/views/galaxy_view.cpp
    client/views/starfield.cpp
    client/views/system_view.cpp)
target_link_libraries(opense4 PRIVATE opense4_sim opense4_game opense4_assets opense4_gfx imgui opense4_warnings)
target_compile_definitions(opense4 PRIVATE OPENSE4_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(opense4 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
