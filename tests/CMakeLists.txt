find_package(Threads REQUIRED)

add_executable(opense4_tests
    main.cpp
    combat_fixture.cpp
    command_samples.cpp
    engine_fixture.cpp
    test_ai.cpp
    test_app_settings.cpp
    test_classic_save.cpp
    test_audio.cpp
    test_audio_mix.cpp
    test_classic_colony_logic.cpp
    test_classic_options.cpp
    test_client_layout.cpp
    test_client_logic.cpp
    test_colony_orders.cpp
    test_combat.cpp
    test_computer_control.cpp
    test_crash_report.cpp
    test_combat_rules.cpp
    test_combat_planning.cpp
    test_combat_replay.cpp
    test_combat_windows.cpp
    test_tactical.cpp
    test_design_tools.cpp
    test_determinism.cpp
    test_diplomacy.cpp
    test_discovery.cpp
    test_economy.cpp
    test_engine_core.cpp
    test_engine_install.cpp
    test_events.cpp
    test_generate.cpp
    test_hidden_colonies.cpp
    test_history.cpp
    test_imgui_errors.cpp
    test_input_script.cpp
    test_integration.cpp
    test_intel.cpp
    test_learn.cpp
    test_learn_client.cpp
    test_lesson_lock.cpp
    test_lesson_panel.cpp
    test_main_window.cpp
    test_reports.cpp
    test_maps.cpp
    test_movement.cpp
    test_movement_line.cpp
    test_net.cpp
    test_net_resilience.cpp
    test_net_secure.cpp
    test_pbem_client.cpp
    test_pictures.cpp
    test_platform.cpp
    test_redact.cpp
    test_research.cpp
    test_ruleset.cpp
    test_score.cpp
    test_script_value.cpp
    test_serialize.cpp
    test_settings_keys.cpp
    test_setup_model.cpp
    test_scrap_window.cpp
    test_ships_ui.cpp
    test_sight.cpp
    test_stellar.cpp
    test_turn_based.cpp
    test_battle_flow.cpp
    test_turn_order.cpp
    test_unit_groups.cpp
    test_wincursor.cpp
    test_winfont.cpp
    test_xmath.cpp
    # The modding SDK's own tests (docs/MODDING_SDK.md, section 14.6)
    sdk/sdk_test_util.cpp
    sdk/test_mod_client.cpp
    sdk/test_mod_hegemon.cpp
    sdk/test_mod_identity.cpp
    sdk/test_mod_installed.cpp
    sdk/test_mod_layers.cpp
    sdk/test_mod_manifest.cpp
    sdk/test_mod_packages.cpp
    sdk/test_mod_patches.cpp
    sdk/test_mod_sets.cpp
    sdk/test_script_cache.cpp
    sdk/test_script_convert.cpp
    sdk/test_script_determinism.cpp
    sdk/test_script_json.cpp
    sdk/test_script_library.cpp
    sdk/test_script_memory.cpp
    sdk/test_script_runtime.cpp
    sdk/test_script_sandbox.cpp
    sdk/test_sdk_arena.cpp
    sdk/test_sdk_bots.cpp
    sdk/test_sdk_client_players.cpp
    sdk/test_sdk_codec.cpp
    sdk/test_sdk_docs.cpp
    sdk/test_sdk_guide.cpp
    sdk/test_sdk_player_setup.cpp
    sdk/test_sdk_players.cpp
    sdk/test_sdk_players_golden.cpp
    sdk/test_sdk_players_classic.cpp
    sdk/test_sdk_players_replay.cpp
    sdk/test_sdk_python.cpp
    sdk/test_sdk_queries.cpp
    sdk/test_sdk_rules.cpp
    sdk/test_sdk_rules_golden.cpp
    sdk/test_sdk_rules_orders.cpp
    sdk/test_sdk_rules_view.cpp
    sdk/test_sdk_tool.cpp
    sdk/test_sdk_ui.cpp
    sdk/test_sdk_view.cpp)
# Code outside the libraries, tested directly: the server setup-file parser and
# classic client logic without UI dependencies.
target_sources(opense4_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/client/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio_mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio_playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/client/crash_report.cpp
    ${CMAKE_SOURCE_DIR}/src/client/input.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/items.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/script.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/sdl_input.cpp
    ${CMAKE_SOURCE_DIR}/src/client/ui/imgui_errors.cpp
    ${CMAKE_SOURCE_DIR}/src/server/setup_file.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/ability_lines.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/art.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/computer_players.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/data_export.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/facility_markers.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/finale.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_audit.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_lock.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/map_style.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/mods_model.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/movement_line.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/movement_replay.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/net_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/order_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/pbem_play.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/pointer_rules.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/replay.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screen_id.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/sector_view.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/settings.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/session.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/ship_glides.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/status_icons.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/turn_start.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/colony_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/combat_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/design_tools.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/empire_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/setup_model.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/ships_logic.cpp)
# The script runtime's heap driven directly (tests/sdk/test_script_memory.cpp): C
# that includes MicroPython's headers, which the C++ tests never do. Our warnings,
# in their C form on MSVC, as for src/script/port.
add_library(opense4_tests_gc_probe OBJECT sdk/gc_probe.c)
target_link_libraries(opense4_tests_gc_probe PRIVATE opense4_micropython)
set_target_properties(opense4_tests_gc_probe PROPERTIES C_STANDARD 11)
if(MSVC)
    target_compile_options(opense4_tests_gc_probe PRIVATE /W4 /utf-8 $<$<BOOL:${OPENSE4_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_link_libraries(opense4_tests_gc_probe PRIVATE opense4_warnings)
endif()
target_sources(opense4_tests PRIVATE $<TARGET_OBJECTS:opense4_tests_gc_probe>)
target_link_libraries(opense4_tests PRIVATE opense4_sdk opense4_script opense4_mods opense4_game opense4_learn opense4_net opense4_assets doctest tomlplusplus imgui SDL3::SDL3 drlibs Threads::Threads opense4_warnings)
target_include_directories(opense4_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The SDK tool's tests run it (tests/sdk/test_sdk_tool.cpp, test_sdk_arena.cpp), and the
# external bots' tests the dedicated server.
add_dependencies(opense4_tests opense4-sdk opense4-server)
target_compile_definitions(opense4_tests PRIVATE
    OPENSE4_SDK_EXE="$<TARGET_FILE:opense4-sdk>"
    OPENSE4_SERVER_EXE="$<TARGET_FILE:opense4-server>"
    OPENSE4_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    OPENSE4_SDK_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/sdk"
    OPENSE4_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
    OPENSE4_DOCS_DIR="${CMAKE_SOURCE_DIR}/docs"
    OPENSE4_MODS_DIR="${CMAKE_SOURCE_DIR}/mods")

if(OPENSE4_DEV_PATHS)
    # Developer builds find the bundled mods in the source tree (src/mods/mod_set.cpp).
    target_compile_definitions(opense4_tests PRIVATE OPENSE4_TEST_DEV_PATHS=1)
endif()

opense4_copy_runtime_dlls(opense4_tests)

opense4_windows_manifest(opense4_tests)

add_test(NAME opense4_tests COMMAND opense4_tests)
