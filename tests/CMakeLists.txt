add_executable(opense4_tests
    main.cpp
    combat_fixture.cpp
    engine_fixture.cpp
    test_ai.cpp
    test_app_settings.cpp
    test_audio.cpp
    test_classic_colony_logic.cpp
    test_classic_options.cpp
    test_client_layout.cpp
    test_client_logic.cpp
    test_colony_orders.cpp
    test_combat.cpp
    test_combat_rules.cpp
    test_combat_planning.cpp
    test_combat_replay.cpp
    test_combat_windows.cpp
    test_tactical.cpp
    test_design_tools.cpp
    test_diplomacy.cpp
    test_discovery.cpp
    test_economy.cpp
    test_engine_core.cpp
    test_engine_install.cpp
    test_events.cpp
    test_generate.cpp
    test_hidden_colonies.cpp
    test_history.cpp
    test_integration.cpp
    test_intel.cpp
    test_learn.cpp
    test_learn_client.cpp
    test_main_window.cpp
    test_maps.cpp
    test_movement.cpp
    test_net.cpp
    test_pbem_client.cpp
    test_redact.cpp
    test_research.cpp
    test_ruleset.cpp
    test_score.cpp
    test_serialize.cpp
    test_setup_model.cpp
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
    ${CMAKE_SOURCE_DIR}/src/client/audio_playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/client/input.cpp
    ${CMAKE_SOURCE_DIR}/src/server/setup_file.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/facility_markers.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/layout.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/map_style.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/movement_replay.cpp
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
target_link_libraries(opense4_tests PRIVATE opense4_game opense4_learn opense4_net opense4_assets doctest tomlplusplus imgui SDL3::SDL3 opense4_warnings)
target_compile_definitions(opense4_tests PRIVATE
    OPENSE4_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    OPENSE4_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

opense4_copy_runtime_dlls(opense4_tests)

add_test(NAME opense4_tests COMMAND opense4_tests)
