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
    ruleset/races.cpp
    ruleset/ruleset.cpp)
target_link_libraries(opense4_ruleset PUBLIC opense4_core opense4_datafile PRIVATE opense4_warnings)

# --- game: the classic-compatible rules engine (replaces sim/ as it matures) --------
add_library(opense4_game STATIC
    game/abilities.cpp
    game/ai.cpp
    game/combat.cpp
    game/combat_ground.cpp
    game/combat_space.cpp
    game/commands.cpp
    game/design.cpp
    game/diplomacy.cpp
    game/economy.cpp
    game/economy_population.cpp
    game/economy_queue.cpp
    game/events.cpp
    game/generate.cpp
    game/intel.cpp
    game/movement.cpp
    game/query.cpp
    game/research.cpp
    game/rules.cpp
    game/score.cpp
    game/serialize.cpp
    game/setup.cpp
    game/sight.cpp
    game/state.cpp
    game/turn.cpp
    game/types.cpp)
target_link_libraries(opense4_game PUBLIC opense4_ruleset PRIVATE opense4_warnings)

# --- net: multiplayer (TCP host/client sessions, UPnP port mapping, PBEM files) -----
find_package(Threads REQUIRED)
add_library(opense4_net STATIC
    net/auth.cpp
    net/client.cpp
    net/connection.cpp
    net/host.cpp
    net/pbem.cpp
    net/protocol.cpp
    net/socket.cpp
    net/upnp.cpp)
target_link_libraries(opense4_net PUBLIC opense4_game PRIVATE Threads::Threads opense4_warnings)
target_compile_definitions(opense4_net PRIVATE OPENSE4_VERSION="${PROJECT_VERSION}")
if(OPENSE4_ENABLE_UPNP)
    target_sources(opense4_net PRIVATE net/upnp_miniupnpc.cpp)
    target_link_libraries(opense4_net PRIVATE miniupnpc::miniupnpc)
else()
    target_sources(opense4_net PRIVATE net/upnp_disabled.cpp)
endif()
if(WIN32)
    target_link_libraries(opense4_net PRIVATE ws2_32)
endif()

# --- the dedicated server (network games and play-by-e-mail) ------------------------
add_executable(opense4-server
    server/main.cpp
    server/setup_file.cpp)
target_link_libraries(opense4-server PRIVATE opense4_net opense4_game tomlplusplus opense4_warnings)
set_target_properties(opense4-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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
