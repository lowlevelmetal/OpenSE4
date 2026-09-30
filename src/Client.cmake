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
    client/classic/art.cpp
    client/classic/classic_mode.cpp
    client/classic/frontend.cpp
    client/classic/main_window.cpp
    client/classic/reports.cpp
    client/classic/screens/colony_logic.cpp
    client/classic/screens/colony_widgets.cpp
    client/classic/screens/combat_replay.cpp
    client/classic/screens/designs.cpp
    client/classic/screens/empire_status.cpp
    client/classic/screens/empires.cpp
    client/classic/screens/galaxy_map.cpp
    client/classic/screens/game_menu.cpp
    client/classic/screens/help.cpp
    client/classic/screens/log.cpp
    client/classic/screens/multiplayer.cpp
    client/classic/screens/planets.cpp
    client/classic/screens/queues.cpp
    client/classic/screens/registry.cpp
    client/classic/screens/research.cpp
    client/classic/screens/setup.cpp
    client/classic/screens/ships.cpp
    client/classic/session.cpp
    client/classic/ui.cpp
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
