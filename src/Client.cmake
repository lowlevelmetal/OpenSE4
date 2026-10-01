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
    client/app_settings.cpp
    client/audio.cpp
    client/audio_playlist.cpp
    client/classic/art.cpp
    client/classic/classic_mode.cpp
    client/classic/frontend.cpp
    client/classic/facility_markers.cpp
    client/classic/learn_content.cpp
    client/classic/lesson_runner.cpp
    client/classic/main_window.cpp
    client/classic/map_style.cpp
    client/classic/net_transport.cpp
    client/classic/order_rules.cpp
    client/classic/pbem_play.cpp
    client/classic/quadrant_map.cpp
    client/classic/replay.cpp
    client/classic/screen_id.cpp
    client/classic/ship_glides.cpp
    client/classic/status_icons.cpp
    client/classic/reports.cpp
    client/classic/screens/cargo_transfer.cpp
    client/classic/screens/colony_logic.cpp
    client/classic/screens/colony_widgets.cpp
    client/classic/screens/combat_logic.cpp
    client/classic/screens/combat_map.cpp
    client/classic/screens/combat_replay.cpp
    client/classic/screens/design_tools.cpp
    client/classic/screens/communicate.cpp
    client/classic/screens/designs.cpp
    client/classic/screens/empire_status.cpp
    client/classic/screens/empire_logic.cpp
    client/classic/screens/empire_widgets.cpp
    client/classic/screens/empires.cpp
    client/classic/screens/fleet_transfer.cpp
    client/classic/screens/galaxy_map.cpp
    client/classic/screens/game_menu.cpp
    client/classic/screens/help.cpp
    client/classic/screens/item_reports.cpp
    client/classic/screens/intelligence.cpp
    client/classic/screens/launch_recover.cpp
    client/classic/screens/learn_screens.cpp
    client/classic/screens/log.cpp
    client/classic/screens/markdown_view.cpp
    client/classic/screens/multiplayer.cpp
    client/classic/screens/pbem.cpp
    client/classic/screens/planets.cpp
    client/classic/screens/queues.cpp
    client/classic/screens/registry.cpp
    client/classic/screens/research.cpp
    client/classic/screens/scrap.cpp
    client/classic/screens/settings_screen.cpp
    client/classic/screens/setup.cpp
    client/classic/screens/simulator.cpp
    client/classic/screens/setup_empire.cpp
    client/classic/screens/setup_model.cpp
    client/classic/screens/setup_widgets.cpp
    client/classic/screens/ships.cpp
    client/classic/screens/ships_common.cpp
    client/classic/screens/ships_logic.cpp
    client/classic/screens/stellar.cpp
    client/classic/screens/strategic_combat.cpp
    client/classic/screens/tactical.cpp
    client/classic/screens/vehicle_orders.cpp
    client/classic/session.cpp
    client/classic/settings.cpp
    client/classic/ui.cpp
    client/classic/widgets.cpp
    client/input.cpp
    client/settings_window.cpp
    client/main.cpp
    client/ui/bitmap_font.cpp
    client/ui/theme.cpp)
target_link_libraries(opense4 PRIVATE opense4_game opense4_learn opense4_net opense4_assets opense4_gfx opense4_embedded imgui tomlplusplus drlibs opense4_warnings)
target_compile_definitions(opense4 PRIVATE OPENSE4_CLIENT_VERSION="${PROJECT_VERSION}")
if(OPENSE4_DEV_PATHS)
    # Developer convenience: find assets/ in the source tree. Release builds
    # leave it out so the build machine's paths stay out of the binary.
    target_compile_definitions(opense4 PRIVATE OPENSE4_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
endif()
set_target_properties(opense4 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(WIN32)
    # A windowed application: no console window next to the game. main.cpp attaches
    # to the parent console when started from one, so --help still prints.
    set_target_properties(opense4 PROPERTIES WIN32_EXECUTABLE ON)
    # The icon and version information (packaging/windows).
    enable_language(RC)
    set(OPENSE4_ICON "${CMAKE_SOURCE_DIR}/packaging/windows/opense4.ico")
    configure_file("${CMAKE_SOURCE_DIR}/packaging/windows/opense4.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/opense4.rc" @ONLY)
    target_sources(opense4 PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/opense4.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/generated/opense4.rc" PROPERTIES OBJECT_DEPENDS "${OPENSE4_ICON}")
endif()
