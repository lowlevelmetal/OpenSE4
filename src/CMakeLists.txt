# --- core: math, rng, ids, logging (no dependencies) -----------------------------
add_library(opense4_core STATIC
    core/environment.cpp
    core/log.cpp)
target_include_directories(opense4_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_core PRIVATE opense4_warnings)

# --- script: the modding SDK's values and script runtime (docs/MODDING_SDK.md) ---------
add_library(opense4_script STATIC
    script/value.cpp)
target_include_directories(opense4_script PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_script PRIVATE opense4_warnings)

# --- embedded: our own fonts and learning content built into the executable ---------
# (core/embedded.hpp). Only our own files: nothing from the original game.
if(OPENSE4_EMBED_RESOURCES)
    file(GLOB embedded_files RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/fonts/*.ttf")
    # Manual pages, tutorials and training games (docs/LEARNING.md).
    file(GLOB_RECURSE embedded_learn RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/assets/learn/*.md"
        "${CMAKE_SOURCE_DIR}/assets/learn/*.toml")
    list(APPEND embedded_files ${embedded_learn})
    list(SORT embedded_files)
    list(TRANSFORM embedded_files PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE embedded_paths)
    list(JOIN embedded_files "|" embedded_arg)
    set(embedded_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_resources.cpp")
    add_custom_command(
        OUTPUT "${embedded_cpp}"
        COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${embedded_cpp}" "-DROOT=${CMAKE_SOURCE_DIR}" "-DFILES=${embedded_arg}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
        DEPENDS ${embedded_paths} "${CMAKE_SOURCE_DIR}/cmake/EmbedResources.cmake"
        COMMENT "Embedding fonts and learning content"
        VERBATIM)
    add_library(opense4_embedded STATIC "${embedded_cpp}")
else()
    add_library(opense4_embedded STATIC core/embedded_none.cpp)
endif()
target_link_libraries(opense4_embedded PUBLIC opense4_core)

# --- datafile + ruleset: the classic "Key := Value" data format and its typed model ---
add_library(opense4_datafile STATIC
    datafile/datafile.cpp
    datafile/reader.cpp)
target_include_directories(opense4_datafile PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(opense4_datafile PRIVATE opense4_warnings)

add_library(opense4_ruleset STATIC
    ruleset/files.cpp
    ruleset/load.cpp
    ruleset/mods.cpp
    ruleset/races.cpp
    ruleset/ruleset.cpp)
target_link_libraries(opense4_ruleset PUBLIC opense4_core opense4_datafile PRIVATE opense4_warnings)
if(WIN32)
    target_link_libraries(opense4_ruleset PRIVATE advapi32)  # the Steam folder from the registry
endif()

# --- game: the classic rules engine ------------------------------------------------
add_library(opense4_game STATIC
    game/abilities.cpp
    game/ai.cpp
    game/ai_anger.cpp
    game/ai_data.cpp
    game/ai_design.cpp
    game/ai_diplomacy.cpp
    game/ai_economy.cpp
    game/ai_explore.cpp
    game/ai_military.cpp
    game/ai_research.cpp
    game/classic_save_checksums.cpp
    game/classic_save_codec.cpp
    game/classic_save_export.cpp
    game/classic_save_import.cpp
    game/classic_save_tables.cpp
    game/combat.cpp
    game/combat_ground.cpp
    game/combat_space.cpp
    game/combat_tactical.cpp
    game/commands.cpp
    game/design.cpp
    game/diplomacy.cpp
    game/economy.cpp
    game/economy_population.cpp
    game/economy_queue.cpp
    game/events.cpp
    game/generate.cpp
    game/intel.cpp
    game/log_picture.cpp
    game/map_file.cpp
    game/movement.cpp
    game/movement_path.cpp
    game/movement_route.cpp
    game/movement_stellar.cpp
    game/movement_upkeep.cpp
    game/movement_util.cpp
    game/orders.cpp
    game/query.cpp
    game/redact.cpp
    game/research.cpp
    game/rules.cpp
    game/scrap.cpp
    game/score.cpp
    game/serialize.cpp
    game/setup.cpp
    game/simulator.cpp
    game/sight.cpp
    game/state.cpp
    game/turn.cpp
    game/turn_based.cpp
    game/types.cpp)
target_link_libraries(opense4_game PUBLIC opense4_ruleset PRIVATE tomlplusplus opense4_warnings)

# --- mods: packages, mod sets, the layered game files and data patches -------------
# (docs/MODDING_SDK.md, docs/sdk/packages-and-data.md)
add_library(opense4_mods STATIC
    mods/apply.cpp
    mods/data_set.cpp
    mods/generator.cpp
    mods/manifest.cpp
    mods/mod_set.cpp
    mods/package.cpp
    mods/patch.cpp
    mods/tables.cpp
    mods/version.cpp
    mods/zip.cpp)
target_link_libraries(opense4_mods PUBLIC opense4_game opense4_script PRIVATE tomlplusplus miniz monocypher opense4_warnings)

# --- sdk: the engine's side of the modding SDK (docs/MODDING_SDK.md) ---------------
# The view scripts read, their queries and the command codec, on script::Value.
add_library(opense4_sdk STATIC
    sdk/codec.cpp
    sdk/names.cpp
    sdk/parts.cpp
    sdk/queries.cpp
    sdk/rules_view.cpp
    sdk/view.cpp)
target_link_libraries(opense4_sdk PUBLIC opense4_game opense4_script PRIVATE opense4_warnings)

# --- learn: tutorials, training games and the manual (docs/LEARNING.md) --------------
# Headless: the content model, its loaders and the condition evaluator. The
# client draws it; the tests check the built-in content with it.
add_library(opense4_learn STATIC
    learn/access.cpp
    learn/condition.cpp
    learn/ids.cpp
    learn/lesson.cpp
    learn/library.cpp
    learn/markdown.cpp
    learn/progress.cpp
    learn/resume.cpp
    learn/tokens.cpp)
target_link_libraries(opense4_learn PUBLIC opense4_game PRIVATE tomlplusplus opense4_warnings)

# --- net: multiplayer (TCP host/client sessions, UPnP port mapping, PBEM files) -----
find_package(Threads REQUIRED)
add_library(opense4_net STATIC
    net/auth.cpp
    net/client.cpp
    net/connection.cpp
    net/crypto.cpp
    net/host.cpp
    net/pbem.cpp
    net/protocol.cpp
    net/secure.cpp
    net/socket.cpp
    net/upnp.cpp
    net/discovery.cpp)
target_link_libraries(opense4_net PUBLIC opense4_game PRIVATE Threads::Threads monocypher opense4_warnings)
target_compile_definitions(opense4_net PRIVATE OPENSE4_VERSION="${PROJECT_VERSION}")
if(OPENSE4_ENABLE_UPNP)
    target_sources(opense4_net PRIVATE net/upnp_miniupnpc.cpp)
    target_link_libraries(opense4_net PRIVATE miniupnpc::miniupnpc)
else()
    target_sources(opense4_net PRIVATE net/upnp_disabled.cpp)
endif()
if(WIN32)
    target_link_libraries(opense4_net PRIVATE ws2_32 bcrypt)
endif()

# --- the dedicated server (network games and play-by-e-mail) ------------------------
add_executable(opense4-server
    server/main.cpp
    server/setup_file.cpp)
target_link_libraries(opense4-server PRIVATE opense4_net opense4_mods opense4_game tomlplusplus opense4_warnings)
set_target_properties(opense4-server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
opense4_windows_manifest(opense4-server)

# --- assets: runtime access to the player's installed classic art ------------------
add_library(opense4_assets STATIC
    assets/assets.cpp
    assets/sound.cpp
    assets/tiny_font.cpp
    assets/vorbis_impl.cpp
    assets/wincursor.cpp
    assets/winfont.cpp)
target_link_libraries(opense4_assets PUBLIC opense4_core PRIVATE stb opense4_warnings)
# Third-party implementation file (stb_vorbis): don't apply our strict warnings.
set_source_files_properties(assets/vorbis_impl.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gfx/device.hpp")
    include(Client.cmake)
endif()
