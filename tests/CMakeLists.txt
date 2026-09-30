add_executable(opense4_tests
    main.cpp
    engine_fixture.cpp
    test_ai.cpp
    test_audio.cpp
    test_classic_colony_logic.cpp
    test_client_logic.cpp
    test_combat.cpp
    test_combat_replay.cpp
    test_content.cpp
    test_design_tools.cpp
    test_diplomacy.cpp
    test_discovery.cpp
    test_economy.cpp
    test_engine_core.cpp
    test_engine_install.cpp
    test_events.cpp
    test_galaxy.cpp
    test_generate.cpp
    test_integration.cpp
    test_intel.cpp
    test_movement.cpp
    test_net.cpp
    test_pathfinding.cpp
    test_redact.cpp
    test_research.cpp
    test_ruleset.cpp
    test_score.cpp
    test_serialize.cpp
    test_setup_model.cpp
    test_ships_ui.cpp
    test_sight.cpp
    test_turn.cpp)
# Code outside the libraries, tested directly: the server setup-file parser and
# classic client logic without UI dependencies.
target_sources(opense4_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/client/audio_playlist.cpp
    ${CMAKE_SOURCE_DIR}/src/server/setup_file.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/replay.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/colony_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/design_tools.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/empire_logic.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/setup_model.cpp
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/ships_logic.cpp)
target_link_libraries(opense4_tests PRIVATE opense4_sim opense4_game opense4_net doctest tomlplusplus opense4_warnings)
target_compile_definitions(opense4_tests PRIVATE
    OPENSE4_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    OPENSE4_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME opense4_tests COMMAND opense4_tests)
