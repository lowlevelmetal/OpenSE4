add_executable(opense4_tests
    main.cpp
    engine_fixture.cpp
    test_content.cpp
    test_engine_core.cpp
    test_engine_install.cpp
    test_galaxy.cpp
    test_generate.cpp
    test_pathfinding.cpp
    test_ruleset.cpp
    test_setup_model.cpp
    test_turn.cpp
    # The classic client's New Game model has no UI dependencies (screens/setup_model.hpp).
    ${CMAKE_SOURCE_DIR}/src/client/classic/screens/setup_model.cpp)
target_link_libraries(opense4_tests PRIVATE opense4_sim opense4_game doctest tomlplusplus opense4_warnings)
target_compile_definitions(opense4_tests PRIVATE
    OPENSE4_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    OPENSE4_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME opense4_tests COMMAND opense4_tests)
