find_package(Threads REQUIRED)

add_executable(opense4_tests
    main.cpp
    combat_fixture.cpp
    engine_fixture.cpp
    test_ai.cpp
    test_app_settings.cpp
    test_audio.cpp
    test_audio_mix.cpp
    test_classic_colony_logic.cpp
    test_classic_options.cpp
    test_client_layout.cpp
    test_client_logic.cpp
    test_colony_orders.cpp
    test_combat.cpp
    test_computer_control.cpp
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
    test_input_script.cpp
    test_integration.cpp
    test_intel.cpp
    test_learn.cpp
    test_learn_client.cpp
    test_lesson_lock.cpp
    test_lesson_panel.cpp
    test_main_window.cpp
    test_maps.cpp
    test_movement.cpp
    test_movement_line.cpp
    test_net.cpp
    test_net_resilience.cpp
    test_net_secure.cpp
    test_pbem_client.cpp
    test_platform.cpp
    test_redact.cpp
    test_research.cpp
    test_ruleset.cpp
    test_score.cpp
    test_serialize.cpp
    test_settings_keys.cpp
    test_setup_model.cpp
    test_scrap_window.cpp
    test_ships_ui.cpp
    test_sight.cpp
    test_stellar.cpp
    test_turn_based.cpp
    test_turn_order.cpp
    test_unit_groups.cpp
    test_wincursor.cpp
    test_winfont.cpp
    test_xmath.cpp)
# Code outside the libraries, tested directly: the server setup-file parser and
# classic client logic without UI dependencies.
target_sources(opense4_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/client/app_settings.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio_mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio_playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/client/input.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/items.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/script.cpp
    ${CMAKE_SOURCE_DIR}/src/client/script/sdl_input.cpp
    ${CMAKE_SOURCE_DIR}/src/server/setup_file.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/data_export.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/facility_markers.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/finale.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_audit.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_lock.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/lesson_panel.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/map_style.cpp
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
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/colony_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/combat_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/design_tools.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/empire_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/setup_model.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/ships_logic.cpp)
target_link_libraries(opense4_tests PRIVATE opense4_game opense4_learn opense4_net opense4_assets doctest tomlplusplus imgui SDL3::SDL3 drlibs Threads::Threads opense4_warnings)
target_compile_definitions(opense4_tests PRIVATE
    OPENSE4_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    OPENSE4_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

opense4_copy_runtime_dlls(opense4_tests)

opense4_windows_manifest(opense4_tests)

add_test(NAME opense4_tests COMMAND opense4_tests)
