# --- core: math, rng, ids, logging (no dependencies) -----------------------------
add_library(opense4_core STATIC
    core/log.cpp)
target_include_directories(opense4_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_core PRIVATE opense4_warnings)

# --- datafile + ruleset: the classic "Key := Value" data format and its typed model ---
add_library(opense4_datafile STATIC
    datafile/datafile.cpp
    datafile/reader.cpp)
target_include_directories(opense4_datafile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_datafile PRIVATE opense4_warnings)

add_library(opense4_ruleset STATIC
    ruleset/load.cpp
    ruleset/ruleset.cpp)
target_link_libraries(opense4_ruleset PUBLIC opense4_core opense4_datafile PRIVATE opense4_warnings)

# --- game: the classic-compatible rules engine (replaces sim/ as it matures) --------
add_library(opense4_game STATIC
    game/generate.cpp)
target_link_libraries(opense4_game PUBLIC opense4_ruleset PRIVATE opense4_warnings)

# --- assets: runtime access to the player's installed classic art ------------------
add_library(opense4_assets STATIC
    assets/assets.cpp)
target_link_libraries(opense4_assets PUBLIC opense4_core PRIVATE stb opense4_warnings)

# --- sim: the headless, deterministic game rules ---------------------------------
add_library(opense4_sim STATIC
    sim/ai.cpp
    sim/commands.cpp
    sim/content.cpp
    sim/galaxy_gen.cpp
    sim/mutate.cpp
    sim/names.cpp
    sim/pathfinding.cpp
    sim/rules.cpp
    sim/setup.cpp
    sim/turn.cpp
    sim/types.cpp)
target_link_libraries(opense4_sim PUBLIC opense4_core PRIVATE tomlplusplus opense4_warnings)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gfx/device.hpp")
    include(Client.cmake)
endif()
